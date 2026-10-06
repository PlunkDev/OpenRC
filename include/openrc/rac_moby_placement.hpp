#pragma once

#include "openrc/ee_cop1_numeric.hpp"
#include "openrc/rac_moby_admission_compile.hpp"
#include "openrc/rac_moby_authored_tail.hpp"
#include "openrc/rac_moby_fresh_constructor.hpp"
#include "openrc/rac_moby_post_execute.hpp"
#include "openrc/rac_moby_reference.hpp"

namespace openrc {

// ONE current compiler-side actor, not a serialized 256-byte source image.
// Constructor fields remain their single mutable owner after authored/post
// stores. Unlisted fresh bytes retain the constructor's explicit clear.
struct RacMobyPlacementActorV1 {
  RacMobyFreshConstructorStateV1 fields;
  RacMobyAdmissionConstructorFieldsV1 admission;
  std::uint8_t update_distance_bits = 0U;
  std::uint16_t draw_distance_bits = 0U;
  RacMobyPostVectorV1 position_bits{};
  RacMobyPostVectorV1 rotation_bits{};
  std::uint32_t pvar_index_bits = 0U;
  std::uint32_t cached_color_bits = 0U;
  RacMobyPostVectorV1 cached_header_bits{};
  RacMobyPostVectorV1 derived_vector_bits{};
  RacMobyPostMatrixV1 matrix_bits{};
};

// An accepted record must have a proved current class lookup, including a
// genuinely null model. Absence of this entire context means unavailable
// construction input; it is never inspected on ordinary admission rejection.
struct RacMobyPlacementContextV1 {
  std::uint32_t class_id_bits = 0U;
  std::uint8_t class_table_index = 0U;
  std::uint32_t callback_reference = 0U; // stored, never called here
  RacMobyFreshModelV1 *shared_model = nullptr; // null means proved null lookup
  std::uint32_t slot_base_address_bits = 0U;
  std::uint32_t source_actor_address_bits = 0U;
  std::uint32_t source_live_index_bits = 0U;
  RacMobyPostBindingsV1 post_bindings;
  const RacMobyRotationSourceV1 *rotation = nullptr;
  const RacMobyPostSpatialOwnerV1 *spatial = nullptr;
  const RacMobyReferenceBindingsV1 *reference_bindings = nullptr;
  // Reached actor-reference stores require a non-null resolved object token.
  // Namespace/lifetime belongs to the future catalog, never EntityId::slot or
  // a source pointer. The caller must publish the returned actor with it.
  std::optional<std::uint32_t> reference_token;
};

struct RacMobyPlacementLimitsV1 {
  PlacementAdmissionLimitsV1 admission;
  RacMobyAdmissionLimitsV1 source_admission;
  RacMobyPostLimitsV1 post;
  RacMobyReferenceLimitsV1 reference;
};

struct RacMobyPlacementResultV1 {
  game::PlacementAdmissionResultV1 admission;
  std::uint16_t remap_bits = 0xffffU;
  std::optional<RacMobyPlacementActorV1> actor;
  std::optional<EeCop1MulResultV1> authored_scale;
  std::optional<std::array<std::uint16_t, 2U>> shared_model_flags_before_after;
  // Immutable execution evidence; .actor above is the final actor value.
  std::optional<RacMobyPostExecutionResultV1> post;
  std::optional<RacMobyAuthoredTailV1> tail;
  std::optional<RacMobyReferenceResultV1> reference;
  std::uint64_t session_revision_before = 0U;
  std::uint64_t session_revision_after = 0U;
  static constexpr bool physical_console_qualified = false;
};

class RacMobyPlacementError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Compile this authored admission inside the operation, then execute exactly
// one ordered placement through constructor, authored stores, shared-model OR,
// actual post/spatial, color/light and reference. Rooted queries are explicitly
// unsupported. Model/reference/source binding values remain compiler-only.
// All errors preserve the canonical session, shared model and spatial index;
// ordinary rejection commits earlier registration and does not read context.
// A supplied relevant index is copied for staging; missing spatial input is
// required only if post reaches its tail. Publication after the final reached
// reference operation is nonthrowing. No caller live count is incremented,
// no remap table is mutated and no entity/world/catalog is created here.
[[nodiscard]] RacMobyPlacementResultV1 execute_rac_moby_placement_v1(
    const RacGameplayMobyInstanceV1 &authored,
    const RacMobyAdmissionStateBindingsV1 &admission_bindings,
    game::SessionStateV1 &state, std::uint64_t expected_revision,
    const RacMobyPlacementContextV1 *context,
    RacMobyPlacementLimitsV1 limits);

} // namespace openrc
