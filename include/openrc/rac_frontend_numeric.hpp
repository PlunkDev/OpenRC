#pragma once

#include "openrc/rac_moby_post.hpp"
#include "openrc/rac_frontend_owner.hpp"
#include "openrc/scene_timeline.hpp"

namespace openrc {

// Compiler-side source-order 1f9ec0 and 1f9ee8. Columns retain all raw lanes.
// The 3-column helper's fourth column is architectural VF0={0,0,0,1}.
[[nodiscard]] RacMobyPostVectorV1 rac_frontend_transform3_v1(
    const RacMobyPostVectorV1 &vector, const RacMobyPostMatrixV1 &columns);
[[nodiscard]] RacMobyPostVectorV1 rac_frontend_transform4_v1(
    const RacMobyPostVectorV1 &vector,
    const std::array<RacMobyPostVectorV1,4U> &columns);

// Runs the actual reached post continuation. Zero XYZ rotation is evaluated
// from its source identity path; reached blending/spatial tails fail until
// their real owners are integrated. No observations or fake completion.
[[nodiscard]] RacMobyPostActorV1 execute_rac_moby_zero_rotation_post_v1(
    const RacMobyPostActorV1 &actor, const RacMobyPostBindingsV1 &bindings,
    RacMobyPostLimitsV1 limits);

struct RacFrontendCameraV1 {
  SceneCameraV1 camera;
  // Actual Euler-built row matrix and raw source view columns. Neutral
  // camera fields are an adapter of these qualified source operations.
  std::array<RacMobyPostVectorV1,4U> rotation_rows{};
  std::array<RacMobyPostVectorV1,4U> view_columns{};
  std::uint32_t render_width = 512U;
  std::uint32_t render_height = 448U;
  std::uint32_t display_width = 512U;
  std::uint32_t display_height = 512U;
};
[[nodiscard]] RacFrontendCameraV1 execute_rac_frontend_camera_v1(
    const std::array<std::uint32_t,3U> &position_bits,
    const std::array<std::uint32_t,3U> &rotation_bits);

// Actual live reads of 238d90. The caller supplies its source-built 187140
// matrix, not a neutral camera reconstructed with host perspective arithmetic.
struct RacFrontendProjectionStateV1 {
  RacMobyPostVectorV1 camera_position_bits{}; // 187180, xyz read by 1f9bf0
  std::array<RacMobyPostVectorV1,4U> projection_view_columns{}; // 187140
  std::array<std::uint32_t,2U> screen_scale_bits{}; // 18ce00+190/+194
  std::array<std::uint32_t,2U> origin_words{}; // 13e600+8/+12, signed words
};
// Main-owner219c08 + active PAL512x448 projection from1f3008/1f3140.
// Applies before RTT changes; caller must retain the source owner order.
[[nodiscard]] RacFrontendProjectionStateV1 execute_rac_frontend_menu_projection_v1();
[[nodiscard]] RacFrontendProjectedBoundsV1 execute_rac_frontend_project_bounds_v1(
    const RacMobyPostVectorV1 &first_corner,
    const RacMobyPostVectorV1 &second_corner,
    const RacFrontendProjectionStateV1 &state);

} // namespace openrc
