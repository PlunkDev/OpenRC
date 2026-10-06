#include "openrc/rac_moby_post_execute.hpp"

#include "openrc/dvp_vu_numeric.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint32_t kOne = 0x3f800000U;

void require(const bool value, const char *message) {
  if (!value) throw RacMobyPostError(message);
}

void warn(RacMobyPostExecutionResultV1 &result,
          const DvpVuExecutionWarningV1 warning) {
  if (std::ranges::find(result.warnings, warning) == result.warnings.end())
    result.warnings.push_back(warning);
}

RacMobyPostBlendResultV1 blend(const RacMobyPostBlendRequestV1 &request) {
  RacMobyPostBlendResultV1 result;
  for (unsigned lane = 0U; lane < 4U; ++lane) {
    const auto product = dvp_vu_mul_bits_v1(
        request.current_header_bits[lane], request.phase_bits);
    const auto added = dvp_vu_madd_bits_v1(
        {product.bits, product.overflow}, request.previous_header_bits[lane], kOne);
    // MSUB writes VF18, not ACC. Its multiplier retains old*phase order.
    result.header_bits[lane] = dvp_vu_madd_bits_v1(
        {added.result.bits, added.result.overflow},
        request.previous_header_bits[lane], request.phase_bits, true).result.bits;
  }
  return result;
}

RacMobyPostDerivedResultV1 derive(const RacMobyPostDerivedRequestV1 &request) {
  RacMobyPostDerivedResultV1 result;
  for (unsigned lane = 0U; lane < 3U; ++lane) {
    const auto product = dvp_vu_mul_bits_v1(
        request.columns[0U][lane], request.scaled_header_bits[0U]);
    DvpVuAccumulatorLaneV1 accumulator{product.bits, product.overflow};
    for (unsigned column = 1U; column < 3U; ++column) {
      const auto added = dvp_vu_madd_bits_v1(
          accumulator, request.columns[column][lane], request.scaled_header_bits[column]);
      accumulator = {added.result.bits, added.result.overflow};
    }
    result.xyz_bits[lane] = dvp_vu_madd_bits_v1(
        accumulator, request.scaled_position_bits[lane], kOne).result.bits;
  }
  return result;
}

std::optional<game::SpatialCellRectangleV1> rectangle(const std::uint32_t packed) {
  // Source signed-maxY gate: a negative word has no visited cells. For a
  // valid active cell, PSUBSB(cellY,negativeMaxY)>0 also makes every cell
  // outside this inactive bound during the other rectangle's delta phase.
  // Preserve all four actual bytes in actor storage; do not normalize them
  // to the fresh sentinel80807f7f or omit the tail's mandatory SW.
  if (packed & 0x80000000U) return std::nullopt;
  const game::SpatialCellRectangleV1 value{
      static_cast<std::uint16_t>(packed & 255U),
      static_cast<std::uint16_t>((packed >> 8U) & 255U),
      static_cast<std::uint16_t>((packed >> 16U) & 255U),
      static_cast<std::uint16_t>(packed >> 24U)};
  require(value.min_x <= value.max_x && value.min_y <= value.max_y &&
              value.max_x < 64U && value.max_y < 64U,
          "Moby post spatial rectangle leaves the qualified source grid");
  return value;
}

void completed_actor(RacMobyPostExecutionResultV1 &result,
                     const RacMobyPostV1 &post,
                     const RacMobyPostLimitsV1 limits,
                     const bool packed_store) {
  require(post.writes().size() <= limits.max_recorded_writes &&
              (!packed_store || post.writes().size() < limits.max_recorded_writes),
          "Moby post completed writes exceed their explicit limit");
  result.actor = post.staged_actor();
  result.writes.reserve(post.writes().size() + (packed_store ? 1U : 0U));
  for (const auto &write : post.writes()) result.writes.emplace_back(write);
}

void spatial_tail(RacMobyPostExecutionResultV1 &result,
                  const RacMobyPostSpatialRequestV1 &request,
                  const RacMobyPostBindingsV1 &bindings,
                  const RacMobyPostSpatialOwnerV1 &owner) {
  require(owner.source_actor_address_bits == bindings.source_actor_address_bits &&
              owner.source_live_index_bits == request.live_index_bits &&
              request.live_index_bits <= 0xffffU &&
              owner.member_token == request.live_index_bits,
          "Moby post has no matching complete source live-token binding");
  require(owner.index.revision() == owner.expected_revision,
          "Moby post spatial owner has a stale revision");
  const auto before = owner.index.snapshot();
  require(before.layout.columns == 64U && before.layout.rows == 64U,
          "Moby post spatial owner does not have the original64x64 layout");
  const auto old_rectangle = rectangle(request.old_packed_bits);
  const auto new_rectangle = rectangle(request.new_packed_bits);
  const auto existing = std::lower_bound(
      before.memberships.begin(), before.memberships.end(), owner.member_token,
      [](const auto &entry, const std::uint16_t token) { return entry.member < token; });
  const bool found = existing != before.memberships.end() && existing->member == owner.member_token;
  require(old_rectangle ? found && existing->rectangle == old_rectangle
                        : !found || !existing->rectangle,
          "Moby post old packed bounds disagree with actual index membership");

  auto staged = owner.index;
  // Existing transactional delta owner executes removal, strict shrink,
  // first-fit allocation, ordered append and whole stale-block copying.
  staged.set_membership(owner.member_token, new_rectangle, owner.expected_revision);
  result.actor.packed_bounds_bits = request.new_packed_bits;
  result.writes.emplace_back(RacMobyPostPackedBoundsWriteV1{request.new_packed_bits});
  result.spatial = RacMobyPostSpatialResultV1{
      owner.member_token, request.old_packed_bits, request.new_packed_bits,
      old_rectangle, new_rectangle, before.revision, staged.revision()};
  // Nothing after this publication allocates or may throw. A failure above
  // retains both original actor bits and the complete actual index storage.
  static_assert(std::is_nothrow_move_assignable_v<game::OrderedSpatialIndexV1>);
  static_assert(std::is_nothrow_move_constructible_v<RacMobyPostExecutionResultV1>);
  owner.index = std::move(staged);
}

} // namespace

RacMobyPostExecutionResultV1 execute_rac_moby_post_v1(
    const RacMobyPostActorV1 &actor, const RacMobyPostBindingsV1 &bindings,
    const RacMobyRotationSourceV1 *rotation,
    const RacMobyPostSpatialOwnerV1 *spatial,
    const RacMobyPostLimitsV1 limits) {
  RacMobyPostV1 post(actor, bindings, limits);
  RacMobyPostExecutionResultV1 result;
  // At most rotation, blend, scale, derived, spatial; each appears once.
  for (unsigned stage = 0U; stage < 6U; ++stage) {
    const auto &next = post.continuation();
    if (const auto *returned = std::get_if<RacMobyPostReturnedV1>(&next)) {
      completed_actor(result, post, limits, false);
      result.return_reason = returned->reason;
      return result;
    }
    if (const auto *request = std::get_if<RacMobyPostRotationRequestV1>(&next)) {
      require(rotation != nullptr, "Moby post reached rotation without its original source owner");
      const auto evaluated = rotation->evaluate(*request);
      for (const auto warning : evaluated.warnings) warn(result, warning);
      result.rotation_instruction_pairs += evaluated.executed_instruction_pairs;
      ++result.rotation_calls;
      post.resume(evaluated.rotation);
    } else if (const auto *request = std::get_if<RacMobyPostBlendRequestV1>(&next)) {
      warn(result, DvpVuExecutionWarningV1::vu_mul_reference_model);
      warn(result, DvpVuExecutionWarningV1::vu_madd_reference_model);
      post.resume(blend(*request));
      ++result.blend_calls;
    } else if (std::holds_alternative<RacMobyPostScaleRequestV1>(next)) {
      warn(result, DvpVuExecutionWarningV1::vu_mul_reference_model);
      if (actor.flags & 0x8000U)
        warn(result, DvpVuExecutionWarningV1::vu_add_sub_reference_model);
      post.evaluate_scale_reference();
      ++result.scale_calls;
    } else if (const auto *request = std::get_if<RacMobyPostDerivedRequestV1>(&next)) {
      warn(result, DvpVuExecutionWarningV1::vu_mul_reference_model);
      warn(result, DvpVuExecutionWarningV1::vu_madd_reference_model);
      post.resume(derive(*request));
      ++result.derived_calls;
    } else if (const auto *request = std::get_if<RacMobyPostSpatialRequestV1>(&next)) {
      require(spatial != nullptr, "Moby post reached spatial tail without its real index owner");
      completed_actor(result, post, limits, true);
      try { spatial_tail(result, *request, bindings, *spatial); }
      catch (const game::OrderedSpatialIndexError &error) {
        throw RacMobyPostError("Moby post spatial tail failed: " + std::string(error.what()));
      }
      return result;
    } else {
      throw RacMobyPostError("Moby post reached an unknown continuation");
    }
  }
  throw RacMobyPostError("Moby post exceeded its finite source stage count");
}

} // namespace openrc
