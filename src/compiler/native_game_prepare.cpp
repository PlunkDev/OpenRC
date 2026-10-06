#include "openrc/native_game_prepare.hpp"
#include "openrc/native_frontend_profile.hpp"

#include "level_scene_recovery.hpp"
#include "level_scene_render_compile.hpp"

#include "openrc/content_api.hpp"
#include "openrc/disc.hpp"
#include "openrc/hash.hpp"
#include "openrc/level_actor_animation_compile.hpp"
#include "openrc/level_actor_library_compile.hpp"
#include "openrc/level_destructible_scene_compile.hpp"
#include "openrc/level_entity_scene_compile.hpp"
#include "openrc/level_gameplay_scene_compile.hpp"
#include "openrc/level_render_scene_compile.hpp"
#include "openrc/media_clip.hpp"
#include "openrc/rac_actor_library_compile.hpp"
#include "openrc/rac_collectible_scene_compile.hpp"
#include "openrc/rac_destructible_scene_compile.hpp"
#include "openrc/rac_level_foundation_compile.hpp"
#include "openrc/rac_level_moby_assets.hpp"
#include "openrc/rac_moby_actor_scene_compile.hpp"
#include "openrc/rac_moby_animation_compile.hpp"
#include "openrc/rac_ratchet_animation_compile.hpp"
#include "openrc/rac_pss.hpp"
#include "openrc/rac_startup.hpp"
#include "openrc/rac_frontend_scene_compile.hpp"
#include "openrc/rac_frontend_numeric.hpp"
#include "openrc/rac_frontend_title.hpp"
#include "openrc/rac_frontend_environment_compile.hpp"
#include "openrc/rac_frontend_menu_resources.hpp"
#include "openrc/rac_frontend_sound_resources.hpp"
#include "openrc/rac_frontend_state.hpp"
#include "openrc/rac_level_installation.hpp"
#include "openrc/rac_new_game_resources.hpp"
#include "openrc/runtime_level_content.hpp"
#include "openrc/scene_block_geometry.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
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
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace openrc {
namespace {

constexpr std::uint64_t kMaximumDecodedWadBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumElfBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCollisionPayloadBytes = 32U * 1024U * 1024U;
constexpr std::uint64_t kMaximumRenderScenePayloadBytes =
    UINT64_C(512) * 1024U * 1024U;
constexpr std::uint64_t kMaximumActorPayloadBytes =
    UINT64_C(256) * 1024U * 1024U;
constexpr std::uint64_t kMaximumActorAnimationPayloadBytes =
    UINT64_C(64) * 1024U * 1024U;
constexpr std::uint64_t kMaximumEntityScenePayloadBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumGameplayScenePayloadBytes =
    64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumDestructibleScenePayloadBytes =
    64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumTwoFipPixels = 16U * 1024U * 1024U;
constexpr std::uint64_t kMaximumStartupMovieBytes = 32U * 1024U * 1024U;
constexpr std::string_view kStartupMediaType = "openrc.media-clip";
constexpr std::string_view kStartupMovieSource = "rac1/global-toc/1800";
constexpr std::string_view kStartupImagePass = "openrc.rac-startup-image-compile.v1";
constexpr std::string_view kFrontendScenePass = "openrc.rac-frontend-scene-compile.v1";
constexpr std::string_view kPlayerRigKey = "actors/ratchet/rig";
constexpr std::string_view kPlayerHighModelKey = "actors/ratchet/high";
constexpr std::string_view kPlayerSourceSequenceKeyPrefix =
    "actors/ratchet/source-sequence/";
constexpr std::string_view kPlayerIdleAnimationKey =
    "actors/ratchet/source-sequence/000";
constexpr std::string_view kPlayerWalkAnimationKey =
    "actors/ratchet/source-sequence/003";
constexpr std::string_view kPlayerRunAnimationKey =
    "actors/ratchet/source-sequence/004";
constexpr std::uint32_t kPlayerAnimationSourceUpdatesPerSecond = 50U;
constexpr std::array<std::uint16_t, kDiscTocLevelCount>
    kPlayerAnimationClipCountsByLevel{
        134U, 88U, 86U, 79U, 84U, 100U, 89U, 98U, 108U, 79U,
        83U,  96U, 105U, 86U, 94U, 89U, 96U, 99U, 111U,
    };
constexpr std::string_view kPlayerArchetypeKey = "openrc.player/default";
constexpr std::uint32_t kBoltSourceClassId = 13U;
constexpr std::string_view kBoltRigKey = "actors/collectibles/bolt/rig";
constexpr std::string_view kBoltHighModelKey = "actors/collectibles/bolt/high";
constexpr std::string_view kBoltArchetypeKey = "openrc.collectible/bolt";
constexpr std::string_view kBoltItemKey = "openrc.currency/bolts";
constexpr std::uint32_t kBoltCrateSourceClassId = 500U;
constexpr std::string_view kBoltCrateArchetypeKey =
    "openrc.breakable/bolt-crate";
constexpr std::uint32_t kVeldinLevelId = 0U;
constexpr std::uint32_t kVeldinMoby749SourceClassId = 749U;
constexpr std::string_view kVeldinMoby749RigKey =
    "actors/rac1/moby/0749/rig";
constexpr std::string_view kVeldinMoby749HighModelKey =
    "actors/rac1/moby/0749/high";
constexpr std::string_view kVeldinMoby749SourceSequenceKeyPrefix =
    "actors/rac1/moby/0749/source-sequence/";
constexpr std::uint32_t kVeldinMoby749InitialSourceSequence = 1U;
constexpr std::string_view kVeldinMoby749InitialAnimationKey =
    "actors/rac1/moby/0749/initial/source-sequence/001";
constexpr std::string_view kVeldinMoby749ArchetypeKey = "rac1/moby/0749";
constexpr std::uint32_t kVeldinMoby749AnimationSourceUpdatesPerSecond = 50U;
constexpr std::array<std::uint16_t, 8U> kVeldinMoby749FrameCounts{
    1U, 6U, 13U, 15U, 15U, 21U, 25U, 23U};
constexpr std::uint32_t kVeldinMoby749PlacementBegin = 143U;
constexpr std::uint32_t kVeldinMoby749PlacementCount = 16U;

constexpr RacMobyModelGeometryLimitsV1 kMobyModelGeometryLimits{
    {kMaximumDecodedWadBytes, 4096U, 4096U, 4096U, 1'000'000U, 4096U,
     1'000'000U},
    4096U,
    1'000'000U,
    1'000'000U};

constexpr RacLevelMobyTextureLimitsV1 kMobyTextureLimits{
    kMaximumDecodedWadBytes,
    kMaximumDecodedWadBytes,
    kMaximumDecodedWadBytes,
    255U,
    4096U,
    4096U,
    kMaximumTwoFipPixels,
    kMaximumTwoFipPixels,
    kMaximumTwoFipPixels * 4U};

[[noreturn]] void fail(const std::string &message) {
  throw NativeGamePreparationErrorV1(message);
}

[[noreturn]] void cancelled() {
  throw NativeGamePreparationCancelledV1(
      "Native game preparation was cancelled");
}

void report_progress(const NativeGamePreparationControlV1 &control,
                     const NativeGamePreparationPhaseV1 phase,
                     const std::uint32_t level_id,
                     const std::uint32_t completed_levels) {
  if (control.progress == nullptr) {
    return;
  }
  const NativeGamePreparationProgressV1 progress{
      phase, level_id, completed_levels,
      static_cast<std::uint32_t>(kDiscTocLevelCount)};
  if (!control.progress(progress, control.context)) {
    cancelled();
  }
}

[[nodiscard]] RacLevelMobyAssetLimitsV1 make_source_asset_limits() {
  return RacLevelMobyAssetLimitsV1{
      kMaximumDecodedWadBytes,
      kMaximumDecodedWadBytes,
      kMaximumDecodedWadBytes,
      4096U,
      65'536U,
      1'000'000U,
      1'000'000U,
      RacLevelCoreLimitsV1{kMaximumDecodedWadBytes, kMaximumDecodedWadBytes,
                           kMaximumDecodedWadBytes, 4096U, 255U, 4096U, 4096U,
                           4096U, 255U, 255U},
      RacLevelCollisionLimitsV1{kMaximumDecodedWadBytes, 65'536U, 1'000'000U,
                                4'000'000U, 1'000'000U, 16'000'000U,
                                16'000'000U, 65'536U, 4'000'000U, 4'000'000U},
      RacGameplayBankLimitsV1{kMaximumDecodedWadBytes},
      RacMobyClassLimitsV1{kMaximumDecodedWadBytes, false},
      RacMobyClassLimitsV1{kMaximumDecodedWadBytes, true},
      kMobyModelGeometryLimits,
      kMobyTextureLimits,
      RacTieClassLimitsV1{kMaximumDecodedWadBytes, 4096U, 65'536U, 1'000'000U,
                          1'000'000U, 1'000'000U, 16U},
      4096U,
      65'536U,
      1'000'000U,
      1'000'000U};
}

[[nodiscard]] constexpr RacLevelCollisionCompileLimitsV1
make_collision_compile_limits() {
  return RacLevelCollisionCompileLimitsV1{
      1'000'000U,
      16'000'000U,
      16'000'000U,
      65'536U,
      4'000'000U,
      4'000'000U,
      CollisionWorldBuildLimitsV1{1'000'000U, 2'000'000U, 1'000'000U,
                                  16'000'000U,
                                  kCollisionDefaultGridCellSizeQ6V1}};
}

[[nodiscard]] CollisionWorldIoLimitsV1 make_collision_payload_limits() {
  return CollisionWorldIoLimitsV1{kMaximumCollisionPayloadBytes,
                                  make_collision_compile_limits().world};
}

[[nodiscard]] constexpr LevelBootstrapV1Limits make_bootstrap_payload_limits() {
  return LevelBootstrapV1Limits{64U * 1024U, 1024U};
}

[[nodiscard]] RacLevelFoundationCompileLimitsV1
make_foundation_compile_limits(const LevelPackageV1Limits package_limits) {
  const auto source_limits = make_source_asset_limits();
  return RacLevelFoundationCompileLimitsV1{
      source_limits.collision,         source_limits.gameplay,
      make_collision_compile_limits(), make_collision_payload_limits(),
      make_bootstrap_payload_limits(), package_limits};
}

[[nodiscard]] RenderSceneIoLimitsV1 make_render_scene_io_limits() {
  auto limits = game::make_runtime_level_content_limits_v1().render_scene;
  limits.max_encoded_bytes = kMaximumRenderScenePayloadBytes;
  return limits;
}

[[nodiscard]] constexpr RacMobyBindPoseLimitsV1
make_single_moby_bind_pose_limits() {
  return RacMobyBindPoseLimitsV1{
      RacMobyBindRigLimitsV1{kMaximumDecodedWadBytes, 255U, 1.0e-8},
      kMobyModelGeometryLimits,
      1'000'000U};
}

[[nodiscard]] constexpr ActorLibraryLimitsV1
make_single_actor_library_limits() {
  return ActorLibraryLimitsV1{
      1U,
      1U,
      128U,
      256U,
      255U,
      255U,
      16U,
      1U,
      16U,
      17U,
      1U,
      1'000'000U,
      1'000'000U,
      3'000'000U,
      4096U,
      4096U,
      kMaximumTwoFipPixels,
      kMaximumTwoFipPixels * 4U};
}

[[nodiscard]] constexpr ActorLibraryIoLimitsV1 make_actor_io_limits() {
  auto limits = game::make_runtime_level_content_limits_v1().actor_library;
  limits.max_encoded_bytes = kMaximumActorPayloadBytes;
  return limits;
}

[[nodiscard]] constexpr ActorAnimationIoLimitsV1
make_actor_animation_io_limits() {
  auto limits = game::make_runtime_level_content_limits_v1().actor_animation;
  limits.max_encoded_bytes = kMaximumActorAnimationPayloadBytes;
  return limits;
}

[[nodiscard]] RacFrontendSceneCompileLimitsV1 make_frontend_scene_limits() {
  constexpr auto bytes=kMaximumDecodedWadBytes;
  return {bytes,4096U,4096U,bytes,
      {bytes,false},make_single_moby_bind_pose_limits(),
      {bytes,bytes,bytes,255U,4096U,4096U,16000000U,16000000U,bytes},
      make_actor_io_limits().library,
      {{bytes,1024U,100000U,4096U,bytes},{bytes,bytes,255U,255U},
       {255U,bytes,65535U,65535U,1.e-8},make_actor_animation_io_limits().bank}};
}

[[nodiscard]] constexpr RacRatchetAnimationCompileLimitsV1
make_player_animation_compile_limits() {
  return RacRatchetAnimationCompileLimitsV1{
      RacRatchetSequenceLimitsV1{kMaximumDecodedWadBytes,
                                 kMaximumDecodedWadBytes, 255U, 255U},
      RacRatchetPoseLimitsV1{255U, kMaximumDecodedWadBytes, 65'535U,
                             65'535U, 1.0e-8},
      make_actor_animation_io_limits().bank,
  };
}

[[nodiscard]] constexpr RacMobyAnimationCompileLimitsV1
make_moby_animation_compile_limits() {
  return RacMobyAnimationCompileLimitsV1{
      RacRatchetSequenceLimitsV1{kMaximumDecodedWadBytes,
                                 kMaximumDecodedWadBytes, 255U, 255U},
      RacRatchetPoseLimitsV1{255U, kMaximumDecodedWadBytes, 65'535U,
                             65'535U, 1.0e-8},
      make_actor_animation_io_limits().bank,
  };
}

[[nodiscard]] std::vector<RacRatchetAnimationClipProfileV1>
make_player_animation_profiles(const RacLevelCoreIndexV1 &level_core) {
  const std::array confirmed{
      RacRatchetAnimationClipProfileV1{
          0U, 0U, std::string(kPlayerIdleAnimationKey),
          ActorAnimationWrapModeV1::loop},
      RacRatchetAnimationClipProfileV1{
          1U, 3U, std::string(kPlayerWalkAnimationKey),
          ActorAnimationWrapModeV1::loop},
      RacRatchetAnimationClipProfileV1{
          2U, 4U, std::string(kPlayerRunAnimationKey),
          ActorAnimationWrapModeV1::loop},
  };
  return make_rac_ratchet_complete_animation_profiles_v1(
      level_core, confirmed, kPlayerSourceSequenceKeyPrefix,
      ActorAnimationWrapModeV1::clamp);
}

[[nodiscard]] constexpr EntitySceneIoLimitsV1
make_native_entity_scene_io_limits() {
  return EntitySceneIoLimitsV1{
      kMaximumEntityScenePayloadBytes,
      EntitySceneLimitsV1{65'536U, 65'536U, 65'536U, 65'536U, 1U, 256U, 256U,
                          UINT64_C(16) * 1024U * 1024U}};
}

[[nodiscard]] constexpr GameplaySceneIoLimitsV1
make_native_gameplay_scene_io_limits() {
  return GameplaySceneIoLimitsV1{
      kMaximumGameplayScenePayloadBytes,
      GameplaySceneLimitsV1{65'536U, 256U,
                            UINT64_C(16) * 1024U * 1024U}};
}

[[nodiscard]] std::string level_source_locator(const std::uint32_t level_id,
                                               const std::string_view suffix) {
  std::ostringstream stream;
  stream << "rac1/level/" << std::setfill('0') << std::setw(3) << level_id
         << '/' << suffix;
  return stream.str();
}

[[nodiscard]] std::string level_package_path(const std::uint32_t level_id) {
  std::ostringstream stream;
  stream << "levels/" << std::setfill('0') << std::setw(3) << level_id
         << ".orlvl";
  return stream.str();
}

[[nodiscard]] bool path_component_equal(
    const std::filesystem::path &left,
    const std::filesystem::path &right) noexcept {
#ifdef _WIN32
  const auto &left_native = left.native();
  const auto &right_native = right.native();
  if (left_native.size() > static_cast<std::size_t>(INT_MAX) ||
      right_native.size() > static_cast<std::size_t>(INT_MAX)) {
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

// The publisher's two-rename transaction requires one writer for a
// destination parent. Keep a persistent sibling lock file and hold an
// operating-system lock for the complete inspect/reuse/compile/publish job.
// The file is intentionally not deleted: unlinking it after unlock would let
// a third process lock a new inode while a second process still owns the old
// one.
class DestinationPreparationLock final {
public:
  explicit DestinationPreparationLock(
      const std::filesystem::path &destination_root) {
    const auto normalized = destination_root.lexically_normal();
    if (normalized.filename().empty()) {
      fail("The native publication root must name a directory below a "
           "filesystem root");
    }
    auto lock_path = normalized;
    lock_path += ".openrc-prepare.lock";
#ifdef _WIN32
    const auto parent_attributes =
        GetFileAttributesW(normalized.parent_path().c_str());
    if (parent_attributes == INVALID_FILE_ATTRIBUTES ||
        (parent_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
        (parent_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
      fail("The native publication parent must be an existing plain "
           "directory");
    }
    handle_ = CreateFileW(
        lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
      fail("Cannot acquire the native preparation lock; another OpenRC "
           "process may already be preparing this installation (Windows "
           "error " +
           std::to_string(GetLastError()) + ")");
    }
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if (GetFileInformationByHandleEx(
            handle_, FileAttributeTagInfo, &attributes,
            static_cast<DWORD>(sizeof(attributes))) == FALSE ||
        (attributes.FileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U ||
        GetFileType(handle_) != FILE_TYPE_DISK) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
      fail("The native preparation lock is not a plain local file");
    }
#else
    struct stat parent_status {};
    if (::lstat(normalized.parent_path().c_str(), &parent_status) != 0 ||
        !S_ISDIR(parent_status.st_mode)) {
      fail("The native publication parent must be an existing plain "
           "directory");
    }
    descriptor_ = ::open(lock_path.c_str(),
                         O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor_ < 0) {
      fail("Cannot open the native preparation lock: " +
           std::string(std::strerror(errno)));
    }
    struct stat status {};
    if (::fstat(descriptor_, &status) != 0 || !S_ISREG(status.st_mode)) {
      const auto saved_error = errno;
      ::close(descriptor_);
      descriptor_ = -1;
      fail("The native preparation lock is not a plain file: " +
           std::string(std::strerror(saved_error)));
    }
    if (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
      const auto saved_error = errno;
      ::close(descriptor_);
      descriptor_ = -1;
      fail("Cannot acquire the native preparation lock; another OpenRC "
           "process may already be preparing this installation: " +
           std::string(std::strerror(saved_error)));
    }
#endif
  }

  ~DestinationPreparationLock() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
#else
    if (descriptor_ >= 0) {
      ::close(descriptor_);
    }
#endif
  }

  DestinationPreparationLock(const DestinationPreparationLock &) = delete;
  DestinationPreparationLock &
  operator=(const DestinationPreparationLock &) = delete;

private:
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int descriptor_ = -1;
#endif
};

void require_publication_root_outside_source(
    const std::filesystem::path &destination_root,
    const std::filesystem::path &source, const char *source_description) {
  std::error_code error;
  const auto normalized_source = std::filesystem::canonical(source, error);
  if (error) {
    fail(std::string("Cannot resolve the ") + source_description + ": " +
         error.message());
  }
  error.clear();
  const auto normalized_destination =
      std::filesystem::weakly_canonical(destination_root, error);
  if (error) {
    fail(std::string("Cannot resolve the native publication root: ") +
         error.message());
  }

  auto source_component = normalized_source.begin();
  const auto source_end = normalized_source.end();
  auto destination_component = normalized_destination.begin();
  const auto destination_end = normalized_destination.end();
  for (; destination_component != destination_end &&
         source_component != source_end;
       ++destination_component, ++source_component) {
    if (!path_component_equal(*destination_component, *source_component)) {
      return;
    }
  }
  if (destination_component == destination_end) {
    fail(std::string("The native publication root cannot contain or equal the ") +
         source_description);
  }
}

[[nodiscard]] std::uint64_t host_size_to_u64(const std::uintmax_t value,
                                             const char *description) {
  if constexpr (sizeof(std::uintmax_t) > sizeof(std::uint64_t)) {
    if (value > std::numeric_limits<std::uint64_t>::max()) {
      fail(std::string(description) + " exceeds the 64-bit provenance domain");
    }
  }
  return static_cast<std::uint64_t>(value);
}

[[nodiscard]] LevelPackageV1
compile_level_foundation(const RacLevelMobyAssetsV1 &assets,
                         const LevelPackageV1Limits package_limits) {
  const RacLevelFoundationCompileRequestV1 request{
      assets.level_id,
      kOpenRcContentApiVersionV1,
      std::string(kNativeGameBuildIdV1),
      {level_source_locator(assets.level_id, "core/collision"),
       assets.collision_source_bytes},
      {level_source_locator(assets.level_id, "gameplay"),
       assets.gameplay_source_bytes}};
  return compile_rac_level_foundation_package_v1(
      request, make_foundation_compile_limits(package_limits));
}

[[nodiscard]] const RacLevelMobyModelV1 &
require_unique_player_model(const RacLevelMobyAssetsV1 &assets) {
  const RacLevelMobyModelV1 *player = nullptr;
  for (const auto &model : assets.models) {
    if (model.class_id != 0U) {
      continue;
    }
    if (player != nullptr) {
      fail("The RAC1 level contains more than one retained class_id 0 player "
           "model source");
    }
    player = &model;
  }
  if (player == nullptr) {
    fail("The RAC1 level has no retained class_id 0 player model source");
  }
  if (player->source_bytes.empty() || player->source_class.input_bytes == 0U ||
      player->source_class.input_bytes !=
          static_cast<std::uint64_t>(player->source_bytes.size()) ||
      player->joint_count == 0U ||
      player->source_class.joint_count != player->joint_count) {
    fail("The RAC1 class_id 0 player model source is incomplete or "
         "inconsistent");
  }
  return *player;
}

[[nodiscard]] RacMobyBindPoseGeometryV1
compile_player_bind_pose(const RacLevelMobyModelV1 &player) {
  return compile_rac_moby_bind_pose_geometry_v1(
      player.source_bytes, player.source_class, RacMobyLodV1::high,
      make_single_moby_bind_pose_limits());
}

[[nodiscard]] ActorAnimationBankV1 compile_player_animation_bank(
    const RacLevelMobyAssetsV1 &assets, const RacLevelMobyModelV1 &player,
    const RacMobyBindRigV1 &bind_rig) {
  const auto profiles = make_player_animation_profiles(assets.level_core);
  return compile_rac_ratchet_animation_bank_v1(
      assets.level_core_source_bytes, assets.level_core, bind_rig,
      player.source_class.scale, std::string(kPlayerRigKey), profiles,
      kPlayerAnimationSourceUpdatesPerSecond,
      make_player_animation_compile_limits());
}

[[nodiscard]] ActorLibraryV1 compile_player_actor_library(
    RacLevelMobyAssetsV1 &assets, const RacLevelMobyModelV1 &player,
    RacMobyBindPoseGeometryV1 bind_pose) {
  RacActorLibraryCompileRequestV1 request;
  request.rig_semantic_key = kPlayerRigKey;
  request.model_semantic_key = kPlayerHighModelKey;
  request.bind_pose = std::move(bind_pose);
  request.texture_slots = player.texture_slots;
  request.used_texture_slot_count = player.used_texture_slot_count;
  request.texture_bank = std::move(assets.textures);
  return compile_rac_actor_library_v1(request,
                                      make_single_actor_library_limits());
}

[[nodiscard]] const RacLevelMobyModelV1 &
require_unique_moby_model(const RacLevelMobyAssetsV1 &assets,
                          std::uint32_t class_id,
                          std::string_view semantic_description);

struct CompiledMobyActorV1 {
  ActorLibraryV1 library;
  ActorAnimationBankV1 animations;
};

[[nodiscard]] CompiledMobyActorV1 compile_moby_actor(
    const RacLevelMobyAssetsV1 &assets, const std::uint32_t class_id,
    const std::string_view rig_key, const std::string_view model_key,
    const std::string_view sequence_key_prefix,
    const std::optional<std::uint32_t> initial_source_sequence,
    const std::string_view initial_animation_key,
    const std::uint32_t source_updates_per_second,
    const std::string_view semantic_description) {
  const auto &source =
      require_unique_moby_model(assets, class_id, semantic_description);
  auto bind_pose = compile_rac_moby_bind_pose_geometry_v1(
      source.source_bytes, source.source_class, RacMobyLodV1::high,
      make_single_moby_bind_pose_limits());
  const auto animation_rig = bind_pose.bind_rig;

  RacActorLibraryCompileRequestV1 actor_request;
  actor_request.rig_semantic_key = rig_key;
  actor_request.model_semantic_key = model_key;
  actor_request.bind_pose = std::move(bind_pose);
  actor_request.texture_slots = source.texture_slots;
  actor_request.used_texture_slot_count = source.used_texture_slot_count;
  actor_request.texture_bank = assets.textures;
  auto library = compile_rac_actor_library_v1(
      actor_request, make_single_actor_library_limits());

  auto profiles = make_rac_moby_complete_animation_profiles_v1(
      source.source_class, {}, sequence_key_prefix,
      ActorAnimationWrapModeV1::clamp);
  if (initial_source_sequence.has_value() != !initial_animation_key.empty()) {
    fail("A RAC1 Moby initial-animation classification is incomplete");
  }
  if (initial_source_sequence) {
    const auto initial = std::ranges::find_if(
        profiles, [initial_source_sequence](const auto &profile) {
          return profile.source_slot == *initial_source_sequence;
        });
    if (initial == profiles.end()) {
      fail("A RAC1 Moby initial-animation source sequence is unavailable");
    }
    initial->semantic_key = initial_animation_key;
  }
  auto animations = compile_rac_moby_animation_bank_v1(
      source.source_bytes, source.source_class, animation_rig,
      std::string(rig_key), profiles, source_updates_per_second,
      make_moby_animation_compile_limits());
  return CompiledMobyActorV1{std::move(library), std::move(animations)};
}

[[nodiscard]] RacMobyActorSceneCompileLimitsV1
make_moby_actor_scene_compile_limits() {
  return RacMobyActorSceneCompileLimitsV1{
      65'536U, make_single_actor_library_limits(),
      make_native_entity_scene_io_limits().scene};
}

[[nodiscard]] RacMobyActorSceneCompileProfileV1
make_veldin_moby_749_actor_profile() {
  return RacMobyActorSceneCompileProfileV1{
      kVeldinMoby749SourceClassId, std::string(kVeldinMoby749HighModelKey),
      std::string(kVeldinMoby749ArchetypeKey), ActorAffineTransformV1{}};
}

[[nodiscard]] bool has_static_moby_class(const RacLevelMobyAssetsV1 &assets,
                                         const std::uint32_t class_id) {
  return std::ranges::any_of(
      assets.gameplay.static_mobies,
      [class_id](const RacGameplayMobyInstanceV1 &instance) {
        return instance.class_id == class_id;
      });
}

[[nodiscard]] const RacLevelMobyModelV1 &
require_unique_moby_model(const RacLevelMobyAssetsV1 &assets,
                          const std::uint32_t class_id,
                          const std::string_view semantic_description) {
  const RacLevelMobyModelV1 *result = nullptr;
  for (const auto &model : assets.models) {
    if (model.class_id != class_id) {
      continue;
    }
    if (result != nullptr) {
      fail("The RAC1 level contains more than one retained " +
           std::string(semantic_description) + " model source");
    }
    result = &model;
  }
  if (result == nullptr || result->source_bytes.empty() ||
      result->source_class.input_bytes == 0U ||
      result->source_class.input_bytes !=
          static_cast<std::uint64_t>(result->source_bytes.size()) ||
      result->joint_count == 0U ||
      result->source_class.joint_count != result->joint_count) {
    fail("The RAC1 level has no complete retained " +
         std::string(semantic_description) + " model source");
  }
  return *result;
}

[[nodiscard]] ActorLibraryV1
compile_bolt_actor_library(const RacLevelMobyAssetsV1 &assets) {
  const auto &bolt =
      require_unique_moby_model(assets, kBoltSourceClassId, "Bolt collectible");

  RacActorLibraryCompileRequestV1 request;
  request.rig_semantic_key = kBoltRigKey;
  request.model_semantic_key = kBoltHighModelKey;
  request.bind_pose = compile_rac_moby_bind_pose_geometry_v1(
      bolt.source_bytes, bolt.source_class, RacMobyLodV1::high,
      make_single_moby_bind_pose_limits());
  request.texture_slots = bolt.texture_slots;
  request.used_texture_slot_count = bolt.used_texture_slot_count;
  request.texture_bank = assets.textures;
  return compile_rac_actor_library_v1(request,
                                      make_single_actor_library_limits());
}

[[nodiscard]] RacCollectibleCompileProfileV1
make_bolt_collectible_profile(const RacLevelMobyAssetsV1 &assets) {
  const auto &source =
      require_unique_moby_model(assets, kBoltSourceClassId, "Bolt collectible")
          .source_class;
  const auto model_to_world = source.scale / 1024.0F;
  return RacCollectibleCompileProfileV1{
      kBoltSourceClassId,
      std::string(kBoltHighModelKey),
      std::string(kBoltArchetypeKey),
      std::string(kBoltItemKey),
      {source.bounding_sphere[0U] * model_to_world,
       source.bounding_sphere[1U] * model_to_world,
       source.bounding_sphere[2U] * model_to_world},
      source.bounding_sphere[3U] * model_to_world,
      1U,
  };
}

[[nodiscard]] constexpr DestructibleSceneIoLimitsV1
make_native_destructible_scene_io_limits() {
  return DestructibleSceneIoLimitsV1{
      kMaximumDestructibleScenePayloadBytes,
      DestructibleSceneLimitsV1{
          65'536U,
          65'536U,
          16U,
          256U,
          UINT64_C(16) * 1024U * 1024U,
          UINT32_MAX,
          UINT32_MAX,
          1'000'000.0F,
          1'000'000.0F,
      }};
}

[[nodiscard]] RacCollectibleSceneCompileLimitsV1
make_collectible_scene_compile_limits(
    const RenderSceneLimitsV1 render_scene_limits) {
  return RacCollectibleSceneCompileLimitsV1{
      65'536U,
      make_single_actor_library_limits(),
      ActorPoseLimitsV1{255U, 1'000'000U, 1.0e-8, 1.0e-8},
      render_scene_limits,
      make_native_entity_scene_io_limits().scene,
      make_native_gameplay_scene_io_limits().scene,
  };
}

[[nodiscard]] const RacLevelMobyModelV1 &
require_unique_static_moby_model(const RacLevelMobyAssetsV1 &assets,
                                 const std::uint32_t class_id,
                                 const std::string_view description) {
  const RacLevelMobyModelV1 *result = nullptr;
  for (const auto &model : assets.models) {
    if (model.class_id != class_id) {
      continue;
    }
    if (result != nullptr) {
      fail("The RAC1 level contains more than one retained " +
           std::string(description) + " model source");
    }
    result = &model;
  }
  if (result == nullptr || result->source_bytes.empty() ||
      result->source_class.input_bytes != result->source_bytes.size() ||
      result->joint_count != 0U || result->source_class.joint_count != 0U ||
      result->high_lod.vertices.empty() ||
      result->high_lod.triangles.empty()) {
    fail("The RAC1 level has no complete retained static " +
         std::string(description) + " model source");
  }
  return *result;
}

[[nodiscard]] RacDestructibleCompileProfileV1
make_bolt_crate_destructible_profile() {
  return RacDestructibleCompileProfileV1{
      kBoltCrateSourceClassId,
      std::string(kBoltCrateArchetypeKey),
      1U,
      game::kDamageChannelMeleeV1 | game::kDamageChannelProjectileV1 |
          game::kDamageChannelExplosiveV1,
      {DestructibleDropV1{std::string(kBoltItemKey), 1U, 0U}},
  };
}

[[nodiscard]] RacDestructibleSceneCompileLimitsV1
make_destructible_scene_compile_limits(
    const RenderSceneLimitsV1 render_scene_limits) {
  return RacDestructibleSceneCompileLimitsV1{
      65'536U,
      1'000'000U,
      1'000'000U,
      render_scene_limits,
      make_native_entity_scene_io_limits().scene,
      make_native_destructible_scene_io_limits().scene,
  };
}

[[nodiscard]] EntitySceneV1
make_player_entity_scene(const std::uint32_t level_id) {
  EntitySceneV1 scene;
  scene.level_id = level_id;
  scene.definitions.push_back(EntityDefinitionV1{
      0U, std::string(kPlayerArchetypeKey),
      kEntityDefinitionInitiallyEnabledV1,
      kEntitySceneNoAuthoringGroupIdV1});
  scene.actor_bindings.push_back(EntityActorBindingV1{
      0U, std::string(kPlayerHighModelKey), ActorAffineTransformV1{}});
  scene.player_bindings.push_back(PlayerEntityBindingV1{0U, 0U});
  return canonicalize_entity_scene_v1(
      std::move(scene), make_native_entity_scene_io_limits().scene);
}

[[nodiscard]] bool
exact_provenance(const LevelPackageProvenanceV1 &actual,
                 const LevelPackageProvenanceKindV1 kind,
                 const std::string_view source_locator,
                 const std::uint64_t source_bytes,
                 const PreparedContentDigestV1 &source_sha256) {
  return actual.kind == kind && actual.source_locator == source_locator &&
         actual.source_offset == 0U && actual.source_bytes == source_bytes &&
         actual.source_sha256 == source_sha256;
}

[[nodiscard]] const LevelPackageResourceV1 *
find_unique_resource(const LevelPackageV1 &package,
                     const std::string_view resource_id) {
  const LevelPackageResourceV1 *result = nullptr;
  for (const auto &resource : package.resources) {
    if (resource.resource_id != resource_id) {
      continue;
    }
    if (result != nullptr) {
      return nullptr;
    }
    result = &resource;
  }
  return result;
}

template <typename Item>
[[nodiscard]] const Item *
find_authored_record(const std::vector<Item> &items,
                     const std::uint32_t authored_id) {
  const auto found = std::lower_bound(
      items.begin(), items.end(), authored_id,
      [](const Item &candidate, const std::uint32_t id) {
        return candidate.authored_id < id;
      });
  return found != items.end() && found->authored_id == authored_id
             ? &*found
             : nullptr;
}

[[nodiscard]] bool approximately_equal_native_transform(
    const float actual, const double expected) noexcept {
  constexpr double kToleranceFactor =
      64.0 * static_cast<double>(std::numeric_limits<float>::epsilon());
  const auto scale = std::max(
      {1.0, std::abs(static_cast<double>(actual)), std::abs(expected)});
  return std::abs(static_cast<double>(actual) - expected) <=
         kToleranceFactor * scale;
}

[[nodiscard]] bool native_render_transform_matches_entity(
    const RenderSceneAffine3x4V1 &render,
    const game::WorldTransformV1 &entity) noexcept {
  const auto x = static_cast<double>(entity.rotation[0U]);
  const auto y = static_cast<double>(entity.rotation[1U]);
  const auto z = static_cast<double>(entity.rotation[2U]);
  const auto w = static_cast<double>(entity.rotation[3U]);
  const auto sx = static_cast<double>(entity.scale[0U]);
  const auto sy = static_cast<double>(entity.scale[1U]);
  const auto sz = static_cast<double>(entity.scale[2U]);
  const std::array<double, 12U> expected{
      (1.0 - 2.0 * (y * y + z * z)) * sx,
      2.0 * (x * y - z * w) * sy,
      2.0 * (x * z + y * w) * sz,
      static_cast<double>(entity.position[0U]),
      2.0 * (x * y + z * w) * sx,
      (1.0 - 2.0 * (x * x + z * z)) * sy,
      2.0 * (y * z - x * w) * sz,
      static_cast<double>(entity.position[1U]),
      2.0 * (x * z - y * w) * sx,
      2.0 * (y * z + x * w) * sy,
      (1.0 - 2.0 * (x * x + y * y)) * sz,
      static_cast<double>(entity.position[2U]),
  };
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    if (!approximately_equal_native_transform(render.values[index],
                                              expected[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool native_crate_mesh_fits_hit_sphere(
    const RenderSceneMeshV1 &mesh,
    const DestructibleDefinitionV1 &destructible) noexcept {
  if (mesh.vertices.empty()) {
    return false;
  }
  const auto center_x =
      static_cast<double>(destructible.local_hit_center[0U]);
  const auto center_y =
      static_cast<double>(destructible.local_hit_center[1U]);
  const auto center_z =
      static_cast<double>(destructible.local_hit_center[2U]);
  const auto radius = static_cast<double>(destructible.hit_radius);
  constexpr double kToleranceFactor =
      64.0 * static_cast<double>(std::numeric_limits<float>::epsilon());
  for (const auto &vertex : mesh.vertices) {
    const auto vertex_x = static_cast<double>(vertex.x);
    const auto vertex_y = static_cast<double>(vertex.y);
    const auto vertex_z = static_cast<double>(vertex.z);
    const auto scale =
        std::max({1.0, std::abs(center_x), std::abs(center_y),
                  std::abs(center_z), std::abs(vertex_x), std::abs(vertex_y),
                  std::abs(vertex_z), radius});
    const auto slack = kToleranceFactor * scale;
    if (std::hypot(vertex_x - center_x, vertex_y - center_y,
                   vertex_z - center_z) >
        radius + slack) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool exact_upsert_resource_contract(
    const LevelPackageResourceV1 *const resource,
    const std::string_view type_id, const std::uint32_t schema_version) {
  return resource != nullptr && resource->type_id == type_id &&
         resource->schema_version == schema_version &&
         resource->operation == LevelPackageResourceOperationV1::upsert &&
         resource->flags == kLevelPackageResourceOverlayReplaceableV1 &&
         !resource->payload.empty() &&
         !is_zero_prepared_digest_v1(resource->payload_sha256);
}

[[nodiscard]] bool exact_foundation_source_provenance(
    const LevelPackageProvenanceV1 &actual,
    const std::string_view source_locator) {
  return actual.kind == LevelPackageProvenanceKindV1::prepared_resource &&
         actual.source_locator == source_locator &&
         actual.source_offset == 0U && actual.source_bytes != 0U &&
         !is_zero_prepared_digest_v1(actual.source_sha256);
}

[[nodiscard]] bool exact_foundation_resource_provenance(
    const LevelPackageV1 &package, const std::string_view resource_id,
    const std::string_view type_id, const std::uint32_t schema_version,
    const std::string_view source_locator,
    const std::string_view compiler_pass) {
  const auto *const resource = find_unique_resource(package, resource_id);
  if (!exact_upsert_resource_contract(resource, type_id, schema_version) ||
      resource->provenance.size() != 2U) {
    return false;
  }

  bool found_source = false;
  bool found_compiler = false;
  for (const auto &provenance : resource->provenance) {
    found_source |=
        exact_foundation_source_provenance(provenance, source_locator);
    found_compiler |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::generated, compiler_pass, 0U,
        PreparedContentDigestV1{});
  }
  return found_source && found_compiler;
}

[[nodiscard]] bool exact_foundation_provenance(
    const LevelPackageV1 &package, const std::uint32_t level_id) {
  return exact_foundation_resource_provenance(
             package, kCollisionWorldResourceIdV1,
             kCollisionWorldResourceTypeIdV1,
             kCollisionWorldResourceSchemaVersionV1,
             level_source_locator(level_id, "core/collision"),
             kRacCollisionWorldCompilePassV1) &&
         exact_foundation_resource_provenance(
             package, kLevelBootstrapResourceIdV1,
             kLevelBootstrapResourceTypeIdV1,
             kLevelBootstrapResourceSchemaVersionV1,
             level_source_locator(level_id, "gameplay"),
             kRacLevelBootstrapCompilePassV1);
}

[[nodiscard]] bool
exact_actor_provenance(const LevelPackageV1 &package,
                       const std::uint32_t level_id,
                       const std::uint64_t source_image_bytes,
                       const PreparedContentDigestV1 &source_image_sha256) {
  const auto *const resource =
      find_unique_resource(package, kActorLibraryResourceIdV1);
  if (!exact_upsert_resource_contract(
          resource, kActorLibraryResourceTypeIdV1,
          kActorLibraryResourceSchemaVersionV1) ||
      resource->provenance.size() != 2U) {
    return false;
  }

  bool found_image = false;
  bool found_compiler = false;
  for (const auto &provenance : resource->provenance) {
    found_image |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::iso_range,
        "rac1/disc-image", source_image_bytes, source_image_sha256);
    found_compiler |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::generated,
        kLevelActorLibraryCompilePassV1, 0U, PreparedContentDigestV1{});
  }
  if (!found_image || !found_compiler) {
    return false;
  }

  try {
    const auto library =
        decode_actor_library_v1(resource->payload, make_actor_io_limits());
    const auto expected_actor_count = level_id == kVeldinLevelId ? 2U : 1U;
    if (library.rigs.size() != expected_actor_count ||
        library.models.size() != expected_actor_count) {
      return false;
    }
    const auto &rig = library.rigs.front();
    const auto &model = library.models.front();
    if (rig.id != 0U || rig.semantic_key != kPlayerRigKey ||
        rig.rig.joints.empty() || model.id != 0U ||
        model.semantic_key != kPlayerHighModelKey ||
        model.rig_key != kPlayerRigKey || model.meshes.size() != 1U ||
        model.meshes.front().id != 0U ||
        model.meshes.front().vertices.empty() ||
        model.meshes.front().triangle_indices.empty() ||
        model.meshes.front().draw_ranges.empty() || model.materials.empty()) {
      return false;
    }
    if (level_id != kVeldinLevelId) {
      return true;
    }
    const auto &moby_rig = library.rigs[1U];
    const auto &moby_model = library.models[1U];
    return moby_rig.id == 1U &&
           moby_rig.semantic_key == kVeldinMoby749RigKey &&
           moby_rig.rig.joints.size() == 53U && moby_model.id == 1U &&
           moby_model.semantic_key == kVeldinMoby749HighModelKey &&
           moby_model.rig_key == kVeldinMoby749RigKey &&
           moby_model.meshes.size() == 1U &&
           moby_model.meshes.front().id == 0U &&
           !moby_model.meshes.front().vertices.empty() &&
           !moby_model.meshes.front().triangle_indices.empty() &&
           !moby_model.meshes.front().draw_ranges.empty() &&
           !moby_model.materials.empty();
  } catch (const ActorLibraryIoError &) {
    return false;
  } catch (const ActorLibraryError &) {
    return false;
  }
}

[[nodiscard]] bool exact_actor_animation_provenance(
    const LevelPackageV1 &package, const std::uint64_t source_image_bytes,
    const PreparedContentDigestV1 &source_image_sha256) {
  const auto *const animation_resource =
      find_unique_resource(package, kActorAnimationResourceIdV1);
  const auto *const actor_resource =
      find_unique_resource(package, kActorLibraryResourceIdV1);
  if (actor_resource == nullptr ||
      !exact_upsert_resource_contract(
          animation_resource, kActorAnimationResourceTypeIdV1,
          kActorAnimationResourceSchemaVersionV1) ||
      animation_resource->provenance.size() != 2U) {
    return false;
  }

  bool found_image = false;
  bool found_compiler = false;
  for (const auto &provenance : animation_resource->provenance) {
    found_image |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::iso_range,
        "rac1/disc-image", source_image_bytes, source_image_sha256);
    found_compiler |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::generated,
        kLevelActorAnimationCompilePassV1, 0U, PreparedContentDigestV1{});
  }
  if (!found_image || !found_compiler) {
    return false;
  }

  try {
    const auto library = decode_actor_library_v1(
        actor_resource->payload, make_actor_io_limits());
    const auto animations = decode_actor_animation_bank_v1(
        animation_resource->payload, make_actor_animation_io_limits());
    if (package.level_id >= kPlayerAnimationClipCountsByLevel.size()) {
      return false;
    }
    const auto player_clip_count =
        static_cast<std::size_t>(
            kPlayerAnimationClipCountsByLevel[package.level_id]);
    const auto moby_clip_count =
        package.level_id == kVeldinLevelId
            ? kVeldinMoby749FrameCounts.size()
            : 0U;
    if (library.rigs.size() != (moby_clip_count == 0U ? 1U : 2U) ||
        animations.clips.size() != player_clip_count + moby_clip_count) {
      return false;
    }
    const auto &rig = library.rigs.front();
    const auto rig_digest = actor_rig_content_sha256_v1(rig.rig);
    const std::array expected_keys{kPlayerIdleAnimationKey,
                                   kPlayerWalkAnimationKey,
                                   kPlayerRunAnimationKey};
    std::array<bool, kRacLevelCoreRatchetSequenceCountV1> seen_slots{};
    std::optional<std::uint32_t> previous_unclassified_slot;
    for (std::size_t index = 0U; index < player_clip_count; ++index) {
      const auto &clip = animations.clips[index];
      const auto prefix_bytes = kPlayerSourceSequenceKeyPrefix.size();
      if (clip.semantic_key.size() != prefix_bytes + 3U ||
          clip.semantic_key.compare(0U, prefix_bytes,
                                    kPlayerSourceSequenceKeyPrefix) != 0) {
        return false;
      }
      std::uint32_t source_slot = 0U;
      for (std::size_t digit = 0U; digit < 3U; ++digit) {
        const auto value = clip.semantic_key[prefix_bytes + digit];
        if (value < '0' || value > '9') {
          return false;
        }
        source_slot = source_slot * 10U +
                      static_cast<std::uint32_t>(value - '0');
      }
      if (source_slot >= kRacLevelCoreRatchetSequenceCountV1 ||
          seen_slots[source_slot]) {
        return false;
      }
      seen_slots[source_slot] = true;

      const auto is_preview_sequence = index < expected_keys.size();
      if ((is_preview_sequence && clip.semantic_key != expected_keys[index]) ||
          (!is_preview_sequence &&
           (source_slot == 0U || source_slot == 3U || source_slot == 4U ||
            (previous_unclassified_slot &&
             source_slot <= *previous_unclassified_slot))) ||
          clip.id != index || clip.rig_key != kPlayerRigKey ||
          clip.rig_content_sha256 != rig_digest ||
          clip.source_updates_per_second !=
              kPlayerAnimationSourceUpdatesPerSecond ||
          clip.wrap_mode != (is_preview_sequence
                                 ? ActorAnimationWrapModeV1::loop
                                 : ActorAnimationWrapModeV1::clamp) ||
          clip.frames.empty()) {
        return false;
      }
      if (!is_preview_sequence) {
        previous_unclassified_slot = source_slot;
      }
      for (const auto &frame : clip.frames) {
        if (frame.joint_poses.size() != rig.rig.joints.size()) {
          return false;
        }
      }
    }
    if (moby_clip_count != 0U) {
      const auto &moby_rig = library.rigs[1U];
      const auto moby_rig_digest =
          actor_rig_content_sha256_v1(moby_rig.rig);
      for (std::size_t slot = 0U; slot < moby_clip_count; ++slot) {
        const auto clip_index = player_clip_count + slot;
        const auto &clip = animations.clips[clip_index];
        const auto prefix_bytes =
            kVeldinMoby749SourceSequenceKeyPrefix.size();
        const auto is_initial =
            slot == kVeldinMoby749InitialSourceSequence;
        const auto is_source_addressed =
            clip.semantic_key.size() == prefix_bytes + 3U &&
            clip.semantic_key.compare(
                0U, prefix_bytes,
                kVeldinMoby749SourceSequenceKeyPrefix) == 0 &&
            clip.semantic_key[prefix_bytes] == '0' &&
            clip.semantic_key[prefix_bytes + 1U] == '0' &&
            clip.semantic_key[prefix_bytes + 2U] ==
                static_cast<char>('0' + slot);
        if ((is_initial
                 ? clip.semantic_key != kVeldinMoby749InitialAnimationKey
                 : !is_source_addressed) ||
            clip.id != clip_index ||
            clip.rig_key != kVeldinMoby749RigKey ||
            clip.rig_content_sha256 != moby_rig_digest ||
            clip.source_updates_per_second !=
                kVeldinMoby749AnimationSourceUpdatesPerSecond ||
            clip.wrap_mode != ActorAnimationWrapModeV1::clamp ||
            clip.frames.size() != kVeldinMoby749FrameCounts[slot]) {
          return false;
        }
        for (const auto &frame : clip.frames) {
          if (frame.joint_poses.size() != moby_rig.rig.joints.size()) {
            return false;
          }
        }
      }
    }
    return true;
  } catch (const ActorAnimationIoError &) {
    return false;
  } catch (const ActorAnimationError &) {
    return false;
  } catch (const ActorLibraryIoError &) {
    return false;
  } catch (const ActorLibraryError &) {
    return false;
  }
}

[[nodiscard]] bool exact_entity_provenance(
    const LevelPackageV1 &package, const std::uint32_t level_id) {
  const auto *const actor_resource =
      find_unique_resource(package, kActorLibraryResourceIdV1);
  const auto *const render_resource =
      find_unique_resource(package, kRenderSceneResourceIdV1);
  const auto *const entity_resource =
      find_unique_resource(package, kEntitySceneResourceIdV1);
  if (actor_resource == nullptr || render_resource == nullptr ||
      !exact_upsert_resource_contract(
          entity_resource, kEntitySceneResourceTypeIdV1,
          kEntitySceneResourceSchemaVersionV1) ||
      entity_resource->provenance.size() != 3U) {
    return false;
  }

  bool found_actor = false;
  bool found_render = false;
  bool found_compiler = false;
  for (const auto &provenance : entity_resource->provenance) {
    found_actor |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::prepared_resource,
        kActorLibraryResourceIdV1,
        static_cast<std::uint64_t>(actor_resource->payload.size()),
        actor_resource->payload_sha256);
    found_render |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::prepared_resource,
        kRenderSceneResourceIdV1,
        static_cast<std::uint64_t>(render_resource->payload.size()),
        render_resource->payload_sha256);
    found_compiler |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::generated,
        kLevelEntitySceneCompilePassV1, 0U, PreparedContentDigestV1{});
  }
  if (!found_actor || !found_render || !found_compiler) {
    return false;
  }

  try {
    const auto scene = decode_entity_scene_v1(
        entity_resource->payload, make_native_entity_scene_io_limits());
    const auto player = make_player_entity_scene(level_id);
    const auto expected_moby_actor_count =
        level_id == kVeldinLevelId ? kVeldinMoby749PlacementCount : 0U;
    if (scene.level_id != level_id || scene.definitions.empty() ||
        scene.definitions.front() != player.definitions.front() ||
        scene.actor_bindings.size() != 1U + expected_moby_actor_count ||
        scene.actor_bindings.front() != player.actor_bindings.front() ||
        scene.player_bindings != player.player_bindings ||
        scene.transforms.size() + 1U != scene.definitions.size() ||
        scene.render_bindings.size() + expected_moby_actor_count !=
            scene.transforms.size()) {
      return false;
    }
    std::uint32_t found_moby_actor_count = 0U;
    for (std::size_t index = 1U; index < scene.definitions.size(); ++index) {
      const auto &definition = scene.definitions[index];
      const auto *const transform =
          find_authored_record(scene.transforms, definition.authored_id);
      const auto *const render =
          find_authored_record(scene.render_bindings, definition.authored_id);
      const auto *const actor =
          find_authored_record(scene.actor_bindings, definition.authored_id);
      if (definition.authored_id == 0U ||
          definition.flags != kEntityDefinitionInitiallyEnabledV1 ||
          transform == nullptr) {
        return false;
      }
      if (definition.archetype_key == kVeldinMoby749ArchetypeKey) {
        if (level_id != kVeldinLevelId || render != nullptr ||
            actor == nullptr ||
            actor->model_key != kVeldinMoby749HighModelKey ||
            definition.authored_id < kVeldinMoby749PlacementBegin ||
            definition.authored_id >=
                kVeldinMoby749PlacementBegin +
                    kVeldinMoby749PlacementCount) {
          return false;
        }
        ++found_moby_actor_count;
      } else if ((definition.archetype_key == kBoltArchetypeKey ||
                  definition.archetype_key == kBoltCrateArchetypeKey) &&
                 render != nullptr && actor == nullptr) {
        continue;
      } else {
        return false;
      }
    }
    return found_moby_actor_count == expected_moby_actor_count;
  } catch (const EntitySceneIoError &) {
    return false;
  } catch (const EntitySceneError &) {
    return false;
  }
}

[[nodiscard]] bool exact_gameplay_provenance(
    const LevelPackageV1 &package, const std::uint32_t level_id) {
  const auto *const entity_resource =
      find_unique_resource(package, kEntitySceneResourceIdV1);
  const auto *const gameplay_resource =
      find_unique_resource(package, kGameplaySceneResourceIdV1);
  if (entity_resource == nullptr ||
      !exact_upsert_resource_contract(
          gameplay_resource, kGameplaySceneResourceTypeIdV1,
          kGameplaySceneResourceSchemaVersionV1) ||
      gameplay_resource->provenance.size() != 2U) {
    return false;
  }

  bool found_entity = false;
  bool found_compiler = false;
  for (const auto &provenance : gameplay_resource->provenance) {
    found_entity |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::prepared_resource,
        kEntitySceneResourceIdV1,
        static_cast<std::uint64_t>(entity_resource->payload.size()),
        entity_resource->payload_sha256);
    found_compiler |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::generated,
        kLevelGameplaySceneCompilePassV1, 0U, PreparedContentDigestV1{});
  }
  if (!found_entity || !found_compiler) {
    return false;
  }

  try {
    const auto gameplay = decode_gameplay_scene_v1(
        gameplay_resource->payload, make_native_gameplay_scene_io_limits());
    const auto entities = decode_entity_scene_v1(
        entity_resource->payload, make_native_entity_scene_io_limits());
    if (gameplay.level_id != level_id || entities.level_id != level_id) {
      return false;
    }
    std::size_t bolt_definition_count = 0U;
    for (const auto &definition : entities.definitions) {
      if (definition.archetype_key == kBoltArchetypeKey) {
        ++bolt_definition_count;
      }
    }
    if (bolt_definition_count != gameplay.collectibles.size()) {
      return false;
    }
    for (const auto &collectible : gameplay.collectibles) {
      const auto definition = std::lower_bound(
          entities.definitions.begin(), entities.definitions.end(),
          collectible.authored_id,
          [](const EntityDefinitionV1 &candidate, const std::uint32_t id) {
            return candidate.authored_id < id;
          });
      if (definition == entities.definitions.end() ||
          definition->authored_id != collectible.authored_id ||
          definition->archetype_key != kBoltArchetypeKey ||
          collectible.item_key != kBoltItemKey || collectible.amount != 1U ||
          collectible.flags != 0U) {
        return false;
      }
    }
    return true;
  } catch (const GameplaySceneIoError &) {
    return false;
  } catch (const GameplaySceneError &) {
    return false;
  } catch (const EntitySceneIoError &) {
    return false;
  } catch (const EntitySceneError &) {
    return false;
  }
}

[[nodiscard]] bool exact_destructible_provenance(
    const LevelPackageV1 &package, const std::uint32_t level_id,
    const std::uint64_t source_image_bytes,
    const PreparedContentDigestV1 &source_image_sha256) {
  const auto *const entity_resource =
      find_unique_resource(package, kEntitySceneResourceIdV1);
  const auto *const render_resource =
      find_unique_resource(package, kRenderSceneResourceIdV1);
  const auto *const destructible_resource =
      find_unique_resource(package, kDestructibleSceneResourceIdV1);
  if (entity_resource == nullptr || render_resource == nullptr ||
      !exact_upsert_resource_contract(
          destructible_resource, kDestructibleSceneResourceTypeIdV1,
          kDestructibleSceneResourceSchemaVersionV1) ||
      destructible_resource->provenance.size() != 3U) {
    return false;
  }

  bool found_entity = false;
  bool found_image = false;
  bool found_compiler = false;
  for (const auto &provenance : destructible_resource->provenance) {
    found_entity |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::prepared_resource,
        kEntitySceneResourceIdV1,
        static_cast<std::uint64_t>(entity_resource->payload.size()),
        entity_resource->payload_sha256);
    found_image |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::iso_range,
        "rac1/disc-image", source_image_bytes, source_image_sha256);
    found_compiler |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::generated,
        kLevelDestructibleSceneCompilePassV1, 0U,
        PreparedContentDigestV1{});
  }
  if (!found_entity || !found_image || !found_compiler) {
    return false;
  }

  try {
    const auto destructibles = decode_destructible_scene_v1(
        destructible_resource->payload,
        make_native_destructible_scene_io_limits());
    const auto entities = decode_entity_scene_v1(
        entity_resource->payload, make_native_entity_scene_io_limits());
    const auto render = decode_render_scene_v1(
        render_resource->payload, make_render_scene_io_limits());
    if (destructibles.level_id != level_id || entities.level_id != level_id) {
      return false;
    }
    std::size_t crate_definition_count = 0U;
    for (const auto &definition : entities.definitions) {
      if (definition.archetype_key == kBoltCrateArchetypeKey) {
        ++crate_definition_count;
      }
    }
    if (destructibles.destructibles.empty() ||
        crate_definition_count != destructibles.destructibles.size()) {
      return false;
    }
    const auto accepted_channels =
        game::kDamageChannelMeleeV1 | game::kDamageChannelProjectileV1 |
        game::kDamageChannelExplosiveV1;
    const DestructibleDefinitionV1 *shared_hit_sphere = nullptr;
    std::optional<std::uint32_t> shared_mesh_id;
    std::vector<bool> claimed_render_instances(render.instances.size(), false);
    for (const auto &destructible : destructibles.destructibles) {
      const auto *const definition = find_authored_record(
          entities.definitions, destructible.authored_id);
      const auto *const transform =
          find_authored_record(entities.transforms, destructible.authored_id);
      const auto *const binding = find_authored_record(
          entities.render_bindings, destructible.authored_id);
      if (definition == nullptr || transform == nullptr || binding == nullptr ||
          definition->archetype_key != kBoltCrateArchetypeKey ||
          destructible.max_health != 1U ||
          destructible.accepted_damage_channels != accepted_channels ||
          destructible.flags != 0U || destructible.drops.size() != 1U ||
          destructible.drops.front() !=
              DestructibleDropV1{std::string(kBoltItemKey), 1U, 0U}) {
        return false;
      }
      if (binding->render_instance_id >= render.instances.size() ||
          claimed_render_instances[binding->render_instance_id]) {
        return false;
      }
      claimed_render_instances[binding->render_instance_id] = true;
      const auto &instance = render.instances[binding->render_instance_id];
      if (instance.id != binding->render_instance_id ||
          instance.mesh_id >= render.meshes.size() ||
          !native_render_transform_matches_entity(instance.local_to_world,
                                                  transform->transform) ||
          !(transform->transform.scale[0U] > 0.0F) ||
          transform->transform.scale[0U] != transform->transform.scale[1U] ||
          transform->transform.scale[0U] != transform->transform.scale[2U]) {
        return false;
      }
      if (!shared_mesh_id) {
        shared_mesh_id = instance.mesh_id;
      } else if (*shared_mesh_id != instance.mesh_id) {
        return false;
      }
      if (shared_hit_sphere == nullptr) {
        shared_hit_sphere = &destructible;
      } else if (shared_hit_sphere->local_hit_center !=
                     destructible.local_hit_center ||
                 shared_hit_sphere->hit_radius != destructible.hit_radius) {
        return false;
      }
    }
    if (!shared_mesh_id || shared_hit_sphere == nullptr ||
        !native_crate_mesh_fits_hit_sphere(render.meshes[*shared_mesh_id],
                                           *shared_hit_sphere)) {
      return false;
    }
    std::size_t shared_mesh_instance_count = 0U;
    for (const auto &instance : render.instances) {
      if (instance.mesh_id == *shared_mesh_id) {
        ++shared_mesh_instance_count;
      }
    }
    if (shared_mesh_instance_count != destructibles.destructibles.size()) {
      return false;
    }
    return true;
  } catch (const DestructibleSceneIoError &) {
    return false;
  } catch (const DestructibleSceneError &) {
    return false;
  } catch (const EntitySceneIoError &) {
    return false;
  } catch (const EntitySceneError &) {
    return false;
  } catch (const RenderSceneIoError &) {
    return false;
  } catch (const RenderSceneError &) {
    return false;
  }
}

[[nodiscard]] bool
exact_render_provenance(const LevelPackageV1 &package,
                        const std::uint64_t source_image_bytes,
                        const PreparedContentDigestV1 &source_image_sha256,
                        const std::uint64_t boot_executable_bytes,
                        const PreparedContentDigestV1 &boot_executable_sha256) {
  const LevelPackageResourceV1 *render_resource = nullptr;
  for (const auto &resource : package.resources) {
    if (resource.resource_id != kRenderSceneResourceIdV1) {
      continue;
    }
    if (render_resource != nullptr) {
      return false;
    }
    render_resource = &resource;
  }
  if (render_resource == nullptr || render_resource->provenance.size() != 3U) {
    return false;
  }
  if (render_resource->type_id != kRenderSceneResourceTypeIdV1 ||
      render_resource->schema_version != kRenderSceneResourceSchemaVersionV1 ||
      render_resource->operation != LevelPackageResourceOperationV1::upsert ||
      render_resource->flags != kLevelPackageResourceOverlayReplaceableV1) {
    return false;
  }

  bool found_image = false;
  bool found_executable = false;
  bool found_compiler = false;
  for (const auto &provenance : render_resource->provenance) {
    found_image |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::iso_range, "rac1/disc-image",
        source_image_bytes, source_image_sha256);
    found_executable |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::prepared_resource,
        "rac1/boot-executable", boot_executable_bytes, boot_executable_sha256);
    found_compiler |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::generated,
        kLevelRenderSceneCompilePassV1, 0U, PreparedContentDigestV1{});
  }
  return found_image && found_executable && found_compiler;
}

[[nodiscard]] bool exact_native_level_resource_profile(
    const LevelPackageV1 &package, const std::uint32_t level_id,
    const std::uint64_t source_image_bytes,
    const PreparedContentDigestV1 &source_image_sha256,
    const std::uint64_t boot_executable_bytes,
    const PreparedContentDigestV1 &boot_executable_sha256) {
  return package.resources.size() == 8U &&
         exact_foundation_provenance(package, level_id) &&
         exact_render_provenance(
             package, source_image_bytes, source_image_sha256,
             boot_executable_bytes, boot_executable_sha256) &&
         exact_actor_provenance(package, level_id, source_image_bytes,
                                source_image_sha256) &&
         exact_actor_animation_provenance(
             package, source_image_bytes, source_image_sha256) &&
         exact_entity_provenance(package, level_id) &&
         exact_gameplay_provenance(package, level_id) &&
         exact_destructible_provenance(
             package, level_id, source_image_bytes, source_image_sha256);
}

[[nodiscard]] bool exact_mountable_native_level_profile(
    const LevelPackageV1 &package, const std::uint32_t level_id,
    const std::uint64_t source_image_bytes,
    const PreparedContentDigestV1 &source_image_sha256,
    const std::uint64_t boot_executable_bytes,
    const PreparedContentDigestV1 &boot_executable_sha256,
    const PreparedGameV2FilesystemLimitsV1 limits) {
  if (!exact_native_level_resource_profile(
          package, level_id, source_image_bytes, source_image_sha256,
          boot_executable_bytes, boot_executable_sha256)) {
    return false;
  }
  try {
    const auto resolved = resolve_level_package_v1(
        package, std::span<const LevelPackageV1>{}, limits.level_package);
    static_cast<void>(game::load_runtime_level_content_v1(
        resolved, game::make_runtime_level_content_limits_v1()));
    return true;
  } catch (const LevelPackageV1Error &) {
    return false;
  } catch (const game::RuntimeLevelContentError &) {
    return false;
  }
}

struct NativeBootArtifactIdentityV1 {
  std::uint64_t bytes = 0U;
  PreparedContentDigestV1 sha256{};
};

[[nodiscard]] NativeBootArtifactIdentityV1
native_boot_artifact_identity(const LevelPackageV1 &package) {
  const auto *const render =
      find_unique_resource(package, kRenderSceneResourceIdV1);
  if (render == nullptr) {
    fail("The current native publication has no render-scene resource");
  }

  const LevelPackageProvenanceV1 *identity = nullptr;
  for (const auto &provenance : render->provenance) {
    if (provenance.kind !=
            LevelPackageProvenanceKindV1::prepared_resource ||
        provenance.source_locator != "rac1/boot-executable") {
      continue;
    }
    if (identity != nullptr || provenance.source_offset != 0U ||
        provenance.source_bytes == 0U ||
        is_zero_prepared_digest_v1(provenance.source_sha256)) {
      fail("The current native publication has an ambiguous boot-executable "
           "identity");
    }
    identity = &provenance;
  }
  if (identity == nullptr) {
    fail("The current native publication has no boot-executable identity");
  }
  return NativeBootArtifactIdentityV1{identity->source_bytes,
                                      identity->source_sha256};
}

[[nodiscard]] LevelPackageProvenanceV1 prepared_resource_provenance(
    const LevelPackageV1 &package, const std::string_view resource_id,
    const std::string_view type_id, const std::uint32_t schema_version,
    const std::string_view use_description) {
  const auto *const resource =
      find_unique_resource(package, resource_id);
  if (!exact_upsert_resource_contract(resource, type_id, schema_version) ||
      resource->payload_sha256 !=
          prepared_content_sha256_v1(resource->payload)) {
    fail("The compiled " + std::string(resource_id) +
         " resource cannot serve as " + std::string(use_description) +
         " provenance");
  }
  return LevelPackageProvenanceV1{
      LevelPackageProvenanceKindV1::prepared_resource,
      std::string(resource_id),
      0U,
      host_size_to_u64(resource->payload.size(), "A compiled resource payload"),
      resource->payload_sha256};
}

[[nodiscard]] bool exact_native_frontend_profile(
    const LevelPackageV1& package,std::uint64_t source_image_bytes,
    const PreparedContentDigestV1& source_image_sha256,std::uint64_t boot_executable_bytes,
    const PreparedContentDigestV1& boot_executable_sha256) {
  const auto* actors=find_unique_resource(package,"frontend/background/actors");
  const auto* animation=find_unique_resource(package,"frontend/background/animation");
  const auto* timeline=find_unique_resource(package,"frontend/background/timeline");
  const auto* title=find_unique_resource(package,"frontend/title");
  const auto* geometry=find_unique_resource(package,"frontend/background/geometry");
  if(!exact_upsert_resource_contract(actors,"openrc.actor-library",1U)||
      !exact_upsert_resource_contract(animation,"openrc.actor-animation-bank",1U)||
      !exact_upsert_resource_contract(timeline,"openrc.scene-timeline",1U)||
      !exact_upsert_resource_contract(title,"openrc.screen-overlay",1U)||
      !exact_upsert_resource_contract(geometry,"openrc.render-scene",1U)) return false;
  const LevelPackageProvenanceV1* shared_source=nullptr;
  for(const auto* resource:{actors,animation,timeline,title,geometry}) {
    if(resource->provenance.size()!=(resource==timeline?6U:4U)) return false;
    bool image=false,boot=false,source=false,pass=false;
    bool actor_reference=false,animation_reference=false;
    for(const auto& p:resource->provenance) {
      image|=exact_provenance(p,LevelPackageProvenanceKindV1::iso_range,"rac1/disc-image",source_image_bytes,source_image_sha256);
      boot|=exact_provenance(p,LevelPackageProvenanceKindV1::prepared_resource,"rac1/boot-executable",boot_executable_bytes,boot_executable_sha256);
      pass|=exact_provenance(p,LevelPackageProvenanceKindV1::generated,kFrontendScenePass,0U,{});
      actor_reference|=exact_provenance(p,LevelPackageProvenanceKindV1::prepared_resource,actors->resource_id,actors->payload.size(),actors->payload_sha256);
      animation_reference|=exact_provenance(p,LevelPackageProvenanceKindV1::prepared_resource,animation->resource_id,animation->payload.size(),animation->payload_sha256);
      if(p.kind==LevelPackageProvenanceKindV1::iso_range&&p.source_locator=="rac1/global-toc/14e8"&&
          p.source_offset>=UINT64_C(1506)*kDiscTocSectorSize&&p.source_offset%kDiscTocSectorSize==0U&&
          p.source_offset<=source_image_bytes&&p.source_bytes>0U&&p.source_bytes<=kMaximumDecodedWadBytes&&
          p.source_bytes%kDiscTocSectorSize==0U&&p.source_bytes<=source_image_bytes-p.source_offset&&
          !is_zero_prepared_digest_v1(p.source_sha256)) {
        source=true;
        if(shared_source&&(p.source_offset!=shared_source->source_offset||p.source_bytes!=shared_source->source_bytes||
            p.source_sha256!=shared_source->source_sha256)) return false;
        shared_source=&p;
      }
    }
    if(!image||!boot||!source||!pass||(resource==timeline&&(!actor_reference||!animation_reference))) return false;
  }
  try {
    const auto library=decode_actor_library_v1(actors->payload,make_actor_io_limits());
    const auto bank=decode_actor_animation_bank_v1(animation->payload,make_actor_animation_io_limits());
    const auto sequence=decode_scene_timeline_v1(timeline->payload);
    const auto overlay=decode_screen_overlay_v1(title->payload);
    const auto environment=decode_render_scene_v1(geometry->payload,game::make_runtime_level_content_limits_v1().render_scene);
    if(environment.meshes.empty()||environment.instances.empty()) return false;
    if(overlay.canvas_width!=512U||overlay.canvas_height!=448U||overlay.updates_per_second!=50U||
        overlay.coverage_denominator!=128U||overlay.loop_begin!=100U||overlay.images.size()!=192U||overlay.frames.size()!=160U)
      return false;
    if(library.models.size()!=5U||library.rigs.size()!=5U||bank.clips.size()!=75U||
        sequence.actors.size()!=5U||sequence.samples.size()!=1398U||sequence.updates_per_second!=50U||
        !sequence.loop||sequence.display_aspect_numerator!=512U||sequence.display_aspect_denominator!=512U||
        sequence.actor_library_sha256!=actors->payload_sha256||sequence.actor_animation_sha256!=animation->payload_sha256)
      return false;
    for(std::uint32_t i=0;i<5U;++i) {
      const auto key="frontend/background/actor/"+std::to_string(i);
      const auto& binding=sequence.actors[i];
      if(binding.rig_index>=library.rigs.size()||binding.model_index>=library.models.size()||
          library.rigs[binding.rig_index].semantic_key!=key+"/rig"||
          library.models[binding.model_index].semantic_key!=key+"/model") return false;
    }
    for(std::uint32_t i=0;i<75U;++i) {
      const auto& clip=bank.clips[i];
      if(clip.semantic_key!="frontend/background/chunk/"+std::to_string(i/5U)+"/actor/"+std::to_string(i%5U)||
          clip.source_updates_per_second!=50U||clip.wrap_mode!=ActorAnimationWrapModeV1::clamp||
          clip.frames.size()!=(i<70U?49U:29U)) return false;
    }
    validate_scene_timeline_bindings_v1(sequence,library,bank);
    return true;
  } catch(const ActorLibraryIoError&) {return false;}
    catch(const ActorAnimationIoError&) {return false;}
    catch(const SceneTimelineError&) {return false;}
    catch(const ScreenOverlayError&) {return false;}
    catch(const RenderSceneIoError&) {return false;}
}

[[nodiscard]] bool exact_native_startup_media_profile(
    const LevelPackageV1 &package, const std::uint64_t source_image_bytes,
    const PreparedContentDigestV1 &source_image_sha256,
    const std::uint64_t boot_executable_bytes,
    const PreparedContentDigestV1 &boot_executable_sha256) {
  const auto *const resource =
      find_unique_resource(package, kNativeGameStartupIntroResourceIdV1);
  if (package.level_id != kPreparedGameSharedPackageIdV2 ||
      package.resources.size() != 58U ||
      !exact_upsert_resource_contract(resource, kStartupMediaType, 1U) ||
      resource->provenance.size() != 4U) {
    return false;
  }
  bool found_image = false;
  bool found_boot = false;
  bool found_movie = false;
  bool found_compiler = false;
  for (const auto &provenance : resource->provenance) {
    found_image |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::iso_range, "rac1/disc-image",
        source_image_bytes, source_image_sha256);
    found_boot |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::prepared_resource,
        "rac1/boot-executable", boot_executable_bytes, boot_executable_sha256);
    found_compiler |= exact_provenance(
        provenance, LevelPackageProvenanceKindV1::generated,
        kNativeGameStartupMediaCompilePassV1, 0U, PreparedContentDigestV1{});
    found_movie |=
        provenance.kind == LevelPackageProvenanceKindV1::iso_range &&
        provenance.source_locator == kStartupMovieSource &&
        provenance.source_offset >= UINT64_C(1506) * kDiscTocSectorSize &&
        provenance.source_offset % kDiscTocSectorSize == 0U &&
        provenance.source_offset <= source_image_bytes &&
        provenance.source_bytes > 0U &&
        provenance.source_bytes <= kMaximumStartupMovieBytes &&
        provenance.source_bytes <= source_image_bytes - provenance.source_offset &&
        !is_zero_prepared_digest_v1(provenance.source_sha256);
  }
  if (!found_image || !found_boot || !found_movie || !found_compiler ||
      !exact_native_frontend_profile(package,source_image_bytes,source_image_sha256,
          boot_executable_bytes,boot_executable_sha256)||
      !exact_native_menu_flow_profile_v1(package,source_image_bytes,source_image_sha256,
          boot_executable_bytes,boot_executable_sha256)) {
    return false;
  }
  const auto* bitmap=find_unique_resource(package,"startup/post-intro");
  if(!exact_upsert_resource_contract(bitmap,"openrc.image-presentation",1U)||bitmap->provenance.size()!=4U)
    return false;
  bool bitmap_image=false,bitmap_boot=false,bitmap_source=false,bitmap_pass=false;
  for(const auto& p:bitmap->provenance) {
    bitmap_image|=exact_provenance(p,LevelPackageProvenanceKindV1::iso_range,"rac1/disc-image",source_image_bytes,source_image_sha256);
    bitmap_boot|=exact_provenance(p,LevelPackageProvenanceKindV1::prepared_resource,"rac1/boot-executable",boot_executable_bytes,boot_executable_sha256);
    bitmap_pass|=exact_provenance(p,LevelPackageProvenanceKindV1::generated,kStartupImagePass,0U,{});
    bitmap_source|=p.kind==LevelPackageProvenanceKindV1::iso_range&&p.source_locator=="rac1/global-toc/12c0"&&
        p.source_offset>=UINT64_C(1506)*kDiscTocSectorSize&&p.source_offset%kDiscTocSectorSize==0&&
        p.source_offset<=source_image_bytes&&p.source_bytes>0&&p.source_bytes<=kMaximumDecodedWadBytes&&
        p.source_bytes%kDiscTocSectorSize==0&&p.source_bytes<=source_image_bytes-p.source_offset&&!is_zero_prepared_digest_v1(p.source_sha256);
  }
  if(!bitmap_image||!bitmap_boot||!bitmap_source||!bitmap_pass) return false;
  try {
    const auto image=decode_image_presentation_v1(bitmap->payload);
    if(image.width!=512U||image.height!=448U||image.display_aspect_numerator!=1U||image.display_aspect_denominator!=1U||
        image.updates_per_second!=50U||image.transfer_lead_updates!=1U||image.transfer_tail_updates!=1U||
        image.color_transfers.size()!=12U||image.initialization_clock_hz!=15625U||image.initialization_clock_modulus!=65536U||
        image.initialization_credit_divisor!=265U||image.minimum_initialization_updates!=150U) return false;
    for(std::uint32_t step=0;step<12U;++step) {
      const auto remaining=12U-step;
      const auto factor=(remaining-1U)*128U/remaining;
      for(std::uint32_t c=0;c<256;++c)
        if(image.color_transfers[step][c]!=static_cast<std::byte>(c*factor/128U)) return false;
    }
    const auto clip = decode_media_clip_v1(resource->payload);
    // Original PAL entry chooses TOC+1800. Its MPEG picture is 512x416
    // at 25 Hz; 1fac30/1fad10 and 1fb040..d0 stretch it to the logical
    // 512x512 movie raster. This records source raster geometry, without
    // claiming a measurement of the analogue display or physical overscan.
    return clip.width == 512U && clip.height == 416U &&
           clip.frame_rate_numerator == 25U &&
           clip.frame_rate_denominator == 1U &&
           clip.display_aspect_numerator == 1U &&
           clip.display_aspect_denominator == 1U &&
           clip.audio_sample_rate == 44100U && clip.audio_channels == 2U &&
           !clip.audio.empty();
  } catch (const MediaClipError &) {
    return false;
  } catch (const ImagePresentationError &) {
    return false;
  }
}

[[nodiscard]] std::vector<std::byte> compile_native_startup_media_package(
    const NativeGamePreparationRequestV1 &request,
    const std::uint64_t source_image_bytes,
    const PreparedContentDigestV1 &source_image_sha256,
    const std::uint64_t boot_executable_bytes,
    const PreparedContentDigestV1 &boot_executable_sha256,
    const LevelPackageV1Limits package_limits) {
  const auto catalog = read_rac_startup_catalog_v1(
      request.disc_image,
      RacStartupLimitsV1{kMaximumStartupMovieBytes, 2U * kMaximumStartupMovieBytes});
  if (catalog.image_bytes != source_image_bytes || catalog.initial_selector != 1U) {
    fail("The supported PAL startup entry does not select the expected source movie");
  }
  const auto &movie = select_rac_startup_movie_v1(catalog, catalog.initial_selector);
  const auto source = read_rac_startup_movie_v1(
      request.disc_image, movie, kMaximumStartupMovieBytes);
  const auto clip = compile_rac_pss_v1(source, 1U, 1U);
  LevelPackageResourceV1 resource;
  resource.resource_id = kNativeGameStartupIntroResourceIdV1;
  resource.type_id = kStartupMediaType;
  resource.schema_version = 1U;
  resource.flags = kLevelPackageResourceOverlayReplaceableV1;
  resource.provenance = {
      {LevelPackageProvenanceKindV1::iso_range, "rac1/disc-image", 0U,
       source_image_bytes, source_image_sha256},
      {LevelPackageProvenanceKindV1::prepared_resource, "rac1/boot-executable",
       0U, boot_executable_bytes, boot_executable_sha256},
      {LevelPackageProvenanceKindV1::iso_range, std::string(kStartupMovieSource),
       movie.source_byte_offset, static_cast<std::uint64_t>(source.size()),
       prepared_content_sha256_v1(source)},
      {LevelPackageProvenanceKindV1::generated,
       std::string(kNativeGameStartupMediaCompilePassV1), 0U, 0U, {}}};
  resource.payload = encode_media_clip_v1(clip);
  resource.payload_sha256 = prepared_content_sha256_v1(resource.payload);
  LevelPackageV1 package;
  package.level_id = kPreparedGameSharedPackageIdV2;
  package.content_api_version = kOpenRcContentApiVersionV1;
  package.build_id = kNativeGameBuildIdV1;
  package.resources.push_back(std::move(resource));
  const auto bitmap_source=read_rac_startup_wad_v1(request.disc_image,0x12c0U,
      kMaximumDecodedWadBytes,kMaximumDecodedWadBytes);
  const auto bitmap=compile_rac_startup_image_v1(bitmap_source.decoded_bytes,catalog.initial_selector);
  LevelPackageResourceV1 image;
  image.resource_id="startup/post-intro";image.type_id="openrc.image-presentation";image.schema_version=1;
  image.flags=kLevelPackageResourceOverlayReplaceableV1;
  image.provenance={
      {LevelPackageProvenanceKindV1::iso_range,"rac1/disc-image",0U,source_image_bytes,source_image_sha256},
      {LevelPackageProvenanceKindV1::prepared_resource,"rac1/boot-executable",0U,boot_executable_bytes,boot_executable_sha256},
      {LevelPackageProvenanceKindV1::iso_range,"rac1/global-toc/12c0",bitmap_source.source_byte_offset,
        static_cast<std::uint64_t>(bitmap_source.source_bytes.size()),prepared_content_sha256_v1(bitmap_source.source_bytes)},
      {LevelPackageProvenanceKindV1::generated,std::string(kStartupImagePass),0U,0U,{}}};
  image.payload=encode_image_presentation_v1(bitmap);image.payload_sha256=prepared_content_sha256_v1(image.payload);
  package.resources.push_back(std::move(image));
  const auto frontend_source=read_rac_startup_wad_v1(request.disc_image,0x14e8U,
      kMaximumDecodedWadBytes,kMaximumDecodedWadBytes);
  const auto frontend_limits=make_frontend_scene_limits();
  const auto frontend=compile_rac_frontend_scene_v1(frontend_source.decoded_bytes,50U,frontend_limits);
  const auto first=sample_rac_frontend_background_tick_v1(frontend.source_background.decoded_chunks.front(),0U,
      frontend_limits.animation.scene);
  const auto camera=execute_rac_frontend_camera_v1(
      std::bit_cast<std::array<std::uint32_t,3U>>(first.camera.position),
      std::bit_cast<std::array<std::uint32_t,3U>>(first.camera.rotation_xyz_radians));
  const auto timeline=compile_rac_frontend_timeline_v1(frontend,camera.camera,camera.display_width,camera.display_height,
      {make_actor_io_limits(),make_actor_animation_io_limits(),frontend_limits.animation.scene,{}});
  const std::vector<LevelPackageProvenanceV1> frontend_provenance{
      {LevelPackageProvenanceKindV1::iso_range,"rac1/disc-image",0U,source_image_bytes,source_image_sha256},
      {LevelPackageProvenanceKindV1::prepared_resource,"rac1/boot-executable",0U,boot_executable_bytes,boot_executable_sha256},
      {LevelPackageProvenanceKindV1::iso_range,"rac1/global-toc/14e8",frontend_source.source_byte_offset,
        frontend_source.source_bytes.size(),prepared_content_sha256_v1(frontend_source.source_bytes)},
      {LevelPackageProvenanceKindV1::generated,std::string(kFrontendScenePass),0U,0U,{}}};
  const auto append_frontend=[&](std::string id,std::string type,std::vector<std::byte> payload) {
    LevelPackageResourceV1 resource;
    resource.resource_id=std::move(id);resource.type_id=std::move(type);resource.schema_version=1U;
    resource.flags=kLevelPackageResourceOverlayReplaceableV1;resource.provenance=frontend_provenance;
    resource.payload=std::move(payload);resource.payload_sha256=prepared_content_sha256_v1(resource.payload);
    package.resources.push_back(std::move(resource));
  };
  append_frontend("frontend/background/actors","openrc.actor-library",
      encode_actor_library_v1(frontend.actor_library,make_actor_io_limits()));
  append_frontend("frontend/background/animation","openrc.actor-animation-bank",
      encode_actor_animation_bank_v1(frontend.actor_animation,make_actor_animation_io_limits()));
  append_frontend("frontend/background/timeline","openrc.scene-timeline",encode_scene_timeline_v1(timeline));
  for(const auto id:{"frontend/background/actors","frontend/background/animation"}) {
    const auto* resource=find_unique_resource(package,id);
    package.resources.back().provenance.push_back({LevelPackageProvenanceKindV1::prepared_resource,id,0U,
        resource->payload.size(),resource->payload_sha256});
  }
  constexpr auto frontend_bytes=kMaximumDecodedWadBytes;
  const auto title_assets=compile_rac_frontend_title_assets_v1(frontend_source.decoded_bytes,0U,
      {frontend_bytes,1024U,4096U,4096U,16000000U,16000000U,frontend_bytes});
  const auto title=compile_rac_frontend_title_overlay_v1(title_assets,camera.render_width,camera.render_height,50U,0x3f555555U);
  append_frontend("frontend/title","openrc.screen-overlay",encode_screen_overlay_v1(title));
  if(boot_executable_bytes==0U||boot_executable_bytes>kMaximumElfBytes)
    fail("Frontend environment boot input exceeds its bounded envelope");
  std::ifstream boot_file(request.prepared_boot_executable,std::ios::binary);
  std::vector<std::byte> boot_bytes(static_cast<std::size_t>(boot_executable_bytes));
  boot_file.read(reinterpret_cast<char*>(boot_bytes.data()),static_cast<std::streamsize>(boot_bytes.size()));
  if(!boot_file||prepared_content_sha256_v1(boot_bytes)!=boot_executable_sha256)
    fail("Frontend environment boot input changed while preparing");
  const auto environment_assets=decode_rac_frontend_environment_assets_v1(frontend_source.decoded_bytes);
  const auto environment_limits=game::make_runtime_level_content_limits_v1().render_scene;
  auto environment=compile_rac_frontend_sky_shells_v1(environment_assets.sky_source,environment_limits.scene).render_scene;
  auto terrain=compile_rac_frontend_terrain_v1(environment_assets,boot_bytes,environment_limits.scene).render_scene;
  const auto texture_base=static_cast<std::uint32_t>(environment.textures.size());
  const auto material_base=static_cast<std::uint32_t>(environment.materials.size());
  const auto mesh_base=static_cast<std::uint32_t>(environment.meshes.size());
  const auto instance_base=static_cast<std::uint32_t>(environment.instances.size());
  for(auto& t:terrain.textures) {t.id+=texture_base;environment.textures.push_back(std::move(t));}
  for(auto& m:terrain.materials) {m.id+=material_base;if(m.base_color_texture_id)*m.base_color_texture_id+=texture_base;environment.materials.push_back(std::move(m));}
  for(auto& m:terrain.meshes) {m.id+=mesh_base;for(auto& d:m.draw_ranges)d.material_id+=material_base;environment.meshes.push_back(std::move(m));}
  for(auto& i:terrain.instances) {i.id+=instance_base;i.mesh_id+=mesh_base;environment.instances.push_back(i);}
  append_frontend("frontend/background/geometry","openrc.render-scene",encode_render_scene_v1(environment,environment_limits));
  auto menu=compile_rac_frontend_menu_resources_v1(request.disc_image,boot_bytes,frontend_source.decoded_bytes);
  auto new_game=compile_rac_new_game_resources_v1(request.disc_image,boot_bytes,0U,1U,50U);
  auto session=compile_rac_frontend_state_v1(request.disc_image,boot_bytes,{},new_game.sequence_bindings);
  auto audio=compile_rac_frontend_sound_resources_v1(request.disc_image,boot_bytes);
  auto ambient=compile_rac_frontend_ambient_resources_v1(request.disc_image,boot_bytes);
  for(auto& resource:ambient)audio.push_back(std::move(resource));
  const auto append_bound=[&](LevelPackageResourceV1 resource) {
    resource.flags=kLevelPackageResourceOverlayReplaceableV1;
    resource.provenance.insert(resource.provenance.begin(),frontend_provenance.begin(),frontend_provenance.begin()+2);
    package.resources.push_back(std::move(resource));
  };
  for(auto& resource:menu.resources) {
    resource.provenance.push_back(frontend_provenance[2]);
    append_bound(std::move(resource));
  }
  for(auto& resource:new_game.resources) append_bound(std::move(resource));
  for(auto& resource:audio) {
    // This helper already binds the admitted boot executable. Add the full
    // image identity once, retaining its precise bank/module source extents.
    resource.flags=kLevelPackageResourceOverlayReplaceableV1;
    resource.provenance.insert(resource.provenance.begin(),frontend_provenance.front());
    package.resources.push_back(std::move(resource));
  }
  for(auto& resource:session.resources) {
    resource.provenance.push_back({LevelPackageProvenanceKindV1::generated,"openrc.rac-frontend-state-compile.v1",0U,0U,{}});
    append_bound(std::move(resource));
  }
  append_bound(compile_rac_initial_level_installation_v1(request.disc_image,boot_bytes,
      session.initial.schema,session.state_limits));
  const auto reference=[&](std::string_view owner,std::string_view target) {
    const auto* source=find_unique_resource(package,target);
    if(!source)fail("Frontend prepared dependency is absent");
    const LevelPackageProvenanceV1 p{LevelPackageProvenanceKindV1::prepared_resource,
        std::string(target),0U,source->payload.size(),source->payload_sha256};
    const auto found=std::find_if(package.resources.begin(),package.resources.end(),
        [&](const auto& resource){return resource.resource_id==owner;});
    if(found==package.resources.end())fail("Frontend prepared dependency owner is absent");
    found->provenance.push_back(p);
  };
  reference("frontend/menu/timeline","frontend/menu/actors");
  reference("frontend/menu/timeline","frontend/menu/animation");
  reference("frontend/audio/ambient-bank","frontend/audio/ambient-program");
  for(unsigned i=0;i<13;++i)
    reference("frontend/audio/ambient-bank","frontend/audio/ambient-stream-"+std::to_string(i));
  for(unsigned i=0;i<4;++i)
    reference("frontend/audio/ambient-bank","frontend/audio/ambient-gain-"+std::to_string(i));
  reference("frontend/audio/ambient-cues","frontend/audio/ambient-bank");
  reference("frontend/audio/ambient-cues","frontend/background/timeline");
  reference("frontend/no-save-input","frontend/session-state");
  reference("frontend/new-game-sequence","frontend/session-state");
  reference("new-game/level-installation","frontend/session-state");
  for(const auto& resource:session.new_game_continuation.resources)
    reference("frontend/new-game-sequence",resource.resource_id);
  if (!exact_native_startup_media_profile(
          package, source_image_bytes, source_image_sha256,
          boot_executable_bytes, boot_executable_sha256)) {
    fail("The compiled original startup/frontend does not satisfy the native profile");
  }
  auto bytes = encode_level_package_v1(package, package_limits);
  if (bytes.size() > kNativeGameSharedPackageMaxBytesV1) {
    fail("The shared startup package exceeds the native byte budget");
  }
  return bytes;
}

[[nodiscard]] std::optional<PublishedPreparedGameV2V1>
load_matching_publication(const NativeGamePreparationRequestV1 &request,
                          const std::uint64_t source_image_bytes,
                          const PreparedContentDigestV1 &source_image_sha256,
                          const std::uint64_t boot_executable_bytes,
                          const PreparedContentDigestV1 &boot_executable_sha256,
                          const PreparedGameV2FilesystemLimitsV1 limits,
                          const NativeGamePreparationControlV1 &control) {
  std::error_code status_error;
  const auto status =
      std::filesystem::symlink_status(request.destination_root, status_error);
  if (status_error == std::errc::no_such_file_or_directory) {
    return std::nullopt;
  }
  if (status_error) {
    fail("Cannot inspect the existing native publication root: " +
         status_error.message());
  }
  if (!std::filesystem::exists(status)) {
    return std::nullopt;
  }

  PreparedGameV2RootV1 prepared;
  try {
    prepared = load_prepared_game_v2_root_v1(request.destination_root, limits);
  } catch (const PreparedGameV2FilesystemError &error) {
    fail("The existing native installation is damaged or untrusted and "
         "will not be overwritten. Move it aside and retry: " +
         std::string(error.what()));
  }

  const auto &manifest = prepared.manifest;
  if (!manifest.overlays.empty()) {
    fail("The existing native installation references overlays and will not "
         "be replaced automatically");
  }
  bool matches =
      manifest.content_api_version == kOpenRcContentApiVersionV1 &&
      manifest.provenance.game_id == kNativeGameIdV1 &&
      manifest.provenance.build_id == kNativeGameBuildIdV1 &&
      manifest.provenance.compiler_id == kNativeGameCompilerIdV1 &&
      manifest.provenance.compiler_version == kNativeGameCompilerVersionV1 &&
      manifest.provenance.source_image_bytes == source_image_bytes &&
      manifest.provenance.source_image_sha256 == source_image_sha256 &&
      !manifest.provenance.prepared_game_v1_manifest_sha256.has_value() &&
      manifest.levels.size() == kDiscTocLevelCount;

  std::uint64_t total_package_bytes = 0U;
  try {
    if (manifest.shared_package) {
      const auto &shared = *manifest.shared_package;
      const auto package = load_prepared_game_shared_package_v1(prepared, limits);
      matches = matches && shared.package_path == kNativeGameSharedPackagePathV1 &&
                shared.package_bytes <= kNativeGameSharedPackageMaxBytesV1 &&
                exact_native_startup_media_profile(
                    package, source_image_bytes, source_image_sha256,
                    boot_executable_bytes, boot_executable_sha256);
      if (matches) {
        // Offline profile checks cannot prove that a claimed ISO range is
        // the selected movie. Prepare has the source: reproduce this small
        // shared package and compare its complete canonical bytes by digest.
        // This binds TOC selection, offset, size, source hash and media payload,
        // including when someone recomputed all container hashes after editing.
        const auto expected = compile_native_startup_media_package(
            request, source_image_bytes, source_image_sha256,
            boot_executable_bytes, boot_executable_sha256, limits.level_package);
        if (expected.size() != shared.package_bytes ||
            prepared_content_sha256_v1(expected) != shared.package_sha256) {
          fail("The existing startup media does not match the selected source "
               "movie and will not be reused or overwritten");
        }
      }
      total_package_bytes = shared.package_bytes;
    } else {
      matches = false;
    }
    for (std::size_t index = 0U; index < manifest.levels.size(); ++index) {
      const auto &reference = manifest.levels[index];
      report_progress(
          control, NativeGamePreparationPhaseV1::checking_existing_publication,
          static_cast<std::uint32_t>(index),
          static_cast<std::uint32_t>(index));
      if (reference.package_bytes >
          kNativeGamePreparedPackageMaxBytesV1 - total_package_bytes) {
        fail("The existing native installation exceeds its package-byte "
             "budget");
      }
      total_package_bytes += reference.package_bytes;

      const auto package = load_prepared_game_level_package_v1(
          prepared, reference.level_id, limits);
      const auto expected_level_id = static_cast<std::uint32_t>(index);
      const auto package_matches =
          index < kDiscTocLevelCount &&
          reference.level_id == expected_level_id &&
          reference.package_path == level_package_path(expected_level_id) &&
          exact_mountable_native_level_profile(
              package, expected_level_id, source_image_bytes,
              source_image_sha256, boot_executable_bytes,
              boot_executable_sha256, limits);
      matches = matches && package_matches;
    }
  } catch (const NativeGamePreparationCancelledV1 &) {
    throw;
  } catch (const PreparedGameV2FilesystemError &error) {
    fail("The existing native installation contains a damaged level package "
         "and will not be overwritten. Move it aside and retry: " +
         std::string(error.what()));
  } catch (const LevelPackageV1Error &error) {
    fail("The existing native installation contains an invalid level package "
         "and will not be overwritten. Move it aside and retry: " +
         std::string(error.what()));
  }

  if (!matches) {
    return std::nullopt;
  }
  return PublishedPreparedGameV2V1{
      prepared.root, prepared.manifest_sha256,
      static_cast<std::uint32_t>(manifest.levels.size()),
      total_package_bytes};
}

void verify_sources_unchanged(
    const NativeGamePreparationRequestV1 &request,
    const std::uint64_t source_image_bytes_before,
    const PreparedContentDigestV1 &source_image_sha256_before,
    const std::uint64_t boot_executable_bytes_before,
    const PreparedContentDigestV1 &boot_executable_sha256_before) {
  const auto source_image_bytes_after = host_size_to_u64(
      std::filesystem::file_size(request.disc_image), "The source image size");
  const auto boot_executable_bytes_after = host_size_to_u64(
      std::filesystem::file_size(request.prepared_boot_executable),
      "The prepared boot ELF size");
  const auto source_image_sha256_after = sha256_file_digest(request.disc_image);
  const auto boot_executable_sha256_after =
      sha256_file_digest(request.prepared_boot_executable);
  if (source_image_bytes_after != source_image_bytes_before ||
      source_image_sha256_after != source_image_sha256_before) {
    fail("The source image changed during native game preparation");
  }
  if (boot_executable_bytes_after != boot_executable_bytes_before ||
      boot_executable_sha256_after != boot_executable_sha256_before) {
    fail("The prepared boot ELF changed during native game preparation");
  }
}

struct PublishProgressContextV1 {
  NativeGamePreparationControlV1 control;
  bool cancellation_requested = false;
};

[[nodiscard]] bool publish_cancellation_requested(
    const PreparedGameV2PublishCheckpointV1 checkpoint,
    void *const opaque_context) noexcept {
  auto &context = *static_cast<PublishProgressContextV1 *>(opaque_context);
  if (context.control.progress == nullptr) {
    return false;
  }
  const auto phase =
      checkpoint == PreparedGameV2PublishCheckpointV1::staged_and_verified
          ? NativeGamePreparationPhaseV1::publishing_staged
          : NativeGamePreparationPhaseV1::publishing_commit;
  const NativeGamePreparationProgressV1 progress{
      phase, kNativeGamePreparationNoLevelV1,
      static_cast<std::uint32_t>(kDiscTocLevelCount),
      static_cast<std::uint32_t>(kDiscTocLevelCount)};
  context.cancellation_requested =
      !context.control.progress(progress, context.control.context);
  return context.cancellation_requested;
}

} // namespace

void validate_current_native_game_publication_v1(
    const PreparedGameV2RootV1 &prepared) {
  const auto limits = make_native_game_prepared_game_limits_v1();
  const auto &manifest = prepared.manifest;
  if (manifest.content_api_version != kOpenRcContentApiVersionV1 ||
      manifest.provenance.game_id != kNativeGameIdV1 ||
      manifest.provenance.build_id != kNativeGameBuildIdV1 ||
      manifest.provenance.compiler_id != kNativeGameCompilerIdV1 ||
      manifest.provenance.compiler_version != kNativeGameCompilerVersionV1 ||
      manifest.provenance.source_image_bytes == 0U ||
      is_zero_prepared_digest_v1(
          manifest.provenance.source_image_sha256) ||
      manifest.provenance.prepared_game_v1_manifest_sha256.has_value() ||
      !manifest.overlays.empty() ||
      !manifest.shared_package.has_value() ||
      manifest.levels.size() != kDiscTocLevelCount) {
    fail("The prepared game does not match the current native OpenRC profile");
  }

  if (manifest.shared_package->package_path != kNativeGameSharedPackagePathV1 ||
      manifest.shared_package->package_bytes > kNativeGameSharedPackageMaxBytesV1) {
    fail("The prepared game does not contain the canonical bounded shared package");
  }
  std::uint64_t total_package_bytes = manifest.shared_package->package_bytes;
  for (std::size_t index = 0U; index < manifest.levels.size(); ++index) {
    const auto &reference = manifest.levels[index];
    const auto level_id = static_cast<std::uint32_t>(index);
    if (reference.level_id != level_id ||
        reference.package_path != level_package_path(level_id) ||
        reference.package_bytes >
            kNativeGamePreparedPackageMaxBytesV1 - total_package_bytes) {
      fail("The prepared game does not contain the canonical bounded 19-level "
           "set");
    }
    total_package_bytes += reference.package_bytes;
  }

  try {
    auto package = load_prepared_game_level_package_v1(prepared, 0U, limits);
    const auto boot = native_boot_artifact_identity(package);
    const auto shared = load_prepared_game_shared_package_v1(prepared, limits);
    if (!exact_native_startup_media_profile(
            shared, manifest.provenance.source_image_bytes,
            manifest.provenance.source_image_sha256, boot.bytes, boot.sha256)) {
      fail("The prepared shared package does not contain the original startup media profile");
    }
    for (std::uint32_t level_id = 0U; level_id < kDiscTocLevelCount;
         ++level_id) {
      if (level_id != 0U) {
        package =
            load_prepared_game_level_package_v1(prepared, level_id, limits);
      }
      if (!exact_mountable_native_level_profile(
              package, level_id, manifest.provenance.source_image_bytes,
              manifest.provenance.source_image_sha256, boot.bytes, boot.sha256,
              limits)) {
        fail("Prepared native level " + std::to_string(level_id) +
             " does not match the current eight-resource runtime profile");
      }
    }
  } catch (const NativeGamePreparationErrorV1 &) {
    throw;
  } catch (const PreparedGameV2FilesystemError &error) {
    fail("Cannot verify the current native publication: " +
         std::string(error.what()));
  } catch (const LevelPackageV1Error &error) {
    fail("Cannot verify the current native publication: " +
         std::string(error.what()));
  }
}

NativeGamePreparationResultV1
prepare_native_game_v1(const NativeGamePreparationRequestV1 &request,
                       const NativeGamePreparationControlV1 control) {
  report_progress(control, NativeGamePreparationPhaseV1::validating_inputs,
                  kNativeGamePreparationNoLevelV1, 0U);
  if (request.disc_image.empty() || request.prepared_boot_executable.empty() ||
      request.destination_root.empty() || !request.disc_image.is_absolute() ||
      !request.prepared_boot_executable.is_absolute() ||
      !request.destination_root.is_absolute()) {
    fail("Native game preparation requires absolute source, boot ELF, and "
         "destination paths");
  }
  require_publication_root_outside_source(request.destination_root,
                                          request.disc_image, "source ISO");
  require_publication_root_outside_source(request.destination_root,
                                          request.prepared_boot_executable,
                                          "prepared boot ELF");
  const DestinationPreparationLock destination_lock(request.destination_root);

  DiscReport disc;
  try {
    disc = inspect_disc(request.disc_image);
  } catch (const std::exception &error) {
    fail("Cannot inspect the source disc: " + std::string(error.what()));
  }
  if (disc.game != GameId::ratchet_and_clank_2002 || !disc.supported_build) {
    fail("The disc is not the supported RAC1 PAL v2.00 build");
  }
  const auto source_image_bytes =
      host_size_to_u64(disc.image_size, "The source image size");
  if (source_image_bytes == 0U) {
    fail("The source image is empty");
  }

  const auto current_source_image_bytes = host_size_to_u64(
      std::filesystem::file_size(request.disc_image), "The source image size");
  if (current_source_image_bytes != source_image_bytes) {
    fail("The source image changed while it was being inspected");
  }
  const auto boot_executable_bytes = host_size_to_u64(
      std::filesystem::file_size(request.prepared_boot_executable),
      "The prepared boot ELF size");
  if (boot_executable_bytes == 0U || boot_executable_bytes > kMaximumElfBytes) {
    fail("The prepared boot ELF is empty or exceeds the 64 MiB compiler "
         "limit");
  }

  report_progress(control, NativeGamePreparationPhaseV1::hashing_inputs,
                  kNativeGamePreparationNoLevelV1, 0U);
  const auto source_image_sha256 = sha256_file_digest(request.disc_image);
  const auto boot_executable_sha256 =
      sha256_file_digest(request.prepared_boot_executable);
  if (request.expected_source_image_sha256 &&
      *request.expected_source_image_sha256 != source_image_sha256) {
    fail("The source image no longer matches the identity selected by the "
         "frontend");
  }
  if (boot_executable_bytes != disc.boot_size ||
      hex_digest(boot_executable_sha256) != disc.boot_sha256) {
    fail("The prepared boot ELF does not match the supported source disc");
  }

  const auto package_limits =
      make_native_game_prepared_game_limits_v1().level_package;
  const auto prepared_limits = make_native_game_prepared_game_limits_v1();
  const auto source_asset_limits = make_source_asset_limits();
  const auto recovery_limits = runtime::make_level_scene_recovery_limits_v1();
  const auto recovery_profile = runtime::make_level_scene_recovery_profile_v1();
  const auto render_profile =
      runtime::make_level_scene_render_compile_profile_v1();
  const auto render_io_limits = make_render_scene_io_limits();
  const auto actor_io_limits = make_actor_io_limits();
  const auto actor_animation_io_limits = make_actor_animation_io_limits();
  const auto entity_scene_io_limits = make_native_entity_scene_io_limits();
  const auto gameplay_scene_io_limits = make_native_gameplay_scene_io_limits();
  const auto destructible_scene_io_limits =
      make_native_destructible_scene_io_limits();
  const auto collectible_scene_limits =
      make_collectible_scene_compile_limits(render_io_limits.scene);
  const auto destructible_scene_limits =
      make_destructible_scene_compile_limits(render_io_limits.scene);

  report_progress(control,
                  NativeGamePreparationPhaseV1::checking_existing_publication,
                  kNativeGamePreparationNoLevelV1, 0U);
  const auto existing = load_matching_publication(
      request, source_image_bytes, source_image_sha256, boot_executable_bytes,
      boot_executable_sha256, prepared_limits, control);
  if (existing.has_value()) {
    report_progress(control, NativeGamePreparationPhaseV1::verifying_inputs,
                    kNativeGamePreparationNoLevelV1,
                    static_cast<std::uint32_t>(kDiscTocLevelCount));
    verify_sources_unchanged(request, source_image_bytes, source_image_sha256,
                             boot_executable_bytes, boot_executable_sha256);
    report_progress(control,
                    NativeGamePreparationPhaseV1::reusing_existing_publication,
                    kNativeGamePreparationNoLevelV1,
                    static_cast<std::uint32_t>(kDiscTocLevelCount));
    return NativeGamePreparationResultV1{*existing, source_image_sha256,
                                         boot_executable_sha256, true};
  }

  const std::array<LevelPackageProvenanceV1, 2U> render_sources{
      LevelPackageProvenanceV1{LevelPackageProvenanceKindV1::iso_range,
                               "rac1/disc-image", 0U, source_image_bytes,
                               source_image_sha256},
      LevelPackageProvenanceV1{LevelPackageProvenanceKindV1::prepared_resource,
                               "rac1/boot-executable", 0U,
                               boot_executable_bytes, boot_executable_sha256}};
  const std::array<LevelPackageProvenanceV1, 1U> actor_sources{
      LevelPackageProvenanceV1{LevelPackageProvenanceKindV1::iso_range,
                               "rac1/disc-image", 0U, source_image_bytes,
                               source_image_sha256}};

  PreparedGameV2 manifest;
  manifest.content_api_version = kOpenRcContentApiVersionV1;
  manifest.provenance.game_id = kNativeGameIdV1;
  manifest.provenance.build_id = kNativeGameBuildIdV1;
  manifest.provenance.compiler_id = kNativeGameCompilerIdV1;
  manifest.provenance.compiler_version = kNativeGameCompilerVersionV1;
  manifest.provenance.source_image_bytes = source_image_bytes;
  manifest.provenance.source_image_sha256 = source_image_sha256;

  report_progress(control, NativeGamePreparationPhaseV1::compiling_startup_media,
                  kNativeGamePreparationNoLevelV1, 0U);
  std::vector<std::byte> shared_package_bytes;
  try {
    shared_package_bytes = compile_native_startup_media_package(
        request, source_image_bytes, source_image_sha256,
        boot_executable_bytes, boot_executable_sha256, package_limits);
  } catch (const std::exception &error) {
    fail("Cannot compile the original startup media: " + std::string(error.what()));
  }
  manifest.shared_package = PreparedGameSharedReferenceV2{
      std::string(kNativeGameSharedPackagePathV1),
      static_cast<std::uint64_t>(shared_package_bytes.size()),
      prepared_content_sha256_v1(shared_package_bytes)};

  std::vector<std::vector<std::byte>> package_storage;
  package_storage.reserve(kDiscTocLevelCount);
  manifest.levels.reserve(kDiscTocLevelCount);
  std::uint64_t total_package_bytes = manifest.shared_package->package_bytes;

  for (std::uint32_t level_id = 0U; level_id < kDiscTocLevelCount; ++level_id) {
    std::string stage = "loading source assets";
    try {
      report_progress(control,
                      NativeGamePreparationPhaseV1::loading_level_assets,
                      level_id, level_id);
      auto assets = load_rac_level_moby_assets_v1(
          request.disc_image, level_id, source_asset_limits);

      const auto has_bolt_actor =
          has_static_moby_class(assets, kBoltSourceClassId);
      const auto has_veldin_moby_749 = level_id == kVeldinLevelId;
      if (has_veldin_moby_749) {
        if (!has_static_moby_class(assets, kVeldinMoby749SourceClassId)) {
          fail("Veldin is missing the required source class 749 placements");
        }
      }
      const auto has_bolt_crates =
          has_static_moby_class(assets, kBoltCrateSourceClassId);

      auto render_scene = [&] {
        stage = "recovering the complete level scene";
        report_progress(control,
                        NativeGamePreparationPhaseV1::recovering_level_scene,
                        level_id, level_id);
        const runtime::LevelSceneRecoveryRequestV1 recovery_request{
            request.disc_image,
            request.prepared_boot_executable,
            level_id,
            runtime::LevelSceneRecordSelectionV1::all_records,
            0U,
            kSceneBlockSourceGeometryEntrypointV1};
        auto level_recovery_profile = recovery_profile;
        if (has_bolt_crates) {
          level_recovery_profile.excluded_moby_class_ids.push_back(
              kBoltCrateSourceClassId);
        }
        const auto recovered = runtime::recover_level_scene_v1(
            recovery_request, recovery_limits, level_recovery_profile);

        stage = "compiling the neutral render scene";
        report_progress(control,
                        NativeGamePreparationPhaseV1::compiling_render_scene,
                        level_id, level_id);
        return runtime::compile_level_scene_render_v1(recovered,
                                                       render_profile);
      }();

      stage = "compiling the native level foundation";
      report_progress(control,
                      NativeGamePreparationPhaseV1::compiling_level_foundation,
                      level_id, level_id);
      auto package = compile_level_foundation(assets, package_limits);

      std::optional<ActorLibraryV1> bolt_actor;
      if (has_bolt_actor) {
        bolt_actor = compile_bolt_actor_library(assets);
      }

      std::optional<CompiledMobyActorV1> veldin_moby_749;
      if (has_veldin_moby_749) {
        veldin_moby_749 = compile_moby_actor(
            assets, kVeldinMoby749SourceClassId, kVeldinMoby749RigKey,
            kVeldinMoby749HighModelKey,
            kVeldinMoby749SourceSequenceKeyPrefix,
            kVeldinMoby749InitialSourceSequence,
            kVeldinMoby749InitialAnimationKey,
            kVeldinMoby749AnimationSourceUpdatesPerSecond,
            "Veldin Moby class 749");
      }

      stage = "compiling neutral entities and gameplay";
      report_progress(control,
                      NativeGamePreparationPhaseV1::compiling_entity_scene,
                      level_id, level_id);
      auto entity_scene = make_player_entity_scene(level_id);
      GameplaySceneV1 gameplay_scene;
      gameplay_scene.level_id = level_id;
      DestructibleSceneV1 destructible_scene;
      destructible_scene.level_id = level_id;
      if (bolt_actor) {
        auto compiled = compile_rac_collectible_scene_v1(
            render_scene, entity_scene, gameplay_scene, *bolt_actor,
            assets.gameplay.static_mobies,
            make_bolt_collectible_profile(assets), collectible_scene_limits);
        render_scene = std::move(compiled.render_scene);
        entity_scene = std::move(compiled.entity_scene);
        gameplay_scene = std::move(compiled.gameplay_scene);
      } else {
        gameplay_scene = canonicalize_gameplay_scene_v1(
            std::move(gameplay_scene), gameplay_scene_io_limits.scene);
      }
      if (has_bolt_crates) {
        auto compiled = compile_rac_destructible_scene_v1(
            render_scene, entity_scene, destructible_scene,
            require_unique_static_moby_model(
                assets, kBoltCrateSourceClassId, "Bolt Crate"),
            assets.textures, assets.gameplay.static_mobies,
            make_bolt_crate_destructible_profile(),
            destructible_scene_limits);
        render_scene = std::move(compiled.render_scene);
        entity_scene = std::move(compiled.entity_scene);
        destructible_scene = std::move(compiled.destructible_scene);
      } else {
        destructible_scene = canonicalize_destructible_scene_v1(
            std::move(destructible_scene),
            destructible_scene_io_limits.scene);
      }
      if (veldin_moby_749) {
        auto compiled = compile_rac_moby_actor_scene_v1(
            entity_scene, veldin_moby_749->library,
            assets.gameplay.static_mobies,
            make_veldin_moby_749_actor_profile(),
            make_moby_actor_scene_compile_limits());
        if (compiled.authored_ids.size() !=
                kVeldinMoby749PlacementCount ||
            compiled.authored_ids.front() !=
                kVeldinMoby749PlacementBegin ||
            compiled.authored_ids.back() !=
                kVeldinMoby749PlacementBegin +
                    kVeldinMoby749PlacementCount - 1U) {
          fail("Veldin class 749 placements disagree with the exact source "
               "profile");
        }
        entity_scene = std::move(compiled.entity_scene);
      }

      stage = "compiling and attaching neutral actors";
      report_progress(control,
                      NativeGamePreparationPhaseV1::compiling_player_actor,
                      level_id, level_id);
      const auto &player_model = require_unique_player_model(assets);
      auto player_bind_pose = compile_player_bind_pose(player_model);
      const auto animation_bind_rig = player_bind_pose.bind_rig;
      auto player_actor = compile_player_actor_library(
          assets, player_model, std::move(player_bind_pose));
      std::vector<ActorLibraryV1> actor_libraries;
      actor_libraries.reserve(veldin_moby_749 ? 2U : 1U);
      actor_libraries.push_back(std::move(player_actor));
      if (veldin_moby_749) {
        actor_libraries.push_back(std::move(veldin_moby_749->library));
      }
      const auto actor_library = compose_actor_libraries_v1(
          actor_libraries, actor_io_limits.library);
      package = attach_actor_library_to_level_package_v1(
          std::move(package), actor_library, actor_sources,
          actor_io_limits, package_limits);

      stage = "compiling and attaching neutral actor animations";
      report_progress(control,
                      NativeGamePreparationPhaseV1::compiling_player_animation,
                      level_id, level_id);
      auto player_animation = compile_player_animation_bank(
          assets, player_model, animation_bind_rig);
      std::vector<ActorAnimationBankV1> animation_banks;
      animation_banks.reserve(veldin_moby_749 ? 2U : 1U);
      animation_banks.push_back(std::move(player_animation));
      if (veldin_moby_749) {
        animation_banks.push_back(std::move(veldin_moby_749->animations));
      }
      const auto actor_animations = compose_actor_animation_banks_v1(
          animation_banks, actor_animation_io_limits.bank);
      package = attach_actor_animation_bank_to_level_package_v1(
          std::move(package), actor_animations, actor_sources,
          actor_animation_io_limits, package_limits);

      stage = "attaching and encoding the render scene";
      report_progress(control,
                      NativeGamePreparationPhaseV1::encoding_level_package,
                      level_id, level_id);
      package = attach_render_scene_to_level_package_v1(
          std::move(package), render_scene, render_sources, render_io_limits,
          package_limits);

      const std::array<LevelPackageProvenanceV1, 2U> entity_sources{
          prepared_resource_provenance(
              package, kActorLibraryResourceIdV1,
              kActorLibraryResourceTypeIdV1,
              kActorLibraryResourceSchemaVersionV1, "entity-scene"),
          prepared_resource_provenance(
              package, kRenderSceneResourceIdV1, kRenderSceneResourceTypeIdV1,
              kRenderSceneResourceSchemaVersionV1, "entity-scene")};
      package = attach_entity_scene_to_level_package_v1(
          std::move(package), entity_scene, entity_sources,
          entity_scene_io_limits, package_limits);

      const std::array<LevelPackageProvenanceV1, 1U> gameplay_sources{
          prepared_resource_provenance(
              package, kEntitySceneResourceIdV1,
              kEntitySceneResourceTypeIdV1,
              kEntitySceneResourceSchemaVersionV1, "gameplay-scene")};
      package = attach_gameplay_scene_to_level_package_v1(
          std::move(package), gameplay_scene, gameplay_sources,
          gameplay_scene_io_limits, package_limits);

      const std::array<LevelPackageProvenanceV1, 2U> destructible_sources{
          prepared_resource_provenance(
              package, kEntitySceneResourceIdV1,
              kEntitySceneResourceTypeIdV1,
              kEntitySceneResourceSchemaVersionV1, "destructible-scene"),
          actor_sources.front()};
      package = attach_destructible_scene_to_level_package_v1(
          std::move(package), destructible_scene, destructible_sources,
          destructible_scene_io_limits, package_limits);
      if (!exact_mountable_native_level_profile(
              package, level_id, source_image_bytes, source_image_sha256,
              boot_executable_bytes, boot_executable_sha256,
              prepared_limits)) {
        fail("The freshly compiled level does not satisfy the current exact "
             "native runtime profile");
      }
      auto package_bytes = encode_level_package_v1(package, package_limits);
      const auto package_byte_count =
          host_size_to_u64(package_bytes.size(), "A native level package");
      if (package_byte_count >
          kNativeGamePreparedPackageMaxBytesV1 - total_package_bytes) {
        fail("The complete native game exceeds its bounded package-byte "
             "budget");
      }
      total_package_bytes += package_byte_count;

      manifest.levels.push_back(PreparedGameLevelReferenceV2{
          level_id, level_package_path(level_id), package_byte_count,
          prepared_content_sha256_v1(package_bytes)});
      package_storage.push_back(std::move(package_bytes));
    } catch (const NativeGamePreparationCancelledV1 &) {
      throw;
    } catch (const std::exception &error) {
      fail("Native game compilation failed for level " +
           std::to_string(level_id) + " while " + stage + ": " + error.what());
    }
  }

  if (manifest.levels.size() != kDiscTocLevelCount ||
      package_storage.size() != kDiscTocLevelCount) {
    fail("Native game compilation did not produce the canonical 19-level "
         "set");
  }

  report_progress(control, NativeGamePreparationPhaseV1::verifying_inputs,
                  kNativeGamePreparationNoLevelV1,
                  static_cast<std::uint32_t>(kDiscTocLevelCount));
  verify_sources_unchanged(request, source_image_bytes, source_image_sha256,
                           boot_executable_bytes, boot_executable_sha256);

  std::vector<PreparedGameV2LevelPackageBytesV1> package_inputs;
  package_inputs.reserve(package_storage.size());
  for (std::size_t index = 0U; index < package_storage.size(); ++index) {
    package_inputs.push_back(PreparedGameV2LevelPackageBytesV1{
        static_cast<std::uint32_t>(index), package_storage[index]});
  }

  report_progress(control, NativeGamePreparationPhaseV1::publishing,
                  kNativeGamePreparationNoLevelV1,
                  static_cast<std::uint32_t>(kDiscTocLevelCount));
  PublishProgressContextV1 publish_context{control, false};
  PublishedPreparedGameV2V1 publication;
  try {
    publication = publish_prepared_game_v2_v1(
        request.destination_root, manifest, package_inputs, shared_package_bytes,
        prepared_limits,
        PreparedGameV2PublishControlV1{publish_cancellation_requested,
                                       &publish_context});
  } catch (const PreparedGameV2PublishError &error) {
    const std::string message = error.what();
    if (publish_context.cancellation_requested &&
        message.find("rollback was incomplete") == std::string::npos) {
      throw NativeGamePreparationCancelledV1(error.what());
    }
    fail("Cannot publish the native game: " + message);
  }

  return NativeGamePreparationResultV1{std::move(publication),
                                       source_image_sha256,
                                       boot_executable_sha256, false};
}

} // namespace openrc
