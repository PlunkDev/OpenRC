#include "openrc/actor_animation_io.hpp"
#include "openrc/actor_library_io.hpp"
#include "openrc/collision_world_io.hpp"
#include "openrc/content_api.hpp"
#include "openrc/destructible_scene_io.hpp"
#include "openrc/entity_scene_io.hpp"
#include "openrc/gameplay_scene_io.hpp"
#include "openrc/level_actor_library_compile.hpp"
#include "openrc/level_actor_animation_compile.hpp"
#include "openrc/level_bootstrap.hpp"
#include "openrc/level_destructible_scene_compile.hpp"
#include "openrc/level_entity_scene_compile.hpp"
#include "openrc/level_gameplay_scene_compile.hpp"
#include "openrc/level_render_scene_compile.hpp"
#include "openrc/media_clip.hpp"
#include "openrc/image_presentation.hpp"
#include "openrc/scene_timeline.hpp"
#include "openrc/screen_overlay.hpp"
#include "openrc/native_game_prepare.hpp"
#include "openrc/native_frontend_profile.hpp"
#include "openrc/rac_frontend_menu_resources.hpp"
#include "openrc/rac_frontend_state.hpp"
#include "openrc/rac_startup.hpp"
#include "openrc/audio_clip.hpp"
#include "openrc/audio_program_cues.hpp"
#include "openrc/audio_voice_bank_player.hpp"
#include "openrc/state_installation.hpp"
#include "openrc/loading_presentation.hpp"
#include "openrc/prepared_game_v2_fs.hpp"
#include "openrc/rac_level_foundation_compile.hpp"
#include "openrc/render_scene_io.hpp"
#include "openrc/runtime_level_content.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t kSourceImageBytes = 4'700'000'000ULL;
constexpr std::uint64_t kBootExecutableBytes = 3'000'000ULL;
constexpr std::string_view kPlayerRigKey = "actors/ratchet/rig";
constexpr std::string_view kPlayerModelKey = "actors/ratchet/high";
constexpr std::string_view kPlayerArchetypeKey = "openrc.player/default";
constexpr std::string_view kCrateArchetypeKey = "openrc.breakable/bolt-crate";
constexpr std::string_view kBoltItemKey = "openrc.currency/bolts";
constexpr std::string_view kPlayerSourceSequenceKeyPrefix =
    "actors/ratchet/source-sequence/";
constexpr std::string_view kVeldinMoby749RigKey =
    "actors/rac1/moby/0749/rig";
constexpr std::string_view kVeldinMoby749ModelKey =
    "actors/rac1/moby/0749/high";
constexpr std::string_view kVeldinMoby749SequencePrefix =
    "actors/rac1/moby/0749/source-sequence/";
constexpr std::string_view kVeldinMoby749InitialAnimationKey =
    "actors/rac1/moby/0749/initial/source-sequence/001";
constexpr std::string_view kVeldinMoby749ArchetypeKey = "rac1/moby/0749";
constexpr std::array<std::uint16_t, 8U> kVeldinMoby749FrameCounts{
    1U, 6U, 13U, 15U, 15U, 21U, 25U, 23U};
constexpr std::uint32_t kVeldinMoby749PlacementBegin = 143U;
constexpr std::uint32_t kVeldinMoby749PlacementCount = 16U;
constexpr std::array<std::uint16_t, openrc::kDiscTocLevelCount>
    kPlayerAnimationClipCountsByLevel{
        134U, 88U, 86U, 79U, 84U, 100U, 89U, 98U, 108U, 79U,
        83U,  96U, 105U, 86U, 94U, 89U, 96U, 99U, 111U,
    };

const auto kRuntimeLimits =
    openrc::game::make_runtime_level_content_limits_v1();
const auto kPackageLimits = openrc::make_native_game_prepared_game_limits_v1();

enum class ProfileMutation {
  none,
  empty_crates,
  missing_render_instance,
  render_transform_disagrees,
  negative_uniform_scale,
  nonuniform_scale,
  split_crate_mesh,
  inconsistent_hit_sphere,
  hit_sphere_misses_mesh,
  missing_animation,
  animation_wrong_key,
  animation_bad_cadence,
  animation_clamp,
  animation_stale_rig_digest,
  animation_wrong_joint_count,
  animation_missing_source_clip,
};

enum class SharedMutation {
  none,
  missing_reference,
  missing_file,
  wrong_path,
  missing_intro,
  wrong_type,
  bad_media,
  wrong_aspect,
  wrong_cadence,
  missing_audio,
  stale_image,
  stale_boot,
  wrong_movie,
  out_of_image,
  stale_compiler,
  missing_bitmap,
  bitmap_wrong_timing,
  bitmap_wrong_transfer,
  bitmap_wrong_source,
  missing_frontend,
  frontend_wrong_source,
  frontend_wrong_timing,
  frontend_stale_actors,
  missing_geometry,
  bad_geometry,
};

[[nodiscard]] std::vector<std::byte> bytes_of(const std::string_view value) {
  return std::vector<std::byte>(
      reinterpret_cast<const std::byte *>(value.data()),
      reinterpret_cast<const std::byte *>(value.data() + value.size()));
}

[[nodiscard]] openrc::PreparedContentDigestV1
digest_of(const std::string_view value) {
  const auto bytes = bytes_of(value);
  return openrc::prepared_content_sha256_v1(bytes);
}

const auto kSourceImageSha256 = digest_of("native-profile-source-image");
const auto kBootExecutableSha256 = digest_of("native-profile-boot-executable");

void write_bytes(const std::filesystem::path &path,
                 const std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("Cannot create a native-profile test file");
  }
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("Cannot write a native-profile test file");
  }
}

class TemporaryPublicationTree final {
public:
  TemporaryPublicationTree() {
    const auto temporary_root =
        std::filesystem::temp_directory_path().lexically_normal();
    const auto stamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (std::uint32_t attempt = 0U; attempt < 64U; ++attempt) {
      container_ =
          temporary_root / ("openrc-native-profile-" + std::to_string(stamp) +
                            "-" + std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(container_, error)) {
        root = container_ / "prepared";
        std::filesystem::create_directories(root / "levels");
        return;
      }
      if (error) {
        throw std::runtime_error(
            "Cannot reserve a native-profile temporary directory");
      }
    }
    throw std::runtime_error(
        "Cannot choose a unique native-profile temporary path");
  }

  ~TemporaryPublicationTree() {
    std::error_code ignored;
    std::filesystem::remove_all(container_, ignored);
  }

  TemporaryPublicationTree(const TemporaryPublicationTree &) = delete;
  TemporaryPublicationTree &
  operator=(const TemporaryPublicationTree &) = delete;

  std::filesystem::path root;

private:
  std::filesystem::path container_;
};

[[nodiscard]] std::string level_locator(const std::uint32_t level_id,
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

[[nodiscard]] openrc::LevelPackageProvenanceV1
generated_provenance(const std::string_view compiler_pass) {
  return openrc::LevelPackageProvenanceV1{
      openrc::LevelPackageProvenanceKindV1::generated,
      std::string(compiler_pass),
      0U,
      0U,
      {},
  };
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
source_provenance(const openrc::LevelPackageProvenanceKindV1 kind,
                  const std::string_view locator, const std::uint64_t bytes,
                  const openrc::PreparedContentDigestV1 &sha256) {
  return openrc::LevelPackageProvenanceV1{kind, std::string(locator), 0U, bytes,
                                          sha256};
}

[[nodiscard]] openrc::LevelPackageResourceV1 make_resource(
    const std::string_view resource_id, const std::string_view type_id,
    const std::uint32_t schema_version, std::vector<std::byte> payload,
    std::vector<openrc::LevelPackageProvenanceV1> provenance) {
  openrc::LevelPackageResourceV1 result;
  result.resource_id = std::string(resource_id);
  result.type_id = std::string(type_id);
  result.schema_version = schema_version;
  result.flags = openrc::kLevelPackageResourceOverlayReplaceableV1;
  result.provenance = std::move(provenance);
  result.payload = std::move(payload);
  result.payload_sha256 = openrc::prepared_content_sha256_v1(result.payload);
  return result;
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
prepared_resource_provenance(const openrc::LevelPackageResourceV1 &resource) {
  return source_provenance(
      openrc::LevelPackageProvenanceKindV1::prepared_resource,
      resource.resource_id, static_cast<std::uint64_t>(resource.payload.size()),
      resource.payload_sha256);
}

[[nodiscard]] openrc::CollisionWorldV1 make_collision_world() {
  openrc::CollisionMeshV1 mesh;
  mesh.vertices = {
      {-64, -64, 0},
      {64, -64, 0},
      {0, 64, 0},
  };
  mesh.triangles = {
      {{0U, 1U, 2U}, {}, openrc::CollisionLayerV1::world},
  };
  return openrc::build_collision_world_v1(
      std::move(mesh), kRuntimeLimits.foundation.collision.world);
}

[[nodiscard]] openrc::LevelBootstrapV1
make_bootstrap(const std::uint32_t level_id) {
  openrc::LevelBootstrapV1 result;
  result.level_id = level_id;
  result.death_height_world = -10.0;
  result.spawn_points = {{0U, {0.0, 0.0, 1.0}, 0.0}};
  return result;
}

[[nodiscard]] openrc::ActorSkinBindingV1 rigid_skin() {
  openrc::ActorSkinBindingV1 result;
  result.influence_count = 1U;
  result.weight_numerators[0U] = 255U;
  result.weight_sum = 255U;
  return result;
}

[[nodiscard]] openrc::ActorSkinnedVertexV1 actor_vertex(const float x,
                                                        const float y) {
  openrc::ActorSkinnedVertexV1 result;
  result.x = x;
  result.y = y;
  result.skin = rigid_skin();
  return result;
}

[[nodiscard]] openrc::ActorLibraryV1
make_actor_library(const std::uint32_t level_id) {
  openrc::ActorRigAssetV1 rig;
  rig.id = 0U;
  rig.semantic_key = std::string(kPlayerRigKey);
  rig.rig.joints.push_back(
      {-1, openrc::ActorAffineTransformV1{}, openrc::ActorAffineTransformV1{}});

  openrc::RenderSceneMaterialV1 material;
  material.id = 0U;

  openrc::ActorSkinnedMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      actor_vertex(-1.0F, -1.0F),
      actor_vertex(1.0F, -1.0F),
      actor_vertex(0.0F, 1.0F),
  };
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};

  openrc::ActorModelV1 model;
  model.id = 0U;
  model.semantic_key = std::string(kPlayerModelKey);
  model.rig_key = rig.semantic_key;
  model.materials.push_back(material);
  model.meshes.push_back(std::move(mesh));

  openrc::ActorLibraryV1 result;
  result.rigs.push_back(std::move(rig));
  result.models.push_back(std::move(model));
  if (level_id == 0U) {
    openrc::ActorRigAssetV1 moby_rig;
    moby_rig.id = 1U;
    moby_rig.semantic_key = std::string(kVeldinMoby749RigKey);
    moby_rig.rig.joints.push_back(openrc::ActorRigJointV1{});
    for (std::size_t index = 1U; index < 53U; ++index) {
      auto joint = openrc::ActorRigJointV1{};
      joint.parent_index = 0;
      moby_rig.rig.joints.push_back(joint);
    }

    openrc::ActorSkinnedVertexV1 moby_vertex = actor_vertex(0.0F, 0.0F);
    openrc::ActorSkinnedMeshV1 moby_mesh;
    moby_mesh.id = 0U;
    moby_mesh.vertices = {moby_vertex, moby_vertex, moby_vertex};
    moby_mesh.triangle_indices = {0U, 1U, 2U};
    moby_mesh.draw_ranges = {{0U, 0U, 3U}};
    openrc::ActorModelV1 moby_model;
    moby_model.id = 1U;
    moby_model.semantic_key = std::string(kVeldinMoby749ModelKey);
    moby_model.rig_key = std::string(kVeldinMoby749RigKey);
    moby_model.materials.push_back(openrc::RenderSceneMaterialV1{});
    moby_model.meshes.push_back(std::move(moby_mesh));
    result.rigs.push_back(std::move(moby_rig));
    result.models.push_back(std::move(moby_model));
  }
  return result;
}

[[nodiscard]] openrc::ActorAnimationBankV1
make_actor_animations(const ProfileMutation mutation,
                      const std::uint32_t level_id) {
  const auto library = make_actor_library(level_id);
  const auto rig_digest =
      openrc::actor_rig_content_sha256_v1(library.rigs.front().rig);
  const std::array<std::string_view, 3U> keys{
      "actors/ratchet/source-sequence/000",
      "actors/ratchet/source-sequence/003",
      "actors/ratchet/source-sequence/004"};
  const std::array<float, 3U> rates{0.125F, 0.25F, 0.5F};

  openrc::ActorAnimationBankV1 result;
  for (std::size_t index = 0U; index < keys.size(); ++index) {
    openrc::ActorAnimationClipV1 clip;
    clip.id = static_cast<std::uint32_t>(index);
    clip.semantic_key = std::string(keys[index]);
    clip.rig_key = std::string(kPlayerRigKey);
    clip.rig_content_sha256 = rig_digest;
    clip.source_updates_per_second = 50U;
    clip.wrap_mode = openrc::ActorAnimationWrapModeV1::loop;
    clip.frames.push_back(openrc::ActorAnimationFrameV1{
        rates[index], {openrc::ActorJointPoseV1{}}});
    result.clips.push_back(std::move(clip));
  }
  for (std::uint32_t source_slot = 0U;
       result.clips.size() < kPlayerAnimationClipCountsByLevel[level_id];
       ++source_slot) {
    if (source_slot == 0U || source_slot == 3U || source_slot == 4U) {
      continue;
    }
    std::string key(kPlayerSourceSequenceKeyPrefix);
    key.push_back(
        static_cast<char>('0' + (source_slot / 100U) % 10U));
    key.push_back(static_cast<char>('0' + (source_slot / 10U) % 10U));
    key.push_back(static_cast<char>('0' + source_slot % 10U));
    openrc::ActorAnimationClipV1 clip;
    clip.id = static_cast<std::uint32_t>(result.clips.size());
    clip.semantic_key = std::move(key);
    clip.rig_key = std::string(kPlayerRigKey);
    clip.rig_content_sha256 = rig_digest;
    clip.source_updates_per_second = 50U;
    clip.wrap_mode = openrc::ActorAnimationWrapModeV1::clamp;
    clip.frames.push_back(openrc::ActorAnimationFrameV1{
        0.25F, {openrc::ActorJointPoseV1{}}});
    result.clips.push_back(std::move(clip));
  }
  if (level_id == 0U) {
    const auto moby_rig_digest =
        openrc::actor_rig_content_sha256_v1(library.rigs[1U].rig);
    for (std::size_t source_slot = 0U;
         source_slot < kVeldinMoby749FrameCounts.size(); ++source_slot) {
      std::string key(kVeldinMoby749SequencePrefix);
      key.push_back('0');
      key.push_back('0');
      key.push_back(static_cast<char>('0' + source_slot));
      if (source_slot == 1U) {
        key = kVeldinMoby749InitialAnimationKey;
      }
      openrc::ActorAnimationClipV1 clip;
      clip.id = static_cast<std::uint32_t>(result.clips.size());
      clip.semantic_key = std::move(key);
      clip.rig_key = std::string(kVeldinMoby749RigKey);
      clip.rig_content_sha256 = moby_rig_digest;
      clip.source_updates_per_second = 50U;
      clip.wrap_mode = openrc::ActorAnimationWrapModeV1::clamp;
      clip.frames.assign(
          kVeldinMoby749FrameCounts[source_slot],
          openrc::ActorAnimationFrameV1{
              0.25F, std::vector<openrc::ActorJointPoseV1>(53U)});
      result.clips.push_back(std::move(clip));
    }
  }

  if (mutation == ProfileMutation::animation_wrong_key) {
    result.clips.front().semantic_key = "actors/ratchet/not-idle";
  } else if (mutation == ProfileMutation::animation_bad_cadence) {
    result.clips.front().source_updates_per_second = 60U;
  } else if (mutation == ProfileMutation::animation_clamp) {
    result.clips.front().wrap_mode = openrc::ActorAnimationWrapModeV1::clamp;
  } else if (mutation == ProfileMutation::animation_stale_rig_digest) {
    result.clips.front().rig_content_sha256 =
        digest_of("stale-player-animation-rig");
  } else if (mutation == ProfileMutation::animation_wrong_joint_count) {
    result.clips.front().frames.front().joint_poses.push_back(
        openrc::ActorJointPoseV1{});
  } else if (mutation == ProfileMutation::animation_missing_source_clip) {
    result.clips.pop_back();
  }
  return result;
}

[[nodiscard]] openrc::RenderSceneMeshV1 triangle_mesh(const std::uint32_t id,
                                                      const float extent) {
  openrc::RenderSceneMeshV1 mesh;
  mesh.id = id;
  mesh.vertices = {
      {-extent, -extent, 0.0F, 0.0F, 0.0F, UINT32_C(0xffffffff)},
      {extent, -extent, 0.0F, 1.0F, 0.0F, UINT32_C(0xffffffff)},
      {0.0F, extent, 0.0F, 0.5F, 1.0F, UINT32_C(0xffffffff)},
  };
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};
  return mesh;
}

[[nodiscard]] openrc::RenderSceneAffine3x4V1
translated_scaled_affine(const std::array<float, 3U> translation,
                         const float scale) {
  openrc::RenderSceneAffine3x4V1 result;
  result.values = {
      scale, 0.0F, 0.0F,  translation[0U], 0.0F, scale, 0.0F, translation[1U],
      0.0F,  0.0F, scale, translation[2U],
  };
  return result;
}

[[nodiscard]] openrc::RenderSceneV1
make_render_scene(const ProfileMutation mutation) {
  openrc::RenderSceneV1 result;
  result.materials.push_back(openrc::RenderSceneMaterialV1{});
  result.meshes.push_back(triangle_mesh(0U, 0.5F));
  result.instances.push_back(
      {0U, 0U, translated_scaled_affine({0.0F, 0.0F, 0.0F}, 1.0F)});
  if (mutation == ProfileMutation::empty_crates) {
    return result;
  }

  result.meshes.push_back(triangle_mesh(1U, 1.0F));
  result.instances.push_back(
      {1U, 1U, translated_scaled_affine({2.0F, 3.0F, 4.0F}, 1.0F)});
  auto second_scale = 2.0F;
  if (mutation == ProfileMutation::negative_uniform_scale) {
    second_scale = -2.0F;
  }
  result.instances.push_back(
      {2U, mutation == ProfileMutation::split_crate_mesh ? 0U : 1U,
       translated_scaled_affine({-3.0F, 1.0F, 2.0F}, second_scale)});
  if (mutation == ProfileMutation::nonuniform_scale) {
    result.instances[2U].local_to_world.values[5U] = 3.0F;
  }
  if (mutation == ProfileMutation::render_transform_disagrees) {
    result.instances[1U].local_to_world.values[3U] = 9.0F;
  }
  return result;
}

[[nodiscard]] openrc::EntitySceneV1
make_entity_scene(const std::uint32_t level_id,
                  const ProfileMutation mutation) {
  openrc::EntitySceneV1 result;
  result.level_id = level_id;
  result.definitions.push_back({0U, std::string(kPlayerArchetypeKey),
                                openrc::kEntityDefinitionInitiallyEnabledV1,
                                openrc::kEntitySceneNoAuthoringGroupIdV1});
  result.actor_bindings.push_back(
      {0U, std::string(kPlayerModelKey), openrc::ActorAffineTransformV1{}});
  result.player_bindings.push_back({0U, 0U});
  if (mutation == ProfileMutation::empty_crates) {
    return result;
  }

  result.definitions.push_back({1U, std::string(kCrateArchetypeKey),
                                openrc::kEntityDefinitionInitiallyEnabledV1,
                                openrc::kEntitySceneNoAuthoringGroupIdV1});
  result.definitions.push_back({2U, std::string(kCrateArchetypeKey),
                                openrc::kEntityDefinitionInitiallyEnabledV1,
                                openrc::kEntitySceneNoAuthoringGroupIdV1});
  openrc::game::WorldTransformV1 first;
  first.position = {2.0F, 3.0F, 4.0F};
  openrc::game::WorldTransformV1 second;
  second.position = {-3.0F, 1.0F, 2.0F};
  if (mutation == ProfileMutation::negative_uniform_scale) {
    second.scale = {-2.0F, -2.0F, -2.0F};
  } else if (mutation == ProfileMutation::nonuniform_scale) {
    second.scale = {2.0F, 3.0F, 2.0F};
  } else {
    second.scale = {2.0F, 2.0F, 2.0F};
  }
  result.transforms = {{1U, first}, {2U, second}};
  result.render_bindings = {
      {1U, mutation == ProfileMutation::missing_render_instance ? 99U : 1U},
      {2U, 2U},
  };
  if (level_id == 0U) {
    for (std::uint32_t offset = 0U;
         offset < kVeldinMoby749PlacementCount; ++offset) {
      const auto authored_id = kVeldinMoby749PlacementBegin + offset;
      result.definitions.push_back(
          {authored_id, std::string(kVeldinMoby749ArchetypeKey),
           openrc::kEntityDefinitionInitiallyEnabledV1,
           openrc::kEntitySceneNoAuthoringGroupIdV1});
      openrc::game::WorldTransformV1 transform;
      transform.position = {static_cast<float>(offset), 10.0F, 20.0F};
      result.transforms.push_back({authored_id, transform});
      result.actor_bindings.push_back(
          {authored_id, std::string(kVeldinMoby749ModelKey),
           openrc::ActorAffineTransformV1{}});
    }
  }
  return result;
}

[[nodiscard]] openrc::DestructibleSceneV1
make_destructible_scene(const std::uint32_t level_id,
                        const ProfileMutation mutation) {
  openrc::DestructibleSceneV1 result;
  result.level_id = level_id;
  if (mutation == ProfileMutation::empty_crates) {
    return result;
  }

  const auto accepted_channels = openrc::game::kDamageChannelMeleeV1 |
                                 openrc::game::kDamageChannelProjectileV1 |
                                 openrc::game::kDamageChannelExplosiveV1;
  const auto radius =
      mutation == ProfileMutation::hit_sphere_misses_mesh ? 1.0F : 2.0F;
  const std::vector<openrc::DestructibleDropV1> drops{
      {std::string(kBoltItemKey), 1U, 0U}};
  result.destructibles.push_back(
      {1U, 1U, accepted_channels, {0.0F, 0.0F, 0.0F}, radius, 0U, drops});
  result.destructibles.push_back(
      {2U,
       1U,
       accepted_channels,
       {0.0F, 0.0F, 0.0F},
       mutation == ProfileMutation::inconsistent_hit_sphere ? 1.75F : radius,
       0U,
       drops});
  return result;
}

[[nodiscard]] openrc::LevelPackageV1
make_level_package(const std::uint32_t level_id,
                   const ProfileMutation mutation) {
  const auto collision_source_sha256 = digest_of("collision-source");
  const auto gameplay_source_sha256 = digest_of("gameplay-source");

  auto collision = make_resource(
      openrc::kCollisionWorldResourceIdV1,
      openrc::kCollisionWorldResourceTypeIdV1,
      openrc::kCollisionWorldResourceSchemaVersionV1,
      openrc::encode_collision_world_v1(make_collision_world(),
                                        kRuntimeLimits.foundation.collision),
      {source_provenance(
           openrc::LevelPackageProvenanceKindV1::prepared_resource,
           level_locator(level_id, "core/collision"), 16U,
           collision_source_sha256),
       generated_provenance(openrc::kRacCollisionWorldCompilePassV1)});
  auto bootstrap = make_resource(
      openrc::kLevelBootstrapResourceIdV1,
      openrc::kLevelBootstrapResourceTypeIdV1,
      openrc::kLevelBootstrapResourceSchemaVersionV1,
      openrc::encode_level_bootstrap_v1(make_bootstrap(level_id),
                                        kRuntimeLimits.foundation.bootstrap),
      {source_provenance(
           openrc::LevelPackageProvenanceKindV1::prepared_resource,
           level_locator(level_id, "gameplay"), 16U, gameplay_source_sha256),
       generated_provenance(openrc::kRacLevelBootstrapCompilePassV1)});
  auto render = make_resource(
      openrc::kRenderSceneResourceIdV1, openrc::kRenderSceneResourceTypeIdV1,
      openrc::kRenderSceneResourceSchemaVersionV1,
      openrc::encode_render_scene_v1(make_render_scene(mutation),
                                     kRuntimeLimits.render_scene),
      {source_provenance(openrc::LevelPackageProvenanceKindV1::iso_range,
                         "rac1/disc-image", kSourceImageBytes,
                         kSourceImageSha256),
       source_provenance(
           openrc::LevelPackageProvenanceKindV1::prepared_resource,
           "rac1/boot-executable", kBootExecutableBytes, kBootExecutableSha256),
       generated_provenance(openrc::kLevelRenderSceneCompilePassV1)});
  auto actor = make_resource(
      openrc::kActorLibraryResourceIdV1, openrc::kActorLibraryResourceTypeIdV1,
      openrc::kActorLibraryResourceSchemaVersionV1,
      openrc::encode_actor_library_v1(make_actor_library(level_id),
                                      kRuntimeLimits.actor_library),
      {source_provenance(openrc::LevelPackageProvenanceKindV1::iso_range,
                         "rac1/disc-image", kSourceImageBytes,
                         kSourceImageSha256),
       generated_provenance(openrc::kLevelActorLibraryCompilePassV1)});
  auto animation = make_resource(
      openrc::kActorAnimationResourceIdV1,
      openrc::kActorAnimationResourceTypeIdV1,
      openrc::kActorAnimationResourceSchemaVersionV1,
      openrc::encode_actor_animation_bank_v1(
          make_actor_animations(mutation, level_id),
          kRuntimeLimits.actor_animation),
      {source_provenance(openrc::LevelPackageProvenanceKindV1::iso_range,
                         "rac1/disc-image", kSourceImageBytes,
                         kSourceImageSha256),
       generated_provenance(openrc::kLevelActorAnimationCompilePassV1)});
  auto entity = make_resource(
      openrc::kEntitySceneResourceIdV1, openrc::kEntitySceneResourceTypeIdV1,
      openrc::kEntitySceneResourceSchemaVersionV1,
      openrc::encode_entity_scene_v1(make_entity_scene(level_id, mutation),
                                     kRuntimeLimits.entity_scene),
      {prepared_resource_provenance(actor),
       prepared_resource_provenance(render),
       generated_provenance(openrc::kLevelEntitySceneCompilePassV1)});

  openrc::GameplaySceneV1 gameplay_scene;
  gameplay_scene.level_id = level_id;
  auto gameplay = make_resource(
      openrc::kGameplaySceneResourceIdV1,
      openrc::kGameplaySceneResourceTypeIdV1,
      openrc::kGameplaySceneResourceSchemaVersionV1,
      openrc::encode_gameplay_scene_v1(gameplay_scene,
                                       kRuntimeLimits.gameplay_scene),
      {prepared_resource_provenance(entity),
       generated_provenance(openrc::kLevelGameplaySceneCompilePassV1)});
  auto destructible = make_resource(
      openrc::kDestructibleSceneResourceIdV1,
      openrc::kDestructibleSceneResourceTypeIdV1,
      openrc::kDestructibleSceneResourceSchemaVersionV1,
      openrc::encode_destructible_scene_v1(
          make_destructible_scene(level_id, mutation),
          kRuntimeLimits.destructible_scene),
      {prepared_resource_provenance(entity),
       source_provenance(openrc::LevelPackageProvenanceKindV1::iso_range,
                         "rac1/disc-image", kSourceImageBytes,
                         kSourceImageSha256),
       generated_provenance(openrc::kLevelDestructibleSceneCompilePassV1)});

  openrc::LevelPackageV1 result;
  result.level_id = level_id;
  result.content_api_version = openrc::kOpenRcContentApiVersionV1;
  result.build_id = std::string(openrc::kNativeGameBuildIdV1);
  result.resources = {
      std::move(collision),    std::move(bootstrap), std::move(render),
      std::move(actor),        std::move(animation), std::move(entity),
      std::move(gameplay),     std::move(destructible),
  };
  if (mutation == ProfileMutation::missing_animation) {
    result.resources.erase(result.resources.begin() + 4);
  }
  return result;
}

#include "native_frontend_profile_fixture.hpp"

[[nodiscard]] openrc::LevelPackageV1 make_shared_package(
    const SharedMutation mutation) {
  openrc::MediaClipV1 clip;
  clip.width = 512U;
  clip.height = 416U;
  clip.frame_rate_numerator = mutation == SharedMutation::wrong_cadence ? 30U : 25U;
  clip.frame_rate_denominator = 1U;
  clip.display_aspect_numerator = mutation == SharedMutation::wrong_aspect ? 4U : 1U;
  clip.display_aspect_denominator = mutation == SharedMutation::wrong_aspect ? 3U : 1U;
  clip.video = {{0, 0, {std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0xb3},
                        std::byte{0x20}, std::byte{1}, std::byte{0xa0}, std::byte{0x13}}}};
  if (mutation != SharedMutation::missing_audio) {
    clip.audio_sample_rate = 44100U;
    clip.audio_channels = 2U;
    clip.audio_start_time = 0;
    clip.audio = {0, 0};
  }
  openrc::LevelPackageResourceV1 resource;
  resource.resource_id = mutation == SharedMutation::missing_intro ?
      "startup/unrelated" : openrc::kNativeGameStartupIntroResourceIdV1;
  resource.type_id = mutation == SharedMutation::wrong_type ? "openrc.unknown" :
                                                            "openrc.media-clip";
  resource.schema_version = 1U;
  resource.flags = openrc::kLevelPackageResourceOverlayReplaceableV1;
  resource.payload = openrc::encode_media_clip_v1(clip);
  if (mutation == SharedMutation::bad_media) {
    resource.payload.front() ^= std::byte{1};
  }
  resource.provenance = {
      source_provenance(openrc::LevelPackageProvenanceKindV1::iso_range,
                        "rac1/disc-image", kSourceImageBytes,
                        mutation == SharedMutation::stale_image ? digest_of("wrong-image") :
                                                                 kSourceImageSha256),
      source_provenance(openrc::LevelPackageProvenanceKindV1::prepared_resource,
                        "rac1/boot-executable", kBootExecutableBytes,
                        mutation == SharedMutation::stale_boot ? digest_of("wrong-boot") :
                                                                kBootExecutableSha256),
      {openrc::LevelPackageProvenanceKindV1::iso_range,
       mutation == SharedMutation::wrong_movie ? "rac1/global-toc/17f8" :
                                                "rac1/global-toc/1800",
       mutation == SharedMutation::out_of_image ? kSourceImageBytes :
                                                 UINT64_C(44148) * 2048U,
       9879556U, digest_of("synthetic-startup-movie-range")},
      generated_provenance(mutation == SharedMutation::stale_compiler ?
                              "openrc.previous-startup-pass" :
                              openrc::kNativeGameStartupMediaCompilePassV1)};
  openrc::LevelPackageV1 package;
  package.level_id = openrc::kPreparedGameSharedPackageIdV2;
  package.content_api_version = openrc::kOpenRcContentApiVersionV1;
  package.build_id = openrc::kNativeGameBuildIdV1;
  package.resources.push_back(std::move(resource));
  if(mutation!=SharedMutation::missing_bitmap) {
    openrc::ImagePresentationV1 image;
    image.width=512;image.height=448;image.display_aspect_numerator=1;image.display_aspect_denominator=1;
    image.updates_per_second=50;image.transfer_lead_updates=1;image.transfer_tail_updates=1;
    image.initialization_clock_hz=15625;image.initialization_clock_modulus=65536;image.initialization_credit_divisor=265;
    image.minimum_initialization_updates=mutation==SharedMutation::bitmap_wrong_timing?180:150;
    image.rgba.resize(512U*448U*4U,std::byte{255});image.color_transfers.resize(12);
    for(unsigned i=0;i<12;++i) for(unsigned c=0;c<256;++c)
      image.color_transfers[i][c]=static_cast<std::byte>(c*((11U-i)*128U/(12U-i))/128U);
    if(mutation==SharedMutation::bitmap_wrong_transfer) image.color_transfers[0][200]^=std::byte{1};
    package.resources.push_back(make_resource("startup/post-intro","openrc.image-presentation",1,
        openrc::encode_image_presentation_v1(image),{
          source_provenance(openrc::LevelPackageProvenanceKindV1::iso_range,"rac1/disc-image",kSourceImageBytes,kSourceImageSha256),
          source_provenance(openrc::LevelPackageProvenanceKindV1::prepared_resource,"rac1/boot-executable",kBootExecutableBytes,kBootExecutableSha256),
          {openrc::LevelPackageProvenanceKindV1::iso_range,
           mutation==SharedMutation::bitmap_wrong_source?"rac1/global-toc/14e8":"rac1/global-toc/12c0",
           UINT64_C(14365)*2048U,162U*2048U,digest_of("synthetic-boot-bitmap-wad")},
          generated_provenance("openrc.rac-startup-image-compile.v1")}));
  }
  if(mutation!=SharedMutation::missing_frontend) {
    const auto prototype=make_actor_library(1U);
    openrc::ActorLibraryV1 library;
    for(std::uint32_t i=0;i<5U;++i) {
      const auto key="frontend/background/actor/"+std::to_string(i);
      auto rig=prototype.rigs.front();rig.id=i;rig.semantic_key=key+"/rig";
      auto model=prototype.models.front();model.id=i;model.semantic_key=key+"/model";model.rig_key=rig.semantic_key;
      library.rigs.push_back(std::move(rig));library.models.push_back(std::move(model));
    }
    auto actor_bytes=openrc::encode_actor_library_v1(library,kRuntimeLimits.actor_library);
    library=openrc::decode_actor_library_v1(actor_bytes,kRuntimeLimits.actor_library);
    openrc::ActorAnimationBankV1 bank;
    for(std::uint32_t i=0;i<75U;++i) {
      openrc::ActorAnimationClipV1 clip;clip.id=i;
      clip.semantic_key="frontend/background/chunk/"+std::to_string(i/5U)+"/actor/"+std::to_string(i%5U);
      clip.rig_key=library.rigs[i%5U].semantic_key;clip.rig_content_sha256=library.rigs[i%5U].content_sha256;
      clip.source_updates_per_second=50;clip.frames.resize(i<70U?49U:29U);
      for(auto& frame:clip.frames) {frame.phase_rate=0.5F;frame.joint_poses.resize(1);}
      bank.clips.push_back(std::move(clip));
    }
    auto animation_bytes=openrc::encode_actor_animation_bank_v1(bank,kRuntimeLimits.actor_animation);
    openrc::SceneTimelineV1 timeline;
    timeline.actor_library_sha256=openrc::prepared_content_sha256_v1(actor_bytes);
    timeline.actor_animation_sha256=openrc::prepared_content_sha256_v1(animation_bytes);
    if(mutation==SharedMutation::frontend_stale_actors) timeline.actor_library_sha256[0]^=std::byte{1};
    timeline.updates_per_second=mutation==SharedMutation::frontend_wrong_timing?60U:50U;
    timeline.display_aspect_numerator=512U;timeline.display_aspect_denominator=512U;timeline.loop=true;
    for(std::uint32_t i=0;i<5U;++i) timeline.actors.push_back({i,i,{}});
    timeline.samples.resize(1398U);
    for(auto& sample:timeline.samples) {
      sample.camera.tangent_half_horizontal=0.63F;sample.camera.tangent_half_vertical=0.48F;
      sample.camera.near_plane=0.1F;sample.camera.far_plane=1000;
      sample.actors.resize(5U);
      for(std::uint32_t i=0;i<5U;++i) sample.actors[i].clip_index=i;
    }
    const std::vector<openrc::LevelPackageProvenanceV1> provenance{
        source_provenance(openrc::LevelPackageProvenanceKindV1::iso_range,"rac1/disc-image",kSourceImageBytes,kSourceImageSha256),
        source_provenance(openrc::LevelPackageProvenanceKindV1::prepared_resource,"rac1/boot-executable",kBootExecutableBytes,kBootExecutableSha256),
        {openrc::LevelPackageProvenanceKindV1::iso_range,
          mutation==SharedMutation::frontend_wrong_source?"rac1/global-toc/12c0":"rac1/global-toc/14e8",
          UINT64_C(14602)*2048U,2101U*2048U,digest_of("synthetic-frontend-wad")},
        generated_provenance("openrc.rac-frontend-scene-compile.v1")};
    package.resources.push_back(make_resource("frontend/background/actors","openrc.actor-library",1,
        std::move(actor_bytes),provenance));
    const auto actor_provenance=prepared_resource_provenance(package.resources.back());
    package.resources.push_back(make_resource("frontend/background/animation","openrc.actor-animation-bank",1,
        std::move(animation_bytes),provenance));
    const auto animation_provenance=prepared_resource_provenance(package.resources.back());
    package.resources.push_back(make_resource("frontend/background/timeline","openrc.scene-timeline",1,
        openrc::encode_scene_timeline_v1(timeline),provenance));
    package.resources.back().provenance.push_back(actor_provenance);
    package.resources.back().provenance.push_back(animation_provenance);
    openrc::ScreenOverlayV1 overlay;
    overlay.canvas_width=512U;overlay.canvas_height=448U;overlay.updates_per_second=50U;
    overlay.coverage_denominator=128U;overlay.loop_begin=100U;
    overlay.images.resize(192U);
    for(auto& image:overlay.images) {image.width=1U;image.height=1U;image.rgb_coverage.resize(4U);}
    overlay.frames.resize(160U);
    package.resources.push_back(make_resource("frontend/title","openrc.screen-overlay",1,
        openrc::encode_screen_overlay_v1(overlay),provenance));
    package.resources.push_back(make_resource("frontend/background/geometry","openrc.render-scene",1,
        openrc::encode_render_scene_v1(make_render_scene(ProfileMutation::none),kRuntimeLimits.render_scene),provenance));
    if(mutation==SharedMutation::missing_geometry) package.resources.pop_back();
    else if(mutation==SharedMutation::bad_geometry) {
      package.resources.back().payload={std::byte{1}};
      package.resources.back().payload_sha256=openrc::prepared_content_sha256_v1(package.resources.back().payload);
    }
  }
  const auto &flow=frontend_flow_fixture();
  package.resources.insert(package.resources.end(),flow.begin(),flow.end());
  append_frontend_ambient_fixture(package);
  return package;
}

class PublicationFixture final {
public:
  explicit PublicationFixture(
      const ProfileMutation mutation,
      const std::string_view compiler_version =
          openrc::kNativeGameCompilerVersionV1,
      const SharedMutation shared_mutation = SharedMutation::none) {
    openrc::PreparedGameV2 manifest;
    manifest.content_api_version = openrc::kOpenRcContentApiVersionV1;
    manifest.provenance.game_id = std::string(openrc::kNativeGameIdV1);
    manifest.provenance.build_id = std::string(openrc::kNativeGameBuildIdV1);
    manifest.provenance.compiler_id =
        std::string(openrc::kNativeGameCompilerIdV1);
    manifest.provenance.compiler_version =
        std::string(compiler_version);
    manifest.provenance.source_image_bytes = kSourceImageBytes;
    manifest.provenance.source_image_sha256 = kSourceImageSha256;

    for (std::uint32_t level_id = 0U; level_id < openrc::kDiscTocLevelCount;
         ++level_id) {
      const auto package = make_level_package(
          level_id, level_id == 0U ? mutation : ProfileMutation::none);
      const auto package_bytes = openrc::encode_level_package_v1(
          package, kPackageLimits.level_package);
      const auto package_path = level_package_path(level_id);
      write_bytes(tree_.root / std::filesystem::path(package_path),
                  package_bytes);
      manifest.levels.push_back(openrc::PreparedGameLevelReferenceV2{
          level_id,
          package_path,
          static_cast<std::uint64_t>(package_bytes.size()),
          openrc::prepared_content_sha256_v1(package_bytes),
      });
    }

    if (shared_mutation != SharedMutation::missing_reference) {
      const auto bytes = openrc::encode_level_package_v1(
          make_shared_package(shared_mutation), kPackageLimits.level_package);
      const auto path = shared_mutation == SharedMutation::wrong_path ?
          std::string("other.orlevel") :
          std::string(openrc::kNativeGameSharedPackagePathV1);
      if (shared_mutation != SharedMutation::missing_file) {
        write_bytes(tree_.root / path, bytes);
      }
      manifest.shared_package = openrc::PreparedGameSharedReferenceV2{
          path, static_cast<std::uint64_t>(bytes.size()),
          openrc::prepared_content_sha256_v1(bytes)};
    }

    const auto manifest_bytes =
        openrc::encode_prepared_game_v2(manifest, kPackageLimits.manifest);
    write_bytes(tree_.root / openrc::kPreparedGameV2ManifestFileName,
                manifest_bytes);
    prepared_ =
        openrc::load_prepared_game_v2_root_v1(tree_.root, kPackageLimits);
  }

  void validate() const {
    openrc::validate_current_native_game_publication_v1(prepared_);
  }

private:
  TemporaryPublicationTree tree_;
  openrc::PreparedGameV2RootV1 prepared_;
};

void test_menu_flow_resource_profile() {
  using namespace openrc;
  const auto original=make_shared_package(SharedMutation::none);
  const auto accepted=[](const LevelPackageV1 &package) {
    return exact_native_menu_flow_profile_v1(package,kSourceImageBytes,kSourceImageSha256,kBootExecutableBytes,kBootExecutableSha256);
  };
  if(original.resources.size()!=58||!accepted(original))throw std::runtime_error("Complete synthetic51-resource menu flow profile was rejected");
  unsigned rejected=0;
  const auto rejects=[&](LevelPackageV1 package) {
    if(accepted(package))throw std::runtime_error("Native menu profile accepted an altered frontend resource");++rejected;
  };
  for(std::size_t i=7;i<original.resources.size();++i) {
    auto copy=original;copy.resources.erase(copy.resources.begin()+i);rejects(std::move(copy));
  }
  const auto mutate=[&](const char *id,const std::function<void(LevelPackageResourceV1&)> &change,bool repair_hash=true) {
    auto copy=original;auto found=std::find_if(copy.resources.begin(),copy.resources.end(),[&](const auto &r){return r.resource_id==id;});
    if(found==copy.resources.end())throw std::runtime_error("Mutation target absent");change(*found);
    if(repair_hash)found->payload_sha256=prepared_content_sha256_v1(found->payload);
    // Repair dependent provenance to exercise payload/semantic checks too.
    const auto link=prepared_resource_provenance(*found);
    for(auto &r:copy.resources)for(auto &p:r.provenance)
      if(p.kind==LevelPackageProvenanceKindV1::prepared_resource&&p.source_locator==found->resource_id)p=link;
    rejects(std::move(copy));
  };
  mutate("frontend/menu/actors",[](auto &r){r.flags=0;});
  mutate("frontend/menu/actors",[](auto &r){r.payload[0]^=std::byte{1};},false);
  mutate("frontend/menu/animation",[](auto &r){r.provenance.pop_back();});
  mutate("frontend/menu/timeline",[](auto &r){r.provenance.back().source_sha256[0]^=std::byte{1};});
  mutate("frontend/dialog/backdrop",[](auto &r){r.provenance[0].source_sha256[0]^=std::byte{1};});
  mutate("frontend/menu/timeline",[](auto &r){auto p=decode_scene_timeline_v1(r.payload);p.samples.pop_back();r.payload=encode_scene_timeline_v1(p);});
  mutate("frontend/menu/timeline",[](auto &r){auto p=decode_scene_timeline_v1(r.payload);p.samples[0].actors[6].enabled=true;r.payload=encode_scene_timeline_v1(p);});
  mutate("frontend/menu/lists",[](auto &r){auto p=decode_screen_overlay_v1(r.payload);p.frames[11]=p.frames[12];r.payload=encode_screen_overlay_v1(p);});
  mutate("frontend/dialog/backdrop",[](auto &r){auto p=decode_screen_overlay_v1(r.payload);p.images[0].rgb_coverage[3]=std::byte{49};r.payload=encode_screen_overlay_v1(p);});
  mutate("frontend/dialog/body/absent",[](auto &r){auto p=decode_screen_overlay_v1(r.payload);p.frames[25]=p.frames[0];r.payload=encode_screen_overlay_v1(p);});
  mutate("frontend/dialog/prompts/new",[](auto &r){auto p=decode_screen_overlay_v1(r.payload);p.frames.pop_back();r.payload=encode_screen_overlay_v1(p);});
  mutate("frontend/audio/variant-0",[](auto &r){auto p=decode_audio_clip_v1(r.payload);p.sample_rate=44100;r.payload=encode_audio_clip_v1(p);});
  mutate("frontend/audio/variant-1",[](auto &r){auto p=decode_audio_clip_v1(r.payload);p.channels=1;r.payload=encode_audio_clip_v1(p);});
  mutate("frontend/audio/variant-2",[](auto &r){r.provenance.pop_back();});
  mutate("frontend/audio/variant-3",[](auto &r){for(auto &p:r.provenance)if(p.source_locator=="rac1/frontend-sound-bank")p.source_sha256[0]^=std::byte{1};});
  mutate("frontend/audio/variant-4",[](auto &r){for(auto &p:r.provenance)if(p.source_locator=="rac1/iop-module-bundle")p.source_offset+=2048;});
  mutate("frontend/audio/ambient-program",[](auto &r){auto p=decode_audio_program_bank_v1(r.payload);
      p.ticks_per_second=239;r.payload=encode_audio_program_bank_v1(p);});
  mutate("frontend/audio/ambient-program",[](auto &r){auto p=decode_audio_program_bank_v1(r.payload);
      p.random.forward_tap=104;r.payload=encode_audio_program_bank_v1(p);});
  mutate("frontend/audio/ambient-program",[](auto &r){auto p=decode_audio_program_bank_v1(r.payload);
      auto marker=std::find_if(p.programs[0].nodes.begin(),p.programs[0].nodes.end(),[](const auto& n){
        return std::holds_alternative<AudioProgramStopSectionV1>(n.action);});
      p.programs[0].nodes[marker->next].delay_ticks=1;r.payload=encode_audio_program_bank_v1(p);});
  mutate("frontend/audio/ambient-bank",[](auto &r){r.provenance.pop_back();});
  mutate("frontend/audio/ambient-bank",[](auto &r){auto p=decode_audio_voice_bank_v1(r.payload);
      p.program_resource_id="frontend/audio/variant-0";r.payload=encode_audio_voice_bank_v1(p);});
  mutate("frontend/audio/ambient-bank",[](auto &r){auto p=decode_audio_voice_bank_v1(r.payload);
      p.observation.zero_observations_before_completion=3;r.payload=encode_audio_voice_bank_v1(p);});
  mutate("frontend/audio/ambient-bank",[](auto &r){auto p=decode_audio_voice_bank_v1(r.payload);
      p.bindings[0].read_ahead.reset();r.payload=encode_audio_voice_bank_v1(p);});
  mutate("frontend/audio/ambient-bank",[](auto &r){auto p=decode_audio_voice_bank_v1(r.payload);
      p.phase_curves[0].increments[0]=20000;r.payload=encode_audio_voice_bank_v1(p);});
  mutate("frontend/audio/ambient-stream-0",[](auto &r){auto p=decode_audio_stream_v1(r.payload);
      p.output_sample_rate=44100;r.payload=encode_audio_stream_v1(p);});
  mutate("frontend/audio/ambient-stream-3",[](auto &r){auto p=decode_audio_stream_v1(r.payload);
      p.repeat_begin=p.repeat_end=0;r.payload=encode_audio_stream_v1(p);});
  mutate("frontend/audio/ambient-gain-0",[](auto &r){for(auto &p:r.provenance)
      if(p.source_locator=="rac1/frontend-sound-bank")p.source_sha256[0]^=std::byte{1};});
  mutate("frontend/audio/ambient-gain-1",[](auto &r){auto p=decode_audio_gain_table_v1(r.payload);
      p.gain_denominator=65536;r.payload=encode_audio_gain_table_v1(p);});
  mutate("frontend/audio/ambient-cues",[](auto &r){auto p=decode_audio_program_cues_v1(r.payload);
      ++p.cues[4].first_scene_sample;r.payload=encode_audio_program_cues_v1(p);});
  mutate("frontend/audio/ambient-cues",[](auto &r){auto p=decode_audio_program_cues_v1(r.payload);
      p.timeline_resource_id="frontend/menu/timeline";r.payload=encode_audio_program_cues_v1(p);});
  mutate("frontend/audio/ambient-cues",[](auto &r){auto p=decode_audio_program_cues_v1(r.payload);
      p.cues[0].program_key=9;r.payload=encode_audio_program_cues_v1(p);});
  mutate("new-game/loading-1",[](auto &r){auto p=decode_loading_presentation_v1(r.payload);p.bands[0].y=178;r.payload=encode_loading_presentation_v1(p);});
  mutate("new-game/loading-2",[](auto &r){auto p=decode_loading_presentation_v1(r.payload);p.library.frames.pop_back();r.payload=encode_loading_presentation_v1(p);});
  mutate("new-game/movie-0",[](auto &r){auto p=decode_media_clip_v1(r.payload);p.audio_sample_rate=44100;r.payload=encode_media_clip_v1(p);});
  mutate("new-game/movie-1",[](auto &r){for(auto &p:r.provenance)if(p.source_locator=="disc/new-game-movies")p.source_offset=30000ULL*2048;});
  mutate("new-game/fade-5",[](auto &r){auto p=decode_frame_color_transfer_sequence_v1(r.payload);p.lead_updates=0;r.payload=encode_frame_color_transfer_sequence_v1(p);});
  mutate("new-game/fade-2",[](auto &r){auto p=decode_frame_color_transfer_sequence_v1(r.payload);p.transfers[0][127]^=std::byte{1};r.payload=encode_frame_color_transfer_sequence_v1(p);});
  mutate("new-game/fade-4",[](auto &r){auto p=decode_frame_color_transfer_sequence_v1(r.payload);p.tail_updates=0;r.payload=encode_frame_color_transfer_sequence_v1(p);});
  mutate("frontend/no-save-input",[](auto &r){auto p=decode_frontend_no_save_plan_v1(r.payload);p.reset_writes.pop_back();r.payload=encode_frontend_no_save_plan_v1(p);});
  mutate("frontend/no-save-input",[](auto &r){auto p=decode_frontend_no_save_plan_v1(r.payload);p.fields[0]=p.fields[1];r.payload=encode_frontend_no_save_plan_v1(p);});
  mutate("frontend/new-game-sequence",[](auto &r){auto p=decode_frontend_sequence_v1(r.payload);
      auto fade=std::find_if(p.cues.begin(),p.cues.end(),[](const auto &c){return c.kind==FrontendSequenceCueKindV1::fade;});
      if(fade==p.cues.end())throw std::runtime_error("Fixture lost fade");++fade->updates;r.payload=encode_frontend_sequence_v1(p);});
  mutate("frontend/new-game-sequence",[](auto &r){auto p=decode_frontend_sequence_v1(r.payload);p.cues.pop_back();r.payload=encode_frontend_sequence_v1(p);});
  mutate("frontend/session-state",[](auto &r){const auto limits=frontend_session_state_limits_v1();auto p=decode_session_state_initial_v1(r.payload,{4U*1024U*1024U,limits});
      for(auto &b:p.buffers)if(b.buffer_key=="frontend/config/title-fade-counter")b.bytes[0]=std::byte{51};
      r.payload=encode_session_state_initial_v1(p,{4U*1024U*1024U,limits});});
  auto duplicate=original;duplicate.resources.push_back(duplicate.resources[7]);rejects(std::move(duplicate));
  mutate("new-game/level-installation",[](auto& r){auto p=decode_state_installation_v1(r.payload);p.level_id=1U;r.payload=encode_state_installation_v1(p);});
  mutate("new-game/level-installation",[](auto& r){auto p=decode_state_installation_v1(r.payload);p.writes.pop_back();r.payload=encode_state_installation_v1(p);});
  mutate("new-game/level-installation",[](auto& r){auto p=decode_state_installation_v1(r.payload);p.writes.back().value_bits=1U;r.payload=encode_state_installation_v1(p);});
  mutate("new-game/level-installation",[](auto& r){auto p=decode_state_installation_v1(r.payload);std::swap(p.writes.front(),p.writes.back());r.payload=encode_state_installation_v1(p);});
  std::cout<<"Native menu flow: "<<(original.resources.size()-7U)
      <<" continuation resources accepted in "<<original.resources.size()
      <<" shared resources, "<<rejected<<" missing/tampered profiles rejected\n";
}

template <typename Callback>
void expect_native_profile_rejected(Callback &&callback,
                                    const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::NativeGamePreparationErrorV1 &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_complete_prepared_only_profile_is_accepted() {
  PublicationFixture fixture(ProfileMutation::none);
  fixture.validate();
}

void test_pre_loi_fix_compiler_cache_is_rejected() {
  const std::string current(openrc::kNativeGameCompilerVersionV1);
  const auto suffix = current.find("-native-eight-resource-");
  if (suffix == std::string::npos) {
    throw std::runtime_error("native compiler identity lacks its profile suffix");
  }
  for (const auto *old_suffix : {"-native-eight-resource-v4-moby749-initial",
                                 "-native-eight-resource-v5-vu-loi",
                                 "-native-eight-resource-v6-vu-addsub",
                                 "-native-eight-resource-v7-vu-mul",
                                 "-native-eight-resource-v12-frontend-transition"}) {
    const auto previous = current.substr(0U, suffix) + old_suffix;
    PublicationFixture fixture(ProfileMutation::none, previous);
    expect_native_profile_rejected(
        [&] { fixture.validate(); },
        "a structurally valid pre-numeric-fix cache was accepted as current");
  }
}

void test_shared_startup_profile_is_required() {
  constexpr std::array mutations{
      SharedMutation::missing_reference, SharedMutation::missing_file,
      SharedMutation::wrong_path, SharedMutation::missing_intro,
      SharedMutation::wrong_type, SharedMutation::bad_media,
      SharedMutation::wrong_aspect, SharedMutation::wrong_cadence,
      SharedMutation::missing_audio, SharedMutation::stale_image,
      SharedMutation::stale_boot, SharedMutation::wrong_movie,
      SharedMutation::out_of_image, SharedMutation::stale_compiler,
      SharedMutation::missing_bitmap, SharedMutation::bitmap_wrong_timing,
      SharedMutation::bitmap_wrong_transfer, SharedMutation::bitmap_wrong_source,
      SharedMutation::missing_frontend,SharedMutation::frontend_wrong_source,
      SharedMutation::frontend_wrong_timing,SharedMutation::frontend_stale_actors,
      SharedMutation::missing_geometry,SharedMutation::bad_geometry};
  for (const auto mutation : mutations) {
    PublicationFixture fixture(ProfileMutation::none,
                               openrc::kNativeGameCompilerVersionV1, mutation);
    expect_native_profile_rejected(
        [&] { fixture.validate(); },
        "the current profile accepted invalid/missing original startup resources");
  }
  PublicationFixture v7(ProfileMutation::none,
                         OPENRC_VERSION "-native-eight-resource-v7-vu-mul",
                         SharedMutation::missing_reference);
  expect_native_profile_rejected(
      [&] { v7.validate(); },
      "a former v7 level-only cache was accepted as complete current content");
}

void test_empty_crate_profile_is_rejected() {
  PublicationFixture fixture(ProfileMutation::empty_crates);
  expect_native_profile_rejected(
      [&] { fixture.validate(); },
      "the exact eight-resource profile accepted an empty crate scene");
}

void test_crate_binding_chain_is_rejected_when_broken() {
  PublicationFixture missing(ProfileMutation::missing_render_instance);
  expect_native_profile_rejected(
      [&] { missing.validate(); },
      "the exact profile accepted a crate binding to a missing instance");

  PublicationFixture disagreement(ProfileMutation::render_transform_disagrees);
  expect_native_profile_rejected(
      [&] { disagreement.validate(); },
      "the exact profile accepted disagreeing entity/render transforms");

  PublicationFixture negative(ProfileMutation::negative_uniform_scale);
  expect_native_profile_rejected(
      [&] { negative.validate(); },
      "the exact profile accepted a negative crate placement scale");

  PublicationFixture nonuniform(ProfileMutation::nonuniform_scale);
  expect_native_profile_rejected(
      [&] { nonuniform.validate(); },
      "the exact profile accepted a non-uniform crate placement scale");
}

void test_crates_must_share_one_render_mesh() {
  PublicationFixture fixture(ProfileMutation::split_crate_mesh);
  expect_native_profile_rejected(
      [&] { fixture.validate(); },
      "the exact profile accepted crates split across render meshes");
}

void test_crates_must_share_one_containing_hit_sphere() {
  PublicationFixture inconsistent(ProfileMutation::inconsistent_hit_sphere);
  expect_native_profile_rejected(
      [&] { inconsistent.validate(); },
      "the exact profile accepted inconsistent crate hit spheres");

  PublicationFixture misses(ProfileMutation::hit_sphere_misses_mesh);
  expect_native_profile_rejected(
      [&] { misses.validate(); },
      "the exact profile accepted a crate hit sphere missing its mesh");
}

void test_player_animation_profile_is_exact() {
  const std::array mutations{
      ProfileMutation::missing_animation,
      ProfileMutation::animation_wrong_key,
      ProfileMutation::animation_bad_cadence,
      ProfileMutation::animation_clamp,
      ProfileMutation::animation_stale_rig_digest,
      ProfileMutation::animation_wrong_joint_count,
      ProfileMutation::animation_missing_source_clip,
  };
  const std::array<std::string_view, mutations.size()> descriptions{
      "a missing animation bank",
      "an unexpected player animation key",
      "an unexpected player animation cadence",
      "a clamped player animation",
      "a stale player rig digest",
      "a player animation joint-count mismatch",
      "an incomplete source-addressed animation bank",
  };

  for (std::size_t index = 0U; index < mutations.size(); ++index) {
    PublicationFixture fixture(mutations[index]);
    expect_native_profile_rejected(
        [&] { fixture.validate(); },
        "the exact profile accepted " + std::string(descriptions[index]));
  }
}

} // namespace

int main() {
  try {
    test_menu_flow_resource_profile();
    test_complete_prepared_only_profile_is_accepted();
    test_pre_loi_fix_compiler_cache_is_rejected();
    test_shared_startup_profile_is_required();
    test_empty_crate_profile_is_rejected();
    test_crate_binding_chain_is_rejected_when_broken();
    test_crates_must_share_one_render_mesh();
    test_crates_must_share_one_containing_hit_sphere();
    test_player_animation_profile_is_exact();
    std::cout << "Native game preparation profile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Native game preparation profile tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
