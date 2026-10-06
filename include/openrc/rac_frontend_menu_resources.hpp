#pragma once

#include "openrc/rac_frontend_menu_actor_compile.hpp"
#include "openrc/prepared_game_v2.hpp"
#include <filesystem>

namespace openrc {
struct RacFrontendMenuResourceLimitsV1 {
  std::uint64_t max_source_wad_bytes=32U*1024U*1024U;
  std::uint64_t max_total_payload_bytes=128U*1024U*1024U;
  RacFrontendMainCompileLimitsV1 main;
};
struct RacFrontendMenuResourcesV1 {
  // Existing neutral actor/animation/timeline/overlay resources, ready to add
  // to the shared package. Source records remain compiler-only.
  std::vector<LevelPackageResourceV1> resources;
  PreparedContentDigestV1 source_elf_sha256{},source_wad_sha256{},source_text_sha256{};
  std::uint32_t entry_updates=12,active_timeline_sample=12;
};
[[nodiscard]] ActorLibraryIoLimitsV1 rac_frontend_menu_actor_io_limits_v1();
[[nodiscard]] ActorAnimationIoLimitsV1 rac_frontend_menu_animation_io_limits_v1();
[[nodiscard]] ScreenOverlayLimitsV1 rac_frontend_menu_overlay_limits_v1();

// Source language0/PAL50 menu, original class1138,14actor slots,13samples and
// three original list RTTs. The list overlay starts with the same12-update
// invisible prefix and then evaluates the complete focused-row color ramp.
// WAD is the already decoded source14e8 payload; no forensic fixture is read.
// The first four resources are actors, animation, timeline and lists. Nine
// subsequent overlays own the original absent-card dialog: backdrop, four
// body/panel variants and four prompt variants. Body/prompt frame indices are
// their independently supplied remaining counters0..25, not elapsed updates.
// Decoration has separate compiler ownership and is not emitted here.
[[nodiscard]] RacFrontendMenuResourcesV1 compile_rac_frontend_menu_resources_v1(
    const std::filesystem::path &source_image,std::span<const std::byte> source_elf,
    std::span<const std::byte> decoded_frontend_wad,
    RacFrontendMenuResourceLimitsV1 limits={});
} // namespace openrc
