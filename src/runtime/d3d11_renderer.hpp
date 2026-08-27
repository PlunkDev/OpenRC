#pragma once

#include "scene_geometry.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <memory>

namespace openrc::runtime {

class D3d11Renderer final {
public:
    D3d11Renderer(HWND window, const SceneGeometryV1& geometry);
    D3d11Renderer(HWND window,
                  const SceneGeometryV1& raster_geometry,
                  const SceneGeometry3dV1& source_geometry);
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
