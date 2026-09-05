#include "openrc/actor_library.hpp"
#include "openrc/actor_library_io.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::ActorLibraryIoLimitsV1 kLimits{
    4U * 1024U * 1024U,
    {
        8U,
        8U,
        64U,
        1024U,
        64U,
        256U,
        32U,
        8U,
        128U,
        32U,
        16U,
        128U,
        16'384U,
        49'152U,
        4096U,
        4096U,
        16U * 1024U * 1024U,
        32U * 1024U * 1024U,
    },
};

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

template <typename Function>
void expect_io_error(Function &&function, const std::string &message) {
  try {
    std::forward<Function>(function)();
  } catch (const openrc::ActorLibraryIoError &) {
    return;
  }
  fail(message);
}

template <typename Function>
void expect_library_error(Function &&function, const std::string &message) {
  try {
    std::forward<Function>(function)();
  } catch (const openrc::ActorLibraryError &) {
    return;
  }
  fail(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_u32(const std::span<const std::byte> bytes,
                                     const std::size_t offset) {
  return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t read_u64(const std::span<const std::byte> bytes,
                                     const std::size_t offset) {
  std::uint64_t result = 0U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    result |= static_cast<std::uint64_t>(byte_value(bytes[offset + index]))
              << (index * 8U);
  }
  return result;
}

void write_u32(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT32_C(0xff));
  }
}

[[nodiscard]] std::vector<std::byte> rgba(const std::uint32_t width,
                                          const std::uint32_t height,
                                          const std::uint8_t seed) {
  std::vector<std::byte> result(static_cast<std::size_t>(width) * height * 4U);
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = static_cast<std::byte>(
        static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(index)));
  }
  return result;
}

[[nodiscard]] openrc::ActorAffineTransformV1
affine(const float translate_x = 0.0F) {
  openrc::ActorAffineTransformV1 result;
  result.values = {
      1.0F, 0.0F, 0.0F, translate_x, 0.0F, 1.0F,
      0.0F, 0.0F, 0.0F, 0.0F,        1.0F, 0.0F,
  };
  return result;
}

[[nodiscard]] openrc::ActorRigAssetV1
make_rig(const std::uint32_t id, std::string key, const float offset) {
  openrc::ActorRigAssetV1 result;
  result.id = id;
  result.semantic_key = std::move(key);
  result.rig.joints = {
      {-1, affine(-0.0F), affine()},
      {0, affine(offset), affine(-offset)},
  };
  return result;
}

[[nodiscard]] openrc::ActorSkinBindingV1 skin() {
  openrc::ActorSkinBindingV1 result;
  result.influence_count = 2U;
  result.joint_indices = {1U, 0U, 0U};
  result.weight_numerators = {64U, 192U, 0U};
  result.weight_sum = 256U;
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
  result.skin = skin();
  return result;
}

[[nodiscard]] openrc::ActorModelV1
make_model(const std::uint32_t id, std::string key, std::string rig_key,
           const bool mipmapped, const std::uint8_t seed) {
  openrc::ActorModelV1 result;
  result.id = id;
  result.semantic_key = std::move(key);
  result.rig_key = std::move(rig_key);

  openrc::RenderSceneTextureV1 texture;
  texture.id = 0U;
  texture.color_space = openrc::RenderSceneTextureColorSpaceV1::srgb;
  if (mipmapped) {
    texture.mips.push_back({2U, 2U, rgba(2U, 2U, seed)});
    texture.mips.push_back(
        {1U, 1U, rgba(1U, 1U, static_cast<std::uint8_t>(seed + 16U))});
  } else {
    texture.mips.push_back({1U, 1U, rgba(1U, 1U, seed)});
  }
  result.textures.push_back(std::move(texture));

  openrc::RenderSceneMaterialV1 material;
  material.id = 0U;
  material.base_color_texture_id = 0U;
  material.mipmap_filter = mipmapped
                               ? openrc::RenderSceneMipmapFilterV1::nearest
                               : openrc::RenderSceneMipmapFilterV1::none;
  result.materials.push_back(material);

  openrc::ActorSkinnedMeshV1 mesh;
  mesh.id = 0U;
  mesh.vertices = {
      vertex(-0.0F, 0.0F, 0.0F, 0.0F),
      vertex(1.0F, 0.0F, 1.0F, 0.0F),
      vertex(0.0F, 1.0F, 0.0F, 1.0F),
  };
  mesh.vertices[1U].rgba8 = UINT32_C(0xff804020);
  mesh.vertices[1U].nx = 0.25F;
  mesh.vertices[1U].ny = 0.5F;
  mesh.vertices[1U].nz = 0.75F;
  mesh.triangle_indices = {0U, 1U, 2U};
  mesh.draw_ranges = {{0U, 0U, 3U}};
  result.meshes.push_back(std::move(mesh));
  return result;
}

[[nodiscard]] openrc::ActorLibraryV1 make_library(const bool reversed = true) {
  auto rig_0 = make_rig(0U, "actors/ratchet/rig", 1.0F);
  auto rig_1 = make_rig(1U, "actors/clank/rig", 2.0F);
  auto model_0 =
      make_model(0U, "actors/ratchet/high", "actors/ratchet/rig", true, 0x10U);
  auto model_1 =
      make_model(1U, "actors/clank/high", "actors/clank/rig", false, 0x70U);
  openrc::ActorLibraryV1 result;
  if (reversed) {
    result.rigs = {std::move(rig_1), std::move(rig_0)};
    result.models = {std::move(model_1), std::move(model_0)};
  } else {
    result.rigs = {std::move(rig_0), std::move(rig_1)};
    result.models = {std::move(model_0), std::move(model_1)};
  }
  return result;
}

void test_identity_round_trip_and_determinism() {
  expect(openrc::kActorLibraryResourceIdV1 == "actors/library" &&
             openrc::kActorLibraryResourceTypeIdV1 == "openrc.actor-library" &&
             openrc::kActorLibraryResourceSchemaVersionV1 == 1U,
         "ActorLibraryV1 prepared-resource identity changed");

  const auto canonical =
      openrc::canonicalize_actor_library_v1(make_library(), kLimits.library);
  const auto bytes = openrc::encode_actor_library_v1(make_library(), kLimits);
  const auto ordered_bytes =
      openrc::encode_actor_library_v1(make_library(false), kLimits);
  expect(bytes == ordered_bytes,
         "ActorLibraryV1 table order changed its canonical bytes");
  expect(bytes.size() == 1664U,
         "ActorLibraryV1 exact encoded size changed unexpectedly");
  expect(read_u32(bytes, 0x08U) == 1U && read_u32(bytes, 0x0cU) == 0xa0U &&
             read_u64(bytes, 0x10U) == bytes.size() &&
             read_u32(bytes, 0x18U) == 1U,
         "ActorLibraryV1 envelope fields are wrong");
  expect(read_u32(bytes, 0x1cU) == 2U && read_u32(bytes, 0x20U) == 2U &&
             read_u32(bytes, 0x24U) == 2U && read_u32(bytes, 0x28U) == 3U &&
             read_u32(bytes, 0x2cU) == 2U && read_u32(bytes, 0x30U) == 2U &&
             read_u64(bytes, 0x38U) == 4U && read_u64(bytes, 0x40U) == 2U &&
             read_u64(bytes, 0x48U) == 6U && read_u64(bytes, 0x50U) == 6U &&
             read_u64(bytes, 0x58U) == 24U && read_u64(bytes, 0x60U) == 104U,
         "ActorLibraryV1 aggregate header counts are wrong");

  const auto decoded = openrc::decode_actor_library_v1(bytes, kLimits);
  expect(decoded == canonical,
         "ActorLibraryV1 canonical round trip changed logical content");
  expect(!std::signbit(
             decoded.rigs[0U].rig.joints[0U].local_bind_transform.values[3U]) &&
             !std::signbit(decoded.models[0U].meshes[0U].vertices[0U].x) &&
             !std::signbit(decoded.models[0U].meshes[0U].vertices[0U].z) &&
             !std::signbit(decoded.models[0U].meshes[0U].vertices[0U].nx),
         "ActorLibraryV1 did not canonicalize signed zero");
  const auto &binding = decoded.models[0U].meshes[0U].vertices[0U].skin;
  expect(binding.joint_indices[0U] == 0U && binding.joint_indices[1U] == 1U &&
             binding.weight_numerators[0U] == 192U &&
             binding.weight_numerators[1U] == 64U,
         "ActorLibraryV1 did not canonicalize skin influence order");
  const auto &normal = decoded.models[0U].meshes[0U].vertices[1U];
  expect(normal.nx == 0.25F && normal.ny == 0.5F && normal.nz == 0.75F &&
             !openrc::is_zero_prepared_digest_v1(
                 decoded.rigs[0U].content_sha256) &&
             !openrc::is_zero_prepared_digest_v1(
                 decoded.models[0U].content_sha256),
         "ActorLibraryV1 lost normals or content addresses");
  expect(openrc::encode_actor_library_v1(decoded, kLimits) == bytes,
         "ActorLibraryV1 re-encoding is not byte deterministic");

  const openrc::ActorLibraryV1 empty;
  const auto empty_bytes = openrc::encode_actor_library_v1(empty, kLimits);
  expect(empty_bytes.size() == openrc::kActorLibraryIoHeaderBytesV1 &&
             openrc::decode_actor_library_v1(empty_bytes, kLimits) == empty,
         "Canonical empty ActorLibraryV1 did not round-trip");
}

template <typename Mutation>
void expect_corrupt_decode(Mutation &&mutation, const std::string &message) {
  auto bytes = openrc::encode_actor_library_v1(make_library(), kLimits);
  std::forward<Mutation>(mutation)(bytes);
  expect_io_error(
      [&] { (void)openrc::decode_actor_library_v1(bytes, kLimits); }, message);
}

void test_bounded_envelope_and_reserved_rejection() {
  expect_corrupt_decode([](auto &bytes) { bytes[0U] = std::byte{'X'}; },
                        "decoder accepted corrupt ActorLibraryV1 magic");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 0x08U, 2U); },
      "decoder accepted an unknown ActorLibraryV1 format version");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[0x68U] = std::byte{1U}; },
      "decoder accepted non-zero ActorLibraryV1 header reserved data");
  expect_corrupt_decode([](auto &bytes) { bytes[0xacU] = std::byte{1U}; },
                        "decoder accepted a non-zero actor rig reserved word");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[0xe6U] = std::byte{1U}; },
      "decoder accepted a non-zero actor joint reserved word");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[0x2daU] = std::byte{1U}; },
      "decoder accepted a non-zero actor model reserved word");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 0x1cU, UINT32_MAX); },
      "decoder accepted a rig count beyond the caller limit");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 0xa8U, UINT32_MAX); },
      "decoder accepted a nested joint count beyond its pre-allocation limit");
  expect_corrupt_decode([](auto &bytes) { bytes.push_back(std::byte{0U}); },
                        "decoder accepted trailing ActorLibraryV1 data");

  auto small = kLimits;
  small.max_encoded_bytes = 1024U;
  expect_io_error(
      [&] {
        (void)openrc::decode_actor_library_v1(
            openrc::encode_actor_library_v1(make_library(), kLimits), small);
      },
      "decoder ignored its encoded-byte envelope");
}

void test_content_addresses_keys_and_rig_validation() {
  const auto addressed =
      openrc::canonicalize_actor_library_v1(make_library(), kLimits.library);
  auto renamed_source = make_library(false);
  renamed_source.rigs[0U].semantic_key = "actors/hero/rig";
  renamed_source.models[0U].semantic_key = "actors/hero/high";
  renamed_source.models[0U].rig_key = "actors/hero/rig";
  const auto renamed = openrc::canonicalize_actor_library_v1(
      std::move(renamed_source), kLimits.library);
  expect(renamed.rigs[0U].content_sha256 == addressed.rigs[0U].content_sha256 &&
             renamed.models[0U].content_sha256 ==
                 addressed.models[0U].content_sha256,
         "semantic aliases changed content addresses");

  auto changed_rig_source = make_library(false);
  changed_rig_source.rigs[0U].rig.joints[1U].local_bind_transform =
      affine(3.0F);
  changed_rig_source.rigs[0U].rig.joints[1U].inverse_bind_transform =
      affine(-3.0F);
  const auto changed_rig = openrc::canonicalize_actor_library_v1(
      std::move(changed_rig_source), kLimits.library);
  expect(changed_rig.rigs[0U].content_sha256 !=
                 addressed.rigs[0U].content_sha256 &&
             changed_rig.models[0U].content_sha256 !=
                 addressed.models[0U].content_sha256,
         "model content address did not incorporate its rig content");

  auto stale = addressed;
  stale.models[0U].meshes[0U].vertices[0U].nx = 0.5F;
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(stale, kLimits); },
      "encoder accepted a stale model digest after a normal changed");

  auto bad_key = make_library();
  bad_key.rigs[0U].semantic_key = "Actors/clank/rig";
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(bad_key, kLimits); },
      "encoder accepted a non-canonical semantic key");

  auto sparse_ids = make_library();
  sparse_ids.models[0U].id = 3U;
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(sparse_ids, kLimits); },
      "encoder accepted non-dense actor model IDs");

  auto duplicate_key = make_library();
  duplicate_key.rigs[0U].semantic_key = duplicate_key.rigs[1U].semantic_key;
  duplicate_key.models[0U].rig_key = duplicate_key.rigs[0U].semantic_key;
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(duplicate_key, kLimits); },
      "encoder accepted duplicate rig semantic keys");

  auto missing_rig = make_library();
  missing_rig.models[0U].rig_key = "actors/missing/rig";
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(missing_rig, kLimits); },
      "encoder accepted a missing semantic rig reference");

  auto bad_parent = make_library();
  bad_parent.rigs[0U].rig.joints[1U].parent_index = 1;
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(bad_parent, kLimits); },
      "encoder accepted a non-parent-first rig");

  auto singular = make_library();
  singular.rigs[0U].rig.joints[0U].local_bind_transform.values[0U] = 0.0F;
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(singular, kLimits); },
      "encoder accepted a singular bind transform");
}

void test_skin_geometry_material_and_float_validation() {
  auto bad_weight_sum = make_library();
  bad_weight_sum.models[0U].meshes[0U].vertices[0U].skin.weight_sum = 255U;
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(bad_weight_sum, kLimits); },
      "encoder accepted skin weights with an inexact sum");

  auto missing_joint = make_library();
  missing_joint.models[0U].meshes[0U].vertices[0U].skin.joint_indices[0U] = 7U;
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(missing_joint, kLimits); },
      "encoder accepted a skin reference to a missing joint");

  auto incomplete_draw = make_library();
  incomplete_draw.models[0U].meshes[0U].draw_ranges[0U].index_count = 0U;
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(incomplete_draw, kLimits); },
      "encoder accepted an incomplete draw partition");

  auto missing_vertex = make_library();
  missing_vertex.models[0U].meshes[0U].triangle_indices[2U] = 9U;
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(missing_vertex, kLimits); },
      "encoder accepted a triangle reference to a missing vertex");

  auto missing_texture = make_library();
  missing_texture.models[0U].materials[0U].base_color_texture_id = 3U;
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(missing_texture, kLimits); },
      "encoder accepted a material reference to a missing texture");

  auto malformed_mip = make_library();
  malformed_mip.models[0U].textures[0U].mips[0U].rgba8.pop_back();
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(malformed_mip, kLimits); },
      "encoder accepted an inexact RGBA8 mip payload");

  auto non_finite_normal = make_library();
  non_finite_normal.models[0U].meshes[0U].vertices[0U].ny =
      std::numeric_limits<float>::infinity();
  expect_io_error(
      [&] {
        (void)openrc::encode_actor_library_v1(non_finite_normal, kLimits);
      },
      "encoder accepted a non-finite actor normal");

  auto canonical =
      openrc::canonicalize_actor_library_v1(make_library(), kLimits.library);
  canonical.models[0U].meshes[0U].vertices[0U].ny = -0.0F;
  expect_library_error(
      [&] { openrc::validate_actor_library_v1(canonical, kLimits.library); },
      "validator accepted non-canonical negative zero");
}

[[nodiscard]] openrc::ActorLibraryV1 single_actor_library(
    std::string rig_key, std::string model_key, const float rig_offset,
    const std::uint8_t texture_seed) {
  openrc::ActorLibraryV1 result;
  result.rigs.push_back(make_rig(0U, rig_key, rig_offset));
  result.models.push_back(make_model(0U, std::move(model_key),
                                     std::move(rig_key), false,
                                     texture_seed));
  return openrc::canonicalize_actor_library_v1(std::move(result),
                                                kLimits.library);
}

void test_library_composition_for_multiple_runtime_actors() {
  const auto ratchet = single_actor_library(
      "actors/ratchet/rig", "actors/ratchet/high", 1.0F, 0x10U);
  const auto toad = single_actor_library(
      "actors/horny-toad/rig", "actors/horny-toad/high", 2.0F, 0x40U);
  const std::vector sources{ratchet, toad};
  const auto combined = openrc::compose_actor_libraries_v1(
      std::span<const openrc::ActorLibraryV1>(sources), kLimits.library);
  expect(combined.rigs.size() == 2U && combined.models.size() == 2U &&
             combined.rigs[0U].id == 0U && combined.rigs[1U].id == 1U &&
             combined.models[0U].id == 0U &&
             combined.models[1U].id == 1U &&
             combined.models[1U].semantic_key == "actors/horny-toad/high" &&
             combined.models[1U].rig_key == "actors/horny-toad/rig",
         "actor-library composition did not retain two independent actors");

  auto ratchet_low = ratchet;
  ratchet_low.models[0U].semantic_key = "actors/ratchet/low";
  const std::vector shared_rig_sources{ratchet, ratchet_low};
  const auto shared_rig = openrc::compose_actor_libraries_v1(
      std::span<const openrc::ActorLibraryV1>(shared_rig_sources),
      kLimits.library);
  expect(shared_rig.rigs.size() == 1U && shared_rig.models.size() == 2U &&
             shared_rig.models[1U].rig_key == shared_rig.rigs[0U].semantic_key,
         "actor-library composition duplicated an identical shared rig");

  auto conflicting_rig = ratchet;
  conflicting_rig.rigs[0U].rig.joints[1U].local_bind_transform = affine(3.0F);
  conflicting_rig.rigs[0U].rig.joints[1U].inverse_bind_transform =
      affine(-3.0F);
  conflicting_rig.rigs[0U].content_sha256 = {};
  conflicting_rig.models[0U].content_sha256 = {};
  conflicting_rig = openrc::canonicalize_actor_library_v1(
      std::move(conflicting_rig), kLimits.library);
  const std::vector conflicting_sources{ratchet, conflicting_rig};
  expect_library_error(
      [&] {
        (void)openrc::compose_actor_libraries_v1(
            std::span<const openrc::ActorLibraryV1>(conflicting_sources),
            kLimits.library);
      },
      "actor-library composition accepted different rigs behind one key");

  const std::vector duplicate_sources{ratchet, ratchet};
  expect_library_error(
      [&] {
        (void)openrc::compose_actor_libraries_v1(
            std::span<const openrc::ActorLibraryV1>(duplicate_sources),
            kLimits.library);
      },
      "actor-library composition accepted a repeated model key");

  auto one_model_limit = kLimits.library;
  one_model_limit.max_models = 1U;
  expect_library_error(
      [&] {
        (void)openrc::compose_actor_libraries_v1(
            std::span<const openrc::ActorLibraryV1>(sources),
            one_model_limit);
      },
      "actor-library composition ignored aggregate caller limits");
}

void test_explicit_limits() {
  auto too_small = kLimits;
  too_small.library.max_vertices = 5U;
  expect_io_error(
      [&] { (void)openrc::encode_actor_library_v1(make_library(), too_small); },
      "encoder ignored the aggregate vertex limit");

  auto zero_limit = kLimits;
  zero_limit.library.max_models = 0U;
  expect_io_error(
      [&] {
        (void)openrc::decode_actor_library_v1(
            openrc::encode_actor_library_v1(make_library(), kLimits),
            zero_limit);
      },
      "decoder accepted an invalid zero-valued limit policy");
}

} // namespace

int main() {
  try {
    test_identity_round_trip_and_determinism();
    test_bounded_envelope_and_reserved_rejection();
    test_content_addresses_keys_and_rig_validation();
    test_skin_geometry_material_and_float_validation();
    test_library_composition_for_multiple_runtime_actors();
    test_explicit_limits();
    std::cout << "ActorLibraryV1 tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "ActorLibraryV1 tests failed: " << error.what() << '\n';
    return 1;
  }
}
