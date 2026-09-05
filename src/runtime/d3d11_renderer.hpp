#pragma once

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

namespace openrc {
struct ActorAffineTransformV1;
struct ActorModelV1;
struct ActorRigV1;
struct CollisionVectorV1;
struct RacLevelMobyTextureV1;
struct RenderSceneV1;
}

namespace openrc::game {
struct ThirdPersonCameraViewV1;
}

namespace openrc::runtime {

struct MobySceneMaterialBatchV1;
struct SceneGeometry3dV1;
struct SceneGeometryV1;
struct SceneMaterialBatchV1;

struct D3d11ObjectTextureSourceV1 {
    // Explicit region in the merged source-space triangle list. Keeping both
    // bounds prevents one object family from accidentally claiming the suffix
    // that belongs to a later family.
    std::uint64_t first_triangle = 0U;
    std::uint64_t triangle_count = 0U;
    std::span<const MobySceneMaterialBatchV1> material_batches;
    std::span<const RacLevelMobyTextureV1> textures;
};

struct D3d11TerrainTextureSourceV1 {
    // Terrain is the prefix of the merged source-space geometry.
    std::uint64_t triangle_count = 0U;
    std::span<const SceneMaterialBatchV1> material_batches;
    std::span<const RacLevelMobyTextureV1> textures;
};

struct D3d11SourceTextureSourcesV1 {
    D3d11TerrainTextureSourceV1 terrain;
    std::span<const D3d11ObjectTextureSourceV1> objects;
};

class D3d11Renderer final {
public:
    // Prepared-package runtime path. It consumes only the neutral render
    // resource and does not require an ISO, ELF, or RAC decoder structures.
    D3d11Renderer(HWND window, const openrc::RenderSceneV1& scene);
    // Prepared actor path. Player ownership is resolved by the runtime before
    // entering the renderer; this overload receives only neutral rig/model
    // data and the entity-local model transform.
    D3d11Renderer(
        HWND window,
        const openrc::RenderSceneV1& scene,
        const openrc::ActorRigV1& player_rig,
        const openrc::ActorModelV1& player_model,
        const openrc::ActorAffineTransformV1& model_to_entity);
    D3d11Renderer(HWND window, const SceneGeometryV1& geometry);
    D3d11Renderer(HWND window,
                  const SceneGeometryV1& raster_geometry,
                  const SceneGeometry3dV1& source_geometry);
    D3d11Renderer(HWND window,
                  const SceneGeometryV1& raster_geometry,
                  const SceneGeometry3dV1& source_geometry,
                  D3d11SourceTextureSourcesV1 textures);
    ~D3d11Renderer();

    D3d11Renderer(const D3d11Renderer&) = delete;
    D3d11Renderer& operator=(const D3d11Renderer&) = delete;
    D3d11Renderer(D3d11Renderer&&) noexcept;
    D3d11Renderer& operator=(D3d11Renderer&&) noexcept;

    void resize(std::uint32_t width, std::uint32_t height);
    // Returns false when no frame could be presented (for example while the
    // swap chain is occluded), allowing a continuous frontend to wait instead
    // of busy-spinning.
    bool render();

    // Package-only gameplay presentation. The camera is supplied explicitly
    // by the fixed-tick frontend; the renderer owns only projection and a
    // deliberately synthetic player marker while actor rendering is absent.
    void set_gameplay_presentation(
        const openrc::game::ThirdPersonCameraViewV1& camera,
        const openrc::CollisionVectorV1& feet_position,
        double facing_yaw_radians,
        double capsule_radius,
        double capsule_height);

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
