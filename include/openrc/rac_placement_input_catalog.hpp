#pragma once

#include "openrc/rac_level_moby_assets.hpp"
#include "openrc/rac_ratchet_sequence.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

// Compiler-only CURRENT registration inputs. Absence of model_index means a
// proved null model, never an unresolved lookup. Every used class must have a
// binding. A present model names RacLevelMobyAssetsV1::models, not an EE address.
// The caller owns proof of current flags; later accepted placement ORs must
// update ONE staged shared model, not these immutable per-catalog inputs.
struct RacPlacementClassBindingV1 {
  std::uint32_t class_id = 0;
  std::uint8_t class_table_index = 0;
  std::uint32_t callback_reference = 0;
  std::uint32_t callback_table_reference = 0;
  // Identity of the selected dispatch row, not a claim that its behavior is
  // implemented. Empty exactly when the actual update reference is null.
  std::string callback_semantic_key;
  std::optional<std::uint32_t> model_index;
  std::optional<std::uint16_t> current_model_flags;
  // Original bucket0 external sequence table replacement. A zero external
  // table word leaves the ordinary model sequence pointer unchanged.
  bool apply_external_sequence_table = false;
};

enum class RacPlacementSequenceOwnerV1 : std::uint8_t {
  model_source,
  decoded_level_core,
};

struct RacPlacementSequenceInputV1 {
  RacPlacementSequenceOwnerV1 owner = RacPlacementSequenceOwnerV1::model_source;
  RacRatchetSequenceRangeV1 range;
  std::uint64_t frame0_owner_offset = 0;
  std::uint32_t frame0_source_word = 0;
  std::array<std::uint32_t, 4> header_bits{};
  std::uint8_t frame_count = 0;
  std::uint8_t sound_byte = 0;
  std::uint8_t trigger_byte = 0;
  PreparedContentDigestV1 source_sha256{};
};

struct RacPlacementModelInputV1 {
  std::uint32_t source_model_index = 0;
  PreparedContentDigestV1 source_sha256{};
  std::uint16_t current_flags = 0;
  std::uint32_t scale_bits = 0;
  // Owner-relative source offsets/raw words, not relocated pointers. The
  // later compiler composition must bind actual relocated references before
  // passing these inputs to fresh-constructor/post APIs that require them.
  std::uint32_t spatial_offset = 0;
  std::uint32_t auxiliary_word = 0;
  std::uint8_t byte_06 = 0;
  std::uint8_t byte_0c = 0;
  std::uint8_t byte_0e = 0;
  std::uint8_t byte_0f = 0;
  std::optional<RacPlacementSequenceInputV1> sequence0;
};

struct RacPlacementInputV1 {
  std::uint32_t source_ordinal = 0;
  std::uint32_t class_binding_index = 0;
  std::optional<std::uint32_t> model_input_index;
  // Exact authored admission/scalar/color/reference/PVar inputs. No source
  // placement is omitted for lacking renderable geometry or a bound model.
  RacGameplayMobyInstanceV1 authored;
  // Retained requirement, never an evaluated query or successful no-op.
  bool requires_rooted_query = false;
};

struct RacPlacementInputCatalogV1 {
  std::uint32_t level_id = 0;
  std::uint32_t spawnable_count = 0;
  PreparedContentDigestV1 gameplay_source_sha256{};
  std::vector<RacPlacementClassBindingV1> classes;
  std::vector<RacPlacementModelInputV1> models;
  std::vector<RacPlacementInputV1> placements;
};

struct RacPlacementInputCatalogLimitsV1 {
  std::uint32_t max_placements = 0;
  std::uint32_t max_classes = 0;
  std::uint32_t max_models = 0;
  std::uint32_t max_key_bytes = 0;
  std::uint64_t max_source_bytes = 0;
  RacRatchetSequenceLimitsV1 sequence;
};

class RacPlacementInputCatalogError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Complete source-order INPUT catalog only. Neither admission, construction,
// rooted queries, numeric post/spatial work nor callbacks are executed. The
// existing parsers validate actual sequence0/frame0, including class0's
// external table. All output references remain compiler-side and must never
// be serialized directly into a neutral runtime package.
[[nodiscard]] RacPlacementInputCatalogV1 compile_rac_placement_input_catalog_v1(
    const RacLevelMobyAssetsV1 &assets,
    std::span<const RacPlacementClassBindingV1> current_bindings,
    RacPlacementInputCatalogLimitsV1 limits);

// Actual supported Veldin initial registration producer: qualified overlay
// counter160080==0, original index table order, first matching dispatch triplet
// or its actual sentinel fallback, and external bucket0 sequence replacement.
// Model flags are the initial registered file values; this is not a replay of
// earlier loader services, nor permission to reset a retained live registry.
[[nodiscard]] std::vector<RacPlacementClassBindingV1>
resolve_rac_veldin_initial_placement_bindings_v1(
    const RacLevelMobyAssetsV1 &assets, std::span<const std::byte> overlay,
    RacPlacementInputCatalogLimitsV1 limits);

} // namespace openrc
