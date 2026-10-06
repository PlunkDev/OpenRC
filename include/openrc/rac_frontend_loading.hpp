#pragma once

#include "openrc/rac_frontend_texture.hpp"
#include "openrc/rac_integer_quad.hpp"

#include <optional>

namespace openrc {

// Compiler-side 232b90 bindings: shared tile at directory+4 and authored
// cards at directory+8+4*index. Original PIF/GS bytes never enter runtime.
struct RacFrontendLoadingAssetsV1 {
  RacFrontendTextureV1 tile;
  std::array<RacFrontendTextureV1,2U> cards;
};
[[nodiscard]] RacFrontendLoadingAssetsV1 parse_rac_frontend_loading_assets_v1(
    std::span<const std::byte> decoded_wad, std::uint32_t first_card,
    std::uint32_t second_card, RacFrontendTextureLimitsV1 limits);

struct RacFrontendLoadingCardStateV1 {
  std::uint32_t first_card=0, second_card=0;
  std::uint32_t frame=0, duration=0;
  bool load_pending=false;
  bool render_pending=false;
};
struct RacFrontendLoadingCardBeginV1 {
  RacFrontendLoadingCardStateV1 state;
  // 232f54 -> 12f4a8(level), before the first render gate. Even a skipped
  // draw loop still submits this request and clears halfwords15ef48/15ef4a.
  std::optional<std::uint32_t> requested_level;
  bool clear_level_counters=false;
};
[[nodiscard]] RacFrontendLoadingCardBeginV1 begin_rac_frontend_loading_card_v1(
    std::uint32_t first_card, std::uint32_t second_card,
    std::uint32_t evaluated_duration, bool load_level,
    std::uint32_t level_word, std::uint32_t card_dc, std::uint32_t card_e4);

// Call after the real per-frame 209e68/209070 I/O and presentation tail.
// A pending level load requires the completed 204c60 result for THIS frame;
// zero extends duration to at least frame+20, nonzero ends further polling.
// The caller must not substitute an assumed success or omit the I/O owners.
[[nodiscard]] RacFrontendLoadingCardStateV1 advance_rac_frontend_loading_card_v1(
    const RacFrontendLoadingCardStateV1 &state, std::uint32_t card_dc,
    std::uint32_t card_e4, std::optional<std::uint32_t> completed_load_poll);

// No render_pending means the source reaches its final 1f4e08(2) fade.
// These are literal two updates, not another scaled 1f98c0 argument.
inline constexpr std::uint32_t kRacFrontendLoadingFinalFadeUpdatesV1=2U;

struct RacFrontendLoadingDrawV1 {
  std::uint32_t source_call_pc=0;
  // 0=shared tile, 1=first authored card, 2=second authored card.
  std::uint32_t texture_slot=0;
  std::array<std::uint32_t,4U> rectangle_words{};
  std::uint32_t rgba=0;
  bool uses_stq=false;
  // U0,U1,V0,V1 raw singles for 232a00. The integer card uses UV0..512/64.
  std::array<std::uint32_t,4U> st_endpoint_bits{};
};
// Executes 233000..233230, including CVT.S.W, ordered MUL.S/ADD.S.
// origin_y is actual13e600+0c (the display centre, not framebuffer height).
// Produces only the reached draw requests; raster sampling remains separate.
[[nodiscard]] std::vector<RacFrontendLoadingDrawV1> execute_rac_frontend_loading_draws_v1(
    const RacFrontendLoadingCardStateV1 &state, std::uint32_t origin_y);

struct RacFrontendStqQuadInputsV1 {
  std::array<std::uint32_t,4U> rectangle_words{};
  std::array<std::uint32_t,2U> screen_offset_reads{}; // original Y then X
  std::array<std::uint32_t,4U> st_endpoint_bits{}; // U0,U1,V0,V1
  std::uint64_t rgbaq=0;
  std::uint64_t tex0=0;
};
// Complete integer/packing 232a00: raw caller ST, Q forced to1, PRIM54,
// no cull, original signed-word packing and ordered cursor advances.
[[nodiscard]] RacIntegerQuadEmissionV1 emit_rac_frontend_stq_quad_v1(
    const RacFrontendStqQuadInputsV1 &input) noexcept;

} // namespace openrc
