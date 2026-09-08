#pragma once

#include "openrc/rac_frontend_gs_scope.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

namespace openrc {

// Compiler-only snapshots of source reads, not a synthetic runtime menu tree.
// Each phase has its own observations because source callees may mutate them.
struct RacFrontendSlotSourceV1 {
  std::uint32_t object_address = 0U;
  std::uint32_t enabled_word = 0U;
  std::uint32_t slot_six_enabled_word = 0U;
  bool operator==(const RacFrontendSlotSourceV1 &) const = default;
};

struct RacFrontendProjectedBoundsV1 {
  // Explicit outputs of original238d90, not host projection calculations.
  std::array<std::uint32_t, 4> width_height_x_y{};
  bool operator==(const RacFrontendProjectedBoundsV1 &) const = default;
};

struct RacFrontendPrepassSlotV1 {
  RacFrontendSlotSourceV1 source;
  std::uint32_t projected_object_address = 0U;
  std::uint32_t node_address = 0U;
  std::optional<RacFrontendProjectedBoundsV1> projection;
  std::uint32_t source_color_word = 0U;
};

struct RacFrontendRttLiveV1 {
  // Original151880 fields and allocator high boundary are live, not ELF
  // defaults.
  std::uint32_t allocator_limit_word = 0U;
  std::int16_t frame_format_half = 0;
  std::uint16_t depth_format_half = 0U;
  std::int16_t depth_base_half = 0;
  std::uint32_t clear_color_word = 0U;
  // Four actual post-setup offset reads, Y0,X0,Y1,X1. Projection is an
  // unexecuted dependency, not permission to assume its output live words.
  std::array<std::uint32_t, 4> clear_offset_reads{};
  // Original21aa74..84 re-reads node-table slot and callback AFTER setup/clear.
  // These need not equal the addresses observed by the earlier selection gates.
  std::uint32_t callback_node_after_setup = 0U;
  std::uint32_t callback_after_setup = 0U;
  // Actual live161000 read by restore1fb498: zero calls SDK1224b0 directly;
  // nonzero appends REF9. No implicit append-only assumption.
  std::uint32_t restore_command_cursor_word = 0U;
  // Original restore1fb498 references (live_draw_environment + 0x30) &
  // 0xfffffff. The caller owns those exact144 bytes; this plan never
  // manufactures them.
  std::uint32_t live_draw_environment = 0U;
  RacFrontendGsRegionV1 restore_environment_region;
  std::int16_t restore_width_half = 0;
  std::int16_t restore_height_half = 0;
  // Actual post-callback/post-restore read of15efd0, only when composite
  // occurs.
  std::optional<std::uint64_t> composite_tex0;
};

struct RacFrontendDrawSlotV1 {
  RacFrontendSlotSourceV1 source;
  std::uint32_t node_address = 0U;
  std::uint32_t node_flags = 0U;
  std::uint32_t callback_address = 0U;
  // Fresh source projected object fields+50/+54/+58/+5c at this attempt.
  std::array<std::uint32_t, 4> x_y_width_height{};
  // Ignored for direct calls and skipped nodes; mandatory for a reached RTT
  // call.
  std::optional<std::uint32_t> callback_return_word;
  std::optional<RacFrontendRttLiveV1> rtt_live;
};

enum class RacFrontendOwnerCallKindV1 {
  prepare_render_20e0c8,
  save_scratch_20e040,
  copy_scratch_20e068,
  draw_object_20e180,
  menu_camera_219c08,
  camera_projection_1f2608,
  project_bounds_238d90,
  projected_clear_201640,
  callback,
  restore_scratch_20e098,
  submit_objects_20e200,
  cold_batch_begin_1f4630,
  final_object_2250b8,
  cold_batch_end_1f4748,
  composite_quad_1f5800,
  immediate_environment_1224b0,
};

struct RacFrontendOwnerCallV1 {
  RacFrontendOwnerCallKindV1 kind = RacFrontendOwnerCallKindV1::callback;
  std::uint32_t source_call_pc = 0U;
  // For callback only; known direct callees are named by kind above.
  std::uint32_t callback_address = 0U;
  // Source integer ABI values. For project_bounds only, arguments0/1 identify
  // the original source vectors copied to the original stack before that call;
  // stack addresses are not invented. Pointer-shaped values are source tokens,
  // never permission for host dereference. Unused fields remain zero.
  std::array<std::uint64_t, 10> arguments{};
  bool operator==(const RacFrontendOwnerCallV1 &) const = default;
};

struct RacFrontendOwnerWordWriteV1 {
  std::uint32_t source_address = 0U;
  std::uint32_t value = 0U;
  bool operator==(const RacFrontendOwnerWordWriteV1 &) const = default;
};

struct RacFrontendOwnerPacketV1 {
  std::uint32_t source_owner = 0U;
  std::vector<std::byte> bytes;
  std::vector<RacFrontendGsRegionV1> external_reads;
  bool operator==(const RacFrontendOwnerPacketV1 &) const = default;
};

enum class RacFrontendViewportOpcodeV1 {
  write_raw_word,
  convert_word_to_single,
  multiply_single,
  write_single_value,
  // Full original1f3140 remains an explicit unexecuted source dependency.
  call_projection_1f3140,
};
struct RacFrontendViewportEffectV1 {
  RacFrontendViewportOpcodeV1 opcode =
      RacFrontendViewportOpcodeV1::write_raw_word;
  std::uint32_t source_pc = 0U;
  // write: destination source address; numeric: result temporary ID.
  std::uint32_t destination = 0U;
  // convert: raw source word; multiply: temporary ID and raw single constant;
  // write_single: temporary ID. Not an evaluator or a general-purpose VM.
  std::uint32_t left = 0U;
  std::uint32_t right = 0U;
  bool operator==(const RacFrontendViewportEffectV1 &) const = default;
};
struct RacFrontendViewportPlanV1 {
  std::array<std::uint32_t, 8> screen_words{};
  std::vector<RacFrontendViewportEffectV1> effects;
  bool operator==(const RacFrontendViewportPlanV1 &) const = default;
};

struct RacFrontendRttSetupV1 {
  std::uint32_t width_exponent = 0U;
  std::uint32_t height_exponent = 0U;
  std::uint32_t source_base_word = 0U;
  std::uint64_t tex0_write = 0U;
  // Source global writes1519e0/e2/e6 and15efd0 precede the setup packet.
  std::array<std::uint16_t, 3> width_height_base_halves{};
  RacFrontendOwnerPacketV1 packet;
  bool operator==(const RacFrontendRttSetupV1 &) const = default;
};

enum class RacFrontendOwnerSkipV1 {
  absent_object,
  absent_node_table,
  absent_node,
  hidden_node,
  disabled_slot,
  absent_callback,
  disabled_slot_six,
  different_pass,
};
struct RacFrontendOwnerSkippedV1 {
  std::uint32_t pass = 0U;
  std::uint32_t slot = 0U;
  RacFrontendOwnerSkipV1 reason = RacFrontendOwnerSkipV1::absent_object;
  bool operator==(const RacFrontendOwnerSkippedV1 &) const = default;
};

using RacFrontendOwnerEffectV1 =
    std::variant<RacFrontendOwnerCallV1, RacFrontendOwnerWordWriteV1,
                 RacFrontendOwnerPacketV1, RacFrontendViewportPlanV1,
                 RacFrontendRttSetupV1, RacFrontendOwnerSkippedV1>;

struct RacFrontendOwnerInputsV1 {
  std::uint32_t initial_object_address = 0U;
  std::array<RacFrontendSlotSourceV1, 14> object_draw_slots;
  // Read after the initial object pass; source caches root+44 across two
  // passes.
  std::uint32_t node_root_address = 0U;
  std::array<RacFrontendPrepassSlotV1, 14> prepass_slots;
  std::array<std::array<RacFrontendDrawSlotV1, 14>, 2> draw_slots;
  std::array<RacFrontendSlotSourceV1, 14> final_slots;
  // Exact screen-offset reads for each projected_clear201640 call. The bounds
  // projector/camera callees are NOT assumed to preserve one global snapshot.
  // Read order is Y0,X0,Y1,X1.
  std::array<std::array<std::uint32_t, 4>, 14> prepass_offset_reads{};
  std::uint32_t max_effects = 4096U;
};

struct RacFrontendOwnerPlanV1 {
  std::vector<RacFrontendOwnerEffectV1> effects;
  std::uint32_t direct_callbacks = 0U;
  std::uint32_t rtt_callbacks = 0U;
  std::uint32_t composites = 0U;
  // This is a source call/effect plan, not proof any opaque callee executed.
};

class RacFrontendOwnerError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Exact reached owner21a610 ordering with explicit fresh phase snapshots and
// callee outputs. All source callbacks, camera/projectors and object rendering
// calls remain typed, unexecuted dependencies. No invented node/GS/display
// state.
[[nodiscard]] RacFrontendOwnerPlanV1
plan_rac_frontend_owner_v1(const RacFrontendOwnerInputsV1 &input);

// Original RTT viewport setters1f3760 / restore1f3008. All COP1 arithmetic is
// retained symbolically and projection1f3140 remains an explicit call. Restore
// reconstructs display state; it does NOT save/restore arbitrary incoming GS.
[[nodiscard]] RacFrontendViewportPlanV1
plan_rac_frontend_rtt_viewport_v1(std::uint32_t width_word,
                                  std::uint32_t height_word, bool restore);

} // namespace openrc
