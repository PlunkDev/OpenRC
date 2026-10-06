#pragma once

#include "openrc/scene_timeline.hpp"

namespace openrc {

// Additional screen-space2250b8 draws, after both RTT passes. Actor meshes
// use the existing model renderer; these bitmap layers have separate source
// resource selection and cannot be replaced by another model instance.
enum class RacFrontendDecorationAssetV1 {
  sprite_bank_e99e_variant7,
  catalog26_flash,
  catalog28_overlay,
  catalog25_border
};
struct RacFrontendDecorationDrawV1 {
  RacFrontendDecorationAssetV1 asset{};
  std::uint32_t source_call_pc=0;
  std::array<std::uint32_t,4> rectangle_words{}; // pixel X,Y,width,height
  // 2008b8 uses full source image UV bounds; the other draws use these
  // original pixel-coordinate1f5800 UV arguments and wrap, without clipping.
  bool full_image_uv=false;
  std::array<std::uint32_t,4> uv_rectangle_words{};
  std::uint64_t rgbaq=0;
  std::uint64_t alpha=0;
  bool operator==(const RacFrontendDecorationDrawV1 &) const=default;
};
struct RacFrontendDecorationStateV1 {
  std::uint32_t flash_word=0; // PVar+48
  std::uint32_t age_word=0;   // PVar+4c
  // The actual shared1160d8 generator state; callers preserve its position
  // among other original random consumers. No default seed is asserted.
  std::uint32_t random_seed_word=0;
};
struct RacFrontendDecorationInputsV1 {
  bool object_present=false;
  bool pvar_present=false;
  bool pal=false; // live15ee80
  std::array<std::uint32_t,4> rectangle_words{}; // PVar+50,+54,+58,+5c
  RacFrontendDecorationStateV1 state;
};
struct RacFrontendDecorationResultV1 {
  RacFrontendDecorationStateV1 state;
  std::vector<RacFrontendDecorationDrawV1> draws;
  std::uint32_t random_calls=0;
  bool culled=false;
};
[[nodiscard]] RacFrontendDecorationResultV1 execute_rac_frontend_decoration_v1(
    const RacFrontendDecorationInputsV1 &input);

// 219c08's separate model camera, with the same effective source projection
// scale and source1024-unit conversion as the main projector. The original
// scene retains its global512x512 display aspect; no second runtime/world.
[[nodiscard]] SceneCameraV1 execute_rac_frontend_menu_camera_v1();

} // namespace openrc
