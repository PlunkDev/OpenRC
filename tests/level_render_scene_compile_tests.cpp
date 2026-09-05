#include "openrc/level_render_scene_compile.hpp"

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

constexpr openrc::RenderSceneIoLimitsV1 kSceneLimits{
    1U << 20U,
    {
        8U,
        8U,
        16U,
        8U,
        8U,
        16U,
        16U,
        64U,
        64U,
        4096U,
        1024U,
        3072U,
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
  } catch (const openrc::LevelRenderSceneCompileError &) {
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
mod_source(const std::string_view identity = "render-scene-mod") {
  return direct_source(openrc::LevelPackageProvenanceKindV1::mod_resource,
                       "mods/render-scene-test/source", 0U, identity);
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
  // Deliberately zero: the input writer must derive the canonical digest.
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

  // Deliberately reversed to prove that the attachment step canonicalizes the
  // whole package without changing either pre-existing resource.
  result.resources.push_back(base_resource(
      "world/collision", "openrc.collision-world", "compiler/test/collision-v1",
      {std::byte{0x04}, std::byte{0x05}, std::byte{0x06}}));
  result.resources.push_back(base_resource(
      "world/bootstrap", "openrc.level-bootstrap", "compiler/test/bootstrap-v1",
      {std::byte{0x01}, std::byte{0x02}}));
  return result;
}

[[nodiscard]] openrc::RenderSceneV1 make_scene(const float x = -0.0F) {
  openrc::RenderSceneMaterialV1 material;
  material.id = 0U;

  openrc::RenderSceneMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      {x, 0.0F, 0.0F, 0.0F, 0.0F, UINT32_C(0xffffffff)},
      {1.0F, 0.0F, 0.0F, 1.0F, 0.0F, UINT32_C(0xff00ffff)},
      {0.0F, 1.0F, 0.0F, 0.0F, 1.0F, UINT32_C(0xffff00ff)},
  };
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};

  openrc::RenderSceneInstanceV1 instance;
  instance.id = 0U;
  instance.mesh_id = 0U;

  openrc::RenderSceneV1 result;
  result.materials.push_back(material);
  result.meshes.push_back(std::move(mesh));
  result.instances.push_back(instance);
  return result;
}

[[nodiscard]] std::array<openrc::LevelPackageProvenanceV1, 2U> make_sources() {
  // Deliberately reversed relative to the canonical enum ordering.
  return {
      direct_source(openrc::LevelPackageProvenanceKindV1::prepared_resource,
                    "rac1/level/000/decoded/scene", 0U,
                    "complete-decoded-scene"),
      direct_source(openrc::LevelPackageProvenanceKindV1::iso_range,
                    "disc/level/000/scene-block", 0x123400U,
                    "complete-disc-range"),
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

void test_success_order_round_trip_determinism_and_exact_provenance() {
  const auto sources = make_sources();
  const auto first = openrc::attach_render_scene_to_level_package_v1(
      make_base(), make_scene(), sources, kSceneLimits, kPackageLimits);
  const auto second = openrc::attach_render_scene_to_level_package_v1(
      make_base(), make_scene(), sources, kSceneLimits, kPackageLimits);
  const auto first_bytes =
      openrc::encode_level_package_v1(first, kPackageLimits);
  const auto second_bytes =
      openrc::encode_level_package_v1(second, kPackageLimits);

  expect(first_bytes == second_bytes,
         "render-scene package attachment is not byte deterministic");
  expect(first.resources.size() == 3U &&
             first.resources[0U].resource_id == "world/bootstrap" &&
             first.resources[1U].resource_id == "world/collision" &&
             first.resources[2U].resource_id == "world/render-scene",
         "render-scene attachment returned non-canonical resource order");

  const auto &resource = find_resource(first, openrc::kRenderSceneResourceIdV1);
  expect(
      resource.type_id == openrc::kRenderSceneResourceTypeIdV1 &&
          resource.schema_version ==
              openrc::kRenderSceneResourceSchemaVersionV1 &&
          resource.operation ==
              openrc::LevelPackageResourceOperationV1::upsert &&
          resource.flags == openrc::kLevelPackageResourceOverlayReplaceableV1 &&
          (resource.flags & openrc::kLevelPackageResourceOverlayRemovableV1) ==
              0U &&
          resource.payload_sha256 ==
              openrc::prepared_content_sha256_v1(resource.payload),
      "render-scene resource identity, flags, or digest are wrong");

  expect(resource.provenance.size() == 3U &&
             provenance_equal(resource.provenance[0U], sources[1U]) &&
             provenance_equal(resource.provenance[1U], sources[0U]),
         "render-scene direct provenance is not exact and canonical");
  const auto &pass = resource.provenance[2U];
  expect(pass.kind == openrc::LevelPackageProvenanceKindV1::generated &&
             pass.source_locator == openrc::kLevelRenderSceneCompilePassV1 &&
             pass.source_offset == 0U && pass.source_bytes == 0U &&
             openrc::is_zero_prepared_digest_v1(pass.source_sha256),
         "render-scene compiler-pass provenance is not exact");

  const auto decoded =
      openrc::decode_render_scene_v1(resource.payload, kSceneLimits);
  expect(decoded == openrc::canonicalize_render_scene_v1(make_scene(),
                                                         kSceneLimits.scene),
         "attached RenderSceneV1 payload changed neutral scene semantics");
  const auto parsed =
      openrc::parse_level_package_v1(first_bytes, kPackageLimits);
  expect(openrc::encode_level_package_v1(parsed, kPackageLimits) == first_bytes,
         "attached package did not survive a canonical round trip");

  auto stripped = first;
  std::erase_if(
      stripped.resources, [](const openrc::LevelPackageResourceV1 &candidate) {
        return candidate.resource_id == openrc::kRenderSceneResourceIdV1;
      });
  expect(openrc::encode_level_package_v1(stripped, kPackageLimits) ==
             openrc::encode_level_package_v1(make_base(), kPackageLimits),
         "render-scene attachment rewrote an existing resource");
}

void test_rejects_duplicate_overlay_and_dishonest_provenance() {
  const auto sources = make_sources();

  auto duplicate = make_base();
  duplicate.resources.push_back(base_resource(
      openrc::kRenderSceneResourceIdV1, openrc::kRenderSceneResourceTypeIdV1,
      "compiler/test/existing-render-scene", {std::byte{0x01}}));
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            duplicate, make_scene(), sources, kSceneLimits, kPackageLimits));
      },
      "attachment accepted an existing world/render-scene resource");

  auto overlay = make_base();
  overlay.layer_kind = openrc::LevelPackageLayerKindV1::overlay;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            overlay, make_scene(), sources, kSceneLimits, kPackageLimits));
      },
      "attachment accepted an overlay package");

  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            make_base(), make_scene(), {}, kSceneLimits, kPackageLimits));
      },
      "attachment accepted empty source provenance");

  for (const auto dishonest_kind :
       {openrc::LevelPackageProvenanceKindV1::generated,
        openrc::LevelPackageProvenanceKindV1::mod_resource}) {
    auto dishonest = sources;
    dishonest[0U].kind = dishonest_kind;
    expect_compile_error(
        [&] {
          static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
              make_base(), make_scene(), dishonest, kSceneLimits,
              kPackageLimits));
        },
        "attachment accepted generated or mod source provenance");
  }

  auto zero_bytes = sources;
  zero_bytes[0U].source_bytes = 0U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            make_base(), make_scene(), zero_bytes, kSceneLimits,
            kPackageLimits));
      },
      "attachment accepted zero-byte source provenance");

  auto zero_digest = sources;
  zero_digest[0U].source_sha256 = {};
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            make_base(), make_scene(), zero_digest, kSceneLimits,
            kPackageLimits));
      },
      "attachment accepted source provenance without a digest");

  auto overflowing = sources;
  overflowing[0U].source_offset = std::numeric_limits<std::uint64_t>::max();
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            make_base(), make_scene(), overflowing, kSceneLimits,
            kPackageLimits));
      },
      "attachment accepted an overflowing source range");

  const std::array repeated{sources[0U], sources[0U]};
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            make_base(), make_scene(), repeated, kSceneLimits, kPackageLimits));
      },
      "attachment accepted duplicate source provenance");
}

void test_rejects_every_relevant_limit() {
  const auto sources = make_sources();
  const auto scene_payload =
      openrc::encode_render_scene_v1(make_scene(), kSceneLimits);
  const auto canonical_base_bytes =
      openrc::encode_level_package_v1(make_base(), kPackageLimits);
  constexpr std::uint64_t kBasePayloadBytes = 5U;

  auto limits = kPackageLimits;
  limits.max_resources = 2U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its package resource limit");

  limits = kPackageLimits;
  limits.max_provenance_per_resource = 2U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its per-resource provenance limit");

  limits = kPackageLimits;
  limits.max_total_provenance_records = 4U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its aggregate provenance limit");

  limits = kPackageLimits;
  limits.max_payload_bytes = scene_payload.size() - 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its per-resource payload limit");

  limits = kPackageLimits;
  limits.max_total_payload_bytes =
      kBasePayloadBytes + scene_payload.size() - 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its aggregate payload limit");

  limits = kPackageLimits;
  limits.max_input_bytes = canonical_base_bytes.size();
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            make_base(), make_scene(), sources, kSceneLimits, limits));
      },
      "attachment ignored its encoded package byte limit");

  auto scene_limits = kSceneLimits;
  scene_limits.max_encoded_bytes = openrc::kRenderSceneIoHeaderBytesV1;
  expect_compile_error(
      [&] {
        static_cast<void>(openrc::attach_render_scene_to_level_package_v1(
            make_base(), make_scene(), sources, scene_limits, kPackageLimits));
      },
      "attachment ignored its RenderSceneV1 encoded-byte limit");
}

[[nodiscard]] openrc::LevelPackageV1
make_scene_overlay(const openrc::LevelPackageV1 &base,
                   const openrc::LevelPackageResourceOperationV1 operation) {
  openrc::LevelPackageV1 result;
  result.level_id = base.level_id;
  result.content_api_version = base.content_api_version;
  result.build_id = base.build_id;
  result.layer_kind = openrc::LevelPackageLayerKindV1::overlay;
  result.layer_id = operation == openrc::LevelPackageResourceOperationV1::upsert
                        ? "mods/render-scene-replacement"
                        : "mods/render-scene-removal";
  result.priority = 10;
  result.required_base_package_sha256 =
      openrc::level_package_sha256_v1(base, kPackageLimits);

  openrc::LevelPackageResourceV1 resource;
  resource.resource_id = std::string(openrc::kRenderSceneResourceIdV1);
  resource.type_id = std::string(openrc::kRenderSceneResourceTypeIdV1);
  resource.schema_version = openrc::kRenderSceneResourceSchemaVersionV1;
  resource.operation = operation;
  resource.provenance.push_back(mod_source());
  if (operation == openrc::LevelPackageResourceOperationV1::upsert) {
    resource.flags = openrc::kLevelPackageResourceOverlayReplaceableV1;
    resource.payload =
        openrc::encode_render_scene_v1(make_scene(2.0F), kSceneLimits);
  }
  result.resources.push_back(std::move(resource));
  return result;
}

void test_overlay_replacement_allowed_but_removal_forbidden() {
  const auto sources = make_sources();
  const auto base = openrc::attach_render_scene_to_level_package_v1(
      make_base(), make_scene(), sources, kSceneLimits, kPackageLimits);

  const auto replacement =
      make_scene_overlay(base, openrc::LevelPackageResourceOperationV1::upsert);
  const std::array replacement_layers{replacement};
  const auto resolved = openrc::resolve_level_package_v1(
      base, replacement_layers, kPackageLimits);
  const auto resolved_scene = std::find_if(
      resolved.resources.begin(), resolved.resources.end(),
      [](const openrc::LevelPackageResourceV1 &resource) {
        return resource.resource_id == openrc::kRenderSceneResourceIdV1;
      });
  expect(resolved_scene != resolved.resources.end() &&
             resolved_scene->payload == replacement.resources[0U].payload,
         "an explicit overlay could not replace the render scene");

  const auto removal =
      make_scene_overlay(base, openrc::LevelPackageResourceOperationV1::remove);
  const std::array removal_layers{removal};
  expect_package_error(
      [&] {
        static_cast<void>(openrc::resolve_level_package_v1(base, removal_layers,
                                                           kPackageLimits));
      },
      "an overlay removed the required render scene");
}

} // namespace

int main() {
  try {
    test_success_order_round_trip_determinism_and_exact_provenance();
    test_rejects_duplicate_overlay_and_dishonest_provenance();
    test_rejects_every_relevant_limit();
    test_overlay_replacement_allowed_but_removal_forbidden();
    std::cout << "level render-scene compile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "level render-scene compile tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
