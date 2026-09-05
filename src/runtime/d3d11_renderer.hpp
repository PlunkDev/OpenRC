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

namespace openrc {
struct ActorAffineTransformV1;
struct ActorModelV1;
struct ActorRigV1;
struct CollisionVectorV1;
struct RenderSceneV1;
}

namespace openrc::game {
struct ThirdPersonCameraViewV1;
}

namespace openrc::runtime {

class D3d11Renderer final {
public:
    // Prepared-package runtime path. It consumes only the neutral render
    // resource.
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

    // Package-only gameplay presentation. The fixed-tick frontend supplies
    // the camera and player transform. The renderer presents the neutral
    // actor model when supplied, with a synthetic player marker as fallback.
    void set_gameplay_presentation(
        const openrc::game::ThirdPersonCameraViewV1& camera,
        const openrc::CollisionVectorV1& feet_position,
        double facing_yaw_radians,
        double capsule_radius,
        double capsule_height);

    // EntityRenderBindingV1 visibility is updated independently from static
    // scene allocation. IDs address the neutral RenderSceneV1 instance table.
    void set_render_instance_enabled(std::uint32_t instance_id, bool enabled);
    [[nodiscard]] bool
    render_instance_enabled(std::uint32_t instance_id) const;
    // Reports whether at least one DrawIndexed call for the neutral instance
    // was submitted by the most recent render attempt. A render attempt that
    // cannot begin (for example, for a zero-sized target) clears the result.
    // This lets the graphical package smoke test prove an entity crossed the
    // renderer boundary before gameplay hid it.
    [[nodiscard]] bool last_frame_render_instance_submitted(
        std::uint32_t instance_id) const;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace openrc::runtime
