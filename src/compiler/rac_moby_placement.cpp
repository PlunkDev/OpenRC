#include "openrc/rac_moby_placement.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace openrc {
namespace {

void require(const bool value, const char *message) {
  if (!value) throw RacMobyPlacementError(message);
}

bool overlaps(const std::uint64_t a, const std::uint64_t size_a,
              const std::uint64_t b, const std::uint64_t size_b) noexcept {
  return a < b + size_b && b < a + size_a;
}

void validate_slot(const RacGameplayMobyInstanceV1 &authored,
                   const RacMobyPlacementContextV1 &context) {
  require(context.class_id_bits == authored.class_id,
          "Placement class lookup does not belong to this authored record");
  const auto address = std::uint64_t{context.slot_base_address_bits} +
                       (std::uint64_t{context.source_live_index_bits} << 8U);
  require(context.slot_base_address_bits != 0U &&
              (context.slot_base_address_bits & 15U) == 0U &&
              context.source_live_index_bits <= 0xffffU &&
              address == context.source_actor_address_bits &&
              address + 256U <= 0x100000000ULL,
          "Placement actor has no bounded source slot/live-index ownership proof");
}

RacMobyPostActorV1 post_input(const RacMobyPlacementActorV1 &actor) {
  // Every omitted scalar was cleared by the actual fresh-constructor path;
  // no update/sequence callback has run in between these source operations.
  RacMobyPostActorV1 result;
  result.flags = actor.fields.flags;
  result.model_reference = actor.fields.model_reference;
  result.scale_bits = actor.fields.scale_bits;
  result.header_cache_key = actor.fields.header_cache_key;
  result.rotation_bits = actor.rotation_bits;
  result.position_bits = actor.position_bits;
  result.counter_word_bits = actor.fields.live_index_counter_bits;
  result.spatial_reference = actor.fields.spatial_reference;
  result.packed_bounds_bits = actor.fields.packed_bounds_bits;
  result.live_index_bits = actor.fields.live_index_bits;
  return result;
}

void apply_post(RacMobyPlacementActorV1 &actor, const RacMobyPostActorV1 &post) {
  actor.fields.header_cache_key = post.header_cache_key;
  actor.fields.live_index_counter_bits = post.counter_word_bits;
  actor.fields.packed_bounds_bits = post.packed_bounds_bits;
  actor.cached_header_bits = post.cached_header_bits;
  actor.derived_vector_bits = post.derived_vector_bits;
  actor.matrix_bits = post.matrix_bits;
}

void validate_reference_target(const RacMobyReferenceResultV1 &reference,
                               const RacMobyPlacementContextV1 &context) {
  if (!reference.write) return;
  require(context.reference_token && *context.reference_token != 0U,
          "Placement actor-reference store has no non-null object token");
  // The existing helper proves the full bound source/native reference window.
  // Also exclude aliasing this operation's named actor/model owners, which
  // cannot truthfully be represented by a separate reference-token view.
  const std::uint32_t address = 0x1792b8U + (*reference.combined_index_bits << 2U);
  require(!overlaps(address, 4U, context.source_actor_address_bits, 256U),
          "Placement reference destination aliases its actor");
  if (context.shared_model)
    require(!overlaps(address, 4U, context.shared_model->reference, 0x4cU),
            "Placement reference destination aliases its shared model");
}

} // namespace

RacMobyPlacementResultV1 execute_rac_moby_placement_v1(
    const RacGameplayMobyInstanceV1 &authored,
    const RacMobyAdmissionStateBindingsV1 &admission_bindings,
    game::SessionStateV1 &state, const std::uint64_t expected_revision,
    const RacMobyPlacementContextV1 *context,
    const RacMobyPlacementLimitsV1 limits) {
  const auto compiled = compile_rac_moby_admission_v1(
      authored.admission, admission_bindings, limits.admission,
      limits.source_admission);
  auto staged_state = state;
  RacMobyPlacementResultV1 result;
  result.session_revision_before = state.revision();
  result.admission = game::execute_placement_admission_v1(
      compiled.plan, staged_state, expected_revision, limits.admission);
  if (!result.admission.admitted) {
    result.session_revision_after = staged_state.revision();
    static_assert(std::is_nothrow_move_assignable_v<game::SessionStateV1>);
    static_assert(std::is_nothrow_move_constructible_v<RacMobyPlacementResultV1>);
    state = std::move(staged_state);
    return result; // Context/model/index/token were never inspected.
  }

  require(context != nullptr, "Accepted placement has no current construction context");
  validate_slot(authored, *context);
  result.remap_bits = static_cast<std::uint16_t>(context->source_live_index_bits);
  std::optional<RacMobyFreshModelV1> model;
  if (context->shared_model) {
    model = *context->shared_model;
    require(model->reference != 0U && (model->reference & 3U) == 0U &&
                std::uint64_t{model->reference} + 0x4cU <= 0x100000000ULL &&
                !overlaps(model->reference, 0x4cU, context->source_actor_address_bits, 256U),
            "Placement shared model aliases the fresh slot or lacks a valid owner");
  }
  RacMobyPlacementActorV1 actor;
  actor.fields = construct_rac_moby_fresh_v1({
      authored.class_id, context->class_table_index, context->source_live_index_bits,
      context->callback_reference, model});

  // 243420..24343c: retain the source store widths and helper fallback byte.
  actor.admission.key_bits = compiled.constructor_key_bits;
  actor.admission.auxiliary_slot_bits = result.admission.registration_write
      ? static_cast<std::uint8_t>(result.admission.registration_write->slot_index)
      : compiled.unregistered_slot_bits;
  actor.admission.current_count_bits = static_cast<std::uint16_t>(result.admission.current_bits);
  actor.admission.selector_bits = compiled.constructor_selector_bits;
  actor.admission.maximum_count_bits = static_cast<std::uint16_t>(result.admission.maximum_bits);
  if (model) {
    result.authored_scale = ee_cop1_mul_bits_v1(model->scale_bits, authored.scale_bits);
    actor.fields.scale_bits = result.authored_scale->bits;
  }
  actor.draw_distance_bits = static_cast<std::uint16_t>(authored.draw_distance_raw);
  actor.update_distance_bits = static_cast<std::uint8_t>(authored.update_distance);
  std::copy(authored.position_bits.begin(), authored.position_bits.end(), actor.position_bits.begin());
  std::copy(authored.rotation_bits.begin(), authored.rotation_bits.end(), actor.rotation_bits.begin());
  actor.fields.group_byte = static_cast<std::uint8_t>(authored.group_index);
  require(authored.rooted == 0, "Placement reached unsupported original rooted query");
  actor.fields.occlusion_bits = 0x7f80U;
  actor.pvar_index_bits = static_cast<std::uint32_t>(authored.pvar_index);
  if (authored.occlusion == 0) actor.fields.occlusion_bits = 0U;

  const auto authored_flags = static_cast<std::uint16_t>(authored.mode_bits);
  if (model) {
    const auto before = model->flags;
    model->flags = static_cast<std::uint16_t>(model->flags | authored_flags);
    result.shared_model_flags_before_after = std::array{before, model->flags};
  }
  actor.fields.flags = static_cast<std::uint16_t>(actor.fields.flags | authored_flags);

  std::optional<game::OrderedSpatialIndexV1> staged_index;
  std::optional<RacMobyPostSpatialOwnerV1> staged_spatial;
  if (model) {
    require(context->post_bindings.source_actor_address_bits == context->source_actor_address_bits,
            "Placement post bindings belong to another actor");
    if (actor.fields.spatial_reference != 0U && context->spatial) {
      // Transactional copy only; reached checks still belong to post. An
      // absent owner remains absent and is not demanded by an earlier return.
      staged_index.emplace(context->spatial->index);
      staged_spatial.emplace(RacMobyPostSpatialOwnerV1{
          *staged_index, context->spatial->expected_revision,
          context->spatial->source_actor_address_bits,
          context->spatial->source_live_index_bits, context->spatial->member_token});
    }
    result.post = execute_rac_moby_post_v1(post_input(actor), context->post_bindings,
        context->rotation, staged_spatial ? &*staged_spatial : nullptr, limits.post);
    apply_post(actor, result.post->actor);
  }

  // 243560..2435ac: helper SD precedes the caller's low-word light SW.
  result.tail = recover_rac_moby_authored_tail_v1(authored.authored_color_words,
      static_cast<std::uint32_t>(authored.light_index), authored.authored_reference_index_bits);
  actor.cached_color_bits = result.tail->cached_color_bits;
  actor.fields.packed_bits = result.tail->helper_packed_store_bits;
  actor.fields.packed_bits = (actor.fields.packed_bits & 0xffffffff00000000ULL) |
                            result.tail->light_word_bits;
  if (result.tail->reference_helper_index_bits) {
    require(context->reference_bindings != nullptr,
            "Placement reached reference helper without current bindings");
    result.reference = execute_rac_moby_reference_store_v1(*context->reference_bindings,
        staged_state, staged_state.revision(), *result.tail->reference_helper_index_bits,
        context->reference_token, limits.reference);
    validate_reference_target(*result.reference, *context);
  }
  result.actor = std::move(actor);
  result.session_revision_after = staged_state.revision();

  // No allocations or fallible operations after publication begins. The
  // caller still owns actor/remap publication and its next live-count step.
  static_assert(std::is_nothrow_move_assignable_v<game::SessionStateV1>);
  static_assert(std::is_nothrow_move_assignable_v<game::OrderedSpatialIndexV1>);
  static_assert(std::is_nothrow_move_constructible_v<RacMobyPlacementResultV1>);
  if (result.post && result.post->spatial)
    context->spatial->index = std::move(*staged_index);
  if (model) context->shared_model->flags = model->flags;
  state = std::move(staged_state);
  return result;
}

} // namespace openrc
