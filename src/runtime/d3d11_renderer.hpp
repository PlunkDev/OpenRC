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
struct ActorLibraryV1;
struct ActorModelV1;
struct ActorPosePaletteV1;
struct ActorRigV1;
struct CollisionVectorV1;
struct RenderSceneV1;
}

namespace openrc::game {
struct RuntimePlayerActorResolutionV1;
struct RuntimeWorldActorResolutionV1;
struct ThirdPersonCameraViewV1;
struct WorldTransformV1;
}

namespace openrc::runtime {

class D3d11Renderer final {
public:
    // Prepared-package runtime path. It consumes only the neutral render
    // resource.
    D3d11Renderer(HWND window, const openrc::RenderSceneV1& scene);
    // Prepared actor path. Player and non-player ownership are resolved by the
    // runtime before entering the renderer; this overload receives only
    // neutral dense asset handles and world/entity transforms.
    D3d11Renderer(
        HWND window,
        const openrc::RenderSceneV1& scene,
        const openrc::ActorLibraryV1& actor_library,
        const openrc::game::RuntimePlayerActorResolutionV1& player_actor,
        std::span<const openrc::game::RuntimeWorldActorResolutionV1>
            world_actors);
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

    // Replaces the prepared actor's current joint palette independently from
    // its entity transform. This keeps animation advancing even when the
    // player remains stationary.
    void set_gameplay_actor_pose(
        const openrc::ActorPosePaletteV1& pose);

    // Non-player actor instances remain addressed by the sparse authored ID
    // carried by EntitySceneV1. Pose, transform, and visibility updates are
    // independent so later source-backed AI can drive each instance without
    // rebuilding the renderer or duplicating immutable model textures.
    void set_world_actor_pose(
        std::uint32_t authored_id,
        const openrc::ActorPosePaletteV1& pose);
    void set_world_actor_transform(
        std::uint32_t authored_id,
        const openrc::game::WorldTransformV1& transform);
    void set_world_actor_enabled(std::uint32_t authored_id, bool enabled);
    [[nodiscard]] bool world_actor_enabled(std::uint32_t authored_id) const;
    [[nodiscard]] bool
    last_frame_world_actor_submitted(std::uint32_t authored_id) const;

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
