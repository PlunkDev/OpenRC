#include "openrc/actor_library_io.hpp"
#include "openrc/collision_world_io.hpp"
#include "openrc/content_api.hpp"
#include "openrc/destructible_scene_io.hpp"
#include "openrc/entity_scene_io.hpp"
#include "openrc/gameplay_scene_io.hpp"
#include "openrc/level_actor_library_compile.hpp"
#include "openrc/level_bootstrap.hpp"
#include "openrc/level_destructible_scene_compile.hpp"
#include "openrc/level_entity_scene_compile.hpp"
#include "openrc/level_gameplay_scene_compile.hpp"
#include "openrc/level_render_scene_compile.hpp"
#include "openrc/native_game_prepare.hpp"
#include "openrc/prepared_game_v2_fs.hpp"
#include "openrc/rac_level_foundation_compile.hpp"
#include "openrc/render_scene_io.hpp"
#include "openrc/runtime_level_content.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
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

[[nodiscard]] openrc::ActorLibraryV1 make_actor_library() {
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
      openrc::encode_actor_library_v1(make_actor_library(),
                                      kRuntimeLimits.actor_library),
      {source_provenance(openrc::LevelPackageProvenanceKindV1::iso_range,
                         "rac1/disc-image", kSourceImageBytes,
                         kSourceImageSha256),
       generated_provenance(openrc::kLevelActorLibraryCompilePassV1)});
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
      std::move(actor),        std::move(entity),    std::move(gameplay),
      std::move(destructible),
  };
  return result;
}

class PublicationFixture final {
public:
  explicit PublicationFixture(const ProfileMutation mutation) {
    openrc::PreparedGameV2 manifest;
    manifest.content_api_version = openrc::kOpenRcContentApiVersionV1;
    manifest.provenance.game_id = std::string(openrc::kNativeGameIdV1);
    manifest.provenance.build_id = std::string(openrc::kNativeGameBuildIdV1);
    manifest.provenance.compiler_id =
        std::string(openrc::kNativeGameCompilerIdV1);
    manifest.provenance.compiler_version =
        std::string(openrc::kNativeGameCompilerVersionV1);
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

void test_empty_crate_profile_is_rejected() {
  PublicationFixture fixture(ProfileMutation::empty_crates);
  expect_native_profile_rejected(
      [&] { fixture.validate(); },
      "the exact seven-resource profile accepted an empty crate scene");
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

} // namespace

int main() {
  try {
    test_complete_prepared_only_profile_is_accepted();
    test_empty_crate_profile_is_rejected();
    test_crate_binding_chain_is_rejected_when_broken();
    test_crates_must_share_one_render_mesh();
    test_crates_must_share_one_containing_hit_sphere();
    std::cout << "Native game preparation profile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Native game preparation profile tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
