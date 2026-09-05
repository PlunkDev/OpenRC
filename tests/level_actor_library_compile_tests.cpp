#include "openrc/level_actor_library_compile.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr openrc::ActorLibraryIoLimitsV1 kActorLimits{
    1U << 20U,
    {
        4U,
        4U,
        64U,
        512U,
        32U,
        64U,
        8U,
        8U,
        16U,
        8U,
        8U,
        16U,
        1024U,
        3072U,
        64U,
        64U,
        4096U,
        16'384U,
    },
};

constexpr openrc::LevelPackageV1Limits kPackageLimits{
    2U * 1024U * 1024U, 8U, 8U, 32U, 128U, 1U * 1024U * 1024U,
    2U * 1024U * 1024U, 8U,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_compile_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::LevelActorLibraryCompileError &) {
    return;
  }
  throw std::runtime_error(message);
}

template <typename Callback>
void expect_package_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::LevelPackageV1Error &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::PreparedContentDigestV1
digest_of(const std::string_view value) {
  return openrc::prepared_content_sha256_v1(std::as_bytes(std::span(value)));
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
generated(const std::string_view pass) {
  return {
      openrc::LevelPackageProvenanceKindV1::generated,
      std::string(pass),
      0U,
      0U,
      {},
  };
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
direct_source(const openrc::LevelPackageProvenanceKindV1 kind,
              const std::string_view locator, const std::uint64_t offset,
              const std::string_view identity) {
  return {
      kind, std::string(locator), offset, identity.size(), digest_of(identity),
  };
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
mod_source(const std::string_view identity = "actor-library-mod") {
  return direct_source(openrc::LevelPackageProvenanceKindV1::mod_resource,
                       "mods/actor-library-test/source", 0U, identity);
}

[[nodiscard]] openrc::LevelPackageResourceV1
base_resource(const std::string_view id, const std::string_view type,
              const std::string_view pass, std::vector<std::byte> payload) {
  openrc::LevelPackageResourceV1 result;
  result.resource_id = std::string(id);
  result.type_id = std::string(type);
  result.schema_version = 1U;
  result.operation = openrc::LevelPackageResourceOperationV1::upsert;
  result.flags = openrc::kLevelPackageResourceOverlayReplaceableV1;
  result.provenance.push_back(generated(pass));
  result.payload = std::move(payload);
  return result;
}

[[nodiscard]] openrc::LevelPackageV1 make_base() {
  openrc::LevelPackageV1 result;
  result.level_id = 0U;
  result.content_api_version = 1U;
  result.build_id = "SCES-50916-PAL-v2.00";
  result.layer_kind = openrc::LevelPackageLayerKindV1::base;
  result.layer_id = "base";
  result.priority = 0;

  // Reversed input order proves that attaching the actor library preserves the
  // canonical bytes of both existing resources.
  result.resources.push_back(base_resource(
      "world/collision", "openrc.collision-world", "compiler/test/collision-v1",
      {std::byte{0x04}, std::byte{0x05}, std::byte{0x06}}));
  result.resources.push_back(base_resource(
      "world/bootstrap", "openrc.level-bootstrap", "compiler/test/bootstrap-v1",
      {std::byte{0x01}, std::byte{0x02}}));
  return result;
}

[[nodiscard]] openrc::ActorAffineTransformV1 identity_transform() {
  openrc::ActorAffineTransformV1 result;
  result.values = {
      1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
  };
  return result;
}

[[nodiscard]] openrc::ActorSkinBindingV1 rigid_skin() {
  openrc::ActorSkinBindingV1 result;
  result.influence_count = 1U;
  result.joint_indices = {0U, 0U, 0U};
  result.weight_numerators = {255U, 0U, 0U};
  result.weight_sum = 255U;
  return result;
}

[[nodiscard]] openrc::ActorSkinnedVertexV1
vertex(const float x, const float y, const float u, const float v) {
  openrc::ActorSkinnedVertexV1 result;
  result.x = x;
  result.y = y;
  result.z = -0.0F;
  result.nx = -0.0F;
  result.ny = 0.0F;
  result.nz = 1.0F;
  result.u = u;
  result.v = v;
  result.skin = rigid_skin();
  return result;
}

[[nodiscard]] openrc::ActorLibraryV1 make_library(const float x = -0.0F) {
  openrc::ActorRigAssetV1 rig;
  rig.id = 0U;
  rig.semantic_key = "actors/ratchet/rig";
  rig.rig.joints.push_back({-1, identity_transform(), identity_transform()});

  openrc::RenderSceneTextureV1 texture;
  texture.id = 0U;
  texture.color_space = openrc::RenderSceneTextureColorSpaceV1::srgb;
  texture.mips.push_back(
      {1U,
       1U,
       {std::byte{0x20}, std::byte{0x40}, std::byte{0x80}, std::byte{0xff}}});

  openrc::RenderSceneMaterialV1 material;
  material.id = 0U;
  material.base_color_texture_id = 0U;

  openrc::ActorSkinnedMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      vertex(x, 0.0F, 0.0F, 0.0F),
      vertex(1.0F, 0.0F, 1.0F, 0.0F),
      vertex(0.0F, 1.0F, 0.0F, 1.0F),
  };
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};

  openrc::ActorModelV1 model;
  model.id = 0U;
  model.semantic_key = "actors/ratchet/high";
  model.rig_key = rig.semantic_key;
  model.textures.push_back(std::move(texture));
  model.materials.push_back(material);
  model.meshes.push_back(std::move(mesh));

  openrc::ActorLibraryV1 result;
  result.rigs.push_back(std::move(rig));
  result.models.push_back(std::move(model));
  return result;
}

[[nodiscard]] std::array<openrc::LevelPackageProvenanceV1, 2U> make_sources() {
  // Deliberately reversed relative to canonical provenance-kind order.
  return {
      direct_source(openrc::LevelPackageProvenanceKindV1::prepared_resource,
                    "rac1/level/000/mobys/class-0000", 0U,
                    "decoded-ratchet-class"),
      direct_source(openrc::LevelPackageProvenanceKindV1::iso_range,
                    "disc/level/000/moby-block", 0x123400U,
                    "complete-moby-source-range"),
  };
}

[[nodiscard]] const openrc::LevelPackageResourceV1 &
find_resource(const openrc::LevelPackageV1 &package,
              const std::string_view id) {
  const auto found =
      std::find_if(package.resources.begin(), package.resources.end(),
                   [id](const openrc::LevelPackageResourceV1 &resource) {
                     return resource.resource_id == id;
                   });
  if (found == package.resources.end()) {
    throw std::runtime_error("test package is missing resource " +
                             std::string(id));
  }
  return *found;
}

[[nodiscard]] bool
provenance_equal(const openrc::LevelPackageProvenanceV1 &left,
                 const openrc::LevelPackageProvenanceV1 &right) {
  return left.kind == right.kind &&
         left.source_locator == right.source_locator &&
         left.source_offset == right.source_offset &&
         left.source_bytes == right.source_bytes &&
         left.source_sha256 == right.source_sha256;
}

void test_success_determinism_and_preservation() {
  const auto sources = make_sources();
  const auto first = openrc::attach_actor_library_to_level_package_v1(
      make_base(), make_library(), sources, kActorLimits, kPackageLimits);
  const auto second = openrc::attach_actor_library_to_level_package_v1(
      make_base(), make_library(), sources, kActorLimits, kPackageLimits);
  const auto first_bytes =
      openrc::encode_level_package_v1(first, kPackageLimits);
  const auto second_bytes =
      openrc::encode_level_package_v1(second, kPackageLimits);

  expect(first_bytes == second_bytes,
         "actor-library package attachment is not byte deterministic");
  expect(first.resources.size() == 3U &&
             first.resources[0U].resource_id == "actors/library" &&
             first.resources[1U].resource_id == "world/bootstrap" &&
             first.resources[2U].resource_id == "world/collision",
         "actor-library attachment returned non-canonical resource order");

  const auto &resource =
      find_resource(first, openrc::kActorLibraryResourceIdV1);
  expect(
      resource.type_id == openrc::kActorLibraryResourceTypeIdV1 &&
          resource.schema_version ==
              openrc::kActorLibraryResourceSchemaVersionV1 &&
          resource.operation ==
              openrc::LevelPackageResourceOperationV1::upsert &&
          resource.flags == openrc::kLevelPackageResourceOverlayReplaceableV1 &&
          (resource.flags & openrc::kLevelPackageResourceOverlayRemovableV1) ==
              0U &&
          resource.payload_sha256 ==
              openrc::prepared_content_sha256_v1(resource.payload),
      "actor-library resource identity, flags, or digest are wrong");
  expect(resource.provenance.size() == 3U &&
             provenance_equal(resource.provenance[0U], sources[1U]) &&
             provenance_equal(resource.provenance[1U], sources[0U]),
         "actor-library direct provenance is not exact and canonical");
  const auto &pass = resource.provenance[2U];
  expect(pass.kind == openrc::LevelPackageProvenanceKindV1::generated &&
             pass.source_locator == openrc::kLevelActorLibraryCompilePassV1 &&
             pass.source_offset == 0U && pass.source_bytes == 0U &&
             openrc::is_zero_prepared_digest_v1(pass.source_sha256),
         "actor-library compiler-pass provenance is not exact");

  const auto decoded =
      openrc::decode_actor_library_v1(resource.payload, kActorLimits);
  expect(decoded == openrc::canonicalize_actor_library_v1(make_library(),
                                                          kActorLimits.library),
         "attached ActorLibraryV1 payload changed neutral actor semantics");
  const auto parsed =
      openrc::parse_level_package_v1(first_bytes, kPackageLimits);
  expect(openrc::encode_level_package_v1(parsed, kPackageLimits) == first_bytes,
         "attached package did not survive a canonical round trip");

  auto stripped = first;
  std::erase_if(
      stripped.resources, [](const openrc::LevelPackageResourceV1 &candidate) {
        return candidate.resource_id == openrc::kActorLibraryResourceIdV1;
      });
  expect(openrc::encode_level_package_v1(stripped, kPackageLimits) ==
             openrc::encode_level_package_v1(make_base(), kPackageLimits),
         "actor-library attachment rewrote an existing resource");
}

void test_rejects_duplicate_overlay_and_bad_provenance() {
  const auto sources = make_sources();

  auto duplicate = make_base();
  duplicate.resources.push_back(base_resource(
      openrc::kActorLibraryResourceIdV1, openrc::kActorLibraryResourceTypeIdV1,
      "compiler/test/existing-actor-library", {std::byte{0x01}}));
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            duplicate, make_library(), sources, kActorLimits, kPackageLimits));
      },
      "attachment accepted an existing actors/library resource");

  auto overlay = make_base();
  overlay.layer_kind = openrc::LevelPackageLayerKindV1::overlay;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            overlay, make_library(), sources, kActorLimits, kPackageLimits));
      },
      "attachment accepted an overlay package");

  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), make_library(), {}, kActorLimits, kPackageLimits));
      },
      "attachment accepted empty source provenance");

  for (const auto dishonest_kind :
       {openrc::LevelPackageProvenanceKindV1::generated,
        openrc::LevelPackageProvenanceKindV1::mod_resource}) {
    auto dishonest = sources;
    dishonest[0U].kind = dishonest_kind;
    expect_compile_error(
        [&] {
          static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
              make_base(), make_library(), dishonest, kActorLimits,
              kPackageLimits));
        },
        "attachment accepted generated or mod source provenance");
  }

  auto zero_bytes = sources;
  zero_bytes[0U].source_bytes = 0U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), make_library(), zero_bytes, kActorLimits,
            kPackageLimits));
      },
      "attachment accepted zero-byte source provenance");

  auto zero_digest = sources;
  zero_digest[0U].source_sha256 = {};
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), make_library(), zero_digest, kActorLimits,
            kPackageLimits));
      },
      "attachment accepted source provenance without a digest");

  auto overflowing = sources;
  overflowing[0U].source_offset = std::numeric_limits<std::uint64_t>::max();
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), make_library(), overflowing, kActorLimits,
            kPackageLimits));
      },
      "attachment accepted an overflowing source range");

  const std::array repeated{sources[0U], sources[0U]};
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), make_library(), repeated, kActorLimits,
            kPackageLimits));
      },
      "attachment accepted duplicate source provenance");
}

void test_rejects_tight_limits_and_invalid_library() {
  const auto sources = make_sources();
  const auto actor_payload =
      openrc::encode_actor_library_v1(make_library(), kActorLimits);
  const auto canonical_base_bytes =
      openrc::encode_level_package_v1(make_base(), kPackageLimits);
  constexpr std::uint64_t kBasePayloadBytes = 5U;

  auto limits = kPackageLimits;
  limits.max_resources = 2U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), make_library(), sources, kActorLimits, limits));
      },
      "attachment ignored its package resource limit");

  limits = kPackageLimits;
  limits.max_provenance_per_resource = 2U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), make_library(), sources, kActorLimits, limits));
      },
      "attachment ignored its per-resource provenance limit");

  limits = kPackageLimits;
  limits.max_total_provenance_records = 4U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), make_library(), sources, kActorLimits, limits));
      },
      "attachment ignored its aggregate provenance limit");

  limits = kPackageLimits;
  limits.max_payload_bytes = actor_payload.size() - 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), make_library(), sources, kActorLimits, limits));
      },
      "attachment ignored its per-resource payload limit");

  limits = kPackageLimits;
  limits.max_total_payload_bytes =
      kBasePayloadBytes + actor_payload.size() - 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), make_library(), sources, kActorLimits, limits));
      },
      "attachment ignored its aggregate payload limit");

  limits = kPackageLimits;
  limits.max_input_bytes = canonical_base_bytes.size();
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), make_library(), sources, kActorLimits, limits));
      },
      "attachment ignored its encoded package byte limit");

  auto actor_limits = kActorLimits;
  actor_limits.max_encoded_bytes = openrc::kActorLibraryIoHeaderBytesV1;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), make_library(), sources, actor_limits,
            kPackageLimits));
      },
      "attachment ignored its ActorLibraryV1 encoded-byte limit");

  auto invalid = make_library();
  invalid.models[0U].meshes[0U].vertices[0U].skin.weight_sum = 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_actor_library_to_level_package_v1(
            make_base(), invalid, sources, kActorLimits, kPackageLimits));
      },
      "attachment accepted an invalid ActorLibraryV1");
}

[[nodiscard]] openrc::LevelPackageV1
make_actor_overlay(const openrc::LevelPackageV1 &base,
                   const openrc::LevelPackageResourceOperationV1 operation) {
  openrc::LevelPackageV1 result;
  result.level_id = base.level_id;
  result.content_api_version = base.content_api_version;
  result.build_id = base.build_id;
  result.layer_kind = openrc::LevelPackageLayerKindV1::overlay;
  result.layer_id = operation == openrc::LevelPackageResourceOperationV1::upsert
                        ? "mods/actor-library-replacement"
                        : "mods/actor-library-removal";
  result.priority = 10;
  result.required_base_package_sha256 =
      openrc::level_package_sha256_v1(base, kPackageLimits);

  openrc::LevelPackageResourceV1 resource;
  resource.resource_id = std::string(openrc::kActorLibraryResourceIdV1);
  resource.type_id = std::string(openrc::kActorLibraryResourceTypeIdV1);
  resource.schema_version = openrc::kActorLibraryResourceSchemaVersionV1;
  resource.operation = operation;
  resource.provenance.push_back(mod_source());
  if (operation == openrc::LevelPackageResourceOperationV1::upsert) {
    resource.flags = openrc::kLevelPackageResourceOverlayReplaceableV1;
    resource.payload =
        openrc::encode_actor_library_v1(make_library(2.0F), kActorLimits);
  }
  result.resources.push_back(std::move(resource));
  return result;
}

void test_overlay_replacement_allowed_but_removal_forbidden() {
  const auto sources = make_sources();
  const auto base = openrc::attach_actor_library_to_level_package_v1(
      make_base(), make_library(), sources, kActorLimits, kPackageLimits);

  const auto replacement =
      make_actor_overlay(base, openrc::LevelPackageResourceOperationV1::upsert);
  const std::array replacement_layers{replacement};
  const auto resolved = openrc::resolve_level_package_v1(
      base, replacement_layers, kPackageLimits);
  const auto resolved_actor = std::find_if(
      resolved.resources.begin(), resolved.resources.end(),
      [](const openrc::LevelPackageResourceV1 &resource) {
        return resource.resource_id == openrc::kActorLibraryResourceIdV1;
      });
  expect(resolved_actor != resolved.resources.end() &&
             resolved_actor->payload == replacement.resources[0U].payload,
         "an explicit overlay could not replace the actor library");

  const auto removal =
      make_actor_overlay(base, openrc::LevelPackageResourceOperationV1::remove);
  const std::array removal_layers{removal};
  expect_package_error(
      [&] {
        static_cast<void>(openrc::resolve_level_package_v1(base, removal_layers,
                                                           kPackageLimits));
      },
      "an overlay removed the required actor library");
}

} // namespace

int main() {
  try {
    test_success_determinism_and_preservation();
    test_rejects_duplicate_overlay_and_bad_provenance();
    test_rejects_tight_limits_and_invalid_library();
    test_overlay_replacement_allowed_but_removal_forbidden();
    std::cout << "level actor-library compile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "level actor-library compile tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
