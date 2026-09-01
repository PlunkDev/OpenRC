#pragma once

#include "moby_scene_geometry.hpp"
#include "scene_geometry.hpp"

#include "openrc/rac_level_moby_texture.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <memory>
#include <span>

namespace openrc::runtime {

struct D3d11MobyTextureSourceV1 {
    // Triangle ordinal where the Moby suffix begins in source_geometry.
    std::uint64_t first_triangle = 0U;
    std::span<const MobySceneMaterialBatchV1> material_batches;
    std::span<const RacLevelMobyTextureV1> textures;
};

class D3d11Renderer final {
public:
    D3d11Renderer(HWND window, const SceneGeometryV1& geometry);
    D3d11Renderer(HWND window,
                  const SceneGeometryV1& raster_geometry,
                  const SceneGeometry3dV1& source_geometry);
    D3d11Renderer(HWND window,
                  const SceneGeometryV1& raster_geometry,
                  const SceneGeometry3dV1& source_geometry,
                  D3d11MobyTextureSourceV1 moby_textures);
    ~D3d11Renderer();

    D3d11Renderer(const D3d11Renderer&) = delete;
    D3d11Renderer& operator=(const D3d11Renderer&) = delete;
    D3d11Renderer(D3d11Renderer&&) noexcept;
    D3d11Renderer& operator=(D3d11Renderer&&) noexcept;

    void resize(std::uint32_t width, std::uint32_t height);
    void render();

    // Diagnostic source-space controls. Angles are radians and wheel_steps is
    // positive when zooming in. They never modify or rerun the recovered VU
    // frame transform.
    void orbit(float yaw_delta, float pitch_delta);
    void zoom(float wheel_steps);
    void reset_camera();
    void toggle_view_mode();
    [[nodiscard]] bool is_3d_view() const noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace openrc::runtime
