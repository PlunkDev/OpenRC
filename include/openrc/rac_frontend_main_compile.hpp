#pragma once

#include "openrc/rac_frontend_list_draw.hpp"
#include "openrc/rac_frontend_object.hpp"
#include "openrc/rac_frontend_numeric.hpp"
#include "openrc/screen_overlay.hpp"

namespace openrc {

struct RacFrontendMainVisibilityV1 {
  std::array<std::uint32_t,14U> enabled_words{};
  std::uint32_t slot_six_enabled_word=0U;
};
// ELF-owned initial1ce640 flags and root+d8. Startup219e60 leaves them
// unchanged; the separate in-game menu initializer219c70 has another owner.
[[nodiscard]] RacFrontendMainVisibilityV1 decode_rac_frontend_main_visibility_v1(
    std::span<const std::byte> boot_elf,std::uint64_t max_elf_bytes);

struct RacFrontendMainNodeV1 {
  std::uint32_t source_reference=0U;
  std::uint32_t object_slot=0U;
  std::uint32_t owner_flags=0U;
  std::uint32_t text_flags=0U;
  std::uint32_t selected_row=0U;
  std::vector<RacFrontendListRowV1> rows;
};
struct RacFrontendMainAssetsV1 {
  RacFrontendObjectScreenV1 screen;
  std::uint32_t focused_node=0U;
  std::array<RacFrontendMainNodeV1,3U> nodes;
  RacFontMetricTablesV1 metrics;
  RacFrontendTextureBankV1 textures;
  RacTextBankV1 text;
  std::vector<std::byte> missing_text;
  std::uint32_t clear_rgba=0U,timer_argument=0U;
  std::array<std::uint32_t,2U> shadow_x_y{};
  std::optional<RacFrontendMainVisibilityV1> initial_visibility;
};
struct RacFrontendMainCompileLimitsV1 {
  std::uint64_t max_elf_bytes=32U*1024U*1024U;
  RacFrontendTextureLimitsV1 textures{16U*1024U*1024U,128U,2048U,2048U,
      524272U,4194304U,32U*1024U*1024U};
  RacTextBankLimitsV1 text{2U*1024U*1024U,65536U,2U*1024U*1024U};
  ScreenOverlayLimitsV1 overlay;
};

// Reads original main descriptor1d4948, its three actual21c1b0 nodes/rows,
// metrics1df3d0 and clear/timer/shadow1602b0. Language is the exact1eb300
// numeric text-bank slot; it is not the title texture's different selector.
[[nodiscard]] RacFrontendMainAssetsV1 compile_rac_frontend_main_assets_v1(
    std::span<const std::byte> boot_elf,std::span<const std::byte> frontend_wad,
    std::span<const std::byte> frontend_text,std::uint32_t language_word,
    RacFrontendMainCompileLimitsV1 limits={});

// Real source glyph1:1 -> PSMCT32 RTT -> return2 crop1:1 -> FIX128 copy.
// All sampled RTT rows must have been cleared by the source SDK/2017c8 path;
// the uncleared final RTT scanline is explicitly outside this bounded helper.
// Output is opaque RGB because the original composite ignores texture alpha.
[[nodiscard]] ScreenOverlayImageV1 compile_rac_frontend_list_rtt_v1(
    const RacFrontendListDrawPlanV1 &draw,const RacFrontendTextureV1 &font,
    std::uint32_t width,std::uint32_t height,std::uint32_t clear_rgba,
    ScreenOverlayLimitsV1 limits={});

// Three actual list RTTs at already executed238d90 bounds. Owner adds one
// to X/Y. Ages are live signed row halfwords after21bb90; each node owns one
// row. This only lowers list layers, not the separate class1138 model draws.
[[nodiscard]] ScreenOverlayV1 compile_rac_frontend_main_lists_v1(
    const RacFrontendMainAssetsV1 &assets,
    const std::array<RacFrontendProjectedBoundsV1,3U> &bounds,
    const std::array<std::int16_t,3U> &row_ages,std::uint32_t time_scale_bits,
    std::uint32_t updates_per_second,RacFrontendMainCompileLimitsV1 limits={});

struct RacFrontendMainModelStateV1 {
  RacFrontendObjectAnimationStateV1 animation;
  RacMobyPostActorV1 post;
  bool enabled=false;
};
struct RacFrontendMainGeometryFrameV1 {
  bool main_active=false;
  std::array<std::array<RacMobyPostVectorV1,4U>,14U> corners;
  std::array<RacFrontendProjectedBoundsV1,3U> list_bounds;
  // Model renderer20e180/212658 consumes the completed post, whereas the
  // corners above were sampled before it. Compiler-only source state; the
  // menu actor adapter lowers this to the existing neutral timeline schema.
  std::array<RacFrontendMainModelStateV1,14U> models;
};
// Original14-object admission, same-screen reverse request,12-update gate,
// and pre/stop -> corners -> post. Returns13 entry updates, including the
// first active main frame. Source references are internally relocated into
// compiler-owned ranges, not reported as observed physical game allocations.
[[nodiscard]] std::array<RacFrontendMainGeometryFrameV1,13U>
compile_rac_frontend_main_entry_geometry_v1(
    std::span<const std::byte> class1138,const RacFrontendMainAssetsV1 &assets);

class RacFrontendMainCompileError final: public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
} // namespace openrc
