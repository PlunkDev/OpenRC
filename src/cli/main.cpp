#include "level_scene_render_compile.hpp"
#include "moby_scene_geometry.hpp"
#include "tie_scene_geometry.hpp"

#include "openrc/boundary_table.hpp"
#include "openrc/companion_wad_index.hpp"
#include "openrc/content_api.hpp"
#include "openrc/disc.hpp"
#include "openrc/disc_toc.hpp"
#include "openrc/dvp_vu.hpp"
#include "openrc/dvp_vu_execute.hpp"
#include "openrc/ee_r5900_boundary.hpp"
#include "openrc/elf.hpp"
#include "openrc/gif_gs.hpp"
#include "openrc/hash.hpp"
#include "openrc/level_render_scene_compile.hpp"
#include "openrc/localized_subtitle_bank.hpp"
#include "openrc/map_art.hpp"
#include "openrc/native_game_prepare.hpp"
#include "openrc/paths.hpp"
#include "openrc/player_simulation.hpp"
#include "openrc/preparation.hpp"
#include "openrc/prepared_game_v2_fs.hpp"
#include "openrc/prepared_game_v2_publish.hpp"
#include "openrc/ps2_save_bundle.hpp"
#include "openrc/rac_gameplay_bank.hpp"
#include "openrc/rac_level_bootstrap_compile.hpp"
#include "openrc/rac_level_collision_compile.hpp"
#include "openrc/rac_level_core.hpp"
#include "openrc/rac_level_foundation_compile.hpp"
#include "openrc/rac_moby_class.hpp"
#include "openrc/rac_moby_model_geometry.hpp"
#include "openrc/rac_moby_packet_geometry.hpp"
#include "openrc/rac_player_locomotion.hpp"
#include "openrc/rac_ratchet_sequence.hpp"
#include "openrc/runtime_gameplay.hpp"
#include "openrc/runtime_level_content.hpp"
#include "openrc/runtime_level_foundation.hpp"
#include "openrc/runtime_render_scene.hpp"
#include "openrc/sblk.hpp"
#include "openrc/sblk_audio.hpp"
#include "openrc/sblk_wav.hpp"
#include "openrc/scene_animation_bank.hpp"
#include "openrc/scene_block_directory.hpp"
#include "openrc/scene_block_task_execute.hpp"
#include "openrc/scene_block_vif.hpp"
#include "openrc/scene_block_vu.hpp"
#include "openrc/scene_block_vu_phase.hpp"
#include "openrc/two_fip.hpp"
#include "openrc/vagp.hpp"
#include "openrc/wad.hpp"
#include "openrc/wad_bundle.hpp"
#include "openrc/wad_payload_family.hpp"
#include "openrc/wad_payload_family_tsv.hpp"
#include "openrc/wad_payload_inventory.hpp"
#include "openrc/wad_payload_probes.hpp"
#include "openrc/wad_payload_tsv.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#endif

#ifndef OPENRC_VERSION
#define OPENRC_VERSION "0.1.0-dev"
#endif

namespace {

constexpr int kUsageError = 2;
constexpr int kOperationError = 3;
constexpr int kUnsupportedBuild = 4;
constexpr int kCancelled = 5;
constexpr std::uint64_t kMaximumCliDecodedWadBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliWadInventoryObservations = 100'000U;
constexpr std::uint64_t kMaximumCliWadInventoryUniquePayloads = 100'000U;
constexpr std::uint64_t kMaximumCliWadInventoryTotalDecodedBytes =
    UINT64_C(512) * 1024U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliWadInventoryProbes = 16U;
constexpr std::uint64_t kMaximumCliWadInventoryProbeInvocations = 1'600'000U;
constexpr std::uint64_t kMaximumCliLocalizedSubtitleEntries = 4096U;
constexpr std::uint64_t kMaximumCliSceneAnimationActorTracks = 256U;
constexpr std::uint64_t kMaximumCliSceneAnimationTotalFrameRanges = 65'536U;
constexpr std::uint32_t kMaximumCliWadFamilySampledBytesPerPayload = 4096U;
constexpr std::uint64_t kMaximumCliWadFamilyTotalSampledBytes =
    kMaximumCliWadInventoryUniquePayloads *
    kMaximumCliWadFamilySampledBytesPerPayload;
constexpr std::uint64_t kMaximumCliWadFamilyLevels = 64U;
constexpr std::uint64_t kMaximumCliWadFamilyUniqueLevelMemberships = 2'000'000U;
constexpr std::uint32_t kMaximumCliWadBundleNestingDepth = 8U;
constexpr std::uint64_t kMaximumCliSceneBlockRecords = 4096U;
constexpr std::uint64_t kMaximumCliTwoFipPixels = 16U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliPs2SaveBundleBytes = 1U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliVagpBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliVagpFrames = 1'000'000U;
constexpr std::uint64_t kMaximumCliVagpSamples =
    kMaximumCliVagpFrames * openrc::kPsAdpcmSamplesPerFrame;
constexpr std::uint64_t kMaximumCliSBlkItems = 1'000'000U;
constexpr std::uint64_t kMaximumCliSBlkBlocks = 1'000'000U;
constexpr std::uint64_t kMaximumCliSBlkFrames = 1'000'000U;
constexpr std::uint64_t kMaximumCliSBlkSamples =
    kMaximumCliSBlkFrames * openrc::kPsAdpcmSamplesPerFrame;
constexpr std::uint64_t kMaximumCliSBlkWavBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliElfBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliCollisionPayloadBytes =
    32U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliLevelFoundationPackageBytes =
    64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliRenderScenePayloadBytes =
    UINT64_C(512) * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliNativeLevelPackageBytes =
    UINT64_C(768) * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliPreparedFoundationBytes =
    512U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliPreparedNativeBytes =
    kMaximumCliNativeLevelPackageBytes *
    static_cast<std::uint64_t>(openrc::kDiscTocLevelCount) +
    openrc::kNativeGameSharedPackageMaxBytesV1;
constexpr std::string_view kSupportedRacBuildIdV1 =
    "SCES-50916-PAL-v2.00";
constexpr std::size_t kMaximumCliDvpVuListItems = 128U;
constexpr std::size_t kMapArtFirstGlobalSlot = 259;
constexpr std::size_t kPs2SaveBundleGlobalSlot = 1;
constexpr openrc::RacMobyModelGeometryLimitsV1 kCliMobyModelGeometryLimits{
    {kMaximumCliDecodedWadBytes,
     4096U,
     4096U,
     4096U,
     1'000'000U,
     4096U,
     1'000'000U},
    4096U,
    1'000'000U,
    1'000'000U};
constexpr openrc::RacLevelMobyTextureLimitsV1 kCliMobyTextureLimits{
    kMaximumCliDecodedWadBytes,
    kMaximumCliDecodedWadBytes,
    kMaximumCliDecodedWadBytes,
    255U,
    4096U,
    4096U,
    kMaximumCliTwoFipPixels,
    kMaximumCliTwoFipPixels,
    kMaximumCliTwoFipPixels * 4U};

[[nodiscard]] openrc::RacLevelMobyAssetLimitsV1
make_cli_moby_asset_limits() {
    return openrc::RacLevelMobyAssetLimitsV1{
        kMaximumCliDecodedWadBytes,
        kMaximumCliDecodedWadBytes,
        kMaximumCliDecodedWadBytes,
        4096U,
        65'536U,
        1'000'000U,
        1'000'000U,
        openrc::RacLevelCoreLimitsV1{
            kMaximumCliDecodedWadBytes,
            kMaximumCliDecodedWadBytes,
            kMaximumCliDecodedWadBytes,
            4096U,
            255U,
            4096U,
            4096U,
            4096U,
            255U,
            255U},
        openrc::RacLevelCollisionLimitsV1{
            kMaximumCliDecodedWadBytes,
            65'536U,
            1'000'000U,
            4'000'000U,
            1'000'000U,
            16'000'000U,
            16'000'000U,
            65'536U,
            4'000'000U,
            4'000'000U},
        openrc::RacGameplayBankLimitsV1{kMaximumCliDecodedWadBytes},
        openrc::RacMobyClassLimitsV1{kMaximumCliDecodedWadBytes, false},
        openrc::RacMobyClassLimitsV1{kMaximumCliDecodedWadBytes, true},
        kCliMobyModelGeometryLimits,
        kCliMobyTextureLimits,
        openrc::RacTieClassLimitsV1{
            kMaximumCliDecodedWadBytes,
            4096U,
            65'536U,
            1'000'000U,
            1'000'000U,
            1'000'000U,
            16U},
        4096U,
        65'536U,
        1'000'000U,
        1'000'000U};
}

[[nodiscard]] constexpr openrc::RacLevelCollisionCompileLimitsV1
make_cli_collision_compile_limits() {
    return openrc::RacLevelCollisionCompileLimitsV1{
        1'000'000U,
        16'000'000U,
        16'000'000U,
        65'536U,
        4'000'000U,
        4'000'000U,
        openrc::CollisionWorldBuildLimitsV1{
            1'000'000U,
            2'000'000U,
            1'000'000U,
            16'000'000U,
            openrc::kCollisionDefaultGridCellSizeQ6V1}};
}

[[nodiscard]] openrc::CollisionWorldIoLimitsV1
make_cli_collision_payload_limits() {
    return openrc::CollisionWorldIoLimitsV1{
        kMaximumCliCollisionPayloadBytes,
        make_cli_collision_compile_limits().world};
}

[[nodiscard]] constexpr openrc::LevelBootstrapV1Limits
make_cli_bootstrap_payload_limits() {
    return openrc::LevelBootstrapV1Limits{64U * 1024U, 1024U};
}

[[nodiscard]] constexpr openrc::LevelPackageV1Limits
make_cli_level_foundation_package_limits() {
    return openrc::LevelPackageV1Limits{
        kMaximumCliLevelFoundationPackageBytes,
        16U,
        8U,
        64U,
        1024U,
        kMaximumCliCollisionPayloadBytes,
        kMaximumCliLevelFoundationPackageBytes -
            openrc::kLevelPackageHeaderBytesV1,
        64U};
}

[[nodiscard]] constexpr openrc::LevelPackageV1Limits
make_cli_native_level_package_limits() {
    return openrc::LevelPackageV1Limits{kMaximumCliNativeLevelPackageBytes,
                                        64U,
                                        32U,
                                        512U,
                                        1024U,
                                        kMaximumCliRenderScenePayloadBytes,
                                        kMaximumCliNativeLevelPackageBytes -
                                            openrc::kLevelPackageHeaderBytesV1,
                                        64U};
}

[[nodiscard]] openrc::RenderSceneIoLimitsV1 make_cli_render_scene_io_limits() {
    return openrc::RenderSceneIoLimitsV1{
        kMaximumCliRenderScenePayloadBytes,
        openrc::runtime::make_level_scene_render_compile_profile_v1()
            .render_scene_limits};
}

[[nodiscard]] openrc::game::RuntimeLevelContentLimitsV1
make_cli_runtime_level_content_limits() {
    return openrc::game::make_runtime_level_content_limits_v1();
}

[[nodiscard]] constexpr openrc::PreparedGameV2FilesystemLimitsV1
make_cli_prepared_foundation_limits() {
    return openrc::PreparedGameV2FilesystemLimitsV1{
        openrc::PreparedGameV2Limits{
            1024U * 1024U,
            static_cast<std::uint32_t>(openrc::kDiscTocLevelCount),
            128U,
            1024U,
            kMaximumCliPreparedFoundationBytes,
            64U * 1024U * 1024U},
        make_cli_level_foundation_package_limits(),
        kMaximumCliPreparedFoundationBytes};
}

[[nodiscard]] constexpr openrc::PreparedGameV2FilesystemLimitsV1
make_cli_prepared_native_limits() {
    return openrc::PreparedGameV2FilesystemLimitsV1{
        openrc::PreparedGameV2Limits{
            1024U * 1024U,
            static_cast<std::uint32_t>(openrc::kDiscTocLevelCount), 128U, 1024U,
            kMaximumCliPreparedNativeBytes, 64U * 1024U * 1024U},
        make_cli_native_level_package_limits(), kMaximumCliPreparedNativeBytes};
}

[[nodiscard]] openrc::RacLevelFoundationCompileLimitsV1
make_cli_level_foundation_compile_limits(
    const openrc::LevelPackageV1Limits package_limits =
        make_cli_level_foundation_package_limits()) {
    const auto source_limits = make_cli_moby_asset_limits();
    return openrc::RacLevelFoundationCompileLimitsV1{
        source_limits.collision,
        source_limits.gameplay,
        make_cli_collision_compile_limits(),
        make_cli_collision_payload_limits(),
        make_cli_bootstrap_payload_limits(),
        package_limits};
}

[[nodiscard]] std::string level_source_locator(
    const std::uint32_t level_id,
    const std::string_view suffix) {
    std::ostringstream stream;
    stream << "rac1/level/" << std::setfill('0') << std::setw(3) << level_id
           << '/' << suffix;
    return stream.str();
}

[[nodiscard]] std::string level_package_path(
    const std::uint32_t level_id) {
    std::ostringstream stream;
    stream << "levels/" << std::setfill('0') << std::setw(3) << level_id
           << ".orlvl";
    return stream.str();
}

[[nodiscard]] bool cli_path_component_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right) noexcept {
#ifdef _WIN32
    const auto& left_native = left.native();
    const auto& right_native = right.native();
    if (left_native.size() >
            static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        right_native.size() >
            static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    return CompareStringOrdinal(
               left_native.data(), static_cast<int>(left_native.size()),
               right_native.data(), static_cast<int>(right_native.size()),
               TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

void require_publication_root_outside_source(
    const std::filesystem::path &destination_root,
    const std::filesystem::path &source, const char *const source_description) {
    std::error_code filesystem_error;
    const auto normalized_source =
        std::filesystem::canonical(source, filesystem_error);
    if (filesystem_error) {
        throw std::runtime_error(
            std::string("Cannot resolve the ") + source_description + ": " +
            filesystem_error.message());
    }
    filesystem_error.clear();
    const auto normalized_destination =
        std::filesystem::weakly_canonical(
            destination_root, filesystem_error);
    if (filesystem_error) {
        throw std::runtime_error(
            "Cannot resolve the native publication root: " +
            filesystem_error.message());
    }
    auto source_component = normalized_source.begin();
    const auto source_end = normalized_source.end();
    auto destination_component = normalized_destination.begin();
    const auto destination_end = normalized_destination.end();
    for (; destination_component != destination_end &&
           source_component != source_end;
         ++destination_component, ++source_component) {
        if (!cli_path_component_equal(
                *destination_component, *source_component)) {
            return;
        }
    }
    if (destination_component == destination_end) {
        throw std::runtime_error(
            std::string(
                "The native publication root cannot contain or equal the ") +
            source_description);
    }
}

[[nodiscard]] openrc::LevelPackageV1 compile_cli_level_foundation(
    const openrc::RacLevelMobyAssetsV1& assets,
                             const openrc::LevelPackageV1Limits package_limits =
                                 make_cli_level_foundation_package_limits()) {
    const openrc::RacLevelFoundationCompileRequestV1 request{
        assets.level_id,
        openrc::kOpenRcContentApiVersionV1,
        std::string(kSupportedRacBuildIdV1),
        {level_source_locator(assets.level_id, "core/collision"),
         assets.collision_source_bytes},
        {level_source_locator(assets.level_id, "gameplay"),
         assets.gameplay_source_bytes}};
    return openrc::compile_rac_level_foundation_package_v1(
        request, make_cli_level_foundation_compile_limits(package_limits));
}

[[nodiscard]] bool print_native_game_preparation_progress(
    const openrc::NativeGamePreparationProgressV1& progress,
    void*) noexcept {
    try {
        const auto level_prefix = [&progress]() {
            std::ostringstream stream;
            stream << '[' << (progress.completed_levels + 1U) << '/'
                   << progress.total_levels << "] Level "
                   << progress.level_id << ": ";
            return stream.str();
        };
        switch (progress.phase) {
        case openrc::NativeGamePreparationPhaseV1::validating_inputs:
            std::cout << "Validating native-game source inputs...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::hashing_inputs:
            std::cout << "Hashing the source image and prepared boot ELF...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::checking_existing_publication:
            if (progress.level_id ==
                openrc::kNativeGamePreparationNoLevelV1) {
                std::cout
                    << "Checking for a matching verified native game...\n";
            } else {
                std::cout << level_prefix()
                          << "validating existing native package...\n";
            }
            break;
        case openrc::NativeGamePreparationPhaseV1::loading_level_assets:
            std::cout << level_prefix() << "loading source assets...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::compiling_level_foundation:
            std::cout << level_prefix()
                      << "compiling collision and bootstrap...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::recovering_level_scene:
            std::cout << level_prefix()
                      << "recovering all scene records...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::compiling_render_scene:
            std::cout << level_prefix()
                      << "compiling neutral rendering data...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::encoding_level_package:
            std::cout << level_prefix() << "encoding native package...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::verifying_inputs:
            std::cout << "Verifying source inputs after preparation...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::publishing:
            std::cout << "Publishing the complete verified level set...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::publishing_staged:
            std::cout << "Verified staging set is ready for commit...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::publishing_commit:
            std::cout << "Replacing the previous verified installation...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::reusing_existing_publication:
            std::cout << "The existing native game matches the sources; "
                         "reusing it.\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::compiling_player_actor:
            std::cout << level_prefix()
                      << "compiling the reusable player actor...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::compiling_startup_media:
            std::cout << "Preparing the original intro...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::compiling_player_animation:
            std::cout << level_prefix()
                      << "compiling the player locomotion animations...\n";
            break;
        case openrc::NativeGamePreparationPhaseV1::compiling_entity_scene:
            std::cout << level_prefix()
                      << "compiling neutral player entity bindings...\n";
            break;
        }
        std::cout.flush();
        return std::cout.good();
    } catch (...) {
        return false;
    }
}

[[nodiscard]] openrc::game::PlayerSimulationProfileV1
make_cli_player_simulation_profile(double death_height_world);

struct CliPlayerSmokeReportV1 {
    openrc::LevelSpawnPointV1 spawn;
    openrc::game::PlayerSimulationSnapshotV1 snapshot;
    std::uint64_t collision_count = 0U;
    std::uint64_t landed_count = 0U;
    std::uint64_t wall_hit_count = 0U;
};

struct CliEntityGameplaySmokeReportV1 {
    std::size_t entity_definition_count = 0U;
    std::size_t entity_render_binding_count = 0U;
    std::size_t collectible_count = 0U;
    std::size_t destructible_count = 0U;
    std::optional<openrc::game::EntityGameplayEventV1> probe_event;
    std::uint64_t probe_item_total = 0U;
    std::optional<openrc::game::EntityGameplayEventV1>
        destructible_probe_event;
    std::string destructible_drop_item_key;
    std::uint64_t destructible_drop_item_total = 0U;
};

[[nodiscard]] CliPlayerSmokeReportV1 run_cli_player_smoke(
    const openrc::game::RuntimeLevelFoundationV1& foundation) {
    const auto* const spawn = openrc::find_level_spawn_point_v1(
        foundation.bootstrap, foundation.bootstrap.default_spawn_id);
    if (spawn == nullptr) {
        throw std::runtime_error(
            "The runtime level foundation has no default spawn");
    }

    const auto player_profile = make_cli_player_simulation_profile(
        foundation.bootstrap.death_height_world);
    auto player = openrc::game::make_runtime_level_player_simulation_v1(
        foundation, player_profile.character,
        player_profile.fixed_ticks_per_second);
    const auto forward_x = static_cast<std::int16_t>(std::lround(
        std::cos(spawn->facing_yaw_radians) *
        openrc::game::kGameInputAxisMagnitudeV1));
    const auto forward_y = static_cast<std::int16_t>(std::lround(
        std::sin(spawn->facing_yaw_radians) *
        openrc::game::kGameInputAxisMagnitudeV1));

    CliPlayerSmokeReportV1 result;
    result.spawn = *spawn;
    for (std::uint64_t tick = 0U; tick < 360U; ++tick) {
        openrc::game::GameInputCommandV1 input;
        input.tick_index = tick;
        if (tick >= 120U && tick < 240U) {
            input.axes.move_x = forward_x;
            input.axes.move_y = forward_y;
        }
        if (tick == 180U) {
            input.pressed_buttons = openrc::game::game_button_mask_v1(
                openrc::game::GameButtonV1::jump);
        }
        const auto step = player.fixed_update(
            foundation.collision_world, input);
        result.collision_count += step.character.collision_count;
        result.landed_count += step.character.landed ? 1U : 0U;
        result.wall_hit_count += step.character.hit_wall ? 1U : 0U;
    }
    result.snapshot = player.snapshot();
    return result;
}

[[nodiscard]] CliEntityGameplaySmokeReportV1 run_cli_entity_gameplay_smoke(
    const openrc::game::RuntimeLevelContentV1& content) {
    CliEntityGameplaySmokeReportV1 result;
    if (!content.entity_scene) {
        return result;
    }
    result.entity_definition_count = content.entity_scene->definitions.size();
    result.entity_render_binding_count =
        content.entity_scene->render_bindings.size();
    openrc::GameplaySceneV1 gameplay_scene;
    gameplay_scene.level_id = content.foundation.level_id;
    if (content.gameplay_scene) {
        gameplay_scene = *content.gameplay_scene;
        result.collectible_count = gameplay_scene.collectibles.size();
    }
    if (content.destructible_scene) {
        result.destructible_count =
            content.destructible_scene->destructibles.size();
    }

    openrc::game::RuntimeGameplaySessionOptionsV1 options;
    options.entity_gameplay = openrc::game::RuntimeGameplayEntityContentV1{
        *content.entity_scene,
        gameplay_scene,
        openrc::game::make_runtime_entity_gameplay_limits_v1(),
        content.destructible_scene,
    };
    openrc::game::RuntimeGameplaySessionV1 session(content.foundation,
                                                   std::move(options));

    if (!gameplay_scene.collectibles.empty()) {
        const auto& collectible = gameplay_scene.collectibles.front();
        const auto transform = std::lower_bound(
            content.entity_scene->transforms.begin(),
            content.entity_scene->transforms.end(), collectible.authored_id,
            [](const openrc::EntityTransformComponentV1& candidate,
               const std::uint32_t authored_id) {
                return candidate.authored_id < authored_id;
            });
        if (transform == content.entity_scene->transforms.end() ||
            transform->authored_id != collectible.authored_id) {
            throw std::runtime_error(
                "The collectible smoke target has no authored transform");
        }
        auto checkpoint = session.player().snapshot().checkpoint;
        checkpoint.checkpoint_id = collectible.authored_id;
        checkpoint.feet_position = openrc::game::world_collectible_center_v1(
            transform->transform, collectible);
        checkpoint.facing_yaw_radians = 0.0;
        session.set_checkpoint(checkpoint, true);

        const auto frame = session.advance_frame(16'666'667U);
        for (const auto& tick : frame.ticks) {
            for (const auto& event : tick.gameplay_events) {
                if (event.authored_id == collectible.authored_id &&
                    event.kind == openrc::game::EntityGameplayEventKindV1::
                                      item_collected) {
                    if (result.probe_event) {
                        throw std::runtime_error(
                            "The collectible smoke target emitted more than "
                            "once");
                    }
                    result.probe_event = event;
                }
            }
        }
        if (!result.probe_event ||
            result.probe_event->item_key != collectible.item_key ||
            result.probe_event->amount != collectible.amount) {
            throw std::runtime_error(
                "The real mounted collectible did not emit its canonical "
                "event");
        }
        result.probe_item_total = session.item_total(collectible.item_key);
        if (result.probe_item_total < collectible.amount) {
            throw std::runtime_error(
                "The real mounted collectible did not update semantic "
                "inventory");
        }
    }

    if (content.destructible_scene &&
        !content.destructible_scene->destructibles.empty()) {
        const auto& destructible =
            content.destructible_scene->destructibles.front();
        const auto transform = std::lower_bound(
            content.entity_scene->transforms.begin(),
            content.entity_scene->transforms.end(), destructible.authored_id,
            [](const openrc::EntityTransformComponentV1& candidate,
               const std::uint32_t authored_id) {
                return candidate.authored_id < authored_id;
            });
        if (transform == content.entity_scene->transforms.end() ||
            transform->authored_id != destructible.authored_id ||
            destructible.drops.empty()) {
            throw std::runtime_error(
                "The destructible smoke target is incomplete");
        }
        const auto center = openrc::game::world_destructible_center_v1(
            transform->transform, destructible);
        const auto& combat = session.profile().combat;
        auto checkpoint = session.player().snapshot().checkpoint;
        checkpoint.checkpoint_id = destructible.authored_id;
        checkpoint.feet_position = {
            center.x - (combat.forward_start + combat.forward_end) * 0.5,
            center.y,
            center.z - combat.vertical_offset,
        };
        checkpoint.facing_yaw_radians = 0.0;
        session.set_checkpoint(checkpoint, true);

        openrc::game::GameInputSampleV1 attack;
        attack.held_buttons = openrc::game::game_button_mask_v1(
            openrc::game::GameButtonV1::primary_action);
        const auto frame = session.advance_frame(50'000'001U, attack);
        for (const auto& tick : frame.ticks) {
            for (const auto& event : tick.gameplay_events) {
                if (event.authored_id == destructible.authored_id &&
                    event.kind == openrc::game::EntityGameplayEventKindV1::
                                      entity_destroyed) {
                    if (result.destructible_probe_event) {
                        throw std::runtime_error(
                            "The destructible smoke target was destroyed more "
                            "than once");
                    }
                    result.destructible_probe_event = event;
                }
            }
        }
        const auto* const runtime = session.entity_gameplay();
        if (!result.destructible_probe_event || runtime == nullptr ||
            !runtime->destroyed(destructible.authored_id) ||
            runtime->enabled(destructible.authored_id) ||
            runtime->health(destructible.authored_id) != 0U) {
            throw std::runtime_error(
                "The real mounted destructible did not reach canonical "
                "destroyed state");
        }
        const auto& drop = destructible.drops.front();
        result.destructible_drop_item_key = drop.item_key;
        result.destructible_drop_item_total = session.item_total(drop.item_key);
        if (result.destructible_drop_item_total < drop.amount) {
            throw std::runtime_error(
                "The destructible smoke target did not grant its semantic "
                "drop");
        }
    }
    return result;
}

[[nodiscard]] openrc::game::PlayerSimulationProfileV1
make_cli_player_simulation_profile(const double death_height_world) {
    openrc::game::CharacterControllerProfileV1 character;
    character.capsule_radius = 0.35;
    character.capsule_height = 1.65;
    character.skin_width = 0.02;
    character.ground_probe_distance = 0.30;
    character.step_height = 0.55;
    character.maximum_slope_degrees = 50.0;
    character.maximum_ground_speed =
        openrc::game::kRacPlayerStandardFastGroundSpeedV1;
    character.ground_acceleration = 30.0;
    character.ground_deceleration = 40.0;
    character.air_acceleration = 10.0;
    character.gravity = 18.0;
    character.jump_speed = 7.0;
    character.maximum_fall_speed = 40.0;
    character.maximum_substep_distance = 0.05;
    character.maximum_motion_substeps = 256U;
    character.maximum_slide_iterations = 4U;
    character.maximum_depenetration_iterations = 64U;
    character.collision_layers = openrc::kCollisionAllLayersMaskV1;
    character.query_limits = openrc::CollisionQueryLimitsV1{4096U, 8192U};
    return openrc::game::PlayerSimulationProfileV1{
        character, 60U, death_height_world};
}

void print_usage() {
    std::cout
        << "OpenRC command-line tools " << OPENRC_VERSION << "\n\n"
        << "Usage:\n"
        << "  openrc-cli inspect <disc.iso>                    Inspect a "
           "PlayStation 2 disc image\n"
        << "  openrc-cli inventory <disc.iso>                  List files "
           "stored "
           "in a disc image\n"
        << "  openrc-cli toc <disc.iso>                        Inventory the "
           "Ratchet & Clank disc TOC\n"
        << "  openrc-cli toc-assets <disc.iso>                 Validate local "
           "TOC asset tables\n"
        << "  openrc-cli wad <disc.iso> <global-slot>          Decode one "
           "WadV1 "
           "(64 MiB cap)\n"
        << "  openrc-cli wad-payload-inventory <disc.iso> [output.tsv]\n"
        << "                                                    Classify every "
           "decoded WadV1 payload\n"
        << "  openrc-cli wad-families <disc.iso> [output.tsv]  Rank structural "
           "payload families for Veldin\n"
        << "  openrc-cli wad-payload-export <disc.iso> <unique> <output.bin>\n"
        << "                                                    Export one "
           "explicitly selected decoded payload\n"
        << "  openrc-cli wad-scene-animation <disc.iso> <unique>\n"
        << "                                                    Inspect camera "
           "and actor animation tracks\n"
        << "  openrc-cli wad-gameplay <disc.iso> <unique>      Inspect RAC1 "
           "level objects and gameplay blocks\n"
        << "  openrc-cli wad-moby-class <disc.iso> <unique>    Inspect one "
           "decoded RAC1 object model core\n"
        << "  openrc-cli wad-subtitles <disc.iso> <unique>     Inspect a "
           "localized subtitle bank\n"
        << "  openrc-cli vagp <disc.iso> <global-slot> [output.wav]\n"
        << "                                                    Inspect/export "
           "VAGp as mono PCM\n"
        << "  openrc-cli boundary <disc.iso> <global-slot>     Inspect a "
           "seven-region payload table\n"
        << "  openrc-cli map-art <disc.iso> <level-id> [output.tga]\n"
        << "                                                    Inspect/export "
           "a "
           "3-panel map preview\n"
        << "  openrc-cli ps2-save <disc.iso>                   Inspect the "
           "PS2D "
           "save/icon bundle\n"
        << "  openrc-cli sblk <disc.iso> <level-id>            Inspect a level "
           "SBlk audio bank\n"
        << "  openrc-cli sblk-wav <disc.iso> <level-id> <block> <output.wav> "
           "spu-native-48000\n"
        << "  openrc-cli sblk-wav <disc.iso> <level-id> <block> <output.wav> "
           "caller-supplied-hz <hz>\n"
        << "                                                    Export one "
           "physical SBlk block with explicit rate policy\n"
        << "  openrc-cli scene-blocks <disc.iso> <level-id>    Inspect a level "
           "scene-block directory\n"
        << "  openrc-cli scene-block-vu-run <disc.iso> <elf> <level-id> "
           "<record> "
           "<entry-pair> [output.tga]\n"
        << "                                                    Execute and "
           "optionally export an auto-fit wireframe\n"
        << "  openrc-cli level-core <disc.iso> <level-id>      Link RAC1 model "
           "classes, assets, and placements\n"
        << "  openrc-cli level-collision <disc.iso> <level-id> Inspect "
           "authoritative level collision\n"
        << "  openrc-cli level-player-smoke <disc.iso> <level-id>\n"
        << "                                                    Run "
           "deterministic native movement on real collision\n"
        << "  openrc-cli level-foundation-package <disc.iso> <level-id> "
           "<output.orlvl>\n"
        << "                                                    Compile "
           "collision and spawn data into a native level package\n"
        << "  openrc-cli level-native-package <disc.iso> <elf> <level-id> "
           "<output.orlvl>\n"
        << "                                                    Compile "
           "collision, spawn, static scene, and textures\n"
        << "  openrc-cli level-package-smoke <input.orlvl>     Run native "
           "movement without the ISO\n"
        << "  openrc-cli level-native-package-smoke <input.orlvl>\n"
        << "                                                    Validate "
           "movement and rendering data without the ISO\n"
        << "  openrc-cli prepare-native-foundations <disc.iso> "
           "<absolute-root>\n"
        << "                                                    Compile and "
           "publish all native collision/spawn packages\n"
        << "  openrc-cli prepare-native-game <disc.iso> <prepared-elf> "
           "<absolute-root>\n"
        << "                                                    Compile and "
           "atomically publish all 19 renderable native levels\n"
        << "  openrc-cli validate-native-game <absolute-root>\n"
        << "                                                    Verify the exact "
           "current 19-level native profile\n"
        << "  openrc-cli prepared-level-smoke <absolute-root> <level-id>\n"
        << "                                                    Run a "
           "published "
           "collision/spawn foundation without the ISO\n"
        << "  openrc-cli prepared-native-level-smoke <absolute-root> "
           "<level-id>\n"
        << "                                                    Validate "
           "published movement and rendering data without the ISO\n"
        << "  openrc-cli level-moby-scene <disc.iso> <level-id>\n"
        << "                                                    Build static "
           "high-LOD Moby scene geometry\n"
        << "  openrc-cli level-tfrag-texture <disc.iso> <level-id> <texture> "
           "[output.tga]\n"
        << "                                                    Inspect/export "
           "a "
           "decoded terrain texture\n"
        << "  openrc-cli level-moby-texture <disc.iso> <level-id> <texture> "
           "[output.tga]\n"
        << "                                                    Decode/export "
           "one level Moby texture\n"
        << "  openrc-cli companion-wads <disc.iso> <level-id>  Inspect the "
           "terminal common WAD index\n"
        << "  openrc-cli wad-bundle <disc.iso> <lba> <sectors> Inspect a "
           "WadBundleV1 (64 MiB cap)\n"
        << "  openrc-cli twofip <disc.iso> <global-slot> [output.tga]\n"
        << "                                                    Inspect/export "
           "2FIP (16 Mi pixels)\n"
        << "  openrc-cli prepare <disc.iso> [games-directory]  Extract and "
           "verify game files\n"
        << "  openrc-cli elf <executable>                      Inspect a "
           "PlayStation 2 ELF\n"
        << "  openrc-cli r5900-boundaries <executable>         Inventory EE "
           "calls and syscall sites\n"
        << "  openrc-cli dvp-vu <elf> <entry-pairs> <overlay-sections>\n"
        << "                                                    Decode decimal "
           "CSV VU/ELF lists\n"
        << "  openrc-cli dvp-vu-run <elf> <entry-pair> <overlay-sections> "
           "<top-qword>\n"
        << "                                                    Microcode-only "
           "run with unknown state\n"
        << "  openrc-cli paths                                 Show "
           "application "
           "data directories\n"
        << "  openrc-cli help                                  Show this "
           "help\n";
}

[[nodiscard]] std::optional<std::uint64_t> parse_decimal_argument(
    const std::filesystem::path& argument) {
    const auto text = openrc::path_to_utf8(argument);
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(
        text.data(),
        text.data() + text.size(),
        value);
    if (text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<std::vector<std::uint16_t>>
parse_decimal_u16_list(
    const std::filesystem::path& argument,
    const std::size_t maximum_values) {
    const auto text = openrc::path_to_utf8(argument);
    if (text.empty() || maximum_values == 0U) {
        return std::nullopt;
    }

    std::vector<std::uint16_t> values;
    std::size_t begin = 0U;
    while (begin < text.size()) {
        const auto separator = text.find(',', begin);
        const auto end = separator == std::string::npos ? text.size()
                                                        : separator;
        std::uint32_t value = 0U;
        const auto parsed = std::from_chars(
            text.data() + begin,
            text.data() + end,
            value);
        if (begin == end || parsed.ec != std::errc{} ||
            parsed.ptr != text.data() + end ||
            value > std::numeric_limits<std::uint16_t>::max()) {
            return std::nullopt;
        }
        if (values.size() >= maximum_values) {
            return std::nullopt;
        }
        values.push_back(static_cast<std::uint16_t>(value));
        if (separator == std::string::npos) {
            break;
        }
        begin = separator + 1U;
    }
    if (values.empty() || text.back() == ',') {
        return std::nullopt;
    }
    return values;
}

[[nodiscard]] const char* dvp_vu_termination_name(
    const openrc::DvpVuTerminationV1 termination) noexcept {
    switch (termination) {
    case openrc::DvpVuTerminationV1::program_end:
        return "program-end";
    case openrc::DvpVuTerminationV1::stopped_after_xgkick:
        return "first-xgkick";
    case openrc::DvpVuTerminationV1::instruction_limit:
        return "instruction-limit";
    case openrc::DvpVuTerminationV1::xgkick_event_limit:
        return "xgkick-event-limit";
    case openrc::DvpVuTerminationV1::xgkick_tag_limit:
        return "xgkick-tag-limit";
    case openrc::DvpVuTerminationV1::xgkick_qword_limit:
        return "xgkick-qword-limit";
    case openrc::DvpVuTerminationV1::indeterminate_control:
        return "indeterminate-control";
    case openrc::DvpVuTerminationV1::indeterminate_memory_address:
        return "indeterminate-memory-address";
    case openrc::DvpVuTerminationV1::unsupported_instruction:
        return "unsupported-instruction";
    case openrc::DvpVuTerminationV1::unmapped_instruction:
        return "unmapped-instruction";
    }
    return "unknown";
}

[[nodiscard]] const char* dvp_vu_warning_name(
    const openrc::DvpVuExecutionWarningV1 warning) noexcept {
    switch (warning) {
    case openrc::DvpVuExecutionWarningV1::host_float_approximation:
        return "host-float-approximation";
    case openrc::DvpVuExecutionWarningV1::vu_add_sub_reference_model:
        return "vu-add-sub-reference-model";
    case openrc::DvpVuExecutionWarningV1::vu_mul_reference_model:
        return "vu-mul-reference-model";
    case openrc::DvpVuExecutionWarningV1::vu_madd_reference_model:
        return "vu-madd-reference-model";
    case openrc::DvpVuExecutionWarningV1::vu_div_reference_model:
        return "vu-div-reference-model";
    case openrc::DvpVuExecutionWarningV1::q_read_before_ready:
        return "q-read-before-ready";
    case openrc::DvpVuExecutionWarningV1::
            forced_store_commit_for_xgkick_snapshot:
        return "forced-store-commit-for-xgkick-snapshot";
    case openrc::DvpVuExecutionWarningV1::
            documented_undefined_e_delay_memory:
        return "documented-undefined-e-delay-memory";
    }
    return "unknown";
}

[[nodiscard]] std::vector<std::byte> read_bounded_binary_file(
    const std::filesystem::path& path,
    const std::uint64_t maximum_bytes) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Cannot open the input file");
    }
    const auto end_position = input.tellg();
    if (end_position < 0) {
        throw std::runtime_error("Cannot determine the input file size");
    }
    const auto byte_count = static_cast<std::uint64_t>(end_position);
    if (byte_count > maximum_bytes ||
        byte_count > std::numeric_limits<std::size_t>::max() ||
        byte_count > static_cast<std::uint64_t>(
                         std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("The input file exceeds the CLI size limit");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(byte_count));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty() &&
        !input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Cannot read the complete input file");
    }
    return bytes;
}

[[nodiscard]] const char* wad_bundle_kind_name(
    const openrc::WadBundleRecordKind kind) noexcept {
    switch (kind) {
    case openrc::WadBundleRecordKind::empty:
        return "empty";
    case openrc::WadBundleRecordKind::nested_wad:
        return "WAD";
    case openrc::WadBundleRecordKind::elf:
        return "ELF";
    }
    return "unknown";
}

[[nodiscard]] std::string escaped_terminal_text(
    const std::string_view text) {
    constexpr std::array<char, 16> kHexDigits{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    };
    std::string escaped;
    escaped.reserve(text.size());
    for (const auto character : text) {
        const auto value = static_cast<unsigned char>(character);
        if (value >= 0x20U && value <= 0x7eU) {
            escaped.push_back(character);
            continue;
        }
        escaped.push_back('\\');
        escaped.push_back('x');
        escaped.push_back(kHexDigits[value >> 4U]);
        escaped.push_back(kHexDigits[value & 0x0fU]);
    }
    return escaped;
}

[[nodiscard]] std::vector<std::byte> read_disc_extent(
    const std::filesystem::path& image_path,
    const std::uint64_t logical_block,
    const std::uint64_t sector_count,
    const std::uint64_t maximum_bytes) {
    if (sector_count > maximum_bytes / openrc::kDiscTocSectorSize) {
        throw std::runtime_error("The requested disc extent exceeds the CLI size limit");
    }
    if (logical_block >
        std::numeric_limits<std::uint64_t>::max() / openrc::kDiscTocSectorSize) {
        throw std::runtime_error("The requested disc extent offset overflows");
    }
    const auto offset = logical_block * openrc::kDiscTocSectorSize;
    const auto byte_count = sector_count * openrc::kDiscTocSectorSize;
    if (byte_count > std::numeric_limits<std::size_t>::max() ||
        offset > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max())) {
        throw std::runtime_error("The requested disc extent exceeds host limits");
    }

    std::ifstream input(image_path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Cannot open the disc image");
    }
    const auto end_position = input.tellg();
    if (end_position < 0) {
        throw std::runtime_error(
            "Cannot determine the disc image size from the open stream");
    }
    const auto image_bytes = static_cast<std::uint64_t>(end_position);
    if (offset > image_bytes || byte_count > image_bytes - offset) {
        throw std::runtime_error("The requested disc extent lies outside the image");
    }

    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        throw std::runtime_error("Cannot seek to the requested disc extent");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(byte_count));
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        throw std::runtime_error("Unexpected end of image while reading the disc extent");
    }
    return bytes;
}

struct LoadedLevelSBlkV1 {
    openrc::DiscTocExtent primary_extent;
    openrc::DiscTocSubrange subrange;
    openrc::SBlkBundleV3 bundle;
    openrc::SBlkAudioReportV1 audio;
};

[[nodiscard]] LoadedLevelSBlkV1 load_level_sblk_v1(
    const std::filesystem::path& image_path,
    const std::uint32_t level_id) {
    const auto assets = openrc::inspect_disc_toc_assets(image_path);
    const auto level_assets = std::find_if(
        assets.levels.begin(),
        assets.levels.end(),
        [level_id](const openrc::DiscTocLevelAssets& candidate) {
            return candidate.level_id == level_id;
        });
    const auto level_layout = std::find_if(
        assets.layout.levels.begin(),
        assets.layout.levels.end(),
        [level_id](const openrc::DiscTocLevelDescriptor& candidate) {
            return candidate.level_id == level_id;
        });
    if (level_assets == assets.levels.end() ||
        level_layout == assets.layout.levels.end()) {
        throw std::runtime_error("The requested level is absent from DiscTocV1");
    }

    constexpr std::size_t kSBlkPrimarySubrangeIndex = 1U;
    const auto subrange =
        level_assets->primary_extent0.subranges[kSBlkPrimarySubrangeIndex];
    if (subrange.byte_size == 0U) {
        throw std::runtime_error("The level's SBlk subrange is empty");
    }
    const auto primary_extent = level_layout->primary_extents.front();
    const auto primary_bytes = read_disc_extent(
        image_path,
        primary_extent.lba,
        primary_extent.sectors,
        kMaximumCliDecodedWadBytes);
    const auto subrange_offset =
        static_cast<std::uint64_t>(subrange.relative_offset);
    const auto subrange_size = static_cast<std::uint64_t>(subrange.byte_size);
    if (subrange_offset > primary_bytes.size() ||
        subrange_size > primary_bytes.size() - subrange_offset) {
        throw std::runtime_error("The SBlk subrange lies outside primary extent 0");
    }

    auto bundle = openrc::parse_sblk_bundle_v3(
        std::span<const std::byte>(primary_bytes).subspan(
            static_cast<std::size_t>(subrange_offset),
            static_cast<std::size_t>(subrange_size)),
        openrc::SBlkLimits{
            kMaximumCliDecodedWadBytes,
            kMaximumCliSBlkItems,
            kMaximumCliSBlkItems,
            kMaximumCliDecodedWadBytes});
    auto audio = openrc::analyze_sblk_audio_v1(
        bundle,
        openrc::SBlkAudioLimits{
            kMaximumCliSBlkItems,
            kMaximumCliSBlkBlocks,
            kMaximumCliDecodedWadBytes});
    return LoadedLevelSBlkV1{
        primary_extent,
        subrange,
        std::move(bundle),
        std::move(audio)};
}

struct WadPayloadCorpusCountersV1 {
    std::uint64_t global_payloads = 0U;
    std::uint64_t global_tail_payloads = 0U;
    std::uint64_t local_run_payloads = 0U;
    std::uint64_t primary_subrange_payloads = 0U;
    std::uint64_t primary_extent_payloads = 0U;
    std::uint64_t bundle_record_payloads = 0U;
    std::uint64_t companion_record_payloads = 0U;
};

struct CapturedWadPayloadV1 {
    openrc::WadPayloadAddResultV1 added;
    openrc::WadPayloadOriginV1 origin;
    std::vector<std::byte> bytes;
};

struct WadPayloadCaptureStateV1 {
    std::uint64_t requested_unique_payload_index = 0U;
    std::optional<CapturedWadPayloadV1> captured;
};

struct WadPayloadCorpusReportV1 {
    openrc::WadPayloadInventoryV1 inventory;
    openrc::WadPayloadFamilyInventoryV1 families;
    WadPayloadCorpusCountersV1 counters;
    std::vector<std::string> format_names;
    std::optional<CapturedWadPayloadV1> captured_payload;
};

[[nodiscard]] std::string sha256_hex(
    const std::span<const std::byte> bytes) {
    openrc::Sha256 hash;
    hash.update(bytes);
    return openrc::hex_digest(hash.finish());
}

[[nodiscard]] std::string escaped_single_byte_text(
    const std::span<const std::byte> bytes) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(bytes.size());
    for (const auto byte : bytes) {
        const auto value = std::to_integer<std::uint8_t>(byte);
        if (value >= 0x20U && value <= 0x7eU && value != '%') {
            result.push_back(static_cast<char>(value));
        } else {
            result.push_back('%');
            result.push_back(kHex[value >> 4U]);
            result.push_back(kHex[value & 0x0fU]);
        }
    }
    return result;
}

[[nodiscard]] openrc::WadPayloadAddResultV1 add_wad_payload_and_nested_bundles_v1(
    openrc::WadPayloadInventoryBuilderV1& builder,
    openrc::WadPayloadFamilyBuilderV1& family_builder,
    const openrc::WadPayloadOriginV1& origin,
    const std::span<const std::byte> decoded_bytes,
    WadPayloadCorpusCountersV1& counters,
    const std::uint32_t nesting_depth,
    WadPayloadCaptureStateV1* const capture) {
    const auto parent =
        builder.add_decoded_payload(origin, decoded_bytes);
    family_builder.observe(parent, decoded_bytes);
    if (capture != nullptr && !capture->captured &&
        parent.unique_payload_index ==
            capture->requested_unique_payload_index) {
        capture->captured = CapturedWadPayloadV1{
            parent,
            origin,
            std::vector<std::byte>(decoded_bytes.begin(), decoded_bytes.end())};
    }

    openrc::WadBundleV1 bundle;
    try {
        bundle = openrc::parse_wad_bundle_v1(decoded_bytes);
    } catch (const openrc::WadBundleError&) {
        return parent;
    }
    if (nesting_depth >= kMaximumCliWadBundleNestingDepth) {
        throw std::runtime_error(
            "Decoded WadBundleV1 nesting exceeds the CLI depth limit");
    }

    const auto add_record = [&builder,
                             &family_builder,
                             &origin,
                             &decoded_bytes,
                             &counters,
                             capture,
                             parent,
                             nesting_depth](
                                const openrc::WadBundleRecord& record,
                                const std::uint32_t record_index) {
        const auto logical_wad = decoded_bytes.subspan(
            record.offset,
            record.size);
        const auto nested = openrc::decode_wad_bytes(
            logical_wad,
            kMaximumCliDecodedWadBytes);
        openrc::WadPayloadOriginV1 child_origin;
        child_origin.kind = openrc::WadPayloadOriginKindV1::wad_bundle_record;
        child_origin.level_id = origin.level_id;
        child_origin.record_index = record_index;
        child_origin.lba = origin.lba;
        child_origin.container_byte_offset = record.offset;
        child_origin.encoded_bytes = record.size;
        child_origin.encoded_sha256 = sha256_hex(logical_wad);
        child_origin.parent_unique_payload_index = parent.unique_payload_index;
        child_origin.parent_observation_index = parent.observation_index;
        ++counters.bundle_record_payloads;
        (void)add_wad_payload_and_nested_bundles_v1(
            builder,
            family_builder,
            child_origin,
            nested.bytes,
            counters,
            nesting_depth + 1U,
            capture);
    };

    add_record(bundle.initial_record, 0U);
    for (std::size_t slot = 0U; slot < bundle.slots.size(); ++slot) {
        const auto& record = bundle.slots[slot];
        if (record.kind == openrc::WadBundleRecordKind::nested_wad) {
            add_record(record, static_cast<std::uint32_t>(slot + 1U));
        }
    }
    return parent;
}

[[nodiscard]] WadPayloadCorpusReportV1 inventory_disc_wad_payloads_v1(
    const std::filesystem::path& image_path,
    const std::optional<std::uint64_t> capture_unique_payload_index =
        std::nullopt) {
    auto probes = openrc::make_known_wad_payload_probes_v1(
        {kMaximumCliDecodedWadBytes,
         kMaximumCliSceneBlockRecords,
         kMaximumCliSceneAnimationActorTracks,
         kMaximumCliSceneAnimationTotalFrameRanges,
         kMaximumCliLocalizedSubtitleEntries});
    std::vector<std::string> format_names;
    format_names.reserve(probes.size());
    for (const auto& probe : probes) {
        format_names.push_back(probe.format_name);
    }
    openrc::WadPayloadInventoryBuilderV1 builder(
        std::move(probes),
        openrc::WadPayloadInventoryLimitsV1{
            kMaximumCliWadInventoryObservations,
            kMaximumCliWadInventoryUniquePayloads,
            kMaximumCliDecodedWadBytes,
            kMaximumCliWadInventoryTotalDecodedBytes,
            kMaximumCliWadInventoryProbes,
            kMaximumCliWadInventoryProbeInvocations});
    openrc::WadPayloadFamilyBuilderV1 family_builder(
        openrc::WadPayloadFamilyLimitsV1{
            kMaximumCliWadInventoryUniquePayloads,
            kMaximumCliWadInventoryObservations,
            kMaximumCliDecodedWadBytes,
            kMaximumCliWadInventoryTotalDecodedBytes,
            kMaximumCliWadFamilySampledBytesPerPayload,
            kMaximumCliWadFamilyTotalSampledBytes,
            kMaximumCliWadInventoryUniquePayloads,
            kMaximumCliWadInventoryUniquePayloads,
            kMaximumCliWadFamilyLevels,
            kMaximumCliWadFamilyUniqueLevelMemberships});
    WadPayloadCorpusCountersV1 counters;
    std::optional<WadPayloadCaptureStateV1> capture;
    if (capture_unique_payload_index) {
        capture = WadPayloadCaptureStateV1{
            *capture_unique_payload_index, std::nullopt};
    }
    auto* const capture_pointer = capture ? &*capture : nullptr;
    const auto assets = openrc::inspect_disc_toc_assets(image_path);

    for (const auto& entry : assets.layout.global_extents) {
        if (entry.signature != openrc::DiscTocSignature::wad) {
            continue;
        }
        const auto decoded = openrc::decode_wad(
            image_path,
            entry.extent.lba,
            entry.extent.sectors,
            kMaximumCliDecodedWadBytes);
        openrc::WadPayloadOriginV1 origin;
        origin.kind = openrc::WadPayloadOriginKindV1::global_toc;
        origin.container_index = static_cast<std::uint32_t>(entry.slot);
        origin.lba = entry.extent.lba;
        origin.encoded_bytes = decoded.source.total_bytes;
        origin.encoded_sha256 = decoded.source.sha256;
        ++counters.global_payloads;
        (void)add_wad_payload_and_nested_bundles_v1(
            builder,
            family_builder,
            origin,
            decoded.bytes,
            counters,
            0U,
            capture_pointer);
    }

    std::uint64_t global_tail_lba = 0U;
    for (const auto& entry : assets.layout.global_extents) {
        global_tail_lba = std::max(
            global_tail_lba,
            static_cast<std::uint64_t>(entry.extent.lba) +
                entry.extent.sectors);
    }
    const auto global_tail_header = read_disc_extent(
        image_path,
        global_tail_lba,
        1U,
        openrc::kDiscTocSectorSize);
    if (global_tail_header.size() < openrc::kWadV1HeaderSize ||
        global_tail_header[0] != std::byte{'W'} ||
        global_tail_header[1] != std::byte{'A'} ||
        global_tail_header[2] != std::byte{'D'}) {
        throw std::runtime_error(
            "The record following the global TOC extent chain is not WadV1");
    }
    const auto global_tail_logical_bytes =
        std::to_integer<std::uint32_t>(global_tail_header[3]) |
        (std::to_integer<std::uint32_t>(global_tail_header[4]) << 8U) |
        (std::to_integer<std::uint32_t>(global_tail_header[5]) << 16U) |
        (std::to_integer<std::uint32_t>(global_tail_header[6]) << 24U);
    if (global_tail_logical_bytes < openrc::kWadV1HeaderSize) {
        throw std::runtime_error(
            "The global-tail WadV1 record is smaller than its header");
    }
    const auto global_tail_sectors =
        (static_cast<std::uint64_t>(global_tail_logical_bytes) +
         openrc::kWadSectorSize - 1U) /
        openrc::kWadSectorSize;
    const auto global_tail = openrc::decode_wad(
        image_path,
        global_tail_lba,
        global_tail_sectors,
        kMaximumCliDecodedWadBytes);
    openrc::WadPayloadOriginV1 global_tail_origin;
    global_tail_origin.kind =
        openrc::WadPayloadOriginKindV1::global_toc_tail;
    global_tail_origin.lba = global_tail_lba;
    global_tail_origin.encoded_bytes = global_tail.source.total_bytes;
    global_tail_origin.encoded_sha256 = global_tail.source.sha256;
    ++counters.global_tail_payloads;
    (void)add_wad_payload_and_nested_bundles_v1(
        builder,
        family_builder,
        global_tail_origin,
        global_tail.bytes,
        counters,
        0U,
        capture_pointer);
    std::cerr
        << "  global WadV1 payloads: " << counters.global_payloads
        << " + " << counters.global_tail_payloads << " tail record\n";

    for (const auto& level : assets.levels) {
        const auto layout = std::find_if(
            assets.layout.levels.begin(),
            assets.layout.levels.end(),
            [&level](const openrc::DiscTocLevelDescriptor& candidate) {
                return candidate.level_id == level.level_id;
            });
        if (layout == assets.layout.levels.end()) {
            throw std::runtime_error(
                "A local asset level is absent from the DiscTocV1 layout");
        }

        for (std::size_t block_index = 0U;
             block_index < level.local_tables.resource_blocks.size();
             ++block_index) {
            const auto& resource =
                level.local_tables.resource_blocks[block_index];
            for (std::size_t run_index = 0U;
                 run_index < resource.wad_runs.size();
                 ++run_index) {
                const auto& run = resource.wad_runs[run_index];
                for (std::size_t record_index = 0U;
                     record_index < run.wads.size();
                     ++record_index) {
                    const auto& record = run.wads[record_index];
                    const auto decoded = openrc::decode_wad(
                        image_path,
                        record.lba,
                        record.occupied_sectors,
                        kMaximumCliDecodedWadBytes);
                    openrc::WadPayloadOriginV1 origin;
                    origin.kind =
                        openrc::WadPayloadOriginKindV1::local_wad_run;
                    origin.level_id = level.level_id;
                    origin.container_index =
                        static_cast<std::uint32_t>(block_index);
                    origin.run_index = static_cast<std::uint32_t>(run_index);
                    origin.record_index =
                        static_cast<std::uint32_t>(record_index);
                    origin.lba = record.lba;
                    origin.encoded_bytes = decoded.source.total_bytes;
                    origin.encoded_sha256 = decoded.source.sha256;
                    ++counters.local_run_payloads;
                    (void)add_wad_payload_and_nested_bundles_v1(
                        builder,
                        family_builder,
                        origin,
                        decoded.bytes,
                        counters,
                        0U,
                        capture_pointer);
                }
            }
        }

        const auto& primary_extent0 = layout->primary_extents.front();
        const auto primary_bytes = read_disc_extent(
            image_path,
            primary_extent0.lba,
            primary_extent0.sectors,
            kMaximumCliDecodedWadBytes);
        const auto bounded_subrange = [&primary_bytes](
                                          const openrc::DiscTocSubrange& subrange,
                                          const char* description) {
            const auto offset =
                static_cast<std::uint64_t>(subrange.relative_offset);
            const auto size = static_cast<std::uint64_t>(subrange.byte_size);
            if (offset > primary_bytes.size() ||
                size > primary_bytes.size() - offset) {
                throw std::runtime_error(
                    std::string(description) +
                    " lies outside primary extent 0");
            }
            return std::span<const std::byte>(primary_bytes).subspan(
                static_cast<std::size_t>(offset),
                static_cast<std::size_t>(size));
        };

        constexpr std::size_t kCompanionIndexSubrange = 2U;
        constexpr std::size_t kCompanionTargetSubrange = 10U;
        std::optional<openrc::DecodedWadBytes> companion_target;
        std::optional<openrc::WadPayloadAddResultV1> companion_parent;
        for (std::size_t subrange_index = 0U;
             subrange_index < level.primary_extent0.subranges.size();
             ++subrange_index) {
            const auto& subrange =
                level.primary_extent0.subranges[subrange_index];
            if (subrange.byte_size == 0U ||
                subrange.signature != openrc::DiscTocSignature::wad) {
                continue;
            }
            const auto logical_wad = bounded_subrange(
                subrange,
                "A primary-extent-0 WadV1 subrange");
            auto decoded = openrc::decode_wad_bytes(
                logical_wad,
                kMaximumCliDecodedWadBytes);
            openrc::WadPayloadOriginV1 origin;
            origin.kind =
                openrc::WadPayloadOriginKindV1::primary_extent0_subrange;
            origin.level_id = level.level_id;
            origin.container_index =
                static_cast<std::uint32_t>(subrange_index);
            origin.lba = primary_extent0.lba;
            origin.container_byte_offset = subrange.relative_offset;
            origin.encoded_bytes = subrange.byte_size;
            origin.encoded_sha256 = sha256_hex(logical_wad);
            ++counters.primary_subrange_payloads;
            const auto added = add_wad_payload_and_nested_bundles_v1(
                builder,
                family_builder,
                origin,
                decoded.bytes,
                counters,
                0U,
                capture_pointer);
            if (subrange_index == kCompanionTargetSubrange) {
                companion_target = std::move(decoded);
                companion_parent = added;
            }
        }

        for (std::size_t primary_index = 0U;
             primary_index < level.primary_wads.size();
             ++primary_index) {
            const auto& record = level.primary_wads[primary_index];
            if (record.occupied_sectors == 0U) {
                continue;
            }
            if (record.signature != openrc::DiscTocSignature::wad) {
                throw std::runtime_error(
                    "A declared primary WadV1 extent has another signature");
            }
            const auto decoded = openrc::decode_wad(
                image_path,
                record.lba,
                record.occupied_sectors,
                kMaximumCliDecodedWadBytes);
            openrc::WadPayloadOriginV1 origin;
            origin.kind =
                openrc::WadPayloadOriginKindV1::primary_extent;
            origin.level_id = level.level_id;
            origin.container_index =
                static_cast<std::uint32_t>(primary_index + 1U);
            origin.lba = record.lba;
            origin.encoded_bytes = decoded.source.total_bytes;
            origin.encoded_sha256 = decoded.source.sha256;
            ++counters.primary_extent_payloads;
            (void)add_wad_payload_and_nested_bundles_v1(
                builder,
                family_builder,
                origin,
                decoded.bytes,
                counters,
                0U,
                capture_pointer);
        }

        const auto& companion_index_subrange =
            level.primary_extent0.subranges[kCompanionIndexSubrange];
        if (!companion_target || !companion_parent ||
            companion_index_subrange.byte_size == 0U) {
            throw std::runtime_error(
                "A level is missing its companion WadV1 index or target");
        }
        const auto companion_index_bytes = bounded_subrange(
            companion_index_subrange,
            "The companion WadV1 index subrange");
        const auto companion =
            openrc::parse_companion_terminal_wad_index_v1(
                companion_index_bytes,
                companion_target->bytes,
                openrc::CompanionTerminalWadIndexLimits{
                    kMaximumCliDecodedWadBytes,
                    kMaximumCliDecodedWadBytes,
                    kMaximumCliWadInventoryObservations,
                    kMaximumCliDecodedWadBytes});
        for (std::size_t record_index = 0U;
             record_index < companion.records.size();
             ++record_index) {
            const auto& record = companion.records[record_index];
            const auto logical_wad = std::span<const std::byte>(record.wad_bytes);
            const auto decoded = openrc::decode_wad_bytes(
                logical_wad,
                kMaximumCliDecodedWadBytes);
            openrc::WadPayloadOriginV1 origin;
            origin.kind =
                openrc::WadPayloadOriginKindV1::companion_terminal_record;
            origin.level_id = level.level_id;
            origin.record_index =
                static_cast<std::uint32_t>(record_index);
            origin.lba = primary_extent0.lba;
            origin.container_byte_offset = record.target_offset;
            origin.encoded_bytes = record.logical_size;
            origin.encoded_sha256 = sha256_hex(logical_wad);
            origin.parent_unique_payload_index =
                companion_parent->unique_payload_index;
            origin.parent_observation_index =
                companion_parent->observation_index;
            ++counters.companion_record_payloads;
            (void)add_wad_payload_and_nested_bundles_v1(
                builder,
                family_builder,
                origin,
                decoded.bytes,
                counters,
                0U,
                capture_pointer);
        }
        std::cerr
            << "  level " << level.level_id
            << " complete (observations so far: "
            << (counters.global_payloads +
                counters.global_tail_payloads +
                counters.local_run_payloads +
                counters.primary_subrange_payloads +
                counters.primary_extent_payloads +
                counters.bundle_record_payloads +
                counters.companion_record_payloads)
            << ")\n";
    }

    auto inventory = builder.finalize();
    auto families = family_builder.finalize(inventory);
    const auto counted_observations =
        counters.global_payloads + counters.global_tail_payloads +
        counters.local_run_payloads + counters.primary_subrange_payloads +
        counters.primary_extent_payloads + counters.bundle_record_payloads +
        counters.companion_record_payloads;
    if (counted_observations != inventory.observations.size()) {
        throw std::runtime_error("The WadV1 source counters do not cover every "
                                 "retained observation");
    }
    if (capture && !capture->captured) {
        throw std::runtime_error(
            "The requested WadV1 unique payload index does not exist");
    }
    return WadPayloadCorpusReportV1{
        std::move(inventory),
        std::move(families),
        counters,
        std::move(format_names),
        capture ? std::move(capture->captured) : std::nullopt};
}

void write_new_binary_file(
    const std::filesystem::path& output_path,
    const std::span<const std::byte> bytes) {
    if (output_path.empty()) {
        throw std::runtime_error("The output path is empty");
    }

#ifdef _WIN32
    const auto handle = CreateFileW(
        output_path.c_str(),
        GENERIC_WRITE | DELETE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
            throw std::runtime_error(
                "The output path already exists; refusing to overwrite it");
        }
        throw std::runtime_error(
            "Cannot exclusively create the output file (Windows error " +
            std::to_string(error) + ")");
    }

    std::size_t offset = 0;
    DWORD write_error = ERROR_SUCCESS;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto requested = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (WriteFile(
                handle,
                bytes.data() + offset,
                requested,
                &written,
                nullptr) == FALSE ||
            written != requested) {
            write_error = GetLastError();
            if (write_error == ERROR_SUCCESS) {
                write_error = ERROR_WRITE_FAULT;
            }
            break;
        }
        offset += written;
    }
    if (write_error == ERROR_SUCCESS && FlushFileBuffers(handle) == FALSE) {
        write_error = GetLastError();
    }
    if (write_error != ERROR_SUCCESS) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        const bool cleanup_scheduled = SetFileInformationByHandle(
            handle,
            FileDispositionInfo,
            &disposition,
            sizeof(disposition)) != FALSE;
        CloseHandle(handle);
        if (!cleanup_scheduled) {
            throw std::runtime_error(
                "Cannot write the complete output file, and Windows could not "
                "schedule the partial file for removal (write error " +
                std::to_string(write_error) + ")");
        }
        throw std::runtime_error(
            "Cannot write the complete output file (Windows error " +
            std::to_string(write_error) + ")");
    }
    if (CloseHandle(handle) == FALSE) {
        throw std::runtime_error(
            "Cannot close the completed output file (Windows error " +
            std::to_string(GetLastError()) + ")");
    }
#else
    auto temporary_template = output_path.string() + ".tmp-XXXXXX";
    std::vector<char> temporary_name(
        temporary_template.begin(),
        temporary_template.end());
    temporary_name.push_back('\0');
    const auto descriptor = ::mkstemp(temporary_name.data());
    if (descriptor < 0) {
        throw std::runtime_error(
            "Cannot create a unique temporary output file: " +
            std::string(std::strerror(errno)));
    }
    const std::filesystem::path temporary_path(temporary_name.data());
#ifdef FD_CLOEXEC
    if (::fcntl(descriptor, F_SETFD, FD_CLOEXEC) != 0) {
        const auto flag_error = errno;
        (void)::unlink(temporary_path.c_str());
        (void)::close(descriptor);
        throw std::runtime_error(
            "Cannot protect the temporary output descriptor: " +
            std::string(std::strerror(flag_error)));
    }
#endif

    std::size_t offset = 0;
    int write_error = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto requested = std::min<std::size_t>(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const auto written = ::write(
            descriptor,
            bytes.data() + offset,
            requested);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            write_error = errno;
            break;
        }
        if (written == 0) {
            write_error = EIO;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (write_error == 0 && ::fsync(descriptor) != 0) {
        write_error = errno;
    }
    if (::close(descriptor) != 0 && write_error == 0) {
        write_error = errno;
    }
    if (write_error != 0) {
        const bool removed = ::unlink(temporary_path.c_str()) == 0;
        const auto cleanup_error = removed ? 0 : errno;
        if (!removed) {
            throw std::runtime_error(
                "Cannot write the complete output file, and the unique "
                "temporary file could not be removed: " +
                std::string(std::strerror(cleanup_error)));
        }
        throw std::runtime_error(
            "Cannot write the complete output file: " +
            std::string(std::strerror(write_error)));
    }

    if (::link(temporary_path.c_str(), output_path.c_str()) != 0) {
        const auto publish_error = errno;
        const bool removed = ::unlink(temporary_path.c_str()) == 0;
        if (publish_error == EEXIST) {
            if (!removed) {
                throw std::runtime_error(
                    "The output path already exists, and the unique temporary "
                    "file could not be removed");
            }
            throw std::runtime_error(
                "The output path already exists; refusing to overwrite it");
        }
        throw std::runtime_error(
            std::string("Cannot atomically publish the output file") +
            (removed ? ": " : ", and the temporary file could not be removed: ") +
            std::strerror(publish_error));
    }
    if (::unlink(temporary_path.c_str()) != 0) {
        throw std::runtime_error(
            "The output was published, but its temporary hard link could not "
            "be removed: " + std::string(std::strerror(errno)));
    }
#endif
}

[[nodiscard]] const char* phase_name(const openrc::PreparationPhase phase) {
    switch (phase) {
    case openrc::PreparationPhase::scanning:
        return "scanning disc";
    case openrc::PreparationPhase::hashing_image:
        return "hashing image";
    case openrc::PreparationPhase::verifying_files:
        return "verifying files";
    case openrc::PreparationPhase::extracting_files:
        return "extracting files";
    case openrc::PreparationPhase::writing_manifest:
        return "writing manifest";
    case openrc::PreparationPhase::finalizing:
        return "finalizing";
    }
    return "working";
}

class ProgressPrinter final {
public:
    [[nodiscard]] bool update(const openrc::PreparationProgress& progress) {
        const auto now = std::chrono::steady_clock::now();
        const bool phase_changed = !last_phase_ || *last_phase_ != progress.phase;
        const bool completed = progress.total_bytes != 0 &&
                               progress.bytes_processed >= progress.total_bytes;
        const bool completion_changed = completed && !last_was_complete_;

        if (!phase_changed && !completion_changed &&
            now - last_update_ < std::chrono::milliseconds(250)) {
            return true;
        }

        if (phase_changed && has_line_) {
            std::cerr << '\n';
            line_width_ = 0;
        }

        std::ostringstream line;
        line << '[' << phase_name(progress.phase) << ']';

        if (progress.total_bytes != 0) {
            const auto bounded = std::min(progress.bytes_processed, progress.total_bytes);
            const double percent = 100.0 * static_cast<double>(bounded) /
                                   static_cast<double>(progress.total_bytes);
            line << ' ' << std::fixed << std::setprecision(1) << percent << "% ("
                 << progress.bytes_processed << '/' << progress.total_bytes << " bytes)";
        } else if (progress.bytes_processed != 0) {
            line << ' ' << progress.bytes_processed << " bytes";
        }

        if (progress.file_count != 0) {
            line << " file " << progress.file_index << '/' << progress.file_count;
        }
        if (!progress.current_path.empty()) {
            line << "  " << progress.current_path;
        }

        const std::string text = line.str();
        std::cerr << '\r' << text;
        if (text.size() < line_width_) {
            std::cerr << std::string(line_width_ - text.size(), ' ');
        }
        std::cerr << std::flush;

        line_width_ = text.size();
        has_line_ = true;
        last_phase_ = progress.phase;
        last_update_ = now;
        last_was_complete_ = completed;
        return true;
    }

    void finish() {
        if (has_line_) {
            std::cerr << '\n';
            has_line_ = false;
            line_width_ = 0;
        }
    }

private:
    std::optional<openrc::PreparationPhase> last_phase_;
    std::chrono::steady_clock::time_point last_update_{};
    std::size_t line_width_ = 0;
    bool has_line_ = false;
    bool last_was_complete_ = false;
};

[[nodiscard]] std::string hexadecimal(const std::uint64_t value, const int width = 0) {
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << std::setfill('0');
    if (width > 0) {
        output << std::setw(width);
    }
    output << value;
    return output.str();
}

void print_dvp_gif_registers(const openrc::DvpVuGifTagV1& tag) {
    if (!tag.registers_known) {
        std::cout << "indeterminate";
        return;
    }
    for (std::size_t index = 0U; index < tag.register_count; ++index) {
        if (index != 0U) {
            std::cout << ',';
        }
        std::cout << hexadecimal(tag.registers[index], 1);
    }
}

[[nodiscard]] std::string_view gif_gs_topology_name(
    const openrc::GifGsPrimitiveTopologyV1 topology) noexcept {
    switch (topology) {
    case openrc::GifGsPrimitiveTopologyV1::point:
        return "points";
    case openrc::GifGsPrimitiveTopologyV1::line_list:
        return "line-list";
    case openrc::GifGsPrimitiveTopologyV1::line_strip:
        return "line-strip";
    case openrc::GifGsPrimitiveTopologyV1::triangle_list:
        return "triangle-list";
    case openrc::GifGsPrimitiveTopologyV1::triangle_strip:
        return "triangle-strip";
    case openrc::GifGsPrimitiveTopologyV1::triangle_fan:
        return "triangle-fan";
    case openrc::GifGsPrimitiveTopologyV1::sprite:
        return "sprites";
    case openrc::GifGsPrimitiveTopologyV1::invalid:
        return "invalid";
    }
    return "unknown";
}

[[nodiscard]] std::string_view gif_gs_address_name(
    const std::uint8_t address) noexcept {
    switch (address) {
    case 0x00U:
        return "PRIM";
    case 0x18U:
        return "XYOFFSET_1";
    case 0x19U:
        return "XYOFFSET_2";
    case 0x1aU:
        return "PRMODECONT";
    case 0x1bU:
        return "PRMODE";
    case 0x40U:
        return "SCISSOR_1";
    case 0x41U:
        return "SCISSOR_2";
    default:
        return {};
    }
}

[[nodiscard]] openrc::TwoFipImage make_gif_gs_wireframe_image(
    const openrc::GifGsDecodeResultV1& report) {
    constexpr std::uint32_t kWidth = 1280U;
    constexpr std::uint32_t kHeight = 720U;
    constexpr int kMargin = 32;

    std::optional<std::uint16_t> minimum_x;
    std::optional<std::uint16_t> maximum_x;
    std::optional<std::uint16_t> minimum_y;
    std::optional<std::uint16_t> maximum_y;
    std::uint64_t emitted_count = 0U;
    for (const auto& primitive : report.primitives) {
        if (primitive.emission != openrc::GifGsPrimitiveEmissionV1::emitted) {
            continue;
        }
        ++emitted_count;
        if (primitive.vertex_count == 0U ||
            primitive.vertex_count > primitive.vertex_indices.size()) {
            throw std::runtime_error(
                "An emitted GS primitive has an invalid vertex count");
        }
        for (std::size_t index = 0U; index < primitive.vertex_count; ++index) {
            const auto vertex_index = primitive.vertex_indices[index];
            if (vertex_index >= report.vertices.size()) {
                throw std::runtime_error(
                    "An emitted GS primitive references a missing vertex");
            }
            const auto& vertex = report.vertices[
                static_cast<std::size_t>(vertex_index)];
            if (!vertex.x || !vertex.y) {
                throw std::runtime_error(
                    "An emitted GS primitive has indeterminate XY coordinates");
            }
            minimum_x = minimum_x ? std::min(*minimum_x, *vertex.x) : vertex.x;
            maximum_x = maximum_x ? std::max(*maximum_x, *vertex.x) : vertex.x;
            minimum_y = minimum_y ? std::min(*minimum_y, *vertex.y) : vertex.y;
            maximum_y = maximum_y ? std::max(*maximum_y, *vertex.y) : vertex.y;
        }
    }
    if (emitted_count == 0U || !minimum_x || !minimum_y) {
        throw std::runtime_error(
            "The decoded GS stream has no emitted known-XY geometry");
    }

    openrc::TwoFipImage image;
    image.width = kWidth;
    image.height = kHeight;
    image.pixel_storage_format = openrc::kTwoFipPsmT8Format;
    image.trailing_header_fields = {0U, 0U, 1U};
    image.palette[0U] = openrc::TwoFipColor{5U, 8U, 18U, 0x80U};
    image.palette[1U] = openrc::TwoFipColor{38U, 210U, 255U, 0x80U};
    image.palette[2U] = openrc::TwoFipColor{255U, 246U, 190U, 0x80U};
    image.indices.resize(static_cast<std::size_t>(kWidth) * kHeight, 0U);

    const auto x_range = std::max<std::uint32_t>(
        1U, static_cast<std::uint32_t>(*maximum_x - *minimum_x));
    const auto y_range = std::max<std::uint32_t>(
        1U, static_cast<std::uint32_t>(*maximum_y - *minimum_y));
    const auto drawable_width = static_cast<double>(kWidth - 2U * kMargin);
    const auto drawable_height = static_cast<double>(kHeight - 2U * kMargin);
    const auto scale = std::min(
        drawable_width / static_cast<double>(x_range),
        drawable_height / static_cast<double>(y_range));
    const auto used_width = static_cast<double>(x_range) * scale;
    const auto used_height = static_cast<double>(y_range) * scale;
    const auto x_padding =
        (static_cast<double>(kWidth) - used_width) * 0.5;
    const auto y_padding =
        (static_cast<double>(kHeight) - used_height) * 0.5;

    const auto project = [&](const openrc::GifGsVertexV1& vertex) {
        const auto x = static_cast<int>(std::llround(
            x_padding + static_cast<double>(*vertex.x - *minimum_x) * scale));
        const auto y_from_bottom = static_cast<int>(std::llround(
            y_padding + static_cast<double>(*vertex.y - *minimum_y) * scale));
        return std::array<int, 2U>{
            std::clamp(x, 0, static_cast<int>(kWidth) - 1),
            std::clamp(static_cast<int>(kHeight) - 1 - y_from_bottom,
                       0,
                       static_cast<int>(kHeight) - 1),
        };
    };
    const auto put_pixel = [&](const int x,
                               const int y,
                               const std::uint8_t color) {
        if (x < 0 || y < 0 || x >= static_cast<int>(kWidth) ||
            y >= static_cast<int>(kHeight)) {
            return;
        }
        image.indices[static_cast<std::size_t>(y) * kWidth +
                      static_cast<std::size_t>(x)] = color;
    };
    const auto draw_line = [&](std::array<int, 2U> from,
                               const std::array<int, 2U> to) {
        const auto delta_x = std::abs(to[0U] - from[0U]);
        const auto step_x = from[0U] < to[0U] ? 1 : -1;
        const auto delta_y = -std::abs(to[1U] - from[1U]);
        const auto step_y = from[1U] < to[1U] ? 1 : -1;
        auto error = delta_x + delta_y;
        for (;;) {
            put_pixel(from[0U], from[1U], 1U);
            if (from == to) {
                break;
            }
            const auto twice_error = 2 * error;
            if (twice_error >= delta_y) {
                error += delta_y;
                from[0U] += step_x;
            }
            if (twice_error <= delta_x) {
                error += delta_x;
                from[1U] += step_y;
            }
        }
    };

    for (const auto& primitive : report.primitives) {
        if (primitive.emission != openrc::GifGsPrimitiveEmissionV1::emitted) {
            continue;
        }
        std::array<std::array<int, 2U>, 3U> points{};
        for (std::size_t index = 0U; index < primitive.vertex_count; ++index) {
            points[index] = project(report.vertices[static_cast<std::size_t>(
                primitive.vertex_indices[index])]);
        }
        if (primitive.vertex_count == 1U) {
            put_pixel(points[0U][0U], points[0U][1U], 2U);
        } else {
            for (std::size_t index = 1U; index < primitive.vertex_count;
                 ++index) {
                draw_line(points[index - 1U], points[index]);
            }
            if (primitive.vertex_count == 3U) {
                draw_line(points[2U], points[0U]);
            }
        }
        for (std::size_t index = 0U; index < primitive.vertex_count; ++index) {
            for (int offset_y = -1; offset_y <= 1; ++offset_y) {
                for (int offset_x = -1; offset_x <= 1; ++offset_x) {
                    put_pixel(points[index][0U] + offset_x,
                              points[index][1U] + offset_y,
                              2U);
                }
            }
        }
    }
    return image;
}

void print_gif_gs_decode_report(const openrc::GifGsDecodeResultV1& report) {
    std::array<std::uint64_t, 8U> opportunity_topologies{};
    std::array<std::uint64_t, 8U> emitted_topologies{};
    for (const auto& primitive : report.primitives) {
        const auto index = static_cast<std::size_t>(primitive.topology);
        if (index < opportunity_topologies.size()) {
            ++opportunity_topologies[index];
        }
        if (primitive.emission != openrc::GifGsPrimitiveEmissionV1::emitted) {
            continue;
        }
        if (index < emitted_topologies.size()) {
            ++emitted_topologies[index];
        }
    }

    std::array<std::uint64_t, 128U> addressed_registers{};
    for (const auto& write : report.addressed_writes) {
        if (write.dispatched_address) {
            ++addressed_registers[*write.dispatched_address];
        }
    }

    std::array<std::uint32_t, 4U> st_known_all{};
    std::array<std::uint32_t, 4U> st_known_any{};
    std::array<std::uint32_t, 4U> xyz_known_all{};
    std::array<std::uint32_t, 4U> xyz_known_any{};
    st_known_all.fill(0xffffffffU);
    xyz_known_all.fill(0xffffffffU);
    std::uint64_t st_write_count = 0U;
    std::uint64_t xyz_write_count = 0U;
    for (const auto& write : report.register_writes) {
        // These four-lane diagnostics describe the PACKED layout only.
        // REGLIST consumes one raw64 half; its neighbor/padding is not ST/XYZ.
        if (write.format != 0U) {
            continue;
        }
        auto* known_all = static_cast<std::array<std::uint32_t, 4U>*>(nullptr);
        auto* known_any = static_cast<std::array<std::uint32_t, 4U>*>(nullptr);
        if (write.descriptor == openrc::GifGsRegisterDescriptorV1::st) {
            known_all = &st_known_all;
            known_any = &st_known_any;
            ++st_write_count;
        } else if (
            write.descriptor == openrc::GifGsRegisterDescriptorV1::xyzf2) {
            known_all = &xyz_known_all;
            known_any = &xyz_known_any;
            ++xyz_write_count;
        }
        if (known_all == nullptr) {
            continue;
        }
        for (std::size_t lane = 0U; lane < known_all->size(); ++lane) {
            (*known_all)[lane] &= write.payload.lanes[lane].known_mask;
            (*known_any)[lane] |= write.payload.lanes[lane].known_mask;
        }
    }

    std::array<std::uint64_t, 3U> known_xyz{};
    std::array<std::uint64_t, 3U> known_stq{};
    std::array<std::uint64_t, 4U> known_rgba{};
    std::uint64_t submitted_vertices = 0U;
    std::uint64_t suppressed_vertices = 0U;
    std::uint64_t indeterminate_vertices = 0U;
    std::optional<std::uint16_t> minimum_x;
    std::optional<std::uint16_t> maximum_x;
    std::optional<std::uint16_t> minimum_y;
    std::optional<std::uint16_t> maximum_y;
    std::optional<std::uint32_t> minimum_z;
    std::optional<std::uint32_t> maximum_z;
    for (const auto& vertex : report.vertices) {
        if (vertex.x) {
            ++known_xyz[0U];
            minimum_x = minimum_x ? std::min(*minimum_x, *vertex.x) : vertex.x;
            maximum_x = maximum_x ? std::max(*maximum_x, *vertex.x) : vertex.x;
        }
        if (vertex.y) {
            ++known_xyz[1U];
            minimum_y = minimum_y ? std::min(*minimum_y, *vertex.y) : vertex.y;
            maximum_y = maximum_y ? std::max(*maximum_y, *vertex.y) : vertex.y;
        }
        if (vertex.z) {
            ++known_xyz[2U];
            minimum_z = minimum_z ? std::min(*minimum_z, *vertex.z) : vertex.z;
            maximum_z = maximum_z ? std::max(*maximum_z, *vertex.z) : vertex.z;
        }
        known_stq[0U] += vertex.texture.s.has_value() ? 1U : 0U;
        known_stq[1U] += vertex.texture.t.has_value() ? 1U : 0U;
        known_stq[2U] += vertex.texture.q.has_value() ? 1U : 0U;
        known_rgba[0U] += vertex.color.r.has_value() ? 1U : 0U;
        known_rgba[1U] += vertex.color.g.has_value() ? 1U : 0U;
        known_rgba[2U] += vertex.color.b.has_value() ? 1U : 0U;
        known_rgba[3U] += vertex.color.a.has_value() ? 1U : 0U;
        switch (vertex.kick) {
        case openrc::GifGsVertexKickV1::submitted:
            ++submitted_vertices;
            break;
        case openrc::GifGsVertexKickV1::suppressed:
            ++suppressed_vertices;
            break;
        case openrc::GifGsVertexKickV1::indeterminate:
            ++indeterminate_vertices;
            break;
        }
    }

    std::cout
        << "\nDecoded GS stream:\n"
        << "  Register writes:    " << report.register_writes.size() << '\n'
        << "  Raw64 GS writes:    " << report.addressed_writes.size() << '\n'
        << "  Vertices:           " << report.vertices.size() << '\n'
        << "  Vertex kicks:       " << submitted_vertices << " submitted, "
        << suppressed_vertices << " ADC/suppressed, "
        << indeterminate_vertices << " indeterminate\n"
        << "  Primitive groups:   " << report.primitives.size() << '\n'
        << "  Primitive emission: " << report.emitted_primitive_count
        << " emitted, " << report.suppressed_primitive_count
        << " suppressed, " << report.indeterminate_primitive_count
        << " indeterminate\n"
        << "  Pending vertices:   " << report.unassembled_vertex_count << '\n'
        << "  Known X/Y/Z:        " << known_xyz[0U] << '/' << known_xyz[1U]
        << '/' << known_xyz[2U] << " of " << report.vertices.size() << '\n'
        << "  Known S/T/Q:        " << known_stq[0U] << '/' << known_stq[1U]
        << '/' << known_stq[2U] << " of " << report.vertices.size() << '\n'
        << "  Known R/G/B/A:      " << known_rgba[0U] << '/' << known_rgba[1U]
        << '/' << known_rgba[2U] << '/' << known_rgba[3U] << " of "
        << report.vertices.size() << '\n'
        << "  Unsupported writes: " << report.unsupported_register_write_count
        << '\n'
        << "  Unresolved A+D:     " << report.unresolved_addressed_write_count
        << '\n'
        << "  Topology groups:    ";

    bool wrote_topology = false;
    for (std::size_t index = 0U; index < opportunity_topologies.size(); ++index) {
        if (opportunity_topologies[index] == 0U) {
            continue;
        }
        if (wrote_topology) {
            std::cout << ", ";
        }
        std::cout
            << gif_gs_topology_name(
                   static_cast<openrc::GifGsPrimitiveTopologyV1>(index))
            << '=' << opportunity_topologies[index];
        wrote_topology = true;
    }
    if (!wrote_topology) {
        std::cout << "none";
    }
    std::cout << '\n';

    std::cout << "  Emitted topology:   ";
    wrote_topology = false;
    for (std::size_t index = 0U; index < emitted_topologies.size(); ++index) {
        if (emitted_topologies[index] == 0U) {
            continue;
        }
        if (wrote_topology) {
            std::cout << ", ";
        }
        std::cout
            << gif_gs_topology_name(
                   static_cast<openrc::GifGsPrimitiveTopologyV1>(index))
            << '=' << emitted_topologies[index];
        wrote_topology = true;
    }
    if (!wrote_topology) {
        std::cout << "none";
    }
    std::cout << '\n';

    if (minimum_x && minimum_y) {
        std::cout
            << "  Raw XYZ bounds:     X " << *minimum_x << ".." << *maximum_x
            << ", Y " << *minimum_y << ".." << *maximum_y;
        if (minimum_z) {
            std::cout << ", Z " << *minimum_z << ".." << *maximum_z;
        } else {
            std::cout << ", Z indeterminate";
        }
        std::cout << '\n';
    }

    const auto print_masks =
        [](const std::string_view label,
           const std::uint64_t count,
           const std::array<std::uint32_t, 4U>& known_all,
           const std::array<std::uint32_t, 4U>& known_any) {
            if (count == 0U) {
                return;
            }
            std::cout << "  " << label << " masks all: ";
            for (std::size_t lane = 0U; lane < known_all.size(); ++lane) {
                if (lane != 0U) {
                    std::cout << '/';
                }
                std::cout << hexadecimal(known_all[lane], 8);
            }
            std::cout << ", any: ";
            for (std::size_t lane = 0U; lane < known_any.size(); ++lane) {
                if (lane != 0U) {
                    std::cout << '/';
                }
                std::cout << hexadecimal(known_any[lane], 8);
            }
            std::cout << '\n';
        };
    print_masks("PACKED ST", st_write_count, st_known_all, st_known_any);
    print_masks("PACKED XYZF2", xyz_write_count, xyz_known_all, xyz_known_any);

    std::cout << "  Raw64 GS registers: ";
    bool wrote_address = false;
    for (std::size_t address = 0U;
         address < addressed_registers.size();
         ++address) {
        if (addressed_registers[address] == 0U) {
            continue;
        }
        if (wrote_address) {
            std::cout << ", ";
        }
        const auto name =
            gif_gs_address_name(static_cast<std::uint8_t>(address));
        if (!name.empty()) {
            std::cout << name << '(' << hexadecimal(address, 2) << ')';
        } else {
            std::cout << hexadecimal(address, 2);
        }
        std::cout << '=' << addressed_registers[address];
        wrote_address = true;
    }
    if (!wrote_address) {
        std::cout << "none";
    }
    std::cout << '\n';
}

int run(const std::vector<std::filesystem::path>& arguments) {
    const std::string command =
        arguments.empty() ? std::string{} : openrc::path_to_utf8(arguments[0]);

    if (arguments.empty() || command == "help" || command == "--help" || command == "-h") {
        print_usage();
        return 0;
    }

    if (command == "inspect") {
        if (arguments.size() != 2) {
            std::cerr << "error: inspect expects exactly one ISO path\n";
            return kUsageError;
        }

        try {
            const auto report = openrc::inspect_disc(arguments[1]);
            std::cout << openrc::format_disc_report(report);
            return report.supported_build ? 0 : kUnsupportedBuild;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "inventory") {
        if (arguments.size() != 2) {
            std::cerr << "error: inventory expects exactly one ISO path\n";
            return kUsageError;
        }

        try {
            const auto inventory = openrc::inventory_disc(arguments[1]);
            std::cout << openrc::format_disc_report(inventory.disc)
                      << "Inventory files: " << inventory.files.size() << '\n'
                      << "Total file bytes: " << inventory.total_file_bytes << "\n\n";

            for (const auto& file : inventory.files) {
                std::cout << file.iso_path << " -> " << file.output_path << '\n'
                          << "  size: " << file.size << " bytes; extents:";
                if (file.extents.empty()) {
                    std::cout << " none";
                } else {
                    for (const auto& extent : file.extents) {
                        std::cout << " [LBA " << extent.logical_block
                                  << ", " << extent.byte_length << " bytes]";
                    }
                }
                std::cout << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "toc") {
        if (arguments.size() != 2) {
            std::cerr << "error: toc expects exactly one ISO path\n";
            return kUsageError;
        }

        try {
            const auto report = openrc::inspect_disc_toc(arguments[1]);
            std::array<std::size_t, 5> signature_counts{};
            for (const auto& entry : report.global_extents) {
                ++signature_counts[static_cast<std::size_t>(entry.signature)];
            }

            std::cout
                << "OpenRC Ratchet & Clank disc TOC\n"
                << "Image:                " << openrc::path_to_utf8(report.image_path) << '\n'
                << "Image sectors:        " << report.image_sectors << '\n'
                << "Declared sectors:     " << report.declared_volume_sectors << '\n'
                << "TOC LBA:              " << openrc::kDiscTocGlobalLba << '\n'
                << "TOC version:          " << report.version << '\n'
                << "TOC bytes:            " << report.byte_size << '\n'
                << "Global extent slots:  " << report.global_extent_slot_count << '\n'
                << "Used global extents:  " << report.global_extents.size() << '\n'
                << "Empty global slots:   " << report.empty_global_extent_slot_count << '\n'
                << "Level descriptors:    " << report.levels.size() << "\n\n"
                << "Signatures:\n"
                << "  WAD:   "
                << signature_counts[static_cast<std::size_t>(openrc::DiscTocSignature::wad)]
                << '\n'
                << "  VAGp:  "
                << signature_counts[static_cast<std::size_t>(openrc::DiscTocSignature::vagp)]
                << '\n'
                << "  2FIP:  "
                << signature_counts[static_cast<std::size_t>(openrc::DiscTocSignature::two_fip)]
                << '\n'
                << "  PS2D:  "
                << signature_counts[static_cast<std::size_t>(openrc::DiscTocSignature::ps2d)]
                << '\n'
                << "  other: "
                << signature_counts[static_cast<std::size_t>(openrc::DiscTocSignature::other)]
                << "\n\nGlobal extents:\n";

            for (const auto& entry : report.global_extents) {
                const auto bytes =
                    static_cast<std::uint64_t>(entry.extent.sectors) *
                    openrc::kDiscTocSectorSize;
                std::cout
                    << "  [" << entry.slot << "] LBA " << entry.extent.lba
                    << ", " << entry.extent.sectors << " sectors, "
                    << bytes << " bytes, "
                    << openrc::disc_toc_signature_name(entry.signature) << '\n';
            }

            std::cout << "\nLevel TOCs:\n";
            for (const auto& level : report.levels) {
                std::cout
                    << "  level " << level.level_id
                    << ": TOC LBA " << level.toc_lba
                    << ", auxiliary " << level.auxiliary
                    << ", primary extents:";
                for (const auto& extent : level.primary_extents) {
                    std::cout
                        << " [LBA " << extent.lba
                        << ", " << extent.sectors << " sectors]";
                }
                std::cout << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "toc-assets") {
        if (arguments.size() != 2) {
            std::cerr << "error: toc-assets expects exactly one ISO path\n";
            return kUsageError;
        }

        try {
            const auto report = openrc::inspect_disc_toc_assets(arguments[1]);
            std::size_t vag_references = 0;
            std::size_t run_wads = 0;
            std::size_t terminal_zero_sectors = 0;
            std::size_t extent0_subranges = 0;
            std::size_t extent0_wads = 0;
            std::size_t primary_wads = 0;
            std::size_t opaque_pairs = 0;

            std::cout
                << "OpenRC local TOC asset report\n"
                << "Image:                  "
                << openrc::path_to_utf8(report.layout.image_path) << '\n'
                << "Levels:                 " << report.levels.size() << "\n\n"
                << "Per-level counts:\n";
            for (const auto& level : report.levels) {
                std::size_t level_run_wads = 0;
                std::size_t level_terminal_sectors = 0;
                for (const auto& block : level.local_tables.resource_blocks) {
                    for (const auto& run : block.wad_runs) {
                        level_run_wads += run.wads.size();
                        if (run.trailing_zero_sector_lba) {
                            ++level_terminal_sectors;
                        }
                    }
                }

                const auto level_extent0_wads = static_cast<std::size_t>(std::count_if(
                    level.primary_extent0.subranges.begin(),
                    level.primary_extent0.subranges.end(),
                    [](const openrc::DiscTocSubrange& subrange) {
                        return subrange.byte_size != 0 &&
                            subrange.signature == openrc::DiscTocSignature::wad;
                    }));
                std::size_t level_opaque_pairs = 0;
                for (const auto& table : level.primary_extent3.tables) {
                    level_opaque_pairs += table.size();
                }

                vag_references += level.referenced_vags.size();
                run_wads += level_run_wads;
                terminal_zero_sectors += level_terminal_sectors;
                extent0_subranges += level.primary_extent0.used_subrange_count;
                extent0_wads += level_extent0_wads;
                primary_wads += level.primary_wads.size();
                opaque_pairs += level_opaque_pairs;

                std::cout
                    << "  level " << std::setw(2) << level.level_id
                    << ": VAG " << level.referenced_vags.size()
                    << ", run WAD " << level_run_wads
                    << ", sentinels " << level_terminal_sectors
                    << ", extent0 " << level.primary_extent0.used_subrange_count
                    << " (" << level_extent0_wads << " WAD)"
                    << ", extent3 pairs " << level_opaque_pairs << '\n';
            }

            std::cout
                << "\nValidated totals:\n"
                << "  VAG references:        " << vag_references << '\n'
                << "  WAD-run records:       " << run_wads << '\n'
                << "  terminal zero sectors: " << terminal_zero_sectors << '\n'
                << "  extent0 subranges:     " << extent0_subranges << '\n'
                << "  extent0 WAD records:   " << extent0_wads << '\n'
                << "  primary WAD records:   " << primary_wads << '\n'
                << "  extent3 opaque pairs:  " << opaque_pairs << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "wad-scene-animation") {
        if (arguments.size() != 3U) {
            std::cerr
                << "error: wad-scene-animation expects an ISO path and a "
                   "decimal unique payload index\n";
            return kUsageError;
        }
        const auto requested_unique = parse_decimal_argument(arguments[2]);
        if (!requested_unique) {
            std::cerr << "error: unique payload index must be decimal\n";
            return kUsageError;
        }
        try {
            std::cerr
                << "Scanning indexed WadV1 sources for scene-animation "
                   "payload "
                << *requested_unique << "...\n";
            const auto report = inventory_disc_wad_payloads_v1(
                arguments[1], *requested_unique);
            if (!report.captured_payload) {
                throw std::runtime_error(
                    "The requested WadV1 payload was not captured");
            }
            const auto& captured = *report.captured_payload;
            const auto scene = openrc::parse_scene_animation_bank_v1(
                captured.bytes,
                openrc::SceneAnimationBankLimitsV1{
                    kMaximumCliDecodedWadBytes,
                    kMaximumCliSceneAnimationActorTracks,
                    kMaximumCliSceneAnimationTotalFrameRanges,
                    kMaximumCliLocalizedSubtitleEntries,
                    kMaximumCliDecodedWadBytes});
            std::cout
                << "OpenRC SceneAnimationBankV1 report\n"
                << "Unique index:          " << *requested_unique << '\n'
                << "Decoded bytes:         " << captured.bytes.size() << '\n'
                << "Header tag:            0x" << std::hex
                << scene.header_tag << std::dec << '\n'
                << "Scene record:          " << scene.scene_record_index
                << " of " << scene.scene_record_count << '\n'
                << "Actor tracks:          " << scene.actor_track_count << '\n'
                << "Animation frames:      "
                << static_cast<unsigned>(scene.frame_count) << '\n'
                << "Camera records:        " << scene.camera_record_count
                << " (" << scene.camera_track_range.size << " bytes)\n"
                << "Subtitle table:        ";
            if (scene.localized_subtitles) {
                std::cout
                    << "0x" << std::hex << scene.subtitle_table_offset
                    << std::dec << " ("
                    << scene.localized_subtitles->entries.size()
                    << " entries)\n";
            } else {
                std::cout << "none\n";
            }
            std::cout
                << "Trailing opaque bytes: "
                << (scene.localized_subtitles
                        ? scene.localized_subtitles->trailing_opaque_range.size
                        : scene.trailing_range.size)
                << "\n\nActors:\n";
            for (std::size_t actor_index = 0U;
                 actor_index < scene.actors.size();
                 ++actor_index) {
                const auto& actor = scene.actors[actor_index];
                std::cout
                    << "  [" << actor_index << "] class 0x" << std::hex
                    << actor.class_id;
                if (actor.class_id == 0U) {
                    std::cout << " (Ratchet)";
                } else if (actor.class_id == 10U) {
                    std::cout << " (Clank)";
                }
                std::cout
                    << ", section 0x" << actor.section_range.offset
                    << "+0x" << actor.section_range.size
                    << ", root 0x" << actor.root_transform_range.offset
                    << "+0x" << actor.root_transform_range.size
                    << std::dec << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "wad-gameplay") {
        if (arguments.size() != 3U) {
            std::cerr
                << "error: wad-gameplay expects an ISO path and a decimal "
                   "unique payload index\n";
            return kUsageError;
        }
        const auto requested_unique = parse_decimal_argument(arguments[2]);
        if (!requested_unique) {
            std::cerr << "error: unique payload index must be decimal\n";
            return kUsageError;
        }
        try {
            std::cerr
                << "Scanning indexed WadV1 sources for RAC1 gameplay payload "
                << *requested_unique << "...\n";
            const auto report = inventory_disc_wad_payloads_v1(
                arguments[1], *requested_unique);
            if (!report.captured_payload) {
                throw std::runtime_error(
                    "The requested WadV1 payload was not captured");
            }
            const auto& captured = *report.captured_payload;
            const auto gameplay = openrc::parse_rac_gameplay_bank_v1(
                captured.bytes,
                openrc::RacGameplayBankLimitsV1{
                    kMaximumCliDecodedWadBytes});
            std::cout
                << "OpenRC RacGameplayBankV1 report\n"
                << "Unique index:          " << *requested_unique << '\n'
                << "Decoded bytes:         " << captured.bytes.size() << '\n'
                << "Present blocks:        " << gameplay.blocks.size() << '\n'
                << "Death height:          "
                << gameplay.level_settings.death_height << '\n'
                << "Ship position:         "
                << gameplay.level_settings.ship_position[0U] << ", "
                << gameplay.level_settings.ship_position[1U] << ", "
                << gameplay.level_settings.ship_position[2U] << '\n'
                << "Ship rotation Z:       "
                << gameplay.level_settings.ship_rotation_z << '\n'
                << "Moby classes:          " << gameplay.moby_class_count
                << '\n'
                << "Static moby instances: " << gameplay.static_moby_count
                << '\n'
                << "Spawnable mobies:      " << gameplay.spawnable_moby_count
                << '\n'
                << "TIE classes/instances: " << gameplay.tie_class_count << '/'
                << gameplay.tie_instance_count << '\n'
                << "Shrub classes/instances: " << gameplay.shrub_class_count
                << '/' << gameplay.shrub_instance_count
                << "\n\nBlocks:\n";
            for (const auto& block : gameplay.blocks) {
                std::cout
                    << "  [header 0x" << std::hex
                    << block.header_pointer_offset << "] " << std::left
                    << std::setw(28)
                    << openrc::rac_gameplay_block_name_v1(block.kind)
                    << std::right << " 0x" << block.range.offset
                    << "+0x" << block.range.size << std::dec << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "wad-moby-class") {
        if (arguments.size() != 3U) {
            std::cerr
                << "error: wad-moby-class expects an ISO path and a decimal "
                   "unique payload index\n";
            return kUsageError;
        }
        const auto requested_unique = parse_decimal_argument(arguments[2]);
        if (!requested_unique) {
            std::cerr << "error: unique payload index must be decimal\n";
            return kUsageError;
        }
        try {
            std::cerr
                << "Scanning indexed WadV1 sources for RAC1 MobyClass payload "
                << *requested_unique << "...\n";
            const auto report = inventory_disc_wad_payloads_v1(
                arguments[1], *requested_unique);
            if (!report.captured_payload) {
                throw std::runtime_error(
                    "The requested WadV1 payload was not captured");
            }
            const auto& captured = *report.captured_payload;
            const auto moby = openrc::parse_rac_moby_class_v1(
                captured.bytes,
                openrc::RacMobyClassLimitsV1{
                    kMaximumCliDecodedWadBytes, true});
            const auto present_sequences = static_cast<std::size_t>(
                std::count_if(
                    moby.sequence_offsets.begin(),
                    moby.sequence_offsets.end(),
                    [](const std::uint32_t offset) { return offset != 0U; }));
            const auto high_geometry =
                openrc::assemble_rac_moby_model_geometry_v1(
                    captured.bytes,
                    moby,
                    openrc::RacMobyLodV1::high,
                    kCliMobyModelGeometryLimits);
            const auto low_geometry =
                openrc::assemble_rac_moby_model_geometry_v1(
                    captured.bytes,
                    moby,
                    openrc::RacMobyLodV1::low,
                    kCliMobyModelGeometryLimits);
            std::cout
                << "OpenRC RacMobyClassV1 report\n"
                << "Unique index:          " << *requested_unique << '\n'
                << "Decoded bytes:         " << captured.bytes.size() << '\n'
                << "Packet table:          "
                << hexadecimal(moby.packet_table_range.offset, 8) << "+"
                << hexadecimal(moby.packet_table_range.size, 8) << '\n'
                << "Packets high/low/metal: "
                << static_cast<std::uint32_t>(moby.high_lod_packet_count)
                << '/' << static_cast<std::uint32_t>(moby.low_lod_packet_count)
                << '/' << static_cast<std::uint32_t>(moby.metal_packet_count)
                << '\n'
                << "Joints:                "
                << static_cast<std::uint32_t>(moby.joint_count) << '\n'
                << "Sequences present:     " << present_sequences << '/'
                << static_cast<std::uint32_t>(moby.sequence_count) << '\n'
                << "Sound definitions:     "
                << static_cast<std::uint32_t>(moby.sound_count) << '\n'
                << "Scale:                 " << moby.scale << '\n'
                << "Bounding sphere:       " << moby.bounding_sphere[0U] << ", "
                << moby.bounding_sphere[1U] << ", "
                << moby.bounding_sphere[2U] << ", radius "
                << moby.bounding_sphere[3U] << '\n'
                << "Regular transfer verts: "
                << high_geometry.vertices.size() +
                       low_geometry.vertices.size()
                << '\n'
                << "Regular triangles:     "
                << high_geometry.triangles.size() +
                       low_geometry.triangles.size()
                << '\n'
                << "Inherited duplicates:  "
                << high_geometry.inherited_duplicate_count +
                       low_geometry.inherited_duplicate_count
                << " (resolved)\n\nPackets:\n";
            for (std::size_t index = 0U; index < moby.packets.size(); ++index) {
                const auto& packet = moby.packets[index];
                const char* kind = "metal";
                if (packet.kind == openrc::RacMobyPacketKindV1::high_lod) {
                    kind = "high";
                } else if (packet.kind ==
                           openrc::RacMobyPacketKindV1::low_lod) {
                    kind = "low";
                }
                std::cout
                    << "  [" << index << "] " << std::left << std::setw(5)
                    << kind << std::right << " VIF "
                    << hexadecimal(packet.vif_range.offset, 8) << "+"
                    << hexadecimal(packet.vif_range.size, 8) << ", vertices "
                    << hexadecimal(packet.vertex_range.offset, 8) << "+"
                    << hexadecimal(packet.vertex_range.size, 8) << ", transfer "
                    << static_cast<std::uint32_t>(
                           packet.transfer_vertex_count)
                    << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "wad-subtitles") {
        if (arguments.size() != 3U) {
            std::cerr
                << "error: wad-subtitles expects an ISO path and a decimal "
                   "unique payload index\n";
            return kUsageError;
        }
        const auto requested_unique = parse_decimal_argument(arguments[2]);
        if (!requested_unique) {
            std::cerr << "error: unique payload index must be decimal\n";
            return kUsageError;
        }
        try {
            std::cerr
                << "Scanning indexed WadV1 sources for subtitle payload "
                << *requested_unique << "...\n";
            const auto report = inventory_disc_wad_payloads_v1(
                arguments[1], *requested_unique);
            if (!report.captured_payload) {
                throw std::runtime_error(
                    "The requested WadV1 payload was not captured");
            }
            const auto& captured = *report.captured_payload;
            const auto scene = openrc::parse_scene_animation_bank_v1(
                captured.bytes,
                openrc::SceneAnimationBankLimitsV1{
                    kMaximumCliDecodedWadBytes,
                    kMaximumCliSceneAnimationActorTracks,
                    kMaximumCliSceneAnimationTotalFrameRanges,
                    kMaximumCliLocalizedSubtitleEntries,
                    kMaximumCliDecodedWadBytes});
            if (!scene.localized_subtitles) {
                throw std::runtime_error(
                    "The selected scene animation has no subtitle table");
            }
            const auto& subtitles = *scene.localized_subtitles;
            constexpr std::array<std::string_view, 5U> kLanguages{
                "EN", "FR", "DE", "ES", "IT"};
            std::cout
                << "OpenRC localized scene-subtitle report\n"
                << "Unique index:          " << *requested_unique << '\n'
                << "Decoded bytes:         " << captured.bytes.size() << '\n'
                << "Actor tracks:          " << scene.actor_track_count << '\n'
                << "Subtitle table:        0x"
                << std::hex << subtitles.table_offset << std::dec << '\n'
                << "Entries:               " << subtitles.entries.size() << '\n'
                << "Owned text bytes:      "
                << subtitles.total_text_bytes << "\n\n";
            for (std::size_t entry_index = 0U;
                 entry_index < subtitles.entries.size();
                 ++entry_index) {
                const auto& entry = subtitles.entries[entry_index];
                std::cout
                    << "Entry " << entry_index << " (ticks "
                    << entry.start_tick << '-' << entry.end_tick << "):\n";
                for (std::size_t language = 0U;
                     language < entry.texts.size();
                     ++language) {
                    std::cout
                        << "  " << kLanguages[language] << ": "
                        << escaped_single_byte_text(
                               entry.texts[language].bytes)
                        << '\n';
                }
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "wad-payload-export") {
        if (arguments.size() != 4U) {
            std::cerr
                << "error: wad-payload-export expects an ISO path, a decimal "
                   "unique index, and a new output path\n";
            return kUsageError;
        }
        const auto requested_unique = parse_decimal_argument(arguments[2]);
        if (!requested_unique) {
            std::cerr << "error: unique payload index must be decimal\n";
            return kUsageError;
        }
        try {
            std::cerr
                << "Scanning indexed WadV1 sources for unique payload "
                << *requested_unique << "...\n";
            const auto report = inventory_disc_wad_payloads_v1(
                arguments[1], *requested_unique);
            if (!report.captured_payload ||
                report.captured_payload->added.unique_payload_index !=
                    *requested_unique) {
                throw std::runtime_error(
                    "The requested WadV1 payload was not captured");
            }
            const auto& captured = *report.captured_payload;
            const auto& unique = report.inventory.unique_payloads[
                static_cast<std::size_t>(*requested_unique)];
            write_new_binary_file(arguments[3], captured.bytes);
            std::cout
                << "OpenRC decoded WadV1 payload export\n"
                << "Unique index:          " << *requested_unique << '\n'
                << "First observation:     "
                << captured.added.observation_index << '\n'
                << "Decoded bytes:         " << captured.bytes.size() << '\n'
                << "Decoded SHA-256:       " << unique.decoded_sha256 << '\n'
                << "Level:                 ";
            if (captured.origin.level_id) {
                std::cout << *captured.origin.level_id;
            } else {
                std::cout << "none";
            }
            std::cout << "\nResource block:        ";
            if (captured.origin.container_index) {
                std::cout << *captured.origin.container_index;
            } else {
                std::cout << "none";
            }
            std::cout << "\nWAD run:               ";
            if (captured.origin.run_index) {
                std::cout << *captured.origin.run_index;
            } else {
                std::cout << "none";
            }
            std::cout << "\nRecord:                ";
            if (captured.origin.record_index) {
                std::cout << *captured.origin.record_index;
            } else {
                std::cout << "none";
            }
            std::cout
                << "\nOutput:                "
                << openrc::path_to_utf8(arguments[3]) << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "wad-families") {
        if (arguments.size() < 2U || arguments.size() > 3U) {
            std::cerr
                << "error: wad-families expects an ISO path and an optional "
                   "TSV output path\n";
            return kUsageError;
        }

        try {
            constexpr std::uint32_t kVeldinLevelId = 0U;
            std::cerr
                << "Scanning every indexed WadV1 source and grouping "
                         "structural families...\n";
            const auto report = inventory_disc_wad_payloads_v1(arguments[1]);
            const auto& families = report.families;
            const auto ranked = openrc::rank_unknown_wad_payload_families_v1(
                families, kVeldinLevelId);

            std::cout
                << "OpenRC WadV1 structural candidate families\n"
                << "Image:                  "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Profiled unique:        "
                << families.profiled_unique_payloads << '\n'
                << "Candidate families:     " << families.families.size() << '\n'
                << "Families with unknown:  "
                << families.families_with_unknown_payloads << '\n'
                << "Unknown unique:         "
                << families.unknown_unique_payloads << '\n'
                << "Unknown observations:   "
                << families.unknown_observations << "\n\n"
                << "Top Veldin candidates:\n";

            const auto shown = std::min<std::size_t>(20U, ranked.size());
            for (std::size_t rank = 0U; rank < shown; ++rank) {
                const auto& family = families.families[ranked[rank]];
                const auto level = std::find_if(
                    family.levels.begin(),
                    family.levels.end(),
                    [](const openrc::WadPayloadFamilyLevelCountV1& candidate) {
                        return candidate.level_id == kVeldinLevelId;
                    });
                const auto veldin_unknown_unique =
                    level == family.levels.end()
                    ? 0U
                    : level->classification_unique_payloads[
                          openrc::kWadPayloadUnknownClassificationIndexV1];
                const auto veldin_unknown_observations =
                    level == family.levels.end()
                    ? 0U
                    : level->classification_observations[
                          openrc::kWadPayloadUnknownClassificationIndexV1];
                const auto& structure = family.representative_structure;
                std::cout
                    << "  " << std::setw(2) << (rank + 1U)
                    << "  " << family.family_id.substr(0U, 12U)
                    << "  Veldin " << veldin_unknown_unique << '/'
                    << veldin_unknown_observations
                    << ", all "
                    << family.classification_unique_payloads[
                           openrc::kWadPayloadUnknownClassificationIndexV1]
                    << '/'
                    << family.classification_observations[
                           openrc::kWadPayloadUnknownClassificationIndexV1]
                    << ", bytes " << family.minimum_decoded_bytes
                    << '-' << family.maximum_decoded_bytes
                    << ", levels " << family.levels.size()
                    << ", prefix "
                    << structure.prefix_hex.substr(0U, 32U) << '\n';
            }

            if (arguments.size() == 3U) {
                const auto tsv =
                    openrc::encode_wad_payload_families_tsv_v1(
                        families, kVeldinLevelId);
                write_new_binary_file(
                    arguments[2],
                    std::as_bytes(
                        std::span<const char>(tsv.data(), tsv.size())));
                std::cout
                    << "\nTSV output:             "
                    << openrc::path_to_utf8(arguments[2]) << '\n'
                    << "TSV bytes:              " << tsv.size() << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "wad-payload-inventory") {
        if (arguments.size() < 2U || arguments.size() > 3U) {
            std::cerr
                << "error: wad-payload-inventory expects an ISO path and "
                   "an optional TSV output path\n";
            return kUsageError;
        }

        try {
            std::cerr
                << "Scanning every indexed WadV1 source with bounded "
                         "decoders...\n";
            const auto report = inventory_disc_wad_payloads_v1(arguments[1]);
            const auto& inventory = report.inventory;
            std::vector<std::uint64_t> format_unique_counts(
                report.format_names.size(), 0U);
            std::vector<std::uint64_t> format_observation_counts(
                report.format_names.size(), 0U);
            for (const auto& unique : inventory.unique_payloads) {
                for (std::size_t format_index = 0U;
                     format_index < report.format_names.size();
                     ++format_index) {
                    if (std::find(
                            unique.matched_format_names.begin(),
                            unique.matched_format_names.end(),
                            report.format_names[format_index]) !=
                        unique.matched_format_names.end()) {
                        ++format_unique_counts[format_index];
                        format_observation_counts[format_index] +=
                            unique.observation_count;
                    }
                }
            }

            std::cout
                << "OpenRC decoded WadV1 payload inventory\n"
                << "Image:               "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Observations:        " << inventory.observations.size() << '\n'
                << "Unique payloads:     " << inventory.unique_payloads.size() << '\n'
                << "Recognized unique:   "
                << inventory.recognized_unique_payloads << '\n'
                << "Unknown unique:      "
                << inventory.unknown_unique_payloads << '\n'
                << "Ambiguous unique:    "
                << inventory.ambiguous_unique_payloads << '\n'
                << "Observed bytes:      " << inventory.total_decoded_bytes << '\n'
                << "Unique bytes:        " << inventory.unique_decoded_bytes << '\n'
                << "Probe invocations:   " << inventory.probe_invocations << "\n\n"
                << "Source observations:\n"
                << "  global TOC:        " << report.counters.global_payloads << '\n'
                << "  global TOC tail:   "
                << report.counters.global_tail_payloads << '\n'
                << "  local WAD runs:    " << report.counters.local_run_payloads << '\n'
                << "  extent0 subranges: "
                << report.counters.primary_subrange_payloads << '\n'
                << "  primary extents:   "
                << report.counters.primary_extent_payloads << '\n'
                << "  bundle records:    "
                << report.counters.bundle_record_payloads << '\n'
                << "  companion records: "
                << report.counters.companion_record_payloads << "\n\n"
                << "Recognized formats (unique / observations):\n";
            for (std::size_t index = 0U;
                 index < report.format_names.size();
                 ++index) {
                std::cout
                    << "  " << std::left << std::setw(24)
                    << report.format_names[index] << std::right
                    << format_unique_counts[index] << " / "
                    << format_observation_counts[index] << '\n';
            }

            if (arguments.size() == 3U) {
                const auto tsv =
                    openrc::encode_wad_payload_inventory_tsv_v1(inventory);
                write_new_binary_file(
                    arguments[2],
                    std::as_bytes(std::span<const char>(tsv.data(), tsv.size())));
                std::cout
                    << "\nTSV output:          "
                    << openrc::path_to_utf8(arguments[2]) << '\n'
                    << "TSV bytes:           " << tsv.size() << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "wad") {
        if (arguments.size() != 3) {
            std::cerr << "error: wad expects an ISO path and a global TOC slot\n";
            return kUsageError;
        }

        const auto requested_slot_value = parse_decimal_argument(arguments[2]);
        if (!requested_slot_value ||
            *requested_slot_value >= openrc::kDiscTocGlobalExtentSlotCount) {
            std::cerr << "error: global TOC slot must be a decimal number from 0 to "
                      << (openrc::kDiscTocGlobalExtentSlotCount - 1U) << '\n';
            return kUsageError;
        }
        const auto requested_slot = *requested_slot_value;

        try {
            const auto toc = openrc::inspect_disc_toc(arguments[1]);
            const auto entry = std::find_if(
                toc.global_extents.begin(),
                toc.global_extents.end(),
                [requested_slot](const openrc::DiscTocGlobalExtent& candidate) {
                    return candidate.slot == requested_slot;
                });
            if (entry == toc.global_extents.end()) {
                std::cerr << "error: global TOC slot " << requested_slot
                          << " is empty\n";
                return kOperationError;
            }
            if (entry->signature != openrc::DiscTocSignature::wad) {
                std::cerr << "error: global TOC slot " << requested_slot
                          << " is " << openrc::disc_toc_signature_name(entry->signature)
                          << ", not WAD\n";
                return kOperationError;
            }

            const auto decoded = openrc::decode_wad(
                arguments[1],
                entry->extent.lba,
                entry->extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto& report = decoded.source;
            std::cout
                << "OpenRC WadV1 report\n"
                << "Image:               " << openrc::path_to_utf8(report.image_path) << '\n'
                << "Global TOC slot:     " << requested_slot << '\n'
                << "LBA:                 " << report.logical_block << '\n'
                << "Sectors:             " << report.sector_count << '\n'
                << "Extent bytes:        " << report.extent_bytes << '\n'
                << "Compressed bytes:    " << report.total_bytes << '\n'
                << "Compressed payload:  " << report.compressed_payload_bytes << '\n'
                << "Sector padding:      " << report.padding_bytes << '\n'
                << "Compressed SHA-256:  " << report.sha256 << '\n'
                << "Decoded bytes:       " << decoded.bytes.size() << '\n'
                << "Decoded SHA-256:     " << decoded.sha256 << '\n'
                << "Auxiliary bytes:     ";
            for (const auto value : report.auxiliary_bytes) {
                std::cout << std::hex << std::uppercase << std::setfill('0')
                          << std::setw(2) << static_cast<unsigned int>(value);
            }
            std::cout << std::dec << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "vagp") {
        if (arguments.size() < 3 || arguments.size() > 4) {
            std::cerr
                << "error: vagp expects an ISO path, global TOC slot, "
                   "and optional WAV output path\n";
            return kUsageError;
        }

        const auto requested_slot_value = parse_decimal_argument(arguments[2]);
        if (!requested_slot_value ||
            *requested_slot_value >= openrc::kDiscTocGlobalExtentSlotCount) {
            std::cerr << "error: global TOC slot must be a decimal number from 0 to "
                      << (openrc::kDiscTocGlobalExtentSlotCount - 1U) << '\n';
            return kUsageError;
        }
        const auto requested_slot = *requested_slot_value;

        try {
            const auto toc = openrc::inspect_disc_toc(arguments[1]);
            const auto entry = std::find_if(
                toc.global_extents.begin(),
                toc.global_extents.end(),
                [requested_slot](const openrc::DiscTocGlobalExtent& candidate) {
                    return candidate.slot == requested_slot;
                });
            if (entry == toc.global_extents.end()) {
                throw std::runtime_error(
                    "Global TOC slot " + std::to_string(requested_slot) + " is empty");
            }
            if (entry->signature != openrc::DiscTocSignature::vagp) {
                throw std::runtime_error(
                    "Global TOC slot " + std::to_string(requested_slot) +
                    " is " +
                    std::string(openrc::disc_toc_signature_name(entry->signature)) +
                    ", not VAGp");
            }

            const auto extent_bytes = read_disc_extent(
                arguments[1],
                entry->extent.lba,
                entry->extent.sectors,
                kMaximumCliVagpBytes);
            const auto report = openrc::parse_vagp_v1(
                extent_bytes,
                openrc::VagpLimits{
                    kMaximumCliVagpBytes,
                    kMaximumCliVagpBytes,
                    kMaximumCliVagpFrames,
                    kMaximumCliVagpSamples});
            const auto content_frames =
                report.content_end_frame - report.content_begin_frame;
            const auto content_samples =
                content_frames * openrc::kPsAdpcmSamplesPerFrame;

            std::cout
                << "OpenRC VAGp V1 report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Global TOC slot:     " << requested_slot << '\n'
                << "LBA:                 " << entry->extent.lba << '\n'
                << "Sectors:             " << entry->extent.sectors << '\n'
                << "Extent bytes:        " << report.input_bytes << '\n'
                << "Logical bytes:       " << report.logical_bytes << '\n'
                << "Payload bytes:       " << report.payload_bytes << '\n'
                << "Sector padding:      " << report.padding_bytes << '\n'
                << "Version:             " << hexadecimal(report.version, 8) << '\n'
                << "Sample rate:         " << report.sample_rate << " Hz\n"
                << "Name:                "
                << (report.display_name.empty()
                        ? std::string("<empty>")
                        : escaped_terminal_text(report.display_name))
                << '\n'
                << "ADPCM frames:        " << report.frame_count << '\n'
                << "Linear samples:      " << report.sample_count << '\n'
                << "Content frame range: [" << report.content_begin_frame
                << ", " << report.content_end_frame << ")\n"
                << "Content frames:      " << content_frames << '\n'
                << "Content samples:     " << content_samples << '\n';

            if (arguments.size() == 4) {
                const auto wav = openrc::encode_vagp_pcm16_mono_wav(
                    report,
                    kMaximumCliVagpBytes);
                write_new_binary_file(arguments[3], wav);
                std::cout
                    << "WAV output:          "
                    << openrc::path_to_utf8(arguments[3]) << '\n'
                    << "WAV bytes:           " << wav.size() << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "wad-bundle") {
        if (arguments.size() != 4) {
            std::cerr << "error: wad-bundle expects an ISO path, LBA, and "
                         "sector count\n";
            return kUsageError;
        }

        const auto logical_block = parse_decimal_argument(arguments[2]);
        const auto sector_count = parse_decimal_argument(arguments[3]);
        if (!logical_block || !sector_count || *sector_count == 0) {
            std::cerr << "error: LBA and sector count must be unsigned decimal "
                         "numbers, "
                         "and sector count must be non-zero\n";
            return kUsageError;
        }

        try {
            const auto decoded = openrc::decode_wad(
                arguments[1],
                *logical_block,
                *sector_count,
                kMaximumCliDecodedWadBytes);
            const auto bundle = openrc::parse_wad_bundle_v1(decoded.bytes);
            const auto nested_count = 1U + static_cast<unsigned int>(std::count_if(
                bundle.slots.begin(),
                bundle.slots.end(),
                [](const openrc::WadBundleRecord& record) {
                    return record.kind == openrc::WadBundleRecordKind::nested_wad;
                }));
            const auto elf_count = std::count_if(
                bundle.slots.begin(),
                bundle.slots.end(),
                [](const openrc::WadBundleRecord& record) {
                    return record.kind == openrc::WadBundleRecordKind::elf;
                });
            const auto empty_count = std::count_if(
                bundle.slots.begin(),
                bundle.slots.end(),
                [](const openrc::WadBundleRecord& record) {
                    return record.kind == openrc::WadBundleRecordKind::empty;
                });

            std::cout
                << "OpenRC WadBundleV1 report\n"
                << "Image:               " << openrc::path_to_utf8(decoded.source.image_path) << '\n'
                << "LBA:                 " << decoded.source.logical_block << '\n'
                << "Sectors:             " << decoded.source.sector_count << '\n'
                << "Compressed bytes:    " << decoded.source.total_bytes << '\n'
                << "Compressed SHA-256:  " << decoded.source.sha256 << '\n'
                << "Decoded bytes:       " << bundle.decoded_size << '\n'
                << "Decoded SHA-256:     " << decoded.sha256 << '\n'
                << "Header bytes:        " << bundle.header_size << '\n'
                << "Nested WAD records:  " << nested_count << '\n'
                << "ELF records:         " << elf_count << '\n'
                << "Empty slots:         " << empty_count << "\n\n"
                << "Records:\n"
                << "  initial  WAD  offset " << bundle.initial_record.offset
                << ", size " << bundle.initial_record.size << '\n';
            for (std::size_t index = 0; index < bundle.slots.size(); ++index) {
                const auto& record = bundle.slots[index];
                std::cout << "  slot " << std::setw(2) << index << "  "
                          << std::setw(5) << wad_bundle_kind_name(record.kind);
                if (record.kind != openrc::WadBundleRecordKind::empty) {
                    std::cout << "  offset " << record.offset << ", size " << record.size;
                }
                std::cout << '\n';
                if (record.kind == openrc::WadBundleRecordKind::elf) {
                    const auto record_bytes = std::span<const std::byte>(
                        decoded.bytes.data() + record.offset,
                        record.size);
                    const auto elf = openrc::inspect_elf(record_bytes);
                    if (elf.iop_module_info) {
                        std::cout
                            << "           IOP module "
                            << elf.iop_module_info->name
                            << ", version "
                            << hexadecimal(elf.iop_module_info->version, 4)
                            << ", imports " << elf.iop_import_libraries.size()
                            << ", relocation sections "
                            << elf.relocation_summaries.size() << '\n';
                    }
                }
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "boundary") {
        if (arguments.size() != 3) {
            std::cerr
                << "error: boundary expects an ISO path and a global TOC "
                         "slot\n";
            return kUsageError;
        }

        const auto requested_slot_value = parse_decimal_argument(arguments[2]);
        if (!requested_slot_value ||
            *requested_slot_value >= openrc::kDiscTocGlobalExtentSlotCount) {
            std::cerr << "error: global TOC slot must be a decimal number from 0 to "
                      << (openrc::kDiscTocGlobalExtentSlotCount - 1U) << '\n';
            return kUsageError;
        }
        const auto requested_slot = *requested_slot_value;

        try {
            const auto toc = openrc::inspect_disc_toc(arguments[1]);
            const auto entry = std::find_if(
                toc.global_extents.begin(),
                toc.global_extents.end(),
                [requested_slot](const openrc::DiscTocGlobalExtent& candidate) {
                    return candidate.slot == requested_slot;
                });
            if (entry == toc.global_extents.end()) {
                throw std::runtime_error(
                    "Global TOC slot " + std::to_string(requested_slot) + " is empty");
            }
            if (entry->signature != openrc::DiscTocSignature::wad) {
                throw std::runtime_error(
                    "Global TOC slot " + std::to_string(requested_slot) +
                    " is not a WadV1 record");
            }

            const auto decoded = openrc::decode_wad(
                arguments[1],
                entry->extent.lba,
                entry->extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto table = openrc::parse_boundary_table(
                decoded.bytes,
                openrc::BoundaryTableLimits{
                    kMaximumCliDecodedWadBytes,
                    kMaximumCliDecodedWadBytes});

            std::cout
                << "OpenRC boundary-table report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Global TOC slot:     " << requested_slot << '\n'
                << "LBA:                 " << entry->extent.lba << '\n'
                << "Sectors:             " << entry->extent.sectors << '\n'
                << "Decoded bytes:       " << table.input_size << '\n'
                << "Decoded SHA-256:     " << decoded.sha256 << "\n\n"
                << "Regions:\n";
            for (std::size_t index = 0; index < table.regions.size(); ++index) {
                const auto& region = table.regions[index];
                std::cout
                    << "  [" << index << "] offset "
                    << hexadecimal(region.offset, 8)
                    << ", size " << region.size << " bytes\n";
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "sblk-wav") {
        if (arguments.size() < 6U || arguments.size() > 7U) {
            std::cerr
                << "error: sblk-wav expects an ISO path, level ID, physical "
                   "block index, output path, and an explicit sample-rate "
                   "policy\n";
            return kUsageError;
        }

        const auto level_value = parse_decimal_argument(arguments[2]);
        const auto block_value = parse_decimal_argument(arguments[3]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        if (!block_value) {
            std::cerr
                << "error: physical SBlk block index must be an unsigned "
                         "decimal number\n";
            return kUsageError;
        }

        const auto policy_name = openrc::path_to_utf8(arguments[5]);
        openrc::SBlkWavSampleRateV1 sample_rate;
        if (policy_name == "spu-native-48000") {
            if (arguments.size() != 6U) {
                std::cerr
                    << "error: spu-native-48000 takes no numeric sample rate\n";
                return kUsageError;
            }
            sample_rate.policy =
                openrc::SBlkWavSampleRatePolicyV1::
                    spu_native_48000_diagnostic;
        } else if (policy_name == "caller-supplied-hz") {
            if (arguments.size() != 7U) {
                std::cerr
                    << "error: caller-supplied-hz requires one non-zero "
                             "Hz value\n";
                return kUsageError;
            }
            const auto hz = parse_decimal_argument(arguments[6]);
            if (!hz || *hz == 0U ||
                *hz > std::numeric_limits<std::uint32_t>::max()) {
                std::cerr
                    << "error: sample rate must be a non-zero 32-bit unsigned "
                       "decimal value\n";
                return kUsageError;
            }
            sample_rate.policy =
                openrc::SBlkWavSampleRatePolicyV1::caller_supplied_hz;
            sample_rate.caller_supplied_hz = static_cast<std::uint32_t>(*hz);
        } else {
            std::cerr
                << "error: sample-rate policy must be spu-native-48000 or "
                   "caller-supplied-hz\n";
            return kUsageError;
        }

        const auto level_id = static_cast<std::uint32_t>(*level_value);
        try {
            const auto loaded = load_level_sblk_v1(arguments[1], level_id);
            const auto exported =
                openrc::encode_sblk_physical_block_pcm16_mono_wav_v1(
                    loaded.bundle,
                    loaded.audio,
                    *block_value,
                    sample_rate,
                    openrc::SBlkWavLimitsV1{
                        kMaximumCliDecodedWadBytes,
                        kMaximumCliSBlkFrames,
                        kMaximumCliSBlkSamples,
                        kMaximumCliSBlkWavBytes});
            write_new_binary_file(arguments[4], exported.wav_bytes);

            const auto& block = loaded.audio.blocks.at(
                static_cast<std::size_t>(exported.physical_block_index));
            std::cout
                << "OpenRC SBlk PCM16 WAV export\n"
                << "Image:               "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:            " << level_id << '\n'
                << "Primary extent LBA:  " << loaded.primary_extent.lba << '\n'
                << "Subrange offset:     "
                << hexadecimal(loaded.subrange.relative_offset, 8) << '\n'
                << "Physical block:      "
                << exported.physical_block_index << " of "
                << loaded.audio.block_count << '\n'
                << "Block kind:          "
                << (exported.block_kind == openrc::SBlkAudioBlockKind::looped
                        ? "looped"
                        : "one-shot")
                << '\n'
                << "Block bank range:    " << hexadecimal(block.offset, 8)
                << " + " << block.size << " bytes\n"
                << "References:          " << block.references.size() << '\n'
                << "Rate policy:         " << policy_name << '\n'
                << "Sample rate:         " << exported.sample_rate_hz << " Hz\n"
                << "Content frames:      " << exported.content_frame_count << '\n'
                << "Content samples:     " << exported.content_sample_count << '\n';
            if (exported.loop_sample_begin && exported.loop_sample_end) {
                std::cout
                    << "Loop sample range:   ["
                    << *exported.loop_sample_begin << ", "
                    << *exported.loop_sample_end << ")\n";
            } else {
                std::cout << "Loop sample range:   none\n";
            }
            std::cout
                << "WAV output:          "
                << openrc::path_to_utf8(arguments[4]) << '\n'
                << "WAV bytes:           " << exported.wav_bytes.size() << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "sblk") {
        if (arguments.size() != 3) {
            std::cerr << "error: sblk expects an ISO path and a level ID\n";
            return kUsageError;
        }

        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto assets = openrc::inspect_disc_toc_assets(arguments[1]);
            const auto level_assets = std::find_if(
                assets.levels.begin(),
                assets.levels.end(),
                [level_id](const openrc::DiscTocLevelAssets& candidate) {
                    return candidate.level_id == level_id;
                });
            const auto level_layout = std::find_if(
                assets.layout.levels.begin(),
                assets.layout.levels.end(),
                [level_id](const openrc::DiscTocLevelDescriptor& candidate) {
                    return candidate.level_id == level_id;
                });
            if (level_assets == assets.levels.end() ||
                level_layout == assets.layout.levels.end()) {
                throw std::runtime_error("The requested level is absent from DiscTocV1");
            }

            constexpr std::size_t kSBlkPrimarySubrangeIndex = 1;
            const auto& subrange =
                level_assets->primary_extent0.subranges[kSBlkPrimarySubrangeIndex];
            if (subrange.byte_size == 0) {
                throw std::runtime_error("The level's SBlk subrange is empty");
            }
            const auto& primary_extent =
                level_layout->primary_extents.front();
            const auto primary_bytes = read_disc_extent(
                arguments[1],
                primary_extent.lba,
                primary_extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto subrange_offset =
                static_cast<std::uint64_t>(subrange.relative_offset);
            const auto subrange_size =
                static_cast<std::uint64_t>(subrange.byte_size);
            if (subrange_offset > primary_bytes.size() ||
                subrange_size > primary_bytes.size() - subrange_offset) {
                throw std::runtime_error(
                    "The SBlk subrange lies outside primary extent 0");
            }

            const auto bundle = openrc::parse_sblk_bundle_v3(
                std::span<const std::byte>(primary_bytes).subspan(
                    static_cast<std::size_t>(subrange_offset),
                    static_cast<std::size_t>(subrange_size)),
                openrc::SBlkLimits{
                    kMaximumCliDecodedWadBytes,
                    1'000'000U,
                    1'000'000U,
                    kMaximumCliDecodedWadBytes});
            const auto audio = openrc::analyze_sblk_audio_v1(
                bundle,
                openrc::SBlkAudioLimits{
                    1'000'000U,
                    1'000'000U,
                    kMaximumCliDecodedWadBytes});
            std::uint64_t looped_blocks = 0;
            std::uint64_t padded_loops = 0;
            std::uint64_t retuned_blocks = 0;
            for (const auto& block : audio.blocks) {
                if (block.kind == openrc::SBlkAudioBlockKind::looped) {
                    ++looped_blocks;
                    if (block.trailing_padding_frame_count != 0U) {
                        ++padded_loops;
                    }
                }
                if (!block.references.empty()) {
                    const auto first_note = block.references.front().center_note;
                    const auto first_fine = block.references.front().center_fine;
                    if (std::any_of(
                            block.references.begin() + 1,
                            block.references.end(),
                            [first_note, first_fine](
                                const openrc::SBlkAudioReference& reference) {
                                return reference.center_note != first_note ||
                                    reference.center_fine != first_fine;
                            })) {
                        ++retuned_blocks;
                    }
                }
            }
            const auto one_shot_blocks =
                static_cast<std::uint64_t>(audio.block_count) - looped_blocks;

            std::cout
                << "OpenRC SBlkBundleV3 report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:            " << level_id << '\n'
                << "Primary extent LBA:  " << primary_extent.lba << '\n'
                << "Primary sectors:     " << primary_extent.sectors << '\n'
                << "Subrange offset:     "
                << hexadecimal(subrange.relative_offset, 8) << '\n'
                << "Subrange bytes:      " << subrange.byte_size << '\n'
                << "SBlk record:         offset "
                << hexadecimal(bundle.sblk_record.offset, 8)
                << ", size " << bundle.sblk_record.size << '\n'
                << "Secondary record:    offset "
                << hexadecimal(bundle.secondary_record.offset, 8)
                << ", size " << bundle.secondary_record.size << '\n'
                << "Opaque A/B:          "
                << hexadecimal(bundle.opaque_a, 8) << " / "
                << hexadecimal(bundle.opaque_b, 8) << '\n'
                << "Descriptor table:    "
                << hexadecimal(bundle.descriptor_table_offset, 8)
                << " - " << hexadecimal(bundle.descriptor_table_end, 8)
                << " (end exclusive)\n"
                << "Descriptors:         " << bundle.descriptors.size() << '\n'
                << "Items:               " << audio.item_count << '\n'
                << "Audio references:    " << audio.reference_count << '\n'
                << "Audio blocks:        " << audio.block_count << '\n'
                << "One-shot blocks:     " << one_shot_blocks << '\n'
                << "Looped blocks:       " << looped_blocks << '\n'
                << "Padded loops:        " << padded_loops << '\n'
                << "Retuned blocks:      " << retuned_blocks << '\n'
                << "Shared references:   "
                << (audio.reference_count - audio.block_count) << '\n'
                << "ADPCM frames:        " << audio.frame_count << '\n'
                << "Item bytes:          " << bundle.item_bytes.size() << '\n'
                << "Secondary bytes:     " << bundle.secondary_bytes.size() << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "scene-blocks") {
        if (arguments.size() != 3) {
            std::cerr
                << "error: scene-blocks expects an ISO path and a level ID\n";
            return kUsageError;
        }

        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto assets = openrc::inspect_disc_toc_assets(arguments[1]);
            const auto level_assets = std::find_if(
                assets.levels.begin(),
                assets.levels.end(),
                [level_id](const openrc::DiscTocLevelAssets& candidate) {
                    return candidate.level_id == level_id;
                });
            const auto level_layout = std::find_if(
                assets.layout.levels.begin(),
                assets.layout.levels.end(),
                [level_id](const openrc::DiscTocLevelDescriptor& candidate) {
                    return candidate.level_id == level_id;
                });
            if (level_assets == assets.levels.end() ||
                level_layout == assets.layout.levels.end()) {
                throw std::runtime_error("The requested level is absent from DiscTocV1");
            }

            constexpr std::size_t kSceneBlockSubrangeIndex = 10U;
            const auto& subrange =
                level_assets->primary_extent0.subranges[kSceneBlockSubrangeIndex];
            if (subrange.byte_size == 0U) {
                throw std::runtime_error(
                    "The level's scene-block WadV1 subrange is empty");
            }
            if (subrange.signature != openrc::DiscTocSignature::wad) {
                throw std::runtime_error(
                    "The level's scene-block subrange is not WadV1");
            }

            const auto& primary_extent =
                level_layout->primary_extents.front();
            const auto primary_bytes = read_disc_extent(
                arguments[1],
                primary_extent.lba,
                primary_extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto subrange_offset =
                static_cast<std::uint64_t>(subrange.relative_offset);
            const auto subrange_size =
                static_cast<std::uint64_t>(subrange.byte_size);
            if (subrange_offset > primary_bytes.size() ||
                subrange_size > primary_bytes.size() - subrange_offset) {
                throw std::runtime_error("The scene-block WadV1 subrange lies "
                                         "outside primary extent 0");
            }

            const auto logical_wad = std::span<const std::byte>(primary_bytes).subspan(
                static_cast<std::size_t>(subrange_offset),
                static_cast<std::size_t>(subrange_size));
            const auto decoded = openrc::decode_wad_bytes(
                logical_wad,
                kMaximumCliDecodedWadBytes);
            const auto directory = openrc::parse_scene_block_directory_v1(
                decoded.bytes,
                openrc::SceneBlockDirectoryLimits{
                    kMaximumCliDecodedWadBytes,
                    kMaximumCliSceneBlockRecords,
                    kMaximumCliDecodedWadBytes});

            const auto companion_count =
                level_assets->primary_extent3.tables.front().size();
            if (static_cast<std::uint64_t>(companion_count) !=
                directory.record_count) {
                throw std::runtime_error(
                    "The scene-block record count does not match "
                    "companion extent-3 table 0");
            }
            const auto envelope_bytes =
                directory.chain_end - directory.directory_bytes;
            std::array<std::uint64_t, openrc::kSceneBlockSectionCount>
                section_total_bytes{};
            std::array<std::uint64_t, openrc::kSceneBlockSectionCount>
                section_minimum_bytes{};
            std::array<std::uint64_t, openrc::kSceneBlockSectionCount>
                section_maximum_bytes{};
            section_minimum_bytes.fill(
                std::numeric_limits<std::uint64_t>::max());

            constexpr std::array<openrc::SceneBlockVifOpcode, 8U> kVifOpcodes{
                openrc::SceneBlockVifOpcode::nop,
                openrc::SceneBlockVifOpcode::stcycl,
                openrc::SceneBlockVifOpcode::stmod,
                openrc::SceneBlockVifOpcode::strow,
                openrc::SceneBlockVifOpcode::unpack_v3_16,
                openrc::SceneBlockVifOpcode::unpack_v4_32,
                openrc::SceneBlockVifOpcode::unpack_v4_16,
                openrc::SceneBlockVifOpcode::unpack_v4_8,
            };
            constexpr std::array<std::string_view, kVifOpcodes.size()>
                kVifOpcodeNames{
                    "NOP",
                    "STCYCL",
                    "STMOD",
                    "STROW",
                    "UNPACK V3-16",
                    "UNPACK V4-32",
                    "UNPACK V4-16",
                    "UNPACK V4-8",
                };
            std::array<std::uint64_t, kVifOpcodes.size()> vif_opcode_counts{};
            std::uint64_t vif_stream_bytes = 0U;
            std::uint64_t vif_command_count = 0U;
            std::uint64_t vif_unpack_count = 0U;
            std::uint64_t vif_payload_bytes = 0U;
            std::uint64_t vif_padding_bytes = 0U;
            std::uint64_t vu_vector_writes = 0U;
            std::uint64_t vu_unique_qword_writes = 0U;
            std::uint64_t vu_overwrite_vector_writes = 0U;
            std::uint64_t vu_wrapped_vector_writes = 0U;
            std::uint64_t vu_fully_known_qwords = 0U;
            std::uint64_t vu_partially_known_qwords = 0U;
            std::uint64_t vu_indeterminate_qwords = 0U;
            std::uint64_t vu_phase_count = 0U;
            std::uint64_t vu_minimum_phases_per_stream =
                std::numeric_limits<std::uint64_t>::max();
            std::uint64_t vu_maximum_phases_per_stream = 0U;
            std::uint64_t vu_phase_destination_runs = 0U;
            std::uint64_t vu_internal_phase_overwrites = 0U;
            std::uint64_t vu_prior_phase_overwrites = 0U;
            for (const auto& entry : directory.entries) {
                for (std::size_t section_index = 0U;
                     section_index < openrc::kSceneBlockSectionCount;
                     ++section_index) {
                    const auto section_bytes =
                        entry.section_layout.ranges[section_index].size;
                    section_total_bytes[section_index] += section_bytes;
                    section_minimum_bytes[section_index] = std::min(
                        section_minimum_bytes[section_index],
                        section_bytes);
                    section_maximum_bytes[section_index] = std::max(
                        section_maximum_bytes[section_index],
                        section_bytes);
                }

                const auto vif_size = static_cast<std::size_t>(
                    entry.section_layout.relative_boundaries[
                        openrc::kSceneBlockVifSectionCount]);
                const auto prefix_size = static_cast<std::size_t>(
                    openrc::kSceneBlockDirectoryV1Stride);
                if (entry.block_bytes.size() < prefix_size ||
                    vif_size > entry.block_bytes.size() - prefix_size) {
                    throw std::runtime_error("A verified scene-block VIF range "
                                             "exceeds its owned envelope");
                }
                const auto vif_bytes = std::span<const std::byte>(
                    entry.block_bytes).subspan(prefix_size, vif_size);
                const auto execution = openrc::execute_scene_block_vu_v1(
                    vif_bytes,
                    openrc::SceneBlockVuExecutionOptionsV1{0U},
                    openrc::SceneBlockVuLimits{
                        openrc::SceneBlockVifLimits{
                            kMaximumCliDecodedWadBytes,
                            1'000'000U,
                            kMaximumCliDecodedWadBytes},
                        1'000'000U});
                const auto& vif = execution.stream;
                const auto phases =
                    openrc::group_scene_block_vu_phases_v1(execution);
                const auto stream_phase_count =
                    static_cast<std::uint64_t>(phases.size());
                vu_phase_count += stream_phase_count;
                vu_minimum_phases_per_stream = std::min(
                    vu_minimum_phases_per_stream,
                    stream_phase_count);
                vu_maximum_phases_per_stream = std::max(
                    vu_maximum_phases_per_stream,
                    stream_phase_count);
                for (const auto& phase : phases) {
                    vu_phase_destination_runs +=
                        static_cast<std::uint64_t>(
                            phase.unique_destination_runs.size());
                    vu_internal_phase_overwrites +=
                        phase.internal_overwrite_count;
                    vu_prior_phase_overwrites +=
                        phase.prior_phase_overwrite_count;
                }
                vif_stream_bytes += vif.input_bytes;
                vif_command_count +=
                    static_cast<std::uint64_t>(vif.commands.size());
                vif_payload_bytes += vif.total_payload_bytes;
                vif_padding_bytes += vif.total_padding_bytes;
                vu_vector_writes += execution.total_vector_writes;
                vu_unique_qword_writes += execution.unique_qword_writes;
                vu_overwrite_vector_writes +=
                    execution.overwrite_vector_writes;
                vu_wrapped_vector_writes += execution.wrapped_vector_writes;
                for (const auto& qword : execution.memory) {
                    if (qword.write_count == 0U) {
                        continue;
                    }
                    const auto known_lanes = std::count_if(
                        qword.lanes.begin(),
                        qword.lanes.end(),
                        [](const openrc::SceneBlockVuValueV1& lane) {
                            return lane.state ==
                                openrc::SceneBlockVuValueState::known;
                        });
                    if (known_lanes == qword.lanes.end() - qword.lanes.begin()) {
                        ++vu_fully_known_qwords;
                    } else if (known_lanes == 0) {
                        ++vu_indeterminate_qwords;
                    } else {
                        ++vu_partially_known_qwords;
                    }
                }
                for (const auto& vif_command : vif.commands) {
                    const auto opcode = std::find(
                        kVifOpcodes.begin(),
                        kVifOpcodes.end(),
                        vif_command.opcode);
                    if (opcode == kVifOpcodes.end()) {
                        throw std::runtime_error("The SceneBlock VIF parser "
                                                 "returned an unknown opcode");
                    }
                    const auto opcode_index = static_cast<std::size_t>(
                        std::distance(kVifOpcodes.begin(), opcode));
                    ++vif_opcode_counts[opcode_index];
                    if (vif_command.component_count != 0U) {
                        ++vif_unpack_count;
                    }
                }
            }

            std::cout
                << "OpenRC SceneBlockDirectoryV1 report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:            " << level_id << '\n'
                << "Primary extent LBA:  " << primary_extent.lba << '\n'
                << "Primary sectors:     " << primary_extent.sectors << '\n'
                << "Subrange offset:     "
                << hexadecimal(subrange.relative_offset, 8) << '\n'
                << "Compressed bytes:    " << subrange.byte_size << '\n'
                << "Decoded bytes:       " << decoded.bytes.size() << '\n'
                << "Decoded SHA-256:     " << decoded.sha256 << '\n'
                << "Stride:              " << directory.stride_bytes << '\n'
                << "Declared count:      " << directory.declared_count << '\n'
                << "Records:             " << directory.record_count << '\n'
                << "Companion pairs:     " << companion_count << '\n'
                << "Header float:        " << directory.header_float << '\n'
                << "Directory bytes:     " << directory.directory_bytes << '\n'
                << "Overlapped entry:    offset "
                << hexadecimal(directory.overlapped_entry_range.offset, 8)
                << ", size " << directory.overlapped_entry_range.size << " bytes\n"
                << "Block chain end:     "
                << hexadecimal(directory.chain_end, 8) << '\n'
                << "Block envelopes:     " << envelope_bytes << " bytes\n"
                << "Trailing offset:     "
                << hexadecimal(directory.trailing_range.offset, 8) << '\n'
                << "Trailing bytes:      " << directory.trailing_range.size << '\n'
                << "Owned bytes:         " << directory.owned_byte_count << '\n'
                << "Sections per block:  "
                << openrc::kSceneBlockSectionCount << "\n\n"
                << "Neutral remainder sections:\n";
            for (std::size_t section_index = 0U;
                 section_index < openrc::kSceneBlockSectionCount;
                 ++section_index) {
                std::cout
                    << "  [" << section_index << "] total "
                    << section_total_bytes[section_index]
                    << " bytes, per-block range "
                    << section_minimum_bytes[section_index] << ".."
                    << section_maximum_bytes[section_index] << " bytes\n";
            }

            std::cout
                << "\nBounded VIF streams (sections 0-4):\n"
                << "  Streams:           " << directory.record_count << '\n'
                << "  Stream bytes:      " << vif_stream_bytes << '\n'
                << "  Commands:          " << vif_command_count << '\n'
                << "  UNPACK commands:   " << vif_unpack_count << '\n'
                << "  Payload bytes:     " << vif_payload_bytes << '\n'
                << "  Alignment bytes:   " << vif_padding_bytes << '\n'
                << "  Opcodes:\n";
            for (std::size_t opcode_index = 0U;
                 opcode_index < kVifOpcodes.size();
                 ++opcode_index) {
                std::cout
                    << "    " << kVifOpcodeNames[opcode_index] << ": "
                    << vif_opcode_counts[opcode_index] << '\n';
            }

            std::cout
                << "\nNeutral VU1 execution (relative TOPS=0):\n"
                << "  Vector writes:     " << vu_vector_writes << '\n'
                << "  Unique qwords:     " << vu_unique_qword_writes << '\n'
                << "  Overwrite writes:  " << vu_overwrite_vector_writes << '\n'
                << "  Wrapped writes:    " << vu_wrapped_vector_writes << '\n'
                << "  Final fully known: " << vu_fully_known_qwords << '\n'
                << "  Final partial:     " << vu_partially_known_qwords << '\n'
                << "  Final unknown:     " << vu_indeterminate_qwords << '\n'
                << "\nNeutral control-to-UNPACK phases:\n"
                << "  Phases:            " << vu_phase_count << '\n'
                << "  Per-stream range:  ";
            if (directory.entries.empty()) {
                std::cout << "none\n";
            } else {
                std::cout
                    << vu_minimum_phases_per_stream << ".."
                    << vu_maximum_phases_per_stream << '\n';
            }
            std::cout
                << "  Destination runs:  " << vu_phase_destination_runs << '\n'
                << "  Internal overwrite:" << ' '
                << vu_internal_phase_overwrites << '\n'
                << "  Prior-phase overwrite: "
                << vu_prior_phase_overwrites << '\n';

            if (!directory.entries.empty()) {
                const auto& first = directory.entries.front();
                const auto& last = directory.entries.back();
                std::cout
                    << "First block:         offset "
                    << hexadecimal(first.block_offset, 8)
                    << ", envelope " << first.block_range.size << " bytes\n"
                    << "Last block:          offset "
                    << hexadecimal(last.block_offset, 8)
                    << ", envelope " << last.block_range.size << " bytes\n";
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "scene-block-vu-run") {
        if (arguments.size() != 6U && arguments.size() != 7U) {
            std::cerr
                << "error: scene-block-vu-run expects an ISO path, an ELF "
                   "path, a level ID, a record index, and one VU pair "
                   "entrypoint, plus an optional TGA output path\n";
            return kUsageError;
        }

        const auto level_value = parse_decimal_argument(arguments[3]);
        const auto record_value = parse_decimal_argument(arguments[4]);
        const auto entrypoint_value = parse_decimal_argument(arguments[5]);
        constexpr std::array<std::uint16_t, 6U> kTaskEntrypoints{
            6U, 8U, 10U, 14U, 16U, 20U};
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount ||
            !record_value || !entrypoint_value ||
            *entrypoint_value > std::numeric_limits<std::uint16_t>::max() ||
            std::find(kTaskEntrypoints.begin(),
                      kTaskEntrypoints.end(),
                      static_cast<std::uint16_t>(*entrypoint_value)) ==
                kTaskEntrypoints.end()) {
            std::cerr
                << "error: level ID must be 0.."
                << (openrc::kDiscTocLevelCount - 1U)
                << ", record must be a decimal index, and entry-pair must "
                   "be one of 6,8,10,14,16,20\n";
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);
        const auto entrypoint =
            static_cast<std::uint16_t>(*entrypoint_value);

        try {
            const auto disc_report = openrc::inspect_disc(arguments[1]);
            const auto assets = openrc::inspect_disc_toc_assets(arguments[1]);
            const auto level_assets = std::find_if(
                assets.levels.begin(),
                assets.levels.end(),
                [level_id](const openrc::DiscTocLevelAssets& candidate) {
                    return candidate.level_id == level_id;
                });
            const auto level_layout = std::find_if(
                assets.layout.levels.begin(),
                assets.layout.levels.end(),
                [level_id](const openrc::DiscTocLevelDescriptor& candidate) {
                    return candidate.level_id == level_id;
                });
            if (level_assets == assets.levels.end() ||
                level_layout == assets.layout.levels.end()) {
                throw std::runtime_error(
                    "The requested level is absent from DiscTocV1");
            }

            constexpr std::size_t kSceneBlockSubrangeIndex = 10U;
            const auto& subrange =
                level_assets->primary_extent0
                    .subranges[kSceneBlockSubrangeIndex];
            if (subrange.byte_size == 0U ||
                subrange.signature != openrc::DiscTocSignature::wad) {
                throw std::runtime_error("The level's scene-block subrange is "
                                         "not a non-empty WadV1");
            }
            const auto& primary_extent =
                level_layout->primary_extents.front();
            const auto primary_bytes = read_disc_extent(
                arguments[1],
                primary_extent.lba,
                primary_extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto subrange_offset =
                static_cast<std::uint64_t>(subrange.relative_offset);
            const auto subrange_size =
                static_cast<std::uint64_t>(subrange.byte_size);
            if (subrange_offset > primary_bytes.size() ||
                subrange_size > primary_bytes.size() - subrange_offset) {
                throw std::runtime_error("The scene-block WadV1 subrange lies "
                                         "outside primary extent 0");
            }
            const auto logical_wad =
                std::span<const std::byte>(primary_bytes)
                    .subspan(static_cast<std::size_t>(subrange_offset),
                             static_cast<std::size_t>(subrange_size));
            const auto decoded = openrc::decode_wad_bytes(
                logical_wad, kMaximumCliDecodedWadBytes);
            const auto directory = openrc::parse_scene_block_directory_v1(
                decoded.bytes,
                openrc::SceneBlockDirectoryLimits{
                    kMaximumCliDecodedWadBytes,
                    kMaximumCliSceneBlockRecords,
                    kMaximumCliDecodedWadBytes,
                });
            const auto companion_count =
                level_assets->primary_extent3.tables.front().size();
            if (static_cast<std::uint64_t>(companion_count) !=
                directory.record_count) {
                throw std::runtime_error(
                    "The scene-block record count does not match "
                    "companion extent-3 table 0");
            }
            if (*record_value >= directory.entries.size()) {
                throw std::runtime_error(
                    "The requested scene-block record index is out of range");
            }

            const auto elf_bytes =
                read_bounded_binary_file(arguments[2], kMaximumCliElfBytes);
            openrc::Sha256 elf_hash;
            elf_hash.update(std::span<const std::byte>(elf_bytes));
            const auto elf_sha256 = openrc::hex_digest(elf_hash.finish());
            if (disc_report.boot_sha256.empty()) {
                throw std::runtime_error(
                    "The ISO boot executable has no SHA-256 identity");
            }
            if (elf_sha256 != disc_report.boot_sha256) {
                throw std::runtime_error("The supplied ELF does not match the "
                                         "boot executable in the ISO");
            }
            const auto elf =
                openrc::inspect_elf(std::span<const std::byte>(elf_bytes));
            if (!elf.dvp_overlay_table) {
                throw std::runtime_error(
                    "The executable has no DVP overlay table");
            }

            std::vector<openrc::ElfDvpOverlay> selected_overlays;
            for (const auto& overlay : elf.dvp_overlay_table->overlays) {
                if (overlay.name.find(".55907.") != std::string::npos) {
                    selected_overlays.push_back(overlay);
                }
            }
            std::sort(
                selected_overlays.begin(),
                selected_overlays.end(),
                [](const openrc::ElfDvpOverlay& left,
                   const openrc::ElfDvpOverlay& right) {
                    return left.virtual_memory_address <
                        right.virtual_memory_address;
                });
            constexpr std::array<std::uint32_t, 8U> kExpectedOverlayAddresses{
                0x0000U,
                0x0800U,
                0x1000U,
                0x1800U,
                0x2000U,
                0x2800U,
                0x3000U,
                0x3800U,
            };
            constexpr std::array<std::uint32_t, 8U> kExpectedOverlaySizes{
                0x0800U,
                0x0800U,
                0x0800U,
                0x0800U,
                0x0800U,
                0x0800U,
                0x0800U,
                0x0260U,
            };
            if (selected_overlays.size() !=
                kExpectedOverlayAddresses.size()) {
                throw std::runtime_error("DVP program 55907 does not have its "
                                         "exact eight overlay chunks");
            }
            for (std::size_t index = 0U;
                 index < selected_overlays.size();
                 ++index) {
                if (selected_overlays[index].virtual_memory_address !=
                        kExpectedOverlayAddresses[index] ||
                    selected_overlays[index].size !=
                        kExpectedOverlaySizes[index]) {
                    throw std::runtime_error("DVP program 55907 has an "
                                             "unexpected VU address layout");
                }
            }

            const std::array<std::uint16_t, 2U> entrypoints{0U, entrypoint};
            const auto program = openrc::decode_dvp_vu_program_v1(
                std::span<const std::byte>(elf_bytes),
                std::span<const openrc::ElfDvpOverlay>(selected_overlays),
                std::span<const std::uint16_t>(entrypoints),
                openrc::DvpVuLimits{
                    kMaximumCliElfBytes,
                    kMaximumCliDvpVuListItems,
                    openrc::kDvpVu1MicroMemoryBytes,
                    kMaximumCliDvpVuListItems,
                    2U * openrc::kDvpVu1InstructionCount,
                    8U * openrc::kDvpVu1InstructionCount,
                });

            constexpr std::uint32_t kTaskPreambleVirtualAddress = 0x001deac0U;
            constexpr std::uint64_t kTaskPreambleBytes =
                openrc::kSceneBlockTaskPreambleQwordCount * 16U;
            const auto preamble_end =
                static_cast<std::uint64_t>(kTaskPreambleVirtualAddress) +
                kTaskPreambleBytes;
            std::optional<std::span<const std::byte>> preamble_bytes;
            for (const auto& segment : elf.program_headers) {
                constexpr std::uint32_t kLoadSegmentType = 1U;
                if (segment.type != kLoadSegmentType) {
                    continue;
                }
                const auto segment_begin =
                    static_cast<std::uint64_t>(segment.virtual_address);
                const auto segment_end =
                    segment_begin + static_cast<std::uint64_t>(segment.file_size);
                if (kTaskPreambleVirtualAddress < segment_begin ||
                    preamble_end > segment_end) {
                    continue;
                }
                if (preamble_bytes) {
                    throw std::runtime_error(
                        "The SceneBlock task preamble maps to multiple PT_LOAD "
                        "segments");
                }
                const auto file_offset =
                    static_cast<std::uint64_t>(segment.file_offset) +
                    (static_cast<std::uint64_t>(
                         kTaskPreambleVirtualAddress) -
                     segment_begin);
                if (file_offset > elf_bytes.size() ||
                    kTaskPreambleBytes > elf_bytes.size() - file_offset ||
                    file_offset > std::numeric_limits<std::size_t>::max()) {
                    throw std::runtime_error("The SceneBlock task preamble "
                                             "lies outside the ELF bytes");
                }
                preamble_bytes =
                    std::span<const std::byte>(elf_bytes)
                        .subspan(static_cast<std::size_t>(file_offset),
                                 static_cast<std::size_t>(kTaskPreambleBytes));
            }
            if (!preamble_bytes) {
                throw std::runtime_error("The SceneBlock task preamble VA is "
                                         "not file-backed by PT_LOAD");
            }
            const auto preamble =
                openrc::parse_scene_block_task_preamble_v1(*preamble_bytes);

            constexpr openrc::DvpVuExecutionLimitsV1 kExecutionLimits{
                1'000'000U,
                64U,
                1024U,
                65'536U,
            };
            openrc::SceneBlockTaskFrameInputV1 frame_input;
            for (std::size_t row = 0U;
                 row < frame_input.transform_qwords.size();
                 ++row) {
                for (std::size_t lane = 0U;
                     lane < frame_input.transform_qwords[row].lanes.size();
                     ++lane) {
                    frame_input.transform_qwords[row].lanes[lane] =
                        openrc::DvpVuWordV1{
                            row == lane ? 0x3f800000U : 0U,
                            std::numeric_limits<std::uint32_t>::max(),
                        };
                }
            }
            // These results carry complete VU1 memory snapshots. Keep them on
            // the heap so this diagnostic-only command does not reserve
            // hundreds of KiB in run()'s stack frame for every other CLI
            // command, including prepare-native-game.
            const auto initialized =
                std::unique_ptr<openrc::SceneBlockTaskInitializationResultV1>(
                    new openrc::SceneBlockTaskInitializationResultV1(
                        openrc::initialize_scene_block_task_execution_v1(
                            program, preamble, frame_input,
                            kExecutionLimits)));

            std::cout
                << "OpenRC real SceneBlock task execution\n"
                << "Image:               "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Executable:          "
                << openrc::path_to_utf8(arguments[2]) << '\n'
                << "Executable SHA-256:  " << elf_sha256 << '\n'
                << "Level/record:        " << level_id << '/'
                << *record_value << '\n'
                << "Companion pairs:     " << companion_count << '\n'
                << "State basis:         frame transform + entry 0 + selected "
                   "record (no prior records)\n"
                << "Frame transform:     deterministic identity debug seed\n"
                << "Entrypoint pair:     "
                << hexadecimal(entrypoint, 3) << '\n'
                << "DVP overlay chunks:  ";
            for (std::size_t index = 0U;
                 index < selected_overlays.size();
                 ++index) {
                if (index != 0U) {
                    std::cout << ',';
                }
                std::cout << selected_overlays[index].overlay_section_index;
            }
            std::cout
                << '\n'
                << "Preamble VA:         "
                << hexadecimal(kTaskPreambleVirtualAddress, 8) << '\n'
                << "Initialization:      "
                << dvp_vu_termination_name(
                       initialized->vu_execution.termination)
                << '\n'
                << "Initialization pairs:"
                << ' ' << initialized->vu_execution.executed_instruction_pairs
                << '\n';
            if (!initialized->ready_state) {
                if (initialized->vu_execution.stopped_instruction_address) {
                    std::cout
                        << "Initialization stop: "
                        << hexadecimal(
                               *initialized->vu_execution
                                    .stopped_instruction_address,
                               3)
                        << '\n';
                }
                return kOperationError;
            }

            const auto execution =
                std::unique_ptr<openrc::SceneBlockTaskRecordExecutionV1>(
                    new openrc::SceneBlockTaskRecordExecutionV1(
                        openrc::execute_scene_block_task_record_v1(
                            directory.entries[static_cast<std::size_t>(
                                *record_value)],
                            entrypoint,
                            program,
                            *initialized->ready_state,
                            openrc::SceneBlockTaskExecutionLimitsV1{
                                openrc::SceneBlockTaskBuildLimitsV1{
                                    kMaximumCliDecodedWadBytes,
                                    openrc::kSceneBlockTaskMaximumDmaReferences,
                                    kMaximumCliDecodedWadBytes,
                                    openrc::SceneBlockVifLimits{
                                        kMaximumCliDecodedWadBytes,
                                        1'000'000U,
                                        kMaximumCliDecodedWadBytes,
                                    },
                                },
                                1'000'000U,
                                openrc::SceneBlockDvpVuBridgeLimitsV1{
                                    1'000'000U},
                                kExecutionLimits,
                            })));

            std::array<std::uint64_t, openrc::kSceneBlockVuLaneCount>
                known_vif_lanes{};
            std::array<std::uint64_t, 5U> vif_lane_sources{};
            for (const auto& write : execution->vif_execution.writes) {
                for (std::size_t lane = 0U; lane < write.lanes.size(); ++lane) {
                    if (write.lanes[lane].written_value.state ==
                        openrc::SceneBlockVuValueState::known) {
                        ++known_vif_lanes[lane];
                    }
                    const auto source_index =
                        static_cast<std::size_t>(write.lanes[lane].source);
                    if (source_index < vif_lane_sources.size()) {
                        ++vif_lane_sources[source_index];
                    }
                }
            }

            std::cout
                << "TOP/next TOPS:       " << execution->top_qword << '/'
                << execution->next_tops_qword << '\n'
                << "DMA references:      "
                << execution->invocation.dma_references.size() << '\n'
                << "Referenced bytes:    "
                << execution->invocation.total_reference_bytes << '\n'
                << "Expanded VIF bytes:  "
                << execution->invocation.vif_bytes.size() << '\n'
                << "VIF commands/writes: "
                << execution->vif_execution.stream.commands.size() << '/'
                << execution->vif_execution.writes.size() << '\n'
                << "Known VIF X/Y/Z/W:   " << known_vif_lanes[0U] << '/'
                << known_vif_lanes[1U] << '/' << known_vif_lanes[2U] << '/'
                << known_vif_lanes[3U] << " of "
                << execution->vif_execution.writes.size() << '\n'
                << "VIF payload/fill/V3-spill/V3-zero/V3-missing: "
                << vif_lane_sources[0U] << '/' << vif_lane_sources[1U] << '/'
                << vif_lane_sources[2U] << '/' << vif_lane_sources[3U] << '/'
                << vif_lane_sources[4U] << '\n'
                << "Final VIF CL/WL:     "
                << execution->vif_execution.final_state.cycle_length << '/'
                << execution->vif_execution.final_state.write_length << '\n'
                << "Termination:         "
                << dvp_vu_termination_name(execution->vu_execution.termination)
                << '\n'
                << "Executed pairs:      "
                << execution->vu_execution.executed_instruction_pairs << '\n'
                << "Final PC:            "
                << hexadecimal(execution->vu_execution.final_state.pc, 3)
                << '\n';
            if (execution->vu_execution.stopped_instruction_address) {
                std::cout
                    << "Stopped instruction:"
                    << ' '
                    << hexadecimal(
                           *execution->vu_execution.stopped_instruction_address,
                           3)
                    << '\n';
            }
            std::cout
                << "XGKICK events:       "
                << execution->vu_execution.xgkick_events.size() << '\n'
                << "Warnings:            ";
            if (execution->vu_execution.warnings.empty()) {
                std::cout << "none";
            } else {
                for (std::size_t index = 0U;
                     index < execution->vu_execution.warnings.size();
                     ++index) {
                    if (index != 0U) {
                        std::cout << ',';
                    }
                    std::cout << dvp_vu_warning_name(
                        execution->vu_execution.warnings[index]);
                }
            }
            std::cout << '\n';

            std::optional<openrc::GifGsDecodeResultV1> gs_decode;
            const auto& xgkick_events =
                execution->vu_execution.xgkick_events;
            const bool complete_gs_stream =
                !xgkick_events.empty() &&
                std::all_of(
                    xgkick_events.begin(),
                    xgkick_events.end(),
                    [](const openrc::DvpVuXgkickEventV1& event) {
                        return event.packet_complete;
                    });
            if (complete_gs_stream) {
                gs_decode = openrc::decode_dvp_vu_xgkick_gs_stream_v1(
                    std::span<const openrc::DvpVuXgkickEventV1>{
                        xgkick_events.data(), xgkick_events.size()},
                    openrc::GifGsDecodeLimitsV1{
                        65'536U,
                        1'000'000U,
                        1'000'000U,
                        1'000'000U,
                        1'000'000U,
                        64U,
                    });
            }

            for (std::size_t event_index = 0U;
                 event_index < execution->vu_execution.xgkick_events.size();
                 ++event_index) {
                const auto& event =
                    execution->vu_execution.xgkick_events[event_index];
                std::cout
                    << "XGKICK " << event_index
                    << " pc=" << hexadecimal(event.instruction_address, 3)
                    << " base=";
                if ((event.base_qword.known_mask & 0x03ffU) == 0x03ffU) {
                    std::cout << (event.base_qword.bits & 0x03ffU);
                } else {
                    std::cout << "indeterminate";
                }
                std::cout
                    << " qwords=" << event.packet_qwords.size()
                    << " tags=" << event.tags.size()
                    << " complete="
                    << (event.packet_complete ? "yes" : "no") << '\n';
                for (std::size_t tag_index = 0U;
                     tag_index < event.tags.size();
                     ++tag_index) {
                    const auto& tag = event.tags[tag_index];
                    std::cout
                        << "  tag " << tag_index
                        << " memory=" << tag.memory_qword
                        << " nloop=" << tag.tag.nloop
                        << " eop=" << (tag.tag.eop ? 1 : 0)
                        << " flg="
                        << static_cast<unsigned int>(tag.tag.format)
                        << " nreg="
                        << static_cast<unsigned int>(tag.tag.register_count)
                        << " regs=";
                    print_dvp_gif_registers(tag.tag);
                    std::cout
                        << " payload-qwords="
                    << tag.tag.payload_qword_count << '\n';
                }
            }
            if (gs_decode) {
                print_gif_gs_decode_report(*gs_decode);
            } else if (!xgkick_events.empty()) {
                std::cout
                    << "\nDecoded GS stream: skipped because an XGKICK "
                       "packet is incomplete\n";
            }
            if (arguments.size() == 7U) {
                if (!gs_decode) {
                    throw std::runtime_error(
                        "Cannot export a wireframe without a complete decoded "
                        "GS stream");
                }
                const auto image = make_gif_gs_wireframe_image(*gs_decode);
                const auto tga = openrc::encode_two_fip_tga(image);
                write_new_binary_file(arguments[6], tga);
                std::cout
                    << "\nWireframe TGA:       "
                    << openrc::path_to_utf8(arguments[6]) << '\n'
                    << "Wireframe size:      " << image.width << 'x'
                    << image.height << '\n'
                    << "Wireframe bytes:     " << tga.size() << '\n';
            }

            return execution->ready_state ? 0 : kOperationError;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "level-tfrag-texture") {
        if (arguments.size() != 4U && arguments.size() != 5U) {
            std::cerr
                << "error: level-tfrag-texture expects an ISO path, level ID, "
                   "texture index, and optional output TGA\n";
            return kUsageError;
        }
        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto texture_value = parse_decimal_argument(arguments[3]);
        if (!texture_value) {
            std::cerr << "error: texture index must be a decimal number\n";
            return kUsageError;
        }

        try {
            const auto level_id = static_cast<std::uint32_t>(*level_value);
            const auto assets = openrc::load_rac_level_moby_assets_v1(
                arguments[1], level_id, make_cli_moby_asset_limits());
            if (*texture_value >= assets.tfrag_textures.textures.size()) {
                throw std::runtime_error("The texture index exceeds this "
                                         "level's tfrag texture bank");
            }
            const auto& texture = assets.tfrag_textures.textures[
                static_cast<std::size_t>(*texture_value)];
            std::cout
                << "OpenRC RAC1 level tfrag texture report\n"
                << "Image:                  "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:               " << level_id << '\n'
                << "Texture index/count:    " << texture.global_index << '/'
                << assets.tfrag_textures.textures.size() << '\n'
                << "Dimensions:             " << texture.entry.width << 'x'
                << texture.entry.height << '\n'
                << "Type/mipmap/trailing:   " << texture.entry.type << '/'
                << texture.entry.mipmap_block << '/'
                << texture.entry.trailing_block << '\n'
                << "Pixel range:            "
                << hexadecimal(texture.entry.pixel_range.offset, 8) << " + "
                << texture.entry.pixel_range.size << " bytes\n"
                << "Palette range:          "
                << hexadecimal(texture.entry.palette_range.offset, 8) << " + "
                << texture.entry.palette_range.size << " bytes\n"
                << "Bank pixels/RGBA bytes: "
                << assets.tfrag_textures.total_pixel_count << '/'
                << assets.tfrag_textures.total_rgba_bytes << '\n';

            if (arguments.size() == 5U) {
                constexpr auto kMaximumTgaBytes =
                    kMaximumCliTwoFipPixels * 4U +
                    openrc::kRacLevelMobyTextureTgaHeaderBytesV1;
                const auto tga =
                    openrc::encode_rac_level_moby_texture_tga_v1(
                        texture, kMaximumTgaBytes);
                write_new_binary_file(arguments[4], tga.bytes);
                std::cout
                    << "TGA output:             "
                    << openrc::path_to_utf8(arguments[4]) << '\n'
                    << "TGA bytes:              " << tga.bytes.size() << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "level-moby-texture") {
        if (arguments.size() != 4U && arguments.size() != 5U) {
            std::cerr
                << "error: level-moby-texture expects an ISO path, level ID, "
                   "texture index, and optional output TGA\n";
            return kUsageError;
        }
        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto texture_value = parse_decimal_argument(arguments[3]);
        if (!texture_value) {
            std::cerr << "error: texture index must be a decimal number\n";
            return kUsageError;
        }

        try {
            const auto level_id = static_cast<std::uint32_t>(*level_value);
            const auto assets = openrc::load_rac_level_moby_assets_v1(
                arguments[1], level_id, make_cli_moby_asset_limits());
            if (*texture_value >= assets.textures.textures.size()) {
                throw std::runtime_error(
                    "The texture index exceeds this level's Moby texture bank");
            }
            const auto& texture = assets.textures.textures[
                static_cast<std::size_t>(*texture_value)];
            std::cout
                << "OpenRC RAC1 level Moby texture report\n"
                << "Image:                  "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:               " << level_id << '\n'
                << "Texture index/count:    " << texture.global_index << '/'
                << assets.textures.textures.size() << '\n'
                << "Dimensions:             " << texture.entry.width << 'x'
                << texture.entry.height << '\n'
                << "Type/mipmap/trailing:   " << texture.entry.type << '/'
                << texture.entry.mipmap_block << '/'
                << texture.entry.trailing_block << '\n'
                << "Pixel range:            "
                << hexadecimal(texture.entry.pixel_range.offset, 8) << " + "
                << texture.entry.pixel_range.size << " bytes\n"
                << "Palette range:          "
                << hexadecimal(texture.entry.palette_range.offset, 8) << " + "
                << texture.entry.palette_range.size << " bytes\n"
                << "Bank pixels/RGBA bytes: "
                << assets.textures.total_pixel_count << '/'
                << assets.textures.total_rgba_bytes << '\n';

            if (arguments.size() == 5U) {
                constexpr auto kMaximumTgaBytes =
                    kMaximumCliTwoFipPixels * 4U +
                    openrc::kRacLevelMobyTextureTgaHeaderBytesV1;
                const auto tga =
                    openrc::encode_rac_level_moby_texture_tga_v1(
                        texture, kMaximumTgaBytes);
                write_new_binary_file(arguments[4], tga.bytes);
                std::cout
                    << "TGA output:             "
                    << openrc::path_to_utf8(arguments[4]) << '\n'
                    << "TGA bytes:              " << tga.bytes.size() << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "level-collision") {
        if (arguments.size() != 3U) {
            std::cerr
                << "error: level-collision expects an ISO path and a "
                         "level ID\n";
            return kUsageError;
        }
        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto assets = openrc::load_rac_level_moby_assets_v1(
                arguments[1], level_id, make_cli_moby_asset_limits());
            const auto& collision = assets.collision;
            std::array<std::uint64_t, 256U> surface_counts{};
            bool have_bounds = false;
            float minimum_x = 0.0F;
            float minimum_y = 0.0F;
            float minimum_z = 0.0F;
            float maximum_x = 0.0F;
            float maximum_y = 0.0F;
            float maximum_z = 0.0F;
            const auto include_position =
                [&](const openrc::RacLevelCollisionVectorV1 position) {
                    if (!have_bounds) {
                        minimum_x = maximum_x = position.x;
                        minimum_y = maximum_y = position.y;
                        minimum_z = maximum_z = position.z;
                        have_bounds = true;
                        return;
                    }
                    minimum_x = std::min(minimum_x, position.x);
                    minimum_y = std::min(minimum_y, position.y);
                    minimum_z = std::min(minimum_z, position.z);
                    maximum_x = std::max(maximum_x, position.x);
                    maximum_y = std::max(maximum_y, position.y);
                    maximum_z = std::max(maximum_z, position.z);
                };
            for (const auto& octant : collision.octants) {
                for (const auto& vertex : octant.vertices) {
                    include_position(vertex.world_position);
                }
                for (const auto& face : octant.faces) {
                    ++surface_counts[face.surface_type];
                }
            }
            for (const auto& group : collision.hero_groups) {
                for (const auto& vertex : group.vertices) {
                    include_position(vertex.world_position);
                }
            }

            std::cout
                << "OpenRC authoritative RAC1 collision report\n"
                << "Image:                  "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:               " << level_id << '\n'
                << "Asset bytes:            " << collision.input_bytes << '\n'
                << "Main mesh range:        "
                << hexadecimal(collision.main_mesh_range.offset, 8) << " + "
                << collision.main_mesh_range.size << " bytes\n"
                << "Tree Z/Y/X slots:       " << collision.z_count << '/'
                << collision.total_y_slots << '/'
                << collision.total_x_slots << '\n'
                << "Octants:                " << collision.octants.size() << '\n'
                << "Main vertices/faces:    "
                << collision.total_main_vertices << '/'
                << collision.total_main_faces << '\n'
                << "Main quads:             "
                << collision.total_main_quads << '\n'
                << "Hero groups/verts/tris: "
                << collision.hero_groups.size() << '/'
                << collision.total_hero_vertices << '/'
                << collision.total_hero_triangles << '\n';
            if (have_bounds) {
                std::cout
                    << "Bounds X:               [" << minimum_x << ", "
                    << maximum_x << "]\n"
                    << "Bounds Y:               [" << minimum_y << ", "
                    << maximum_y << "]\n"
                    << "Bounds Z:               [" << minimum_z << ", "
                    << maximum_z << "]\n";
            }
            std::cout << "Surface types (hex=count):";
            for (std::size_t type = 0U; type < surface_counts.size(); ++type) {
                if (surface_counts[type] != 0U) {
                    std::cout << ' ' << hexadecimal(type, 2) << '='
                              << surface_counts[type];
                }
            }
            std::cout << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "level-foundation-package") {
        if (arguments.size() != 4U) {
            std::cerr
                << "error: level-foundation-package expects an ISO path, "
                   "a level ID, and a new output path\n";
            return kUsageError;
        }
        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto assets = openrc::load_rac_level_moby_assets_v1(
                arguments[1], level_id, make_cli_moby_asset_limits());
            const auto package = compile_cli_level_foundation(assets);
            const auto package_bytes = openrc::encode_level_package_v1(
                package, make_cli_level_foundation_package_limits());
            const auto package_sha256 =
                openrc::prepared_content_sha256_v1(package_bytes);
            write_new_binary_file(arguments[3], package_bytes);

            std::cout
                << "OpenRC native level foundation package\n"
                << "Image:                  "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:               " << level_id << '\n'
                << "Build/content API:      " << package.build_id << '/'
                << package.content_api_version << '\n'
                << "Resources:              " << package.resources.size()
                << '\n'
                << "Package bytes:          " << package_bytes.size() << '\n'
                << "Package SHA-256:        "
                << openrc::hex_digest(package_sha256) << '\n'
                << "Output:                 "
                << openrc::path_to_utf8(arguments[3]) << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "level-native-package") {
        if (arguments.size() != 5U) {
            std::cerr
                << "error: level-native-package expects an ISO path, the "
                   "matching boot ELF, a level ID, and a new output path\n";
            return kUsageError;
        }
        const auto level_value = parse_decimal_argument(arguments[3]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto disc = openrc::inspect_disc(
                arguments[1]);
            if (disc.game != openrc::GameId::ratchet_and_clank_2002 ||
                !disc.supported_build) {
                throw std::runtime_error(
                    "The disc is not the supported RAC1 PAL v2.00 build");
            }
            if (disc.image_size > std::numeric_limits<std::uint64_t>::max()) {
                throw std::runtime_error(
                    "The source image size exceeds the package provenance "
                    "format");
            }

            const auto boot_executable_size_before =
                std::filesystem::file_size(arguments[2]);
            if (boot_executable_size_before == 0U ||
                boot_executable_size_before > kMaximumCliElfBytes) {
                throw std::runtime_error(
                    "The prepared boot ELF is empty or exceeds the 64 MiB "
                    "compiler limit");
            }

            std::cout << "Hashing the source image and prepared boot ELF "
                         "before native scene compilation...\n";
            const auto source_digest_before =
                openrc::sha256_file_digest(arguments[1]);
            const auto boot_executable_digest_before =
                openrc::sha256_file_digest(arguments[2]);
            const auto source_size_before =
                static_cast<std::uint64_t>(disc.image_size);

            const auto package_limits = make_cli_native_level_package_limits();
            const auto assets = openrc::load_rac_level_moby_assets_v1(
                arguments[1], level_id, make_cli_moby_asset_limits());
            auto package = compile_cli_level_foundation(assets, package_limits);

            const openrc::runtime::LevelSceneRecoveryRequestV1 recovery_request{
                arguments[1],
                arguments[2], level_id,
                openrc::runtime::LevelSceneRecordSelectionV1::all_records,
                0U,
                openrc::kSceneBlockSourceGeometryEntrypointV1};
            const auto recovered = openrc::runtime::recover_level_scene_v1(
                recovery_request,
                openrc::runtime::make_level_scene_recovery_limits_v1(),
                openrc::runtime::make_level_scene_recovery_profile_v1());
            const auto render_profile =
                openrc::runtime::make_level_scene_render_compile_profile_v1();
            const auto render_scene =
                openrc::runtime::compile_level_scene_render_v1(recovered,
                                                               render_profile);

            const std::array<openrc::LevelPackageProvenanceV1, 2U>
                render_sources{
                    openrc::LevelPackageProvenanceV1{
                        openrc::LevelPackageProvenanceKindV1::iso_range,
                        "rac1/disc-image",
                        0U,
                        source_size_before,
                        source_digest_before,
                    },
                    openrc::LevelPackageProvenanceV1{
                        openrc::LevelPackageProvenanceKindV1::prepared_resource,
                        "rac1/boot-executable",
                        0U,
                        static_cast<std::uint64_t>(boot_executable_size_before),
                        boot_executable_digest_before,
                    },
                };
            package = openrc::attach_render_scene_to_level_package_v1(
                std::move(package), render_scene, render_sources,
                make_cli_render_scene_io_limits(), package_limits);
            const auto package_bytes =
                openrc::encode_level_package_v1(package, package_limits);

            std::cout
                << "Hashing the source image and prepared boot ELF "
                         "after native scene compilation...\n";
            const auto source_size_after =
                std::filesystem::file_size(arguments[1]);
            const auto boot_executable_size_after =
                std::filesystem::file_size(arguments[2]);
            const auto source_digest_after =
                openrc::sha256_file_digest(arguments[1]);
            const auto boot_executable_digest_after =
                openrc::sha256_file_digest(arguments[2]);
            if (source_size_after != source_size_before ||
                source_digest_after != source_digest_before) {
                throw std::runtime_error(
                    "The source image changed during native scene "
                    "compilation");
            }
            if (boot_executable_size_after != boot_executable_size_before ||
                boot_executable_digest_after != boot_executable_digest_before) {
                throw std::runtime_error(
                    "The prepared boot ELF changed during native scene "
                    "compilation");
            }

            std::uint64_t vertex_count = 0U;
            std::uint64_t index_count = 0U;
            for (const auto &mesh : render_scene.meshes) {
                vertex_count += mesh.vertices.size();
                index_count += mesh.triangle_indices.size();
            }
            std::uint64_t texture_bytes = 0U;
            for (const auto &texture : render_scene.textures) {
                for (const auto &mip : texture.mips) {
                    texture_bytes += mip.rgba8.size();
                }
            }

            write_new_binary_file(arguments[4], package_bytes);
            std::cout << "OpenRC native renderable level package\n"
                      << "Image:                  "
                      << openrc::path_to_utf8(arguments[1]) << '\n'
                      << "Level ID:               " << level_id << '\n'
                      << "Resources:              " << package.resources.size()
                      << '\n'
                      << "Meshes/instances:       "
                      << render_scene.meshes.size() << '/'
                      << render_scene.instances.size() << '\n'
                      << "Vertices/triangles:     " << vertex_count << '/'
                      << (index_count / 3U) << '\n'
                      << "Textures/materials:     "
                      << render_scene.textures.size() << '/'
                      << render_scene.materials.size() << '\n'
                      << "Texture RGBA8 bytes:    " << texture_bytes << '\n'
                      << "Skipped animated Mobies: "
                      << recovered.moby_animated_placement_count << '\n'
                      << "Package bytes:          " << package_bytes.size()
                      << '\n'
                      << "Package SHA-256:        "
                      << openrc::hex_digest(
                             openrc::prepared_content_sha256_v1(package_bytes))
                      << '\n'
                      << "Output:                 "
                      << openrc::path_to_utf8(arguments[4])
                << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "prepare-native-foundations") {
        if (arguments.size() != 3U) {
            std::cerr
                << "error: prepare-native-foundations expects an ISO path "
                   "and an absolute output root\n";
            return kUsageError;
        }
        if (!arguments[2].is_absolute()) {
            std::cerr
                << "error: the native prepared-game root must be absolute\n";
            return kUsageError;
        }

        try {
            require_publication_root_outside_source(arguments[2], arguments[1],
                                                    "source ISO");
            const auto disc = openrc::inspect_disc(arguments[1]);
            if (disc.game != openrc::GameId::ratchet_and_clank_2002 ||
                !disc.supported_build) {
                throw std::runtime_error(
                    "The disc is not the supported RAC1 PAL v2.00 build");
            }
            if (disc.image_size > std::numeric_limits<std::uint64_t>::max()) {
                throw std::runtime_error(
                    "The source image size exceeds the prepared provenance "
                    "format");
            }

            std::cout
                << "Hashing the source image before native compilation...\n";
            const auto source_digest_before =
                openrc::sha256_file_digest(arguments[1]);

            openrc::PreparedGameV2 manifest;
            manifest.content_api_version = openrc::kOpenRcContentApiVersionV1;
            manifest.provenance.game_id = "openrc-rac-2002";
            manifest.provenance.build_id = kSupportedRacBuildIdV1;
            manifest.provenance.compiler_id = "openrc-asset-compiler";
            manifest.provenance.compiler_version = OPENRC_VERSION;
            manifest.provenance.source_image_bytes =
                static_cast<std::uint64_t>(disc.image_size);
            manifest.provenance.source_image_sha256 = source_digest_before;

            std::vector<std::vector<std::byte>> package_storage;
            package_storage.reserve(openrc::kDiscTocLevelCount);
            manifest.levels.reserve(openrc::kDiscTocLevelCount);
            for (std::uint32_t level_id = 0U;
                 level_id < openrc::kDiscTocLevelCount; ++level_id) {
                const auto assets = openrc::load_rac_level_moby_assets_v1(
                    arguments[1], level_id, make_cli_moby_asset_limits());
                const auto package = compile_cli_level_foundation(assets);
                auto package_bytes = openrc::encode_level_package_v1(
                    package, make_cli_level_foundation_package_limits());
                manifest.levels.push_back(openrc::PreparedGameLevelReferenceV2{
                    level_id, level_package_path(level_id),
                    package_bytes.size(),
                    openrc::prepared_content_sha256_v1(package_bytes)});
                package_storage.push_back(std::move(package_bytes));
                std::cout << "Compiled native level " << (level_id + 1U) << '/'
                          << openrc::kDiscTocLevelCount << "\r" << std::flush;
            }
            std::cout << '\n';

            const auto final_image_size =
                std::filesystem::file_size(
                arguments[1]);
            std::cout
                << "Hashing the source image after native compilation...\n";
            const auto source_digest_after =
                openrc::sha256_file_digest(arguments[1]);
            if (final_image_size != disc.image_size ||
                source_digest_after != source_digest_before) {
                throw std::runtime_error(
                    "The source image changed during native compilation");
            }

            std::vector<openrc::PreparedGameV2LevelPackageBytesV1>
                package_inputs;
            package_inputs.reserve(package_storage.size());
            for (std::size_t index = 0U; index < package_storage.size();
                 ++index) {
                package_inputs.push_back(
                    openrc::PreparedGameV2LevelPackageBytesV1{
                        static_cast<std::uint32_t>(index),
                        package_storage[index]});
            }
            const auto published = openrc::publish_prepared_game_v2_v1(
                arguments[2], manifest, package_inputs,
                make_cli_prepared_foundation_limits());
            std::cout << "OpenRC native foundation set published\n"
                      << "Root:                   "
                      << openrc::path_to_utf8(published.root) << '\n'
                      << "Levels:                 " << published.level_count
                      << '\n'
                      << "Level package bytes:    " << published.package_bytes
                      << '\n'
                      << "Manifest SHA-256:       "
                      << openrc::hex_digest(published.manifest_sha256) << '\n'
                      << "Source image SHA-256:   "
                      << openrc::hex_digest(source_digest_before)
                << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "prepare-native-game") {
        if (arguments.size() != 4U) {
            std::cerr
                << "error: prepare-native-game expects absolute paths to an "
                   "ISO, the matching prepared boot ELF, and the output root\n";
            return kUsageError;
        }
        try {
            const auto absolute_path = [](const std::filesystem::path& path,
                                          const char* description) {
                std::error_code error;
                auto result = std::filesystem::absolute(path, error);
                if (error) {
                    throw std::runtime_error(
                        std::string("Cannot resolve the ") + description +
                        ": " + error.message());
                }
                return result.lexically_normal();
            };
            const auto source_image =
                absolute_path(arguments[1], "source ISO path");
            const auto prepared_boot =
                absolute_path(arguments[2], "prepared boot ELF path");
            const auto destination =
                absolute_path(arguments[3], "native output path");
            const auto result = openrc::prepare_native_game_v1(
                openrc::NativeGamePreparationRequestV1{
                    source_image, prepared_boot, destination, std::nullopt},
                openrc::NativeGamePreparationControlV1{
                    print_native_game_preparation_progress, nullptr});
            std::cout
                << (result.already_prepared
                        ? "OpenRC native game is already prepared\n"
                        : "OpenRC native game published\n")
                << "Root:                   "
                << openrc::path_to_utf8(result.publication.root) << '\n'
                << "Levels:                 "
                << result.publication.level_count << '\n'
                << "Level package bytes:    "
                << result.publication.package_bytes << '\n'
                << "Manifest SHA-256:       "
                << openrc::hex_digest(result.publication.manifest_sha256)
                << '\n'
                << "Source image SHA-256:   "
                << openrc::hex_digest(result.source_image_sha256) << '\n'
                << "Prepared ELF SHA-256:   "
                << openrc::hex_digest(
                       result.prepared_boot_executable_sha256)
                << '\n';
            return 0;
        } catch (const openrc::NativeGamePreparationCancelledV1& error) {
            std::cerr << "cancelled: " << error.what() << '\n';
            return kCancelled;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }
    if (command == "validate-native-game") {
        if (arguments.size() != 2U) {
            std::cerr
                << "error: validate-native-game expects an absolute prepared "
                   "root\n";
            return kUsageError;
        }
        try {
            std::error_code error;
            auto root = std::filesystem::absolute(arguments[1], error);
            if (error) {
                throw std::runtime_error(
                    "Cannot resolve the native root: " + error.message());
            }
            root = root.lexically_normal();
            const auto prepared = openrc::load_prepared_game_v2_root_v1(
                root, openrc::make_native_game_prepared_game_limits_v1());
            openrc::validate_current_native_game_publication_v1(prepared);
            std::cout
                << "OpenRC native game matches the current exact profile\n"
                << "Root:                   " << openrc::path_to_utf8(root)
                << '\n'
                << "Levels:                 "
                << prepared.manifest.levels.size() << '\n'
                << "Compiler profile:       "
                << prepared.manifest.provenance.compiler_version << '\n'
                << "Manifest SHA-256:       "
                << openrc::hex_digest(prepared.manifest_sha256) << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }
    if (command == "prepared-level-smoke") {
        if (arguments.size() != 3U) {
            std::cerr
                << "error: prepared-level-smoke expects an absolute prepared "
                   "root and a level ID\n";
            return kUsageError;
        }
        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto limits = make_cli_prepared_native_limits();
            const auto prepared =
                openrc::load_prepared_game_v2_root_v1(
                arguments[1], limits);
            const auto package = openrc::load_prepared_game_level_package_v1(
                prepared, level_id, limits);
            const auto resolved = openrc::resolve_level_package_v1(
                package, std::span<const openrc::LevelPackageV1>{},
                limits.level_package);
            const auto foundation =
                openrc::game::load_runtime_level_foundation_v1(
                    resolved,
                    make_cli_runtime_level_content_limits().foundation);
            const auto smoke = run_cli_player_smoke(foundation);

            std::cout << "OpenRC published-package player simulation smoke\n"
                      << "Prepared root:          "
                      << openrc::path_to_utf8(prepared.root) << '\n'
                      << "Manifest SHA-256:       "
                      << openrc::hex_digest(prepared.manifest_sha256) << '\n'
                      << "Level ID:               " << foundation.level_id
                      << '\n'
                      << "Build/content API:      " << foundation.build_id
                      << '/' << foundation.content_api_version << '\n'
                      << "Collision triangles:    "
                      << foundation.collision_world.mesh.triangles.size()
                      << '\n'
                      << "Spawn:                  "
                      << smoke.spawn.feet_position.x << ", "
                      << smoke.spawn.feet_position.y << ", "
                      << smoke.spawn.feet_position.z << '\n'
                      << "Final feet position:    "
                      << smoke.snapshot.character.feet_position.x << ", "
                << smoke.snapshot.character.feet_position.y << ", "
                << smoke.snapshot.character.feet_position.z << '\n'
                << "Grounded/resets:        "
                      << (smoke.snapshot.character.grounded ? "yes" : "no")
                << '/'
                << smoke.snapshot.reset_count << '\n'
                << "Replay state hash:      "
                << openrc::game::hash_player_simulation_snapshot_v1(
                       smoke.snapshot)
                << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "prepared-native-level-smoke") {
        if (arguments.size() != 3U) {
            std::cerr
                << "error: prepared-native-level-smoke expects an absolute "
                   "prepared root and a level ID\n";
            return kUsageError;
        }
        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto limits = make_cli_prepared_native_limits();
            const auto prepared =
                openrc::load_prepared_game_v2_root_v1(arguments[1], limits);
            const auto resolved =
                openrc::load_resolved_prepared_game_level_package_v1(
                    prepared, level_id,
                    std::span<
                        const openrc::ExplicitLevelPackageOverlayBytesV1>{},
                    limits);
            const auto content = openrc::game::load_runtime_level_content_v1(
                resolved, make_cli_runtime_level_content_limits());
            const auto smoke = run_cli_player_smoke(content.foundation);
            const auto entity_gameplay_smoke =
                run_cli_entity_gameplay_smoke(content);

            std::uint64_t render_vertex_count = 0U;
            std::uint64_t render_index_count = 0U;
            std::uint64_t render_draw_count = 0U;
            for (const auto &mesh : content.render_scene.meshes) {
                render_vertex_count += mesh.vertices.size();
                render_index_count += mesh.triangle_indices.size();
                render_draw_count += mesh.draw_ranges.size();
            }

            std::cout
                << "OpenRC published native-level content smoke\n"
                << "Prepared root:          "
                << openrc::path_to_utf8(prepared.root) << '\n'
                << "Manifest SHA-256:       "
                << openrc::hex_digest(prepared.manifest_sha256) << '\n'
                << "Level ID:               " << content.foundation.level_id
                << '\n'
                << "Build/content API:      " << content.foundation.build_id
                << '/' << content.foundation.content_api_version << '\n'
                << "Resolved resources:     " << resolved.resources.size()
                << '\n'
                << "Collision triangles:    "
                << content.foundation.collision_world.mesh.triangles.size()
                << '\n'
                << "Render meshes/instances: "
                << content.render_scene.meshes.size() << '/'
                << content.render_scene.instances.size() << '\n'
                << "Render vertices/triangles: " << render_vertex_count << '/'
                << (render_index_count / 3U) << '\n'
                << "Render draw ranges:       " << render_draw_count << '\n'
                << "Render textures/materials: "
                << content.render_scene.textures.size() << '/'
                << content.render_scene.materials.size() << '\n'
                << "Actor rigs/models:      "
                << (content.actor_library
                        ? content.actor_library->rigs.size()
                        : 0U)
                << '/'
                << (content.actor_library
                        ? content.actor_library->models.size()
                        : 0U)
                << '\n'
                << "Actor animation clips:  "
                << (content.actor_animation_bank
                        ? content.actor_animation_bank->clips.size()
                        : 0U)
                << '\n'
                << "Entities/render bindings: "
                << entity_gameplay_smoke.entity_definition_count << '/'
                << entity_gameplay_smoke.entity_render_binding_count << '\n'
                << "Gameplay collectibles:  "
                << entity_gameplay_smoke.collectible_count << '\n'
                << "Gameplay destructibles: "
                << entity_gameplay_smoke.destructible_count << '\n'
                << "Spawn:                  " << smoke.spawn.feet_position.x
                << ", " << smoke.spawn.feet_position.y << ", "
                << smoke.spawn.feet_position.z << '\n'
                << "Grounded/resets:        "
                << (smoke.snapshot.character.grounded ? "yes" : "no") << '/'
                << smoke.snapshot.reset_count << '\n'
                << "Replay state hash:      "
                << openrc::game::hash_player_simulation_snapshot_v1(
                       smoke.snapshot)
                << '\n';
            if (entity_gameplay_smoke.probe_event) {
                std::cout
                    << "Collectible probe:      ID "
                    << entity_gameplay_smoke.probe_event->authored_id << ", "
                    << entity_gameplay_smoke.probe_event->item_key << " +"
                    << entity_gameplay_smoke.probe_event->amount << ", total "
                    << entity_gameplay_smoke.probe_item_total << '\n';
            } else {
                std::cout << "Collectible probe:      none authored\n";
            }
            if (entity_gameplay_smoke.destructible_probe_event) {
                std::cout
                    << "Destructible probe:     ID "
                    << entity_gameplay_smoke.destructible_probe_event
                           ->authored_id
                    << ", destroyed, "
                    << entity_gameplay_smoke.destructible_drop_item_key
                    << " total "
                    << entity_gameplay_smoke.destructible_drop_item_total
                    << '\n';
            } else {
                std::cout << "Destructible probe:     none authored\n";
            }
            return 0;
        } catch (const std::exception &error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "level-package-smoke") {
        if (arguments.size() != 2U) {
            std::cerr << "error: level-package-smoke expects one .orlvl path\n";
            return kUsageError;
        }

        try {
            const auto package_limits =
                make_cli_level_foundation_package_limits();
            const auto package_bytes = read_bounded_binary_file(
                arguments[1], package_limits.max_input_bytes);
            const auto package =
                openrc::parse_level_package_v1(package_bytes, package_limits);
            const auto resolved = openrc::resolve_level_package_v1(
                package, std::span<const openrc::LevelPackageV1>{},
                package_limits);
            const auto foundation =
                openrc::game::load_runtime_level_foundation_v1(
                    resolved, openrc::game::RuntimeLevelFoundationLimitsV1{
                                  openrc::kOpenRcContentApiVersionV1,
                                  make_cli_collision_payload_limits(),
                                  make_cli_bootstrap_payload_limits()});
            const auto smoke = run_cli_player_smoke(foundation);

            std::cout << "OpenRC native package-only player simulation smoke\n"
                      << "Package:                "
                      << openrc::path_to_utf8(arguments[1]) << '\n'
                      << "Level ID:               " << foundation.level_id
                      << '\n'
                      << "Build/content API:      " << foundation.build_id
                      << '/' << foundation.content_api_version << '\n'
                      << "Package bytes:          " << package_bytes.size()
                      << '\n'
                      << "Collision vertices:     "
                      << foundation.collision_world.mesh.vertices.size() << '\n'
                      << "Collision triangles:    "
                      << foundation.collision_world.mesh.triangles.size()
                      << '\n'
                      << "Spawn:                  "
                      << smoke.spawn.feet_position.x << ", "
                      << smoke.spawn.feet_position.y << ", "
                      << smoke.spawn.feet_position.z << '\n'
                      << "Death height:           "
                      << foundation.bootstrap.death_height_world << '\n'
                      << "Final feet position:    "
                      << smoke.snapshot.character.feet_position.x << ", "
                      << smoke.snapshot.character.feet_position.y << ", "
                      << smoke.snapshot.character.feet_position.z << '\n'
                      << "Grounded:               "
                      << (smoke.snapshot.character.grounded ? "yes" : "no")
                      << '\n'
                      << "Landings/wall ticks:    " << smoke.landed_count << '/'
                      << smoke.wall_hit_count << '\n'
                      << "Contacts/resets:        " << smoke.collision_count
                      << '/' << smoke.snapshot.reset_count << '\n'
                      << "Replay state hash:      "
                      << openrc::game::hash_player_simulation_snapshot_v1(
                             smoke.snapshot)
                      << '\n';
            return 0;
        } catch (const std::exception &error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "level-native-package-smoke") {
        if (arguments.size() != 2U) {
            std::cerr << "error: level-native-package-smoke expects one .orlvl "
                         "path\n";
            return kUsageError;
        }

        try {
            const auto package_limits = make_cli_native_level_package_limits();
            const auto runtime_limits =
                make_cli_runtime_level_content_limits();
            const auto package_bytes = read_bounded_binary_file(
                arguments[1], package_limits.max_input_bytes);
            const auto package =
                openrc::parse_level_package_v1(package_bytes, package_limits);
            const auto resolved = openrc::resolve_level_package_v1(
                package, std::span<const openrc::LevelPackageV1>{},
                package_limits);
            const auto foundation =
                openrc::game::load_runtime_level_foundation_v1(
                    resolved, runtime_limits.foundation);
            const auto render_scene =
                openrc::game::load_runtime_render_scene_v1(
                    resolved, openrc::kOpenRcContentApiVersionV1,
                    runtime_limits.render_scene);
            const auto smoke = run_cli_player_smoke(foundation);

            std::uint64_t vertex_count = 0U;
            std::uint64_t index_count = 0U;
            for (const auto &mesh : render_scene.meshes) {
                vertex_count += mesh.vertices.size();
                index_count += mesh.triangle_indices.size();
            }
            std::uint64_t texture_bytes = 0U;
            for (const auto &texture : render_scene.textures) {
                for (const auto &mip : texture.mips) {
                    texture_bytes += mip.rgba8.size();
                }
            }

            std::cout
                << "OpenRC native renderable package smoke\n"
                << "Package:                "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:               " << foundation.level_id << '\n'
                << "Build/content API:      " << foundation.build_id << '/'
                << foundation.content_api_version << '\n'
                << "Package bytes:          " << package_bytes.size() << '\n'
                << "Collision triangles:    "
                << foundation.collision_world.mesh.triangles.size() << '\n'
                << "Meshes/instances:       " << render_scene.meshes.size()
                << '/' << render_scene.instances.size() << '\n'
                << "Vertices/triangles:     " << vertex_count << '/'
                << (index_count / 3U) << '\n'
                << "Textures/materials:     " << render_scene.textures.size()
                << '/' << render_scene.materials.size() << '\n'
                << "Texture RGBA8 bytes:    " << texture_bytes << '\n'
                << "Spawn:                  " << smoke.spawn.feet_position.x
                << ", " << smoke.spawn.feet_position.y << ", "
                << smoke.spawn.feet_position.z << '\n'
                << "Final feet position:    "
                << smoke.snapshot.character.feet_position.x << ", "
                << smoke.snapshot.character.feet_position.y << ", "
                << smoke.snapshot.character.feet_position.z << '\n'
                << "Grounded/resets:        "
                << (smoke.snapshot.character.grounded ? "yes" : "no") << '/'
                << smoke.snapshot.reset_count << '\n'
                << "Replay state hash:      "
                << openrc::game::hash_player_simulation_snapshot_v1(
                       smoke.snapshot)
                << '\n';
            return 0;
        } catch (const std::exception &error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "level-player-smoke") {
        if (arguments.size() != 3U) {
            std::cerr << "error: level-player-smoke expects an ISO path and a "
                         "level ID\n";
            return kUsageError;
        }
        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto assets = openrc::load_rac_level_moby_assets_v1(
                arguments[1], level_id, make_cli_moby_asset_limits());
            const auto package = compile_cli_level_foundation(assets);
            const auto package_limits =
                make_cli_level_foundation_package_limits();
            const auto package_bytes =
                openrc::encode_level_package_v1(package, package_limits);
            const auto resolved = openrc::resolve_level_package_v1(
                package, std::span<const openrc::LevelPackageV1>{},
                package_limits);
            const auto foundation =
                openrc::game::load_runtime_level_foundation_v1(
                    resolved, openrc::game::RuntimeLevelFoundationLimitsV1{
                                  openrc::kOpenRcContentApiVersionV1,
                                  make_cli_collision_payload_limits(),
                                  make_cli_bootstrap_payload_limits()});
            const auto &collision = foundation.collision_world;
            const auto &bootstrap = foundation.bootstrap;
            const auto smoke = run_cli_player_smoke(foundation);
            std::cout
                << "OpenRC native player simulation smoke\n"
                << "Image:                  "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:               " << level_id << '\n'
                << "Native package bytes:   " << package_bytes.size() << '\n'
                << "Collision vertices:     " << collision.mesh.vertices.size()
                << '\n'
                << "Collision triangles:    " << collision.mesh.triangles.size()
                << '\n'
                << "Collision grid cells:   " << collision.grid.cells.size()
                << '\n'
                << "Spawn:                  " << smoke.spawn.feet_position.x
                << ", " << smoke.spawn.feet_position.y << ", "
                << smoke.spawn.feet_position.z << '\n'
                << "Death height:           " << bootstrap.death_height_world
                << '\n'
                << "Final feet position:    "
                << smoke.snapshot.character.feet_position.x << ", "
                << smoke.snapshot.character.feet_position.y << ", "
                << smoke.snapshot.character.feet_position.z << '\n'
                << "Grounded:               "
                << (smoke.snapshot.character.grounded ? "yes" : "no") << '\n'
                << "Landings/wall ticks:    " << smoke.landed_count << '/'
                << smoke.wall_hit_count << '\n'
                << "Contacts/resets:        " << smoke.collision_count << '/'
                << smoke.snapshot.reset_count << '\n'
                << "Replay state hash:      "
                << openrc::game::hash_player_simulation_snapshot_v1(
                       smoke.snapshot)
                << '\n';
            return 0;
        } catch (const std::exception &error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "level-moby-scene") {
        if (arguments.size() != 3U) {
            std::cerr
                << "error: level-moby-scene expects an ISO path and a "
                         "level ID\n";
            return kUsageError;
        }
        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto assets = openrc::load_rac_level_moby_assets_v1(
                arguments[1],
                level_id,
                make_cli_moby_asset_limits());
            const auto scene = openrc::runtime::build_moby_scene_geometry_v1(
                assets.models,
                assets.gameplay.static_mobies,
                openrc::runtime::MobySceneCoordinateDomainV1::world_units,
                openrc::runtime::MobySceneGeometryLimitsV1{
                    4096U,
                    65'536U,
                    1'000'000U,
                    3'000'000U,
                    1'000'000U,
                    3'000'000U});
            const auto tie_scene =
                openrc::runtime::build_tie_scene_geometry_v1(
                    assets.tie_models,
                    assets.gameplay.tie_instances,
                    openrc::runtime::TieSceneCoordinateDomainV1::world_units,
                    openrc::runtime::TieSceneGeometryLimitsV1{
                        4096U,
                        65'536U,
                        1'000'000U,
                        3'000'000U,
                        4'000'000U,
                        12'000'000U});
            std::cout
                << "OpenRC static RAC1 environment scene report\n"
                << "Image:                  "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:               " << level_id << '\n'
                << "Models local/shared:    "
                << assets.local_model_count << '/'
                << assets.shared_model_count << '\n'
                << "External/zero models:   "
                << assets.external_model_count << '\n'
                << "Moby textures/pixels:   "
                << assets.textures.textures.size() << '/'
                << assets.textures.total_pixel_count << '\n'
                << "TIE classes/instances/textures: "
                << assets.gameplay.tie_class_count << '/'
                << assets.gameplay.tie_instance_count << '/'
                << assets.tie_textures.textures.size() << '\n'
                << "TIE packets/vertices/triangles: "
                << assets.total_tie_packet_count << '/'
                << assets.total_tie_vertex_count << '/'
                << assets.total_tie_triangle_count << '\n'
                << "Rendered TIE classes/instances: "
                << tie_scene.stats.rendered_model_count << '/'
                << tie_scene.stats.rendered_placement_count << '\n'
                << "TIE material batches: "
                << tie_scene.material_batches.size() << '\n'
                << "Shrub classes/instances/textures: "
                << assets.gameplay.shrub_class_count << '/'
                << assets.gameplay.shrub_instance_count << '/'
                << assets.shrub_textures.textures.size() << '\n'
                << "Model packets/vertices/triangles: "
                << assets.total_model_packet_count << '/'
                << assets.total_model_vertex_count << '/'
                << assets.total_model_triangle_count << '\n'
                << "Static placements:      "
                << scene.stats.placement_count << '\n'
                << "Rendered classes:       "
                << scene.stats.rendered_model_count << '\n'
                << "Rendered placements:    "
                << scene.stats.rendered_placement_count << '\n'
                << "Material batches:       "
                << scene.material_batches.size() << '\n'
                << "Animated placements:    "
                << scene.stats.animated_model_placement_count
                << " (skipped; bind transforms pending)\n"
                << "Missing model placements: "
                << scene.stats.missing_model_placement_count << '\n'
                << "Empty model placements: "
                << scene.stats.empty_model_placement_count << '\n';
            if (scene.geometry) {
                std::cout
                    << "Output vertices:        "
                    << scene.geometry->vertices.size() << '\n'
                    << "Output triangles:       "
                    << scene.geometry->emitted_triangle_count << '\n'
                    << "Bounds X:               ["
                    << scene.geometry->minimum_x << ", "
                    << scene.geometry->maximum_x << "]\n"
                    << "Bounds Y:               ["
                    << scene.geometry->minimum_y << ", "
                    << scene.geometry->maximum_y << "]\n"
                    << "Bounds Z:               ["
                    << scene.geometry->minimum_z << ", "
                    << scene.geometry->maximum_z << "]\n";
            } else {
                std::cout << "Output geometry:        empty\n";
            }
            if (tie_scene.geometry) {
                std::cout
                    << "TIE output vertices:    "
                    << tie_scene.geometry->vertices.size() << '\n'
                    << "TIE output triangles:   "
                    << tie_scene.geometry->emitted_triangle_count << '\n'
                    << "TIE bounds X:           ["
                    << tie_scene.geometry->minimum_x << ", "
                    << tie_scene.geometry->maximum_x << "]\n"
                    << "TIE bounds Y:           ["
                    << tie_scene.geometry->minimum_y << ", "
                    << tie_scene.geometry->maximum_y << "]\n"
                    << "TIE bounds Z:           ["
                    << tie_scene.geometry->minimum_z << ", "
                    << tie_scene.geometry->maximum_z << "]\n";
            } else {
                std::cout << "TIE output geometry:    empty\n";
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "level-core") {
        if (arguments.size() != 3U) {
            std::cerr
                << "error: level-core expects an ISO path and a level ID\n";
            return kUsageError;
        }

        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto assets = openrc::inspect_disc_toc_assets(arguments[1]);
            const auto level_assets = std::find_if(
                assets.levels.begin(),
                assets.levels.end(),
                [level_id](const openrc::DiscTocLevelAssets& candidate) {
                    return candidate.level_id == level_id;
                });
            const auto level_layout = std::find_if(
                assets.layout.levels.begin(),
                assets.layout.levels.end(),
                [level_id](const openrc::DiscTocLevelDescriptor& candidate) {
                    return candidate.level_id == level_id;
                });
            if (level_assets == assets.levels.end() ||
                level_layout == assets.layout.levels.end()) {
                throw std::runtime_error(
                    "The requested level is absent from DiscTocV1");
            }

            constexpr std::size_t kLevelCoreIndexSubrange = 2U;
            constexpr std::size_t kLevelCoreAssetSubrange = 10U;
            const auto& index_subrange = level_assets->primary_extent0
                                             .subranges[kLevelCoreIndexSubrange];
            const auto& asset_subrange = level_assets->primary_extent0
                                             .subranges[kLevelCoreAssetSubrange];
            if (index_subrange.byte_size == 0U ||
                asset_subrange.byte_size == 0U ||
                asset_subrange.signature != openrc::DiscTocSignature::wad) {
                throw std::runtime_error(
                    "The level-core index or asset WadV1 is absent");
            }

            const auto& primary_extent = level_layout->primary_extents.front();
            const auto primary_bytes = read_disc_extent(
                arguments[1],
                primary_extent.lba,
                primary_extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto bounded_subrange =
                [&primary_bytes](const openrc::DiscTocSubrange& subrange,
                                 const char* const description) {
                    const auto offset =
                        static_cast<std::uint64_t>(subrange.relative_offset);
                    const auto size =
                        static_cast<std::uint64_t>(subrange.byte_size);
                    if (offset > primary_bytes.size() ||
                        size > primary_bytes.size() - offset) {
                        throw std::runtime_error(
                            std::string(description) +
                            " lies outside primary extent 0");
                    }
                    return std::span<const std::byte>(primary_bytes).subspan(
                        static_cast<std::size_t>(offset),
                        static_cast<std::size_t>(size));
                };
            const auto index_bytes = bounded_subrange(
                index_subrange, "The level-core index");
            const auto encoded_assets = bounded_subrange(
                asset_subrange, "The level-core asset WadV1");
            const auto decoded_assets = openrc::decode_wad_bytes(
                encoded_assets, kMaximumCliDecodedWadBytes);
            const auto core = openrc::parse_rac_level_core_index_v1(
                index_bytes,
                encoded_assets,
                decoded_assets.bytes,
                openrc::RacLevelCoreLimitsV1{
                    kMaximumCliDecodedWadBytes,
                    kMaximumCliDecodedWadBytes,
                    kMaximumCliDecodedWadBytes,
                    4096U,
                    255U,
                    4096U,
                    4096U,
                    4096U,
                    255U,
                    255U});

            const auto& gameplay_ref = level_assets->primary_wads.front();
            if (gameplay_ref.signature != openrc::DiscTocSignature::wad ||
                gameplay_ref.occupied_sectors == 0U) {
                throw std::runtime_error(
                    "The level's primary gameplay WadV1 is absent");
            }
            const auto decoded_gameplay = openrc::decode_wad(
                arguments[1],
                gameplay_ref.lba,
                gameplay_ref.occupied_sectors,
                kMaximumCliDecodedWadBytes);
            const auto gameplay = openrc::parse_rac_gameplay_bank_v1(
                decoded_gameplay.bytes,
                openrc::RacGameplayBankLimitsV1{
                    kMaximumCliDecodedWadBytes});
            const auto bootstrap =
                openrc::compile_rac_level_bootstrap_v1(gameplay, level_id);
            const auto* const default_spawn =
                openrc::find_level_spawn_point_v1(
                    bootstrap, bootstrap.default_spawn_id);
            if (default_spawn == nullptr) {
                throw std::runtime_error(
                    "The compiled level bootstrap has no default spawn");
            }
            if (gameplay.moby_class_ids.size() != core.moby_classes.size()) {
                throw std::runtime_error(
                    "Gameplay and level-core Moby class counts disagree");
            }
            for (std::size_t index = 0U;
                 index < core.moby_classes.size();
                 ++index) {
                if (gameplay.moby_class_ids[index] !=
                    static_cast<std::uint32_t>(
                        core.moby_classes[index].class_id)) {
                    throw std::runtime_error(
                        "Gameplay and level-core Moby class order disagrees");
                }
            }

            std::vector<std::uint32_t> instance_counts(
                core.moby_classes.size(), 0U);
            for (const auto& instance : gameplay.static_mobies) {
                const auto found = std::find_if(
                    core.moby_classes.begin(),
                    core.moby_classes.end(),
                    [&instance](
                        const openrc::RacLevelCoreMobyClassEntryV1& entry) {
                        return static_cast<std::uint32_t>(entry.class_id) ==
                            instance.class_id;
                    });
                if (found == core.moby_classes.end()) {
                    throw std::runtime_error(
                        "A gameplay Moby instance is absent from level core");
                }
                const auto index = static_cast<std::size_t>(
                    std::distance(core.moby_classes.begin(), found));
                ++instance_counts[index];
            }

            struct ParsedMobySummaryV1 {
                std::int32_t class_id = 0;
                std::uint8_t high_packets = 0U;
                std::uint8_t low_packets = 0U;
                std::uint8_t metal_packets = 0U;
                std::uint8_t joints = 0U;
                std::uint8_t sequences = 0U;
                std::uint64_t decoded_regular_packets = 0U;
                std::uint64_t diagnostic_source_vertices = 0U;
                std::uint64_t transfer_vertices = 0U;
                std::uint64_t diagnostic_triangles = 0U;
                std::uint64_t resolved_inherited_duplicates = 0U;
            };
            std::vector<ParsedMobySummaryV1> local_models;
            std::vector<ParsedMobySummaryV1> gadget_models;
            const auto assemble_model_lod =
                [](const std::span<const std::byte> model_bytes,
                   const openrc::RacMobyClassV1& model,
                   const openrc::RacMobyLodV1 lod,
                   const std::string& description) {
                    try {
                        return openrc::assemble_rac_moby_model_geometry_v1(
                            model_bytes,
                            model,
                            lod,
                            kCliMobyModelGeometryLimits);
                    } catch (
                        const openrc::RacMobyModelGeometryError& error) {
                        const auto lod_name =
                            lod == openrc::RacMobyLodV1::high ? "high" : "low";
                        throw std::runtime_error(
                            description + " " + lod_name +
                            " LOD failed geometry assembly: " + error.what());
                    }
                };
            const auto add_geometry_summary =
                [](ParsedMobySummaryV1& summary,
                   const openrc::RacMobyModelGeometryV1& geometry) {
                    summary.decoded_regular_packets += geometry.packets.size();
                    summary.transfer_vertices += geometry.vertices.size();
                    summary.diagnostic_triangles += geometry.triangles.size();
                    summary.resolved_inherited_duplicates +=
                        geometry.inherited_duplicate_count;
                    for (const auto& packet : geometry.packets) {
                        summary.diagnostic_source_vertices +=
                            packet.local_vertex_count;
                    }
                };
            std::uint64_t local_model_bytes = 0U;
            for (const auto& entry : core.moby_classes) {
                if (entry.asset_range.size == 0U) {
                    continue;
                }
                const auto model_bytes =
                    std::span<const std::byte>(decoded_assets.bytes).subspan(
                        static_cast<std::size_t>(entry.asset_range.offset),
                        static_cast<std::size_t>(entry.asset_range.size));
                openrc::RacMobyClassV1 model;
                try {
                    model = openrc::parse_rac_moby_class_v1(
                        model_bytes,
                        openrc::RacMobyClassLimitsV1{
                            kMaximumCliDecodedWadBytes});
                } catch (const openrc::RacMobyClassError& error) {
                    throw std::runtime_error(
                        "Local Moby class " + std::to_string(entry.class_id) +
                        " failed validation: " + error.what());
                }
                local_model_bytes += entry.asset_range.size;
                ParsedMobySummaryV1 summary{
                    entry.class_id,
                    model.high_lod_packet_count,
                    model.low_lod_packet_count,
                    model.metal_packet_count,
                    model.joint_count,
                    model.sequence_count};
                const auto description =
                    "Local Moby class " + std::to_string(entry.class_id);
                add_geometry_summary(
                    summary,
                    assemble_model_lod(
                        model_bytes,
                        model,
                        openrc::RacMobyLodV1::high,
                        description));
                add_geometry_summary(
                    summary,
                    assemble_model_lod(
                        model_bytes,
                        model,
                        openrc::RacMobyLodV1::low,
                        description));
                local_models.push_back(summary);
            }
            std::uint64_t gadget_decoded_bytes = 0U;
            for (const auto& entry : core.gadgets) {
                const auto wad_bytes =
                    std::span<const std::byte>(decoded_assets.bytes).subspan(
                        static_cast<std::size_t>(entry.encoded_range.offset),
                        static_cast<std::size_t>(entry.encoded_range.size));
                if (gadget_decoded_bytes >= kMaximumCliDecodedWadBytes) {
                    throw std::runtime_error("Shared gadget models exceed the "
                                             "aggregate decode limit");
                }
                const auto remaining =
                    kMaximumCliDecodedWadBytes - gadget_decoded_bytes;
                const auto decoded = openrc::decode_wad_bytes(
                    wad_bytes, remaining);
                gadget_decoded_bytes += decoded.bytes.size();
                const auto model = openrc::parse_rac_moby_class_v1(
                    decoded.bytes,
                    openrc::RacMobyClassLimitsV1{
                        kMaximumCliDecodedWadBytes, true});
                ParsedMobySummaryV1 summary{
                    entry.class_id,
                    model.high_lod_packet_count,
                    model.low_lod_packet_count,
                    model.metal_packet_count,
                    model.joint_count,
                    model.sequence_count};
                const auto description =
                    "Shared gadget Moby class " +
                    std::to_string(entry.class_id);
                add_geometry_summary(
                    summary,
                    assemble_model_lod(
                        decoded.bytes,
                        model,
                        openrc::RacMobyLodV1::high,
                        description));
                add_geometry_summary(
                    summary,
                    assemble_model_lod(
                        decoded.bytes,
                        model,
                        openrc::RacMobyLodV1::low,
                        description));
                gadget_models.push_back(summary);
            }

            if (local_models.size() > core.moby_classes.size() ||
                gadget_models.size() >
                    core.moby_classes.size() - local_models.size()) {
                throw std::runtime_error(
                    "Parsed Moby model ownership exceeds the class table");
            }
            const auto external_models = core.moby_classes.size() -
                local_models.size() - gadget_models.size();
            const auto used_classes = static_cast<std::size_t>(std::count_if(
                instance_counts.begin(),
                instance_counts.end(),
                [](const std::uint32_t count) { return count != 0U; }));
            const auto present_ratchet_sequence_slots =
                static_cast<std::size_t>(std::count_if(
                    core.ratchet_sequence_offsets.begin(),
                    core.ratchet_sequence_offsets.end(),
                    [](const std::uint32_t offset) { return offset != 0U; }));
            std::uint64_t ratchet_sequence_bytes = 0U;
            for (const auto& sequence : core.ratchet_sequences) {
                if (sequence.asset_range.size >
                    std::numeric_limits<std::uint64_t>::max() -
                        ratchet_sequence_bytes) {
                    throw std::runtime_error(
                        "Ratchet sequence byte total overflows uint64_t");
                }
                ratchet_sequence_bytes += sequence.asset_range.size;
            }
            std::vector<openrc::RacRatchetSequenceV1>
                parsed_ratchet_sequences;
            parsed_ratchet_sequences.reserve(core.ratchet_sequences.size());
            std::uint64_t ratchet_animation_frames = 0U;
            std::uint64_t ratchet_animation_triggers = 0U;
            for (const auto& sequence : core.ratchet_sequences) {
                try {
                    parsed_ratchet_sequences.push_back(
                        openrc::parse_rac_ratchet_sequence_v1(
                            decoded_assets.bytes,
                            openrc::RacRatchetSequenceRangeV1{
                                sequence.asset_range.offset,
                                sequence.asset_range.size},
                            openrc::RacRatchetSequenceLimitsV1{
                                kMaximumCliDecodedWadBytes,
                                kMaximumCliDecodedWadBytes,
                                255U,
                                255U}));
                } catch (const openrc::RacRatchetSequenceError& error) {
                    throw std::runtime_error(
                        "Ratchet sequence at " +
                        hexadecimal(sequence.asset_offset, 8) +
                        " failed structural validation: " + error.what());
                }
                const auto& parsed = parsed_ratchet_sequences.back();
                ratchet_animation_frames += parsed.frames.size();
                ratchet_animation_triggers += parsed.trigger_words.size();
            }
            std::uint64_t decoded_regular_packets = 0U;
            std::uint64_t diagnostic_source_vertices = 0U;
            std::uint64_t transfer_vertices = 0U;
            std::uint64_t diagnostic_triangles = 0U;
            std::uint64_t resolved_inherited_duplicates = 0U;
            const auto add_totals =
                [&](const std::vector<ParsedMobySummaryV1>& models) {
                    for (const auto& model : models) {
                        decoded_regular_packets +=
                            model.decoded_regular_packets;
                        diagnostic_source_vertices +=
                            model.diagnostic_source_vertices;
                        transfer_vertices += model.transfer_vertices;
                        diagnostic_triangles += model.diagnostic_triangles;
                        resolved_inherited_duplicates +=
                            model.resolved_inherited_duplicates;
                    }
                };
            add_totals(local_models);
            add_totals(gadget_models);
            std::cout
                << "OpenRC RacLevelCoreIndexV1 report\n"
                << "Image:                 "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:              " << level_id << '\n'
                << "Index bytes:           " << core.index_input_bytes << '\n'
                << "Encoded asset bytes:   "
                << core.encoded_asset_input_bytes << '\n'
                << "Decoded asset bytes:   "
                << core.decoded_asset_input_bytes << '\n'
                << "Moby classes:          " << core.moby_classes.size()
                << '\n'
                << "Local model cores:     " << local_models.size()
                << " (" << local_model_bytes << " bounded bytes)\n"
                << "Shared gadget models:  " << gadget_models.size()
                << " (" << gadget_decoded_bytes << " decoded bytes)\n"
                << "External/zero models:  " << external_models << '\n'
                << "Moby textures:         "
                << core.header.moby_textures.count << '\n'
                << "Death height:          "
                << gameplay.level_settings.death_height << '\n'
                << "Ship position:         "
                << gameplay.level_settings.ship_position[0U] << ", "
                << gameplay.level_settings.ship_position[1U] << ", "
                << gameplay.level_settings.ship_position[2U] << '\n'
                << "Ship rotation Z:       "
                << gameplay.level_settings.ship_rotation_z << '\n'
                << "Player spawn:          "
                << default_spawn->feet_position.x << ", "
                << default_spawn->feet_position.y << ", "
                << default_spawn->feet_position.z << " @ "
                << default_spawn->facing_yaw_radians << " rad\n"
                << "Static placements:     " << gameplay.static_mobies.size()
                << '\n'
                << "Classes placed:        " << used_classes << '\n'
                << "Gameplay class link:   exact count and order\n\n"
                << "Ratchet sequence slots: "
                << present_ratchet_sequence_slots << '/'
                << openrc::kRacLevelCoreRatchetSequenceCountV1 << '\n'
                << "Unique sequence assets: " << core.ratchet_sequences.size()
                << " (" << ratchet_sequence_bytes << " bounded bytes)\n"
                << "Validated sequences:   "
                << parsed_ratchet_sequences.size() << " regular V1\n"
                << "Animation frames:      " << ratchet_animation_frames
                << '\n'
                << "Animation triggers:    " << ratchet_animation_triggers
                << "\n\n"
                << "Regular mesh packets:  " << decoded_regular_packets
                << '\n'
                << "Source vertices:       " << diagnostic_source_vertices
                << '\n'
                << "Transfer vertices:     " << transfer_vertices << '\n'
                << "Decoded triangles:     " << diagnostic_triangles << '\n'
                << "Inherited duplicates:  " << resolved_inherited_duplicates
                << " (resolved)\n\n"
                << "Local model cores:\n";
            for (const auto& model : local_models) {
                const auto class_entry = std::find_if(
                    core.moby_classes.begin(),
                    core.moby_classes.end(),
                    [&model](
                        const openrc::RacLevelCoreMobyClassEntryV1& entry) {
                        return entry.class_id == model.class_id;
                    });
                const auto class_index = static_cast<std::size_t>(
                    std::distance(core.moby_classes.begin(), class_entry));
                std::cout
                    << "  class " << std::setw(4) << model.class_id
                    << ": packets "
                    << static_cast<std::uint32_t>(model.high_packets) << '/'
                    << static_cast<std::uint32_t>(model.low_packets) << '/'
                    << static_cast<std::uint32_t>(model.metal_packets)
                    << ", joints "
                    << static_cast<std::uint32_t>(model.joints)
                    << ", sequences "
                    << static_cast<std::uint32_t>(model.sequences)
                    << ", textures "
                    << static_cast<std::uint32_t>(
                           class_entry->used_texture_slot_count)
                    << ", triangles " << model.diagnostic_triangles
                    << ", placements " << instance_counts[class_index]
                    << '\n';
            }
            std::cout << "\nRatchet sequence slots:\n";
            for (std::size_t slot = 0U;
                 slot < core.ratchet_sequence_offsets.size(); ++slot) {
                const auto offset = core.ratchet_sequence_offsets[slot];
                if (offset == 0U) {
                    continue;
                }
                const auto sequence = std::lower_bound(
                    core.ratchet_sequences.begin(),
                    core.ratchet_sequences.end(),
                    offset,
                    [](const openrc::RacLevelCoreRatchetSequenceV1& candidate,
                       const std::uint32_t value) {
                        return candidate.asset_offset < value;
                    });
                if (sequence == core.ratchet_sequences.end() ||
                    sequence->asset_offset != offset) {
                    throw std::runtime_error(
                        "A Ratchet sequence slot lacks its bounded asset");
                }
                const auto parsed_index = static_cast<std::size_t>(
                    std::distance(core.ratchet_sequences.begin(), sequence));
                if (parsed_index >= parsed_ratchet_sequences.size()) {
                    throw std::runtime_error(
                        "A Ratchet sequence slot lacks parsed metadata");
                }
                const auto& parsed = parsed_ratchet_sequences[parsed_index];
                std::cout
                    << "  [" << std::setw(3) << slot << "] "
                    << hexadecimal(sequence->asset_range.offset, 8) << "+"
                    << hexadecimal(sequence->asset_range.size, 8)
                    << ", regular V1, frames " << parsed.frames.size()
                    << ", triggers " << parsed.trigger_words.size() << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "companion-wads") {
        if (arguments.size() != 3) {
            std::cerr
                << "error: companion-wads expects an ISO path and a level ID\n";
            return kUsageError;
        }

        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto assets = openrc::inspect_disc_toc_assets(arguments[1]);
            const auto level_assets = std::find_if(
                assets.levels.begin(),
                assets.levels.end(),
                [level_id](const openrc::DiscTocLevelAssets& candidate) {
                    return candidate.level_id == level_id;
                });
            const auto level_layout = std::find_if(
                assets.layout.levels.begin(),
                assets.layout.levels.end(),
                [level_id](const openrc::DiscTocLevelDescriptor& candidate) {
                    return candidate.level_id == level_id;
                });
            if (level_assets == assets.levels.end() ||
                level_layout == assets.layout.levels.end()) {
                throw std::runtime_error("The requested level is absent from DiscTocV1");
            }

            constexpr std::size_t kCompanionIndexSubrange = 2U;
            constexpr std::size_t kCompanionTargetSubrange = 10U;
            const auto& index_subrange =
                level_assets->primary_extent0.subranges[kCompanionIndexSubrange];
            const auto& target_subrange =
                level_assets->primary_extent0.subranges[kCompanionTargetSubrange];
            if (index_subrange.byte_size == 0U) {
                throw std::runtime_error(
                    "The level's companion index subrange is empty");
            }
            if (target_subrange.byte_size == 0U ||
                target_subrange.signature != openrc::DiscTocSignature::wad) {
                throw std::runtime_error(
                    "The level's companion target subrange is not WadV1");
            }

            const auto& primary_extent =
                level_layout->primary_extents.front();
            const auto primary_bytes = read_disc_extent(
                arguments[1],
                primary_extent.lba,
                primary_extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto bounded_subrange =
                [&primary_bytes](
                    const openrc::DiscTocSubrange& subrange,
                    const char* description) {
                    const auto offset =
                        static_cast<std::uint64_t>(subrange.relative_offset);
                    const auto size =
                        static_cast<std::uint64_t>(subrange.byte_size);
                    if (offset > primary_bytes.size() ||
                        size > primary_bytes.size() - offset) {
                        throw std::runtime_error(
                            std::string(description) +
                            " lies outside primary extent 0");
                    }
                    return std::span<const std::byte>(primary_bytes).subspan(
                        static_cast<std::size_t>(offset),
                        static_cast<std::size_t>(size));
                };

            const auto index_bytes = bounded_subrange(
                index_subrange,
                "The companion index subrange");
            const auto target_wad = bounded_subrange(
                target_subrange,
                "The companion target WadV1 subrange");
            const auto decoded = openrc::decode_wad_bytes(
                target_wad,
                kMaximumCliDecodedWadBytes);
            const auto index = openrc::parse_companion_terminal_wad_index_v1(
                index_bytes,
                decoded.bytes,
                openrc::CompanionTerminalWadIndexLimits{
                    kMaximumCliDecodedWadBytes,
                    kMaximumCliDecodedWadBytes,
                    1'000'000U,
                    kMaximumCliDecodedWadBytes});

            struct CompanionMobySummaryV1 {
                std::uint64_t decoded_bytes = 0U;
                openrc::RacMobyClassV1 moby;
            };
            std::uint64_t nested_decoded_bytes = 0;
            std::vector<CompanionMobySummaryV1> companion_mobies;
            companion_mobies.reserve(index.records.size());
            for (const auto& record : index.records) {
                const auto remaining_decoded_bytes =
                    kMaximumCliDecodedWadBytes - nested_decoded_bytes;
                const auto nested = openrc::decode_wad_bytes(
                    record.wad_bytes,
                    remaining_decoded_bytes);
                if (nested.bytes.size() >
                    remaining_decoded_bytes) {
                    throw std::runtime_error("The companion WADs exceed the "
                                             "aggregate decoded-byte limit");
                }
                nested_decoded_bytes += nested.bytes.size();
                companion_mobies.push_back(CompanionMobySummaryV1{
                    nested.bytes.size(),
                    openrc::parse_rac_moby_class_v1(
                        nested.bytes,
                        openrc::RacMobyClassLimitsV1{
                            kMaximumCliDecodedWadBytes, true})});
            }

            std::cout
                << "OpenRC CompanionTerminalWadIndexV1 report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:            " << level_id << '\n'
                << "Primary extent LBA:  " << primary_extent.lba << '\n'
                << "Index offset:        "
                << hexadecimal(index_subrange.relative_offset, 8) << '\n'
                << "Index bytes:         " << index.index_input_bytes << '\n'
                << "Table offset:        "
                << hexadecimal(index.table_offset, 8) << '\n'
                << "Opaque header word:  "
                << hexadecimal(index.opaque_header_word, 8) << '\n'
                << "Target WAD offset:   "
                << hexadecimal(target_subrange.relative_offset, 8) << '\n'
                << "Target WAD bytes:    " << target_subrange.byte_size << '\n'
                << "Decoded target:      " << index.target_input_bytes << " bytes\n"
                << "Decoded SHA-256:     " << decoded.sha256 << '\n'
                << "Indexed WADs:        " << index.record_count << '\n'
                << "Pre-table bytes:     " << index.pre_table_bytes.size() << '\n'
                << "Opaque target prefix:"
                << ' ' << index.opaque_target_prefix_range.size << " bytes\n"
                << "Terminal chain:      offset "
                << hexadecimal(index.terminal_chain_range.offset, 8)
                << ", size " << index.terminal_chain_range.size << " bytes\n"
                << "Logical WAD bytes:   " << index.total_logical_bytes << '\n'
                << "Zero padding bytes:  " << index.total_padding_bytes << '\n'
                << "Nested decoded:      " << nested_decoded_bytes << " bytes\n";

            if (!index.records.empty()) {
                const auto& first = index.records.front();
                const auto& last = index.records.back();
                std::cout
                    << "First WAD:           offset "
                    << hexadecimal(first.target_offset, 8)
                    << ", logical " << first.logical_size << " bytes\n"
                    << "Last WAD:            offset "
                    << hexadecimal(last.target_offset, 8)
                    << ", logical " << last.logical_size << " bytes\n";
            }
            std::cout << "\nRAC1 shared gadget classes:\n";
            for (std::size_t record_index = 0U;
                 record_index < index.records.size();
                 ++record_index) {
                const auto& record = index.records[record_index];
                const auto& summary = companion_mobies[record_index];
                std::cout
                    << "  [" << std::setw(2) << record_index << "] class "
                    << std::setw(4) << record.opaque_word << " ("
                    << hexadecimal(record.opaque_word, 4) << "), decoded "
                    << std::setw(5) << summary.decoded_bytes << ", packets "
                    << static_cast<std::uint32_t>(
                           summary.moby.high_lod_packet_count)
                    << '/'
                    << static_cast<std::uint32_t>(
                           summary.moby.low_lod_packet_count)
                    << '/'
                    << static_cast<std::uint32_t>(
                           summary.moby.metal_packet_count)
                    << ", joints "
                    << static_cast<std::uint32_t>(summary.moby.joint_count)
                    << ", sequences "
                    << static_cast<std::uint32_t>(summary.moby.sequence_count)
                    << ", sounds "
                    << static_cast<std::uint32_t>(summary.moby.sound_count)
                    << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "map-art") {
        if (arguments.size() < 3 || arguments.size() > 4) {
            std::cerr
                << "error: map-art expects an ISO path, level ID, "
                   "and optional TGA output path\n";
            return kUsageError;
        }

        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::size_t>(*level_value);
        const auto requested_slot = kMapArtFirstGlobalSlot + level_id;

        try {
            const auto toc = openrc::inspect_disc_toc(arguments[1]);
            const auto entry = std::find_if(
                toc.global_extents.begin(),
                toc.global_extents.end(),
                [requested_slot](const openrc::DiscTocGlobalExtent& candidate) {
                    return candidate.slot == requested_slot;
                });
            if (entry == toc.global_extents.end() ||
                entry->signature != openrc::DiscTocSignature::wad) {
                throw std::runtime_error(
                    "The MapArtV1 global TOC slot is absent or is not WadV1");
            }

            const auto decoded = openrc::decode_wad(
                arguments[1],
                entry->extent.lba,
                entry->extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto map_art = openrc::parse_map_art_v1(
                decoded.bytes,
                openrc::MapArtLimits{
                    openrc::BoundaryTableLimits{
                        kMaximumCliDecodedWadBytes,
                        kMaximumCliDecodedWadBytes},
                    kMaximumCliTwoFipPixels});

            std::cout
                << "OpenRC MapArtV1 report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:            " << level_id << '\n'
                << "Global TOC slot:     " << requested_slot << '\n'
                << "LBA:                 " << entry->extent.lba << '\n'
                << "Sectors:             " << entry->extent.sectors << '\n'
                << "Decoded bytes:       " << decoded.bytes.size() << '\n'
                << "Decoded SHA-256:     " << decoded.sha256 << '\n'
                << "Region-0 records:    "
                << map_art.region0_records.size() << '\n'
                << "Region-0 logical:    " << map_art.region0_logical_bytes << '\n'
                << "Region-0 padding:    " << map_art.region0_padding_bytes << "\n\n"
                << "Owned non-image regions:\n";
            for (std::size_t index = 0;
                 index < map_art.owned_regions.size();
                 ++index) {
                std::cout
                    << "  [" << index << "] "
                    << map_art.owned_regions[index].size() << " bytes\n";
            }
            std::cout << "\n2FIP image regions:\n";
            for (std::size_t index = 0; index < map_art.images.size(); ++index) {
                const auto& image = map_art.images[index];
                std::cout
                    << "  [" << (openrc::kMapArtFirstImageRegion + index) << "] "
                    << image.width << 'x' << image.height
                    << ", id " << hexadecimal(image.opaque_identifier, 8)
                    << ", pixels " << image.indices.size() << '\n';
            }

            if (arguments.size() == 4) {
                const auto tga = openrc::encode_map_art_tga(map_art);
                write_new_binary_file(arguments[3], tga);
                std::cout
                    << "\nTGA output:          "
                    << openrc::path_to_utf8(arguments[3]) << '\n'
                    << "TGA dimensions:      384x128\n"
                    << "TGA bytes:           " << tga.size() << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "ps2-save") {
        if (arguments.size() != 2) {
            std::cerr << "error: ps2-save expects an ISO path\n";
            return kUsageError;
        }

        try {
            const auto toc = openrc::inspect_disc_toc(arguments[1]);
            const auto entry = std::find_if(
                toc.global_extents.begin(),
                toc.global_extents.end(),
                [](const openrc::DiscTocGlobalExtent& candidate) {
                    return candidate.slot == kPs2SaveBundleGlobalSlot;
                });
            if (entry == toc.global_extents.end() ||
                entry->signature != openrc::DiscTocSignature::ps2d) {
                throw std::runtime_error("Global TOC slot 1 is absent or does "
                                         "not have a PS2D signature");
            }

            const auto bytes = read_disc_extent(
                arguments[1],
                entry->extent.lba,
                entry->extent.sectors,
                kMaximumCliPs2SaveBundleBytes);
            const auto bundle = openrc::parse_ps2_save_bundle(
                bytes,
                openrc::Ps2SaveBundleLimits{
                    kMaximumCliPs2SaveBundleBytes,
                    65536U,
                    4096U,
                    65536U,
                    kMaximumCliPs2SaveBundleBytes});

            std::size_t repeated_tlv_entries = 0;
            for (const auto& record : bundle.save_template.repeated_records) {
                repeated_tlv_entries += record.entries.size();
            }

            std::cout
                << "OpenRC PS2 save-bundle report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Global TOC slot:     " << kPs2SaveBundleGlobalSlot << '\n'
                << "LBA:                 " << entry->extent.lba << '\n'
                << "Sectors:             " << entry->extent.sectors << '\n'
                << "Logical bytes:       " << bundle.logical_bytes << '\n'
                << "Sector padding:      " << bundle.padding_bytes << "\n\n"
                << "Outer records:\n";
            for (std::size_t index = 0; index < bundle.records.size(); ++index) {
                std::cout
                    << "  [" << index << "] offset "
                    << hexadecimal(bundle.records[index].offset, 8)
                    << ", size " << bundle.records[index].size << " bytes\n";
            }

            std::cout
                << "\nicon.sys:\n"
                << "  second-line offset: " << bundle.icon_sys.second_line_offset << '\n'
                << "  view icon:          " << bundle.icon_sys.icon_filenames[0] << '\n'
                << "  copy icon:          " << bundle.icon_sys.icon_filenames[1] << '\n'
                << "  delete icon:        " << bundle.icon_sys.icon_filenames[2] << '\n'
                << "\nMemory-card icon:\n"
                << "  version:            "
                << hexadecimal(bundle.icon_model.version, 8) << '\n'
                << "  shapes:             " << bundle.icon_model.shape_count << '\n'
                << "  texture type:       " << bundle.icon_model.texture_type << '\n'
                << "  vertices:           " << bundle.icon_model.vertices.size() << '\n'
                << "  texture bytes:      " << bundle.icon_model.texture_bytes.size() << '\n'
                << "\nSave template:\n"
                << "  primary bytes:      "
                << bundle.save_template.primary_record_size << '\n'
                << "  primary TLV entries:"
                << ' ' << bundle.save_template.primary_record.entries.size() << '\n'
                << "  repeated bytes:     "
                << bundle.save_template.repeated_record_size << '\n'
                << "  repeated records:   "
                << bundle.save_template.repeated_records.size() << '\n'
                << "  repeated TLVs:      " << repeated_tlv_entries << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "twofip") {
        if (arguments.size() < 3 || arguments.size() > 4) {
            std::cerr
                << "error: twofip expects an ISO path, global TOC slot, "
                   "and optional TGA output path\n";
            return kUsageError;
        }

        const auto requested_slot_value = parse_decimal_argument(arguments[2]);
        if (!requested_slot_value ||
            *requested_slot_value >= openrc::kDiscTocGlobalExtentSlotCount) {
            std::cerr << "error: global TOC slot must be a decimal number from 0 to "
                      << (openrc::kDiscTocGlobalExtentSlotCount - 1U) << '\n';
            return kUsageError;
        }
        const auto requested_slot = *requested_slot_value;

        try {
            const auto toc = openrc::inspect_disc_toc(arguments[1]);
            const auto entry = std::find_if(
                toc.global_extents.begin(),
                toc.global_extents.end(),
                [requested_slot](const openrc::DiscTocGlobalExtent& candidate) {
                    return candidate.slot == requested_slot;
                });
            if (entry == toc.global_extents.end()) {
                throw std::runtime_error(
                    "Global TOC slot " + std::to_string(requested_slot) + " is empty");
            }

            std::vector<std::byte> source_bytes;
            const char* source_kind = nullptr;
            if (entry->signature == openrc::DiscTocSignature::two_fip) {
                source_bytes = read_disc_extent(
                    arguments[1],
                    entry->extent.lba,
                    entry->extent.sectors,
                    kMaximumCliDecodedWadBytes);
                source_kind = "direct 2FIP extent";
            } else if (entry->signature == openrc::DiscTocSignature::wad) {
                auto decoded = openrc::decode_wad(
                    arguments[1],
                    entry->extent.lba,
                    entry->extent.sectors,
                    kMaximumCliDecodedWadBytes);
                source_bytes = std::move(decoded.bytes);
                source_kind = "decoded WadV1";
            } else {
                throw std::runtime_error(
                    "Global TOC slot " + std::to_string(requested_slot) +
                    " is " +
                    std::string(openrc::disc_toc_signature_name(entry->signature)) +
                    ", not a direct or WadV1-wrapped 2FIP asset");
            }

            const auto image = openrc::parse_two_fip(
                source_bytes,
                kMaximumCliTwoFipPixels);
            std::cout
                << "OpenRC 2FIP report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Global TOC slot:     " << requested_slot << '\n'
                << "Source:              " << source_kind << '\n'
                << "LBA:                 " << entry->extent.lba << '\n'
                << "Sectors:             " << entry->extent.sectors << '\n'
                << "Opaque identifier:   " << hexadecimal(image.opaque_identifier, 8) << '\n'
                << "Dimensions:          " << image.width << 'x' << image.height << '\n'
                << "Pixel format:        PSMT8 (" << hexadecimal(
                       image.pixel_storage_format)
                << ")\n"
                << "Logical bytes:       " << image.logical_bytes << '\n'
                << "Storage padding:     " << image.padding_bytes << '\n';

            if (arguments.size() == 4) {
                const auto tga = openrc::encode_two_fip_tga(image);
                write_new_binary_file(arguments[3], tga);
                std::cout
                    << "TGA output:          "
                    << openrc::path_to_utf8(arguments[3]) << '\n'
                    << "TGA bytes:           " << tga.size() << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "prepare") {
        if (arguments.size() < 2 || arguments.size() > 3) {
            std::cerr << "error: prepare expects an ISO path and an optional games "
                   "directory\n";
            return kUsageError;
        }

        ProgressPrinter progress;
        try {
            std::filesystem::path games_directory;
            if (arguments.size() == 3) {
                games_directory = arguments[2];
            } else {
                const auto paths = openrc::application_paths();
                openrc::ensure_application_directories(paths);
                games_directory = paths.local_data / "games";
            }

            const auto result = openrc::prepare_game_files(
                arguments[1], games_directory,
                [&progress](const openrc::PreparationProgress& update) {
                    return progress.update(update);
                });
            progress.finish();

            std::cout << (result.already_prepared
                              ? "Preparation already exists and was verified.\n"
                              : "Preparation completed.\n")
                      << "Destination:     " << openrc::path_to_utf8(result.destination) << '\n'
                      << "Manifest:        " << openrc::path_to_utf8(result.manifest_path) << '\n'
                      << "Boot executable: "
                      << openrc::path_to_utf8(result.boot_executable_path) << '\n'
                      << "Image SHA-256:   " << result.image_sha256 << '\n'
                      << "Files:           " << result.file_count << '\n'
                      << "Total bytes:     " << result.total_file_bytes << '\n';
            return 0;
        } catch (const openrc::PreparationCancelled& error) {
            progress.finish();
            std::cerr << "cancelled: " << error.what() << '\n';
            return kCancelled;
        } catch (const std::exception& error) {
            progress.finish();
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "dvp-vu") {
        if (arguments.size() != 4) {
            std::cerr
                << "error: dvp-vu expects an executable, a comma-separated "
                   "entrypoint list, and a comma-separated overlay-section "
                   "list\n";
            return kUsageError;
        }

        const auto entrypoint_addresses =
            parse_decimal_u16_list(
                arguments[2], kMaximumCliDvpVuListItems);
        const auto overlay_section_indices =
            parse_decimal_u16_list(
                arguments[3], kMaximumCliDvpVuListItems);
        if (!entrypoint_addresses || !overlay_section_indices) {
            std::cerr
                << "error: VU pair entrypoints and ELF overlay sections must "
                   "be non-empty comma-separated decimal uint16 lists with at "
                   "most "
                << kMaximumCliDvpVuListItems << " items each\n";
            return kUsageError;
        }

        try {
            const auto bytes =
                read_bounded_binary_file(arguments[1], kMaximumCliElfBytes);
            const auto elf = openrc::inspect_elf(
                std::span<const std::byte>(bytes));
            if (!elf.dvp_overlay_table) {
                throw std::runtime_error(
                    "The executable has no DVP overlay table");
            }

            std::vector<openrc::ElfDvpOverlay> selected_overlays;
            selected_overlays.reserve(overlay_section_indices->size());
            for (const auto requested_index : *overlay_section_indices) {
                const auto overlay = std::find_if(
                    elf.dvp_overlay_table->overlays.begin(),
                    elf.dvp_overlay_table->overlays.end(),
                    [requested_index](const openrc::ElfDvpOverlay& candidate) {
                        return candidate.overlay_section_index ==
                            requested_index;
                    });
                if (overlay == elf.dvp_overlay_table->overlays.end()) {
                    throw std::runtime_error(
                        "A requested DVP overlay section was not found");
                }
                selected_overlays.push_back(*overlay);
            }

            const auto program = openrc::decode_dvp_vu_program_v1(
                std::span<const std::byte>(bytes),
                std::span<const openrc::ElfDvpOverlay>(selected_overlays),
                std::span<const std::uint16_t>(*entrypoint_addresses),
                openrc::DvpVuLimits{
                    kMaximumCliElfBytes,
                    kMaximumCliDvpVuListItems,
                    openrc::kDvpVu1MicroMemoryBytes,
                    kMaximumCliDvpVuListItems,
                    2U * openrc::kDvpVu1InstructionCount,
                    8U * openrc::kDvpVu1InstructionCount,
                });

            std::uint64_t flag_i_count = 0U;
            std::uint64_t flag_e_count = 0U;
            std::uint64_t flag_m_count = 0U;
            std::uint64_t flag_d_count = 0U;
            std::uint64_t flag_t_count = 0U;
            std::uint64_t xtop_count = 0U;
            std::uint64_t xitop_count = 0U;
            std::uint64_t xgkick_count = 0U;
            for (const auto& instruction : program.instructions) {
                flag_i_count += instruction.upper.immediate ? 1U : 0U;
                flag_e_count += instruction.upper.end ? 1U : 0U;
                flag_m_count += instruction.upper.m ? 1U : 0U;
                flag_d_count += instruction.upper.d ? 1U : 0U;
                flag_t_count += instruction.upper.t ? 1U : 0U;
                switch (instruction.lower.opcode) {
                case openrc::DvpVuLowerOpcode::xtop:
                    ++xtop_count;
                    break;
                case openrc::DvpVuLowerOpcode::xitop:
                    ++xitop_count;
                    break;
                case openrc::DvpVuLowerOpcode::xgkick:
                    ++xgkick_count;
                    break;
                default:
                    break;
                }
            }

            std::uint64_t vector_load_count = 0U;
            std::uint64_t vector_store_count = 0U;
            std::uint64_t integer_load_count = 0U;
            std::uint64_t integer_store_count = 0U;
            for (const auto& access : program.memory_accesses) {
                switch (access.kind) {
                case openrc::DvpVuMemoryAccessKind::vector_load:
                    ++vector_load_count;
                    break;
                case openrc::DvpVuMemoryAccessKind::vector_store:
                    ++vector_store_count;
                    break;
                case openrc::DvpVuMemoryAccessKind::integer_load:
                    ++integer_load_count;
                    break;
                case openrc::DvpVuMemoryAccessKind::integer_store:
                    ++integer_store_count;
                    break;
                }
            }

            std::uint64_t direct_transfer_count = 0U;
            std::uint64_t resolved_direct_transfer_count = 0U;
            std::uint64_t end_transfer_count = 0U;
            for (const auto& transfer : program.control_transfers) {
                if (transfer.direct_target_address) {
                    ++direct_transfer_count;
                    resolved_direct_transfer_count +=
                        transfer.direct_target_decoded ? 1U : 0U;
                }
                if (transfer.kind ==
                    openrc::DvpVuControlTransferKind::end_after_delay_slot) {
                    ++end_transfer_count;
                }
            }

            std::cout
                << "OpenRC DVP VU program report\n"
                << "Executable:           "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Input bytes:          " << program.input_bytes << '\n'
                << "Overlay chunks:       " << program.code_chunks.size() << '\n'
                << "Instruction runs:     "
                << program.instruction_runs.size() << '\n'
                << "Code bytes:           " << program.total_code_bytes << '\n'
                << "Instruction pairs:    " << program.instructions.size() << '\n'
                << "Entrypoints:          ";
            for (std::size_t index = 0U;
                 index < program.entrypoints.size();
                 ++index) {
                if (index != 0U) {
                    std::cout << ',';
                }
                std::cout << hexadecimal(
                    program.entrypoints[index].instruction_address, 3);
            }
            std::cout
                << '\n'
                << "Unknown upper/lower: "
                << program.unknown_upper_count << '/'
                << program.unknown_lower_count << '\n'
                << "Flags I/E/M/D/T:     "
                << flag_i_count << '/' << flag_e_count << '/'
                << flag_m_count << '/' << flag_d_count << '/'
                << flag_t_count << '\n'
                << "XTOP/XITOP/XGKICK:   "
                << xtop_count << '/' << xitop_count << '/'
                << xgkick_count << '\n'
                << "Vector load/store:   "
                << vector_load_count << '/' << vector_store_count << '\n'
                << "Integer load/store:  "
                << integer_load_count << '/' << integer_store_count << '\n'
                << "Direct targets:      "
                << resolved_direct_transfer_count << '/'
                << direct_transfer_count << " resolved\n"
                << "Indirect transfers:  "
                << program.indirect_control_transfer_count << '\n'
                << "End flags:            " << end_transfer_count << '\n'
                << "Missing delay slots:  "
                << program.missing_delay_slot_count << '\n'
                << "Basic blocks/edges:  "
                << program.basic_blocks.size() << '/'
                << program.cfg_edges.size() << '\n';

            std::cout << "\nChunks by VU virtual address:\n";
            for (const auto& chunk : program.code_chunks) {
                std::cout
                    << "  section " << chunk.overlay_section_index
                    << " vma="
                    << hexadecimal(chunk.virtual_byte_address, 4)
                    << " instructions=" << chunk.instruction_count
                    << " source=" << hexadecimal(chunk.source_range.offset, 8)
                    << '+' << hexadecimal(chunk.source_range.size) << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "dvp-vu-run") {
        if (arguments.size() != 5) {
            std::cerr
                << "error: dvp-vu-run expects an executable, one decimal "
                   "VU pair entrypoint, a decimal CSV overlay-section list, "
                   "and one decimal VIF1 TOP qword\n";
            return kUsageError;
        }

        const auto entrypoint = parse_decimal_argument(arguments[2]);
        const auto overlay_section_indices =
            parse_decimal_u16_list(
                arguments[3], kMaximumCliDvpVuListItems);
        const auto top_qword = parse_decimal_argument(arguments[4]);
        if (!entrypoint ||
            *entrypoint >= openrc::kDvpVu1InstructionCount ||
            !overlay_section_indices ||
            !top_qword ||
            *top_qword >= openrc::kDvpVuDataMemoryQwordCount) {
            std::cerr
                << "error: entrypoint must be 0..2047, overlay sections "
                   "must be a non-empty decimal uint16 CSV list, and TOP "
                   "must be 0..1023\n";
            return kUsageError;
        }

        try {
            const auto bytes =
                read_bounded_binary_file(arguments[1], kMaximumCliElfBytes);
            const auto elf = openrc::inspect_elf(
                std::span<const std::byte>(bytes));
            if (!elf.dvp_overlay_table) {
                throw std::runtime_error(
                    "The executable has no DVP overlay table");
            }

            std::vector<openrc::ElfDvpOverlay> selected_overlays;
            selected_overlays.reserve(overlay_section_indices->size());
            for (const auto requested_index : *overlay_section_indices) {
                const auto overlay = std::find_if(
                    elf.dvp_overlay_table->overlays.begin(),
                    elf.dvp_overlay_table->overlays.end(),
                    [requested_index](const openrc::ElfDvpOverlay& candidate) {
                        return candidate.overlay_section_index ==
                            requested_index;
                    });
                if (overlay == elf.dvp_overlay_table->overlays.end()) {
                    throw std::runtime_error(
                        "A requested DVP overlay section was not found");
                }
                selected_overlays.push_back(*overlay);
            }

            const std::array entrypoints{
                static_cast<std::uint16_t>(*entrypoint)};
            const auto program = openrc::decode_dvp_vu_program_v1(
                std::span<const std::byte>(bytes),
                std::span<const openrc::ElfDvpOverlay>(selected_overlays),
                std::span<const std::uint16_t>(entrypoints),
                openrc::DvpVuLimits{
                    kMaximumCliElfBytes,
                    kMaximumCliDvpVuListItems,
                    openrc::kDvpVu1MicroMemoryBytes,
                    kMaximumCliDvpVuListItems,
                    2U * openrc::kDvpVu1InstructionCount,
                    8U * openrc::kDvpVu1InstructionCount,
                });

            auto initial_state = openrc::make_dvp_vu_execution_state_v1();
            initial_state.xtop_qword = openrc::DvpVuWordV1{
                static_cast<std::uint32_t>(*top_qword),
                0xffffffffU,
            };
            const auto execution = openrc::execute_dvp_vu_program_v1(
                program,
                std::move(initial_state),
                openrc::DvpVuExecutionOptionsV1{
                    static_cast<std::uint16_t>(*entrypoint),
                    true,
                },
                openrc::DvpVuExecutionLimitsV1{
                    1'000'000U,
                    64U,
                    1024U,
                    65'536U,
                });

            std::cout
                << "OpenRC bounded DVP VU1 execution\n"
                << "Executable:           "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Entrypoint pair:      "
                << hexadecimal(*entrypoint, 3) << '\n'
                << "VIF1 TOP qword:       " << *top_qword << '\n'
                << "Initial state:        registers/RAM indeterminate\n"
                << "Timing model:         bounded-functional-v1\n"
                << "Termination:          "
                << dvp_vu_termination_name(execution.termination) << '\n'
                << "Executed pairs:       "
                << execution.executed_instruction_pairs << '\n'
                << "Final PC:             "
                << hexadecimal(execution.final_state.pc, 3) << '\n';
            if (execution.stopped_instruction_address) {
                std::cout
                    << "Stopped instruction: "
                    << hexadecimal(
                        *execution.stopped_instruction_address, 3)
                    << '\n';
            }
            std::cout
                << "XGKICK events:        "
                << execution.xgkick_events.size() << '\n'
                << "Warnings:             ";
            if (execution.warnings.empty()) {
                std::cout << "none";
            } else {
                for (std::size_t index = 0U;
                     index < execution.warnings.size();
                     ++index) {
                    if (index != 0U) {
                        std::cout << ',';
                    }
                    std::cout
                        << dvp_vu_warning_name(execution.warnings[index]);
                }
            }
            std::cout << '\n';

            for (std::size_t event_index = 0U;
                 event_index < execution.xgkick_events.size();
                 ++event_index) {
                const auto& event = execution.xgkick_events[event_index];
                std::cout
                    << "\nXGKICK " << event_index
                    << " pc="
                    << hexadecimal(event.instruction_address, 3)
                    << " base=";
                if ((event.base_qword.known_mask & 0x03ffU) == 0x03ffU) {
                    std::cout << (event.base_qword.bits & 0x03ffU);
                } else {
                    std::cout << "indeterminate";
                }
                std::cout
                    << " qwords=" << event.packet_qwords.size()
                    << " tags=" << event.tags.size()
                    << " complete="
                    << (event.packet_complete ? "yes" : "no")
                    << " indeterminate-tag="
                    << (event.encountered_indeterminate_tag ? "yes" : "no")
                    << " wrapped="
                    << (event.wrapped_memory ? "yes" : "no")
                    << '\n';
                for (std::size_t tag_index = 0U;
                     tag_index < event.tags.size();
                     ++tag_index) {
                    const auto& tag = event.tags[tag_index];
                    std::cout
                        << "  tag " << tag_index
                        << " memory=" << tag.memory_qword
                        << " nloop=" << tag.tag.nloop
                        << " eop=" << (tag.tag.eop ? 1 : 0)
                        << " flg="
                        << static_cast<unsigned int>(tag.tag.format)
                        << " nreg="
                        << static_cast<unsigned int>(
                               tag.tag.register_count)
                        << " regs=";
                    print_dvp_gif_registers(tag.tag);
                    std::cout
                        << " payload-qwords="
                        << tag.tag.payload_qword_count
                        << '\n';
                }
            }

            const bool successful =
                execution.termination ==
                    openrc::DvpVuTerminationV1::program_end ||
                execution.termination ==
                    openrc::DvpVuTerminationV1::stopped_after_xgkick;
            return successful ? 0 : kOperationError;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "r5900-boundaries") {
        if (arguments.size() != 2U) {
            std::cerr
                << "error: r5900-boundaries expects exactly one "
                         "executable path\n";
            return kUsageError;
        }

        try {
            const auto elf_bytes = read_bounded_binary_file(
                arguments[1],
                kMaximumCliElfBytes);
            const auto report = openrc::inventory_ee_r5900_boundaries_v1(
                elf_bytes,
                openrc::EeR5900BoundaryLimitsV1{
                    kMaximumCliElfBytes,
                    4096U,
                    kMaximumCliElfBytes,
                    kMaximumCliElfBytes / 4U,
                    kMaximumCliElfBytes / 4U,
                    1'000'000U});

            std::array<std::uint64_t, 7> transfer_counts{};
            for (const auto& transfer : report.control_transfers) {
                const auto index = static_cast<std::size_t>(transfer.kind);
                if (index >= transfer_counts.size()) {
                    throw std::runtime_error("The EE/R5900 inventory returned "
                                             "an invalid transfer kind");
                }
                ++transfer_counts[index];
            }

            std::uint64_t jal_count = 0U;
            std::uint64_t jalr_count = 0U;
            std::uint64_t register_link_branch_count = 0U;
            for (const auto& instruction : report.instructions) {
                if (instruction.opcode == openrc::EeR5900OpcodeV1::jal) {
                    ++jal_count;
                } else if (instruction.opcode == openrc::EeR5900OpcodeV1::jalr) {
                    ++jalr_count;
                } else if (
                    instruction.opcode == openrc::EeR5900OpcodeV1::bltzal ||
                    instruction.opcode == openrc::EeR5900OpcodeV1::bgezal ||
                    instruction.opcode == openrc::EeR5900OpcodeV1::bltzall ||
                    instruction.opcode == openrc::EeR5900OpcodeV1::bgezall) {
                    ++register_link_branch_count;
                }
            }

            std::vector<std::uint32_t> wrapper_addresses;
            std::uint64_t selector_proof_count = 0U;
            for (const auto& syscall : report.syscall_sites) {
                if (syscall.selector_v1) {
                    ++selector_proof_count;
                }
                if (syscall.exact_wrapper_entry_address) {
                    wrapper_addresses.push_back(
                        *syscall.exact_wrapper_entry_address);
                }
            }
            std::sort(wrapper_addresses.begin(), wrapper_addresses.end());
            wrapper_addresses.erase(
                std::unique(
                    wrapper_addresses.begin(),
                    wrapper_addresses.end()),
                wrapper_addresses.end());

            std::uint64_t direct_calls_to_wrappers = 0U;
            std::vector<std::uint32_t> called_wrapper_addresses;
            for (const auto& transfer : report.control_transfers) {
                if (transfer.kind !=
                        openrc::EeR5900ControlTransferKindV1::direct_call ||
                    !transfer.direct_target_address ||
                    !std::binary_search(
                        wrapper_addresses.begin(),
                        wrapper_addresses.end(),
                        *transfer.direct_target_address)) {
                    continue;
                }
                ++direct_calls_to_wrappers;
                called_wrapper_addresses.push_back(
                    *transfer.direct_target_address);
            }
            std::sort(
                called_wrapper_addresses.begin(),
                called_wrapper_addresses.end());
            called_wrapper_addresses.erase(
                std::unique(
                    called_wrapper_addresses.begin(),
                    called_wrapper_addresses.end()),
                called_wrapper_addresses.end());

            std::cout
                << "OpenRC EE/R5900 boundary inventory\n"
                << "Executable:           "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Input bytes:          " << report.input_bytes << '\n'
                << "Code regions:         " << report.code_regions.size() << '\n'
                << "Code bytes:           " << report.total_code_bytes << '\n'
                << "Instruction words:    " << report.instructions.size() << '\n'
                << "Unclassified words:  "
                << report.unclassified_instruction_count << '\n'
                << "Control transfers:   "
                << report.control_transfers.size() << '\n'
                << "  direct jumps:      " << transfer_counts[0] << '\n'
                << "  conditional:       " << transfer_counts[1] << '\n'
                << "  direct calls:      " << transfer_counts[2] << '\n'
                << "  conditional calls: " << transfer_counts[3] << '\n'
                << "  indirect jumps:    " << transfer_counts[4] << '\n'
                << "  indirect calls:    " << transfer_counts[5] << '\n'
                << "  returns via ra:    " << transfer_counts[6] << '\n'
                << "JAL / JALR:          " << jal_count << " / "
                << jalr_count << '\n'
                << "REGIMM link branches:" << ' '
                << register_link_branch_count << '\n'
                << "Missing delay slots: "
                << report.missing_delay_slot_count << '\n'
                << "Targets outside code:"
                << ' ' << report.direct_target_outside_code_count << '\n'
                << "SYSCALL sites:       " << report.syscall_sites.size() << '\n'
                << "Selector proofs:     " << selector_proof_count << '\n'
                << "Exact wrappers:      " << wrapper_addresses.size() << '\n'
                << "JALs to wrappers:    " << direct_calls_to_wrappers << '\n'
                << "Called wrappers:     "
                << called_wrapper_addresses.size() << "\n\n"
                << "Code regions:\n";
            for (std::size_t index = 0U;
                 index < report.code_regions.size();
                 ++index) {
                const auto& region = report.code_regions[index];
                std::cout
                    << "  [" << index << "] " << region.section_name
                    << ": file " << hexadecimal(region.source_range.offset, 8)
                    << " + " << region.source_range.size
                    << ", virtual " << hexadecimal(region.virtual_address, 8)
                    << ", words " << region.instruction_count << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "elf") {
        if (arguments.size() != 2) {
            std::cerr << "error: elf expects exactly one executable path\n";
            return kUsageError;
        }

        try {
            const auto report = openrc::inspect_elf(arguments[1]);
            std::cout
                << "Executable:             "
                << openrc::path_to_utf8(report.executable_path) << '\n'
                << "File size:              " << report.file_size << " bytes\n"
                << "Format:                 ELF32, little-endian, MIPS\n"
                << "ELF type:               " << hexadecimal(report.type, 4) << '\n'
                << "Machine:                " << report.machine << '\n'
                << "Flags:                  " << hexadecimal(report.flags, 8) << '\n'
                << "Entry point:            " << hexadecimal(report.entry_point, 8) << '\n'
                << "Program headers:        " << report.program_header_count << '\n'
                << "Section headers:        " << report.section_header_count << '\n'
                << "Loadable segments:      " << report.loadable_segment_count << '\n';
            if (report.loadable_virtual_address_range) {
                std::cout
                    << "Load address range:     "
                    << hexadecimal(report.loadable_virtual_address_range->begin, 8) << " - "
                    << hexadecimal(report.loadable_virtual_address_range->end, 8)
                    << " (end exclusive)\n";
            } else {
                std::cout << "Load address range:     none\n";
            }

            if (report.iop_module_info) {
                const auto& module = *report.iop_module_info;
                std::cout
                    << "\nIOP module:\n"
                    << "  Name:                 " << module.name << '\n'
                    << "  Version:              " << hexadecimal(module.version, 4) << '\n'
                    << "  Module-info address:  "
                    << hexadecimal(module.module_info_address, 8) << '\n'
                    << "  Entry point:          "
                    << hexadecimal(module.entry_point, 8) << '\n'
                    << "  Global pointer:       "
                    << hexadecimal(module.global_pointer, 8) << '\n'
                    << "  Declared text/data/bss: "
                    << module.text_size << '/' << module.data_size << '/'
                    << module.bss_size << " bytes\n"
                    << "  Alloc size matches:   text="
                    << (module.text_size_matches_allocated_sections ? "yes" : "no")
                    << ", data="
                    << (module.data_size_matches_allocated_sections ? "yes" : "no")
                    << ", bss="
                    << (module.bss_size_matches_allocated_sections ? "yes" : "no")
                    << '\n';
            }

            if (!report.relocation_summaries.empty()) {
                std::cout << "\nRelocations:\n";
                for (const auto& summary : report.relocation_summaries) {
                    std::cout
                        << "  section " << summary.section_index
                        << " -> target " << summary.target_section_index
                        << ", entries " << summary.entry_count << ", types";
                    for (const auto& type : summary.types) {
                        std::cout << ' ' << static_cast<unsigned int>(type.type)
                                  << ':' << type.count;
                    }
                    std::cout << '\n';
                }
            }

            if (!report.iop_import_libraries.empty()) {
                std::cout << "\nIOP import libraries:\n";
                for (const auto& library : report.iop_import_libraries) {
                    std::cout
                        << "  " << library.name
                        << " version=" << hexadecimal(library.version, 4)
                        << " flags=" << hexadecimal(library.flags, 4)
                        << " ordinals=";
                    for (std::size_t index = 0; index < library.ordinals.size(); ++index) {
                        if (index != 0U) {
                            std::cout << ',';
                        }
                        std::cout << library.ordinals[index];
                    }
                    std::cout << '\n';
                }
            }

            if (report.dvp_overlay_table) {
                const auto& table = *report.dvp_overlay_table;
                std::cout
                    << "\nDVP overlays: "
                    << table.overlays.size()
                    << " (table section " << table.section_index
                    << ", strings section "
                    << table.string_table_section_index << ")\n";
                for (const auto& overlay : table.overlays) {
                    const auto& code_section =
                        report.section_headers[overlay.code_section_index];
                    std::cout
                        << "  [" << overlay.overlay_section_index << "] "
                        << overlay.name
                        << " lma="
                        << hexadecimal(overlay.load_memory_address, 8)
                        << " vma="
                        << hexadecimal(overlay.virtual_memory_address, 8)
                        << " size=" << hexadecimal(overlay.size)
                        << " code=" << code_section.name
                        << '+' << hexadecimal(
                            overlay.code_file_offset -
                                code_section.file_offset)
                        << " (file "
                        << hexadecimal(overlay.code_file_offset, 8)
                        << ")\n";
                }
            }

            std::cout << "\nProgram headers:\n";
            for (std::size_t index = 0; index < report.program_headers.size(); ++index) {
                const auto& header = report.program_headers[index];
                std::cout
                    << "  [" << index << "] type=" << header.type
                    << " offset=" << hexadecimal(header.file_offset, 8)
                    << " vaddr=" << hexadecimal(header.virtual_address, 8)
                    << " paddr=" << hexadecimal(header.physical_address, 8)
                    << " filesz=" << hexadecimal(header.file_size)
                    << " memsz=" << hexadecimal(header.memory_size)
                    << " flags=" << hexadecimal(header.flags)
                    << " align=" << hexadecimal(header.alignment) << '\n';
            }

            std::cout << "\nSection headers:\n";
            for (std::size_t index = 0; index < report.section_headers.size(); ++index) {
                const auto& section = report.section_headers[index];
                std::cout
                    << "  [" << index << "] "
                    << (section.name.empty() ? "<unnamed>" : section.name)
                    << " type=" << section.type
                    << " flags=" << hexadecimal(section.flags)
                    << " addr=" << hexadecimal(section.virtual_address, 8)
                    << " offset=" << hexadecimal(section.file_offset, 8)
                    << " size=" << hexadecimal(section.size)
                    << " link=" << section.link
                    << " info=" << section.info
                    << " align=" << hexadecimal(section.alignment)
                    << " entsize=" << hexadecimal(section.entry_size) << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "paths") {
        try {
            const auto paths = openrc::application_paths();
            openrc::ensure_application_directories(paths);
            std::cout
                << "Roaming config: " << openrc::path_to_utf8(paths.roaming_config) << '\n'
                << "Local data:     " << openrc::path_to_utf8(paths.local_data) << '\n'
                << "Cache:          " << openrc::path_to_utf8(paths.cache) << '\n'
                << "Logs:           " << openrc::path_to_utf8(paths.logs) << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    std::cerr << "error: unknown command\n\n";
    print_usage();
    return kUsageError;
}

} // namespace

#ifdef _WIN32
int wmain(const int argc, wchar_t* argv[]) {
    std::vector<std::filesystem::path> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return run(arguments);
}
#else
int main(const int argc, char* argv[]) {
    std::vector<std::filesystem::path> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return run(arguments);
}
#endif
