#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <array>

#include "openrc/screen_overlay.hpp"

namespace openrc {
struct ActorAffineTransformV1;
struct ActorLibraryV1;
struct ActorModelV1;
struct ActorPosePaletteV1;
struct ActorRigV1;
struct CollisionVectorV1;
struct RenderSceneV1;
struct SceneCameraV1;
struct ScreenOverlayV1;
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
    // Presentation before a level is mounted uses the same device, swap chain
    // and shaders as gameplay, without constructing a synthetic level.
    explicit D3d11Renderer(HWND window);
    void set_media_frame(std::uint32_t width, std::uint32_t height,
                         std::span<const std::byte> rgba,
                         std::uint32_t aspect_numerator,
                         std::uint32_t aspect_denominator);
    [[nodiscard]] std::uint64_t media_frames_submitted() const noexcept;
    // Diagnostic readback of the current media draw before Present discards
    // the back buffer. Dimensions are the renderer's current client size.
    [[nodiscard]] std::vector<std::byte> capture_media_frame_rgba();
    [[nodiscard]] std::vector<std::byte> capture_frame_rgba();
    // An event covers GPU work submitted before this call. Polling reports
    // the device's completion, independently of Present or elapsed time.
    // Starting another drain invalidates the previous token.
    [[nodiscard]] std::uint64_t begin_submission_drain();
    [[nodiscard]] bool submission_drain_completed(std::uint64_t token);
    // Requires the current event already observed complete. False means later
    // GPU work/admission needs a fresh event; independent of presentation mode.
    // This read-only check does not consume the event or retire any resources.
    [[nodiscard]] bool submission_drain_covers_current_work(std::uint64_t token) const;
    // Release the current completion event only after observed completion of
    // all current work. False leaves it owned when newer work needs a drain.
    // Success invalidates the token; frame, scene and device remain intact.
    [[nodiscard]] bool try_retire_submission_drain(std::uint64_t token);
    // Release cinematic geometry, actors and overlays after this current
    // drain has been observed complete, with no later GPU work/admission.
    // Requires a frozen media frame; keeps it, the device and swap chain.
    // Consumes the token's retirement eligibility and rejects gameplay.
    void retire_scene_for_media(std::uint64_t completed_drain_token);
    // The same contract, except a current observed-complete event followed by
    // newer work returns false without changing resources. A message-pumping
    // caller can then submit and await a fresh event. Other misuse still throws.
    [[nodiscard]] bool try_retire_scene_for_media(std::uint64_t completed_drain_token);
    // Admit a cinematic scene into this same device after media playback.
    // Neutral actor instances share the ordinary world-actor render path.
    void set_scene_actors(const openrc::ActorLibraryV1& library,
        std::span<const openrc::game::RuntimeWorldActorResolutionV1> actors);
    void set_scene_geometry(const openrc::RenderSceneV1& scene);
    // First gameplay admission after frontend/media presentation. Keeps this
    // device and swap chain, replaces scenic actors/geometry, retires overlays
    // and installs the actual prepared player model. The caller establishes
    // gameplay camera/pose before presenting the first gameplay frame.
    void set_gameplay_scene(const openrc::RenderSceneV1& scene,
        const openrc::ActorLibraryV1& library,
        const openrc::game::RuntimePlayerActorResolutionV1& player,
        std::span<const openrc::game::RuntimeWorldActorResolutionV1> actors);
    void set_scene_camera(const openrc::SceneCameraV1& camera,
        std::uint32_t aspect_numerator, std::uint32_t aspect_denominator,
        const std::array<float,4>& clear_color);
    // Legacy single-overlay admission replaces the complete layer bank.
    // Its frame setter addresses layer zero; clear retires every layer.
    void set_screen_overlay(const openrc::ScreenOverlayV1& overlay);
    void set_screen_overlay_frame(std::uint32_t frame_index);
    void clear_screen_overlay();
    // Upload immutable layers once, in submission order. Limits cover the
    // aggregate neutral byte/image/frame/draw counts across all layers.
    // Each admitted layer starts at frame zero; an empty span clears them.
    void set_screen_overlay_layers(
        std::span<const openrc::ScreenOverlayV1> layers,
        openrc::ScreenOverlayLimitsV1 limits = {});
    // Select every layer independently without uploading images. Null hides
    // that layer. All selections are validated before any state is changed.
    void set_screen_overlay_layer_frames(
        std::span<const std::optional<std::uint32_t>> frame_indices);
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
    // A neutral object may use its owner's separate camera (for example a
    // model-based interface over a scenic background). Null restores the
    // scene camera, without changing the object's pose or world transform.
    void set_world_actor_camera(std::uint32_t authored_id,
        const openrc::SceneCameraV1* camera);
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
