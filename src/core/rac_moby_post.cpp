#include "openrc/rac_moby_post.hpp"

#include "openrc/dvp_vu_numeric.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint64_t kAddressSpace = 0x100000000ULL;

[[noreturn]] void fail(const std::string &message) {
  throw RacMobyPostError("RAC Moby post " + message);
}

template <typename Request>
const Request &require_request(const RacMobyPostContinuationV1 &continuation) {
  const auto *request = std::get_if<Request>(&continuation);
  if (!request) {
    fail("numeric response is unused, repeated or does not match the pending "
         "stage");
  }
  return *request;
}

} // namespace

RacMobyPostV1::RacMobyPostV1(const RacMobyPostActorV1 &actor,
                             const RacMobyPostBindingsV1 &bindings,
                             const RacMobyPostLimitsV1 limits)
    : actor_(actor),
      source_actor_address_bits_(bindings.source_actor_address_bits),
      limits_(limits) {
  if (limits.max_sequence_headers == 0U || limits.max_snapshot_vectors == 0U ||
      limits.max_recorded_writes == 0U) {
    fail("limits must be nonzero");
  }
  if (bindings.sequence_headers.size() > limits.max_sequence_headers ||
      bindings.snapshot_vectors.size() > limits.max_snapshot_vectors) {
    fail("resolved input bank exceeds its explicit limits");
  }
  if ((source_actor_address_bits_ & 15U) != 0U ||
      std::uint64_t{source_actor_address_bits_} + 256U > kAddressSpace) {
    fail("complete source actor owner must be aligned16 and non-wrapping");
  }
  for (std::size_t i = 1U; i < bindings.sequence_headers.size(); ++i) {
    if (bindings.sequence_headers[i - 1U].sequence_index >=
        bindings.sequence_headers[i].sequence_index) {
      fail("sequence-header bindings must have unique ascending byte keys");
    }
  }
  for (std::size_t i = 1U; i < bindings.snapshot_vectors.size(); ++i) {
    if (bindings.snapshot_vectors[i - 1U].snapshot_index >=
        bindings.snapshot_vectors[i].snapshot_index) {
      fail("snapshot-vector bindings must have unique ascending byte keys");
    }
  }
  sequence_headers_.assign(bindings.sequence_headers.begin(),
                           bindings.sequence_headers.end());
  snapshot_vectors_.assign(bindings.snapshot_vectors.begin(),
                           bindings.snapshot_vectors.end());
  writes_.reserve(std::min(limits.max_recorded_writes, 7U));
  if ((actor_.state_byte & 0x80U) != 0U) {
    continuation_ =
        RacMobyPostReturnedV1{RacMobyPostReturnReasonV1::negative_state};
    return;
  }
  if ((actor_.flags & 0x100U) == 0U) {
    continuation_ = RacMobyPostRotationRequestV1{actor_.rotation_bits};
    return;
  }
  working_matrix_ = actor_.matrix_bits;
  // Source's cached-matrix entry also sets VF23=VF0. No subsequent actor
  // scalar operation consumes VF23; this adapter does not claim VU state.
  select_header();
}

const RacMobyPostActorV1 &RacMobyPostV1::staged_actor() const noexcept {
  return actor_;
}
const RacMobyPostContinuationV1 &RacMobyPostV1::continuation() const noexcept {
  return continuation_;
}
std::span<const RacMobyPostWriteV1> RacMobyPostV1::writes() const noexcept {
  return writes_;
}

void RacMobyPostV1::validate_read_owner(const std::uint32_t address,
                                        const std::uint32_t bytes,
                                        const std::uint32_t alignment) const {
  const auto end = std::uint64_t{address} + bytes;
  if ((address & (alignment - 1U)) != 0U || end > kAddressSpace) {
    fail("reached source read owner is unaligned or wrapping");
  }
  const auto actor_end = std::uint64_t{source_actor_address_bits_} + 256U;
  if (std::uint64_t{address} < actor_end && source_actor_address_bits_ < end) {
    fail("reached source dependency aliases the actor being patched");
  }
}

std::uint32_t
RacMobyPostV1::validate_table_address(const std::uint8_t sequence) const {
  // The actual ADD here is signed32, not ADDU. Its subsequent LW +48
  // uses the resulting source address; only the effective address wraps32.
  const std::int64_t sum =
      std::int64_t{std::bit_cast<std::int32_t>(actor_.model_reference)} +
      std::int64_t{sequence} * 4;
  if (sum < std::numeric_limits<std::int32_t>::min() ||
      sum > std::numeric_limits<std::int32_t>::max()) {
    fail("reached sequence-table ADD traps signed32 overflow");
  }
  const auto address = static_cast<std::uint32_t>(sum) + 0x48U;
  validate_read_owner(address, 4U, 4U);
  return address;
}

void RacMobyPostV1::observe_read(const std::uint32_t address,
                                 const std::uint32_t bytes,
                                 const RacMobyPostVectorV1 &bits) {
  const auto byte_at = [](const RacMobyPostVectorV1 &value,
                          const std::uint64_t offset) {
    return (value[static_cast<std::size_t>(offset / 4U)] >>
            (8U * static_cast<unsigned>(offset % 4U))) &
           255U;
  };
  for (std::uint8_t i = 0U; i < read_observation_count_; ++i) {
    const auto &prior = read_observations_[i];
    const auto begin =
        std::max(std::uint64_t{address}, std::uint64_t{prior.address});
    const auto end = std::min(std::uint64_t{address} + bytes,
                              std::uint64_t{prior.address} + prior.bytes);
    for (auto shared = begin; shared < end; ++shared) {
      if (byte_at(bits, shared - address) !=
          byte_at(prior.bits, shared - prior.address)) {
        fail("reached immutable source read owners have contradictory shared "
             "bytes");
      }
    }
  }
  if (read_observation_count_ >= read_observations_.size()) {
    fail("finite post read graph exceeded its maximum four observations");
  }
  read_observations_[read_observation_count_++] = {address, bytes, bits};
}

RacMobyPostVectorV1
RacMobyPostV1::sequence_header(const std::uint8_t sequence) {
  const auto table_address = validate_table_address(sequence);
  const auto found = std::lower_bound(
      sequence_headers_.begin(), sequence_headers_.end(), sequence,
      [](const RacMobyPostSequenceHeaderV1 &candidate, std::uint8_t key) {
        return candidate.sequence_index < key;
      });
  if (found == sequence_headers_.end() || found->sequence_index != sequence) {
    fail("reached sequence header has no current resolved owner");
  }
  observe_read(table_address, 4U, {found->header_reference, 0U, 0U, 0U});
  validate_read_owner(found->header_reference, 16U, 16U);
  observe_read(found->header_reference, 16U, found->header_bits);
  return found->header_bits;
}

RacMobyPostVectorV1 RacMobyPostV1::snapshot_vector(const std::uint8_t index) {
  const std::uint32_t address = 0x197080U + std::uint32_t{index} * 16U;
  validate_read_owner(address, 16U, 16U);
  const auto found = std::lower_bound(
      snapshot_vectors_.begin(), snapshot_vectors_.end(), index,
      [](const RacMobyPostSnapshotVectorV1 &candidate, std::uint8_t key) {
        return candidate.snapshot_index < key;
      });
  if (found == snapshot_vectors_.end() || found->snapshot_index != index) {
    fail("reached snapshot VECTOR is missing; frame snapshot data cannot "
         "substitute");
  }
  observe_read(address, 16U, found->vector_bits);
  return found->vector_bits;
}

void RacMobyPostV1::record(const RacMobyPostFieldV1 field,
                           const RacMobyPostVectorV1 &bits) {
  if (writes_.size() >= limits_.max_recorded_writes) {
    fail("source-ordered write record exceeds its explicit limit");
  }
  writes_.push_back({field, bits});
}

void RacMobyPostV1::select_header() {
  if (actor_.previous_sequence == actor_.current_sequence) {
    if (actor_.previous_sequence == actor_.header_cache_key) {
      store_matrix_and_request_scale(actor_.cached_header_bits);
      return;
    }
    const auto header = sequence_header(actor_.previous_sequence);
    actor_.header_cache_key = actor_.previous_sequence;
    record(RacMobyPostFieldV1::header_cache_key,
           {actor_.header_cache_key, 0U, 0U, 0U});
    actor_.cached_header_bits = header;
    record(RacMobyPostFieldV1::cached_header, header);
    store_matrix_and_request_scale(header);
    return;
  }
  // Desired ADD occurs in the branch delay BEFORE either previous-header
  // read, including the previousFF snapshot branch. It can trap separately.
  (void)validate_table_address(actor_.current_sequence);
  const auto previous = actor_.previous_sequence == 0xffU
                            ? snapshot_vector(actor_.previous_frame)
                            : sequence_header(actor_.previous_sequence);
  const auto desired = sequence_header(actor_.current_sequence);
  continuation_ =
      RacMobyPostBlendRequestV1{previous, desired, actor_.phase_bits};
}

void RacMobyPostV1::store_matrix_and_request_scale(
    const RacMobyPostVectorV1 &header) {
  if ((actor_.flags & 0x8000U) != 0U) {
    for (unsigned lane = 0U; lane < 3U; ++lane) {
      working_matrix_[1U][lane] =
          dvp_vu_sub_bits_v1(0U, working_matrix_[1U][lane]).bits;
    }
  }
  actor_.matrix_bits = working_matrix_;
  record(RacMobyPostFieldV1::matrix0, working_matrix_[0U]);
  record(RacMobyPostFieldV1::matrix1, working_matrix_[1U]);
  record(RacMobyPostFieldV1::matrix2, working_matrix_[2U]);
  continuation_ = RacMobyPostScaleRequestV1{header, actor_.scale_bits,
                                            actor_.position_bits};
}

void RacMobyPostV1::resume(const RacMobyPostRotationResultV1 &external_result) {
  (void)require_request<RacMobyPostRotationRequestV1>(continuation_);
  for (const auto &column : external_result.columns) {
    if ((column[3U] & 0x7fffffffU) != 0U) {
      fail("external d18 rotation result has nonzero-magnitude W");
    }
  }
  auto next = *this;
  next.working_matrix_ = external_result.columns;
  next.select_header();
  static_assert(std::is_nothrow_move_assignable_v<RacMobyPostV1>);
  *this = std::move(next);
}

void RacMobyPostV1::resume(const RacMobyPostBlendResultV1 &external_result) {
  (void)require_request<RacMobyPostBlendRequestV1>(continuation_);
  auto next = *this;
  next.store_matrix_and_request_scale(external_result.header_bits);
  *this = std::move(next);
}

void RacMobyPostV1::resume(const RacMobyPostScaleResultV1 &external_result) {
  (void)require_request<RacMobyPostScaleRequestV1>(continuation_);
  auto next = *this;
  // This exact FTOI0.w is reached BEFORE the derived XYZ ACC chain.
  next.converted_radius_bits_ =
      dvp_vu_ftoi_bits_v1(external_result.scaled_header_bits[3U], 0);
  next.continuation_ = RacMobyPostDerivedRequestV1{
      next.working_matrix_, external_result.scaled_header_bits,
      external_result.scaled_position_bits};
  *this = std::move(next);
}

void RacMobyPostV1::evaluate_scale_reference() {
  const auto request =
      require_request<RacMobyPostScaleRequestV1>(continuation_);
  RacMobyPostScaleResultV1 result;
  for (unsigned lane = 0U; lane < 4U; ++lane) {
    result.scaled_header_bits[lane] =
        dvp_vu_mul_bits_v1(request.header_bits[lane], request.scale_bits).bits;
  }
  for (unsigned lane = 0U; lane < 3U; ++lane) {
    result.scaled_position_bits[lane] =
        dvp_vu_mul_bits_v1(request.position_bits[lane], 0x44800000U).bits;
  }
  resume(result);
}

void RacMobyPostV1::resume(const RacMobyPostDerivedResultV1 &external_result) {
  const auto request =
      require_request<RacMobyPostDerivedRequestV1>(continuation_);
  auto next = *this;
  std::array<std::uint32_t, 3U> converted{};
  for (unsigned lane = 0U; lane < 3U; ++lane) {
    converted[lane] = dvp_vu_ftoi_bits_v1(external_result.xyz_bits[lane], 0);
    next.actor_.derived_vector_bits[lane] = external_result.xyz_bits[lane];
  }
  // VMADD.xyz did not write W. A supplied XYZ result cannot replace radius.
  next.actor_.derived_vector_bits[3U] = request.scaled_header_bits[3U];
  next.record(RacMobyPostFieldV1::derived_vector,
              next.actor_.derived_vector_bits);
  const auto low_counter = (next.actor_.counter_word_bits + 1U) & 0xffffU;
  next.actor_.counter_word_bits =
      (next.actor_.counter_word_bits & 0xffff0000U) | low_counter;
  next.record(RacMobyPostFieldV1::counter_low16, {low_counter, 0U, 0U, 0U});
  if (next.actor_.spatial_reference == 0U) {
    next.continuation_ = RacMobyPostReturnedV1{
        RacMobyPostReturnReasonV1::null_spatial_reference};
  } else {
    const auto projection = project_rac_moby_spatial_bounds_v1(
        converted[0U], converted[1U], next.converted_radius_bits_,
        next.actor_.packed_bounds_bits);
    switch (projection.gate) {
    case RacMobySpatialBoundsGateV1::unchanged:
      next.continuation_ =
          RacMobyPostReturnedV1{RacMobyPostReturnReasonV1::unchanged_bounds};
      break;
    case RacMobySpatialBoundsGateV1::rejected_minimum:
      next.continuation_ =
          RacMobyPostReturnedV1{RacMobyPostReturnReasonV1::rejected_minimum};
      break;
    case RacMobySpatialBoundsGateV1::call_spatial_update:
      next.continuation_ = RacMobyPostSpatialRequestV1{
          next.actor_.packed_bounds_bits, projection.packed_bits,
          next.actor_.live_index_bits};
      break;
    }
  }
  *this = std::move(next);
}

} // namespace openrc
