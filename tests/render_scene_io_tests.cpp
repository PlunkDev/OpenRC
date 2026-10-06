#include "openrc/render_scene.hpp"
#include "openrc/render_scene_io.hpp"

#include <bit>
#include <cmath>
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

constexpr openrc::RenderSceneIoLimitsV1 kLimits{
    64U * 1024U * 1024U,
    {
        16U,
        8U,
        64U,
        32U,
        16U,
        64U,
        64U,
        4096U,
        4096U,
        16U * 1024U * 1024U,
        100'000U,
        300'000U,
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
  } catch (const openrc::RenderSceneIoError &) {
    return;
  }
  fail(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_u32(
    const std::span<const std::byte> bytes, const std::size_t offset) {
  return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t read_u64(
    const std::span<const std::byte> bytes, const std::size_t offset) {
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

void write_u64(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint64_t value) {
  for (std::size_t index = 0U; index < 8U; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
  }
}

[[nodiscard]] std::vector<std::byte> rgba(const std::uint32_t width,
                                          const std::uint32_t height,
                                          const std::uint8_t seed) {
  std::vector<std::byte> result(
      static_cast<std::size_t>(width) * height * 4U);
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = static_cast<std::byte>(
        static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(index)));
  }
  return result;
}

[[nodiscard]] openrc::RenderSceneV1 make_scene() {
  openrc::RenderSceneTextureV1 texture_1;
  texture_1.id = 1U;
  texture_1.color_space = openrc::RenderSceneTextureColorSpaceV1::linear;
  texture_1.mips.push_back({1U, 1U, rgba(1U, 1U, 0x70U)});

  openrc::RenderSceneTextureV1 texture_0;
  texture_0.id = 0U;
  texture_0.color_space = openrc::RenderSceneTextureColorSpaceV1::srgb;
  texture_0.mips.push_back({2U, 2U, rgba(2U, 2U, 0x10U)});
  texture_0.mips.push_back({1U, 1U, rgba(1U, 1U, 0x50U)});

  openrc::RenderSceneMaterialV1 material_2;
  material_2.id = 2U;
  material_2.base_color_rgba8 = UINT32_C(0xff804020);

  openrc::RenderSceneMaterialV1 material_0;
  material_0.id = 0U;
  material_0.base_color_texture_id = 0U;
  material_0.mipmap_filter = openrc::RenderSceneMipmapFilterV1::nearest;
  material_0.alpha_mode = openrc::RenderSceneAlphaModeV1::mask;
  material_0.alpha_cutoff_rgba8 = 1U;

  openrc::RenderSceneMaterialV1 material_1;
  material_1.id = 1U;
  material_1.base_color_texture_id = 1U;
  material_1.use_vertex_color = false;
  material_1.double_sided = false;
  material_1.address_u = openrc::RenderSceneAddressModeV1::clamp_to_edge;
  material_1.address_v = openrc::RenderSceneAddressModeV1::clamp_to_edge;
  material_1.min_filter = openrc::RenderSceneFilterV1::nearest;
  material_1.mag_filter = openrc::RenderSceneFilterV1::nearest;

  openrc::RenderSceneMeshV1 mesh_1;
  mesh_1.id = 1U;
  mesh_1.vertices = {
      {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, UINT32_C(0xffffffff)},
      {1.0F, 0.0F, 0.0F, 1.0F, 0.0F, UINT32_C(0xff0000ff)},
      {0.0F, 1.0F, 0.0F, 0.0F, 1.0F, UINT32_C(0xff00ff00)},
  };
  mesh_1.triangle_indices = {0U, 1U, 2U};
  mesh_1.draw_ranges = {{2U, 0U, 3U}};

  openrc::RenderSceneMeshV1 mesh_0;
  mesh_0.id = 0U;
  mesh_0.vertices = {
      {-0.0F, 0.0F, 0.0F, 0.0F, 0.0F, UINT32_C(0xffffffff)},
      {2.0F, 0.0F, 0.0F, 1.0F, 0.0F, UINT32_C(0xffffffff)},
      {2.0F, 2.0F, 0.0F, 1.0F, 1.0F, UINT32_C(0xffffffff)},
      {0.0F, 2.0F, 0.0F, 0.0F, 1.0F, UINT32_C(0xffffffff)},
  };
  mesh_0.triangle_indices = {0U, 1U, 2U, 0U, 2U, 3U};
  mesh_0.draw_ranges = {{0U, 0U, 3U}, {1U, 3U, 3U}};

  openrc::RenderSceneInstanceV1 instance_2;
  instance_2.id = 2U;
  instance_2.mesh_id = 0U;
  instance_2.local_to_world.values[3U] = 10.0F;

  openrc::RenderSceneInstanceV1 instance_0;
  instance_0.id = 0U;
  instance_0.mesh_id = 1U;
  instance_0.local_to_world.values[11U] = -0.0F;

  openrc::RenderSceneInstanceV1 instance_1;
  instance_1.id = 1U;
  instance_1.mesh_id = 0U;
  instance_1.local_to_world.values[0U] = 0.5F;
  instance_1.local_to_world.values[5U] = 0.5F;
  instance_1.local_to_world.values[10U] = 0.5F;

  openrc::RenderSceneV1 result;
  result.textures = {std::move(texture_1), std::move(texture_0)};
  result.materials = {material_2, material_0, material_1};
  result.meshes = {std::move(mesh_1), std::move(mesh_0)};
  result.instances = {instance_2, instance_0, instance_1};
  return result;
}

void test_identity_layout_canonicalization_and_round_trip() {
  const auto expected =
      openrc::canonicalize_render_scene_v1(make_scene(), kLimits.scene);
  const auto bytes = openrc::encode_render_scene_v1(make_scene(), kLimits);
  expect(bytes.size() == 1100U,
         "RenderSceneV1 exact encoded size changed unexpectedly");
  expect(read_u32(bytes, 0x08U) == 1U &&
             read_u32(bytes, 0x0cU) == 0x100U &&
             read_u64(bytes, 0x10U) == bytes.size(),
         "RenderSceneV1 envelope fields are wrong");
  expect(read_u32(bytes, 0x30U) == 2U &&
             read_u32(bytes, 0x34U) == 3U &&
             read_u32(bytes, 0x38U) == 3U &&
             read_u32(bytes, 0x3cU) == 2U &&
             read_u32(bytes, 0x40U) == 3U &&
             read_u32(bytes, 0x44U) == 3U &&
             read_u64(bytes, 0x48U) == 7U &&
             read_u64(bytes, 0x50U) == 9U &&
             read_u64(bytes, 0x58U) == 24U,
         "RenderSceneV1 header counts are wrong");
  expect(read_u64(bytes, 0x80U) == 256U &&
             read_u64(bytes, 0x88U) == 320U &&
             read_u64(bytes, 0x90U) == 416U &&
             read_u64(bytes, 0x98U) == 512U &&
             read_u64(bytes, 0xa0U) == 608U &&
             read_u64(bytes, 0xa8U) == 680U &&
             read_u64(bytes, 0xb0U) == 848U &&
             read_u64(bytes, 0xb8U) == 1040U &&
             read_u64(bytes, 0xc0U) == 1076U,
         "RenderSceneV1 canonical table offsets are wrong");

  const auto decoded = openrc::decode_render_scene_v1(bytes, kLimits);
  expect(decoded == expected,
         "RenderSceneV1 canonical round trip changed logical content");
  expect(!std::signbit(decoded.meshes[0U].vertices[0U].x) &&
             !std::signbit(
                 decoded.instances[0U].local_to_world.values[11U]),
         "RenderSceneV1 encoder did not canonicalize signed zero");
  expect(openrc::encode_render_scene_v1(decoded, kLimits) == bytes,
         "RenderSceneV1 re-encoding is not byte deterministic");

  const openrc::RenderSceneV1 empty;
  const auto empty_bytes = openrc::encode_render_scene_v1(empty, kLimits);
  expect(empty_bytes.size() == openrc::kRenderSceneIoHeaderBytesV1 &&
             openrc::decode_render_scene_v1(empty_bytes, kLimits) == empty,
         "Canonical empty RenderSceneV1 did not round-trip");
}

template <typename Mutation>
void expect_corrupt_decode(Mutation &&mutation, const std::string &message) {
  auto bytes = openrc::encode_render_scene_v1(make_scene(), kLimits);
  std::forward<Mutation>(mutation)(bytes);
  expect_io_error(
      [&] { (void)openrc::decode_render_scene_v1(bytes, kLimits); }, message);
}

void test_header_and_float_corruption() {
  expect_corrupt_decode(
      [](auto &bytes) { bytes[0U] = std::byte{'X'}; },
      "decoder accepted corrupt RenderSceneV1 magic");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 0x08U, 2U); },
      "decoder accepted an unknown RenderSceneV1 version");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 0x74U, 28U); },
      "decoder accepted a wrong vertex record size");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[0xc8U] = std::byte{1U}; },
      "decoder accepted non-zero header reserved data");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, 0x80U, 0U); },
      "decoder accepted a non-canonical table offset");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, 0x10U, bytes.size() - 1U); },
      "decoder accepted a false total byte count");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 680U, UINT32_C(0x80000000)); },
      "decoder accepted negative zero in a vertex");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 680U, UINT32_C(0x7fc00000)); },
      "decoder accepted NaN in a vertex");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 848U + 16U, UINT32_C(0x7f800000)); },
      "decoder accepted infinity in an instance transform");

  auto trailing = openrc::encode_render_scene_v1(make_scene(), kLimits);
  trailing.push_back(std::byte{0U});
  expect_io_error(
      [&] { (void)openrc::decode_render_scene_v1(trailing, kLimits); },
      "decoder accepted trailing RenderSceneV1 data");
}

void test_texture_and_material_corruption() {
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 256U, 1U); },
      "decoder accepted a non-dense texture ID");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[261U] = std::byte{1U}; },
      "decoder accepted texture reserved data");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 256U + 8U, 1U); },
      "decoder accepted a gapped texture mip range");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 320U + 8U, 3U); },
      "decoder accepted a mip dimension inconsistent with its texture");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, 320U + 16U, 1U); },
      "decoder accepted a gapped mip pixel range");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 416U + 12U, 4U); },
      "decoder accepted an unknown material flag");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[416U + 16U] = std::byte{9U}; },
      "decoder accepted an unknown material address mode");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 416U + 4U, 99U); },
      "decoder accepted a missing material texture");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[416U + 32U + 22U] = std::byte{1U}; },
      "decoder accepted an opaque material with an alpha cutoff");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[416U + 30U] = std::byte{1U}; },
      "decoder accepted material reserved data");
}

void test_mesh_draw_instance_and_index_corruption() {
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 512U, 1U); },
      "decoder accepted a non-dense mesh ID");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, 512U + 8U, 1U); },
      "decoder accepted a gapped mesh vertex range");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 608U, 1U); },
      "decoder accepted a draw attached to the wrong mesh");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, 608U + 8U, 3U); },
      "decoder accepted a draw-range gap");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 1040U, 99U); },
      "decoder accepted an out-of-range local mesh index");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[848U + 12U] = std::byte{1U}; },
      "decoder accepted instance reserved data");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 848U + 4U, 99U); },
      "decoder accepted an instance referencing a missing mesh");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, 848U, 1U); },
      "decoder accepted a non-dense instance ID");
}

template <typename Mutation>
void expect_invalid_encode(Mutation &&mutation, const std::string &message) {
  auto scene = openrc::canonicalize_render_scene_v1(make_scene(), kLimits.scene);
  std::forward<Mutation>(mutation)(scene);
  expect_io_error(
      [&] { (void)openrc::encode_render_scene_v1(scene, kLimits); }, message);
}

void test_encoder_semantics_and_limits() {
  expect_invalid_encode(
      [](auto &scene) { scene.schema_version = 2U; },
      "encoder accepted an unknown scene schema");
  expect_invalid_encode(
      [](auto &scene) { scene.textures[1U].id = 0U; },
      "encoder accepted duplicate texture IDs");
  expect_invalid_encode(
      [](auto &scene) { scene.textures[0U].mips.clear(); },
      "encoder accepted a texture without mip zero");
  expect_invalid_encode(
      [](auto &scene) { scene.textures[0U].mips[1U].width = 2U; },
      "encoder accepted a malformed mip chain");
  expect_invalid_encode(
      [](auto &scene) {
        scene.textures[0U].mips.push_back(
            {1U, 1U, rgba(1U, 1U, 0x60U)});
      },
      "encoder accepted a mip chain continuing past 1x1");
  expect_invalid_encode(
      [](auto &scene) { scene.textures[0U].mips[0U].rgba8.pop_back(); },
      "encoder accepted an inexact RGBA8 mip payload");
  expect_invalid_encode(
      [](auto &scene) {
        scene.materials[2U].address_u =
            openrc::RenderSceneAddressModeV1::clamp_to_edge;
      },
      "encoder accepted non-canonical untextured sampler state");
  expect_invalid_encode(
      [](auto &scene) {
        scene.materials[1U].mipmap_filter =
            openrc::RenderSceneMipmapFilterV1::linear;
      },
      "encoder accepted mip filtering from a one-level texture");
  expect_invalid_encode(
      [](auto &scene) { scene.meshes[0U].draw_ranges[1U].first_index = 0U; },
      "encoder accepted overlapping draw ranges");
  expect_invalid_encode(
      [](auto &scene) { scene.meshes[0U].triangle_indices[0U] = 99U; },
      "encoder accepted a missing local vertex reference");
  expect_invalid_encode(
      [](auto &scene) {
        scene.meshes[0U].vertices[0U].x =
            std::numeric_limits<float>::quiet_NaN();
      },
      "encoder accepted a non-finite vertex");
  expect_invalid_encode(
      [](auto &scene) { scene.instances.erase(scene.instances.begin()); },
      "encoder accepted non-dense instance IDs after removal");
  expect_invalid_encode(
      [](auto &scene) {
        openrc::RenderSceneTextureV1 orphan;
        orphan.id = 2U;
        orphan.mips.push_back({1U, 1U, rgba(1U, 1U, 0xa0U)});
        scene.textures.push_back(std::move(orphan));
      },
      "encoder accepted an unused texture");

  const auto bytes = openrc::encode_render_scene_v1(make_scene(), kLimits);
  auto byte_limit = kLimits;
  byte_limit.max_encoded_bytes = bytes.size() - 1U;
  expect_io_error(
      [&] { (void)openrc::encode_render_scene_v1(make_scene(), byte_limit); },
      "encoder ignored its encoded-byte limit");
  expect_io_error(
      [&] { (void)openrc::decode_render_scene_v1(bytes, byte_limit); },
      "decoder ignored its encoded-byte limit");

  auto count_limit = kLimits;
  count_limit.scene.max_vertices = 6U;
  expect_io_error(
      [&] { (void)openrc::encode_render_scene_v1(make_scene(), count_limit); },
      "encoder ignored its vertex limit");
  expect_io_error(
      [&] { (void)openrc::decode_render_scene_v1(bytes, count_limit); },
      "decoder ignored its vertex limit before allocation");

  auto pixel_limit = kLimits;
  pixel_limit.scene.max_total_rgba8_bytes = 23U;
  expect_io_error(
      [&] { (void)openrc::decode_render_scene_v1(bytes, pixel_limit); },
      "decoder ignored its pixel-data limit before allocation");

  auto invalid_limits = kLimits;
  invalid_limits.scene.max_meshes = 0U;
  expect_io_error(
      [&] { (void)openrc::decode_render_scene_v1(bytes, invalid_limits); },
      "decoder accepted an unbounded zero scene limit");
}

void test_encoded_color_extension() {
  using namespace openrc;
  const auto legacy=canonicalize_render_scene_v1(make_scene(),kLimits.scene);
  const auto old_bytes=encode_render_scene_v1(legacy,kLimits);
  for(std::size_t material=0U;material<legacy.materials.size();++material)
    for(std::size_t byte=23U;byte<32U;++byte)
      expect(old_bytes[416U+32U*material+byte]==std::byte{},"Legacy material extension is not wire-zero");
  for(std::size_t instance=0U;instance<legacy.instances.size();++instance)
    expect(read_u64(old_bytes,848U+64U*instance+8U)==0U,"Legacy instance extension is not wire-zero");
  auto extended=legacy;auto &m=extended.materials[1U];
  m.use_vertex_color=true;m.color_math=RenderSceneColorMathV1::encoded_integer;
  m.blend_mode=RenderSceneBlendModeV1::source_over;
  m.interpolation=RenderSceneInterpolationV1::affine;m.depth_test=RenderSceneDepthTestV1::always;
  m.depth_write=false;m.texture_modulation_denominator=128U;m.blend_denominator=128U;
  m.alpha_mode=RenderSceneAlphaModeV1::mask;m.alpha_cutoff_rgba8=96U;
  m.alpha_failure=RenderSceneAlphaFailureV1::rgb_only;
  extended.instances[0U].camera_relative=true;extended.instances[0U].project_to_far_plane=true;
  const auto bytes=encode_render_scene_v1(extended,kLimits);
  constexpr std::array<std::uint8_t,9U> expected{1U,1U,1U,1U,1U,128U,128U,1U,0U};
  for(std::size_t i=0U;i<expected.size();++i)
    expect(byte_value(bytes[448U+23U+i])==expected[i],"Extended material field wire position changed");
  expect(bytes.size()==old_bytes.size() && read_u32(bytes,856U)==3U &&
      decode_render_scene_v1(bytes,kLimits)==extended,"Encoded material/camera/far flags did not round-trip");
  extended.materials[1U]=legacy.materials[1U];extended.instances[0U]=legacy.instances[0U];
  expect(encode_render_scene_v1(extended,kLimits)==old_bytes,"Restored legacy policy changed encoded bytes");
  for(const auto [offset,value]:std::array{
      std::pair{23U,2U},std::pair{24U,2U},std::pair{25U,2U},std::pair{26U,2U},
      std::pair{27U,2U},std::pair{28U,255U},std::pair{29U,255U},std::pair{30U,2U},std::pair{31U,1U}}) {
    auto corrupt=bytes;corrupt[448U+offset]=static_cast<std::byte>(value);
    expect_io_error([&]{(void)decode_render_scene_v1(corrupt,kLimits);},"Decoder admitted unknown/noncanonical material extension");
  }
  auto corrupt=bytes;write_u32(corrupt,856U,4U);
  expect_io_error([&]{(void)decode_render_scene_v1(corrupt,kLimits);},"Decoder admitted unknown instance extension flag");
  const auto rejects=[&](auto change) {auto invalid=decode_render_scene_v1(bytes,kLimits);change(invalid);
    expect_io_error([&]{(void)encode_render_scene_v1(invalid,kLimits);},"Encoder admitted an unsupported encoded material combination");};
  rejects([](auto &s){s.materials[1U].base_color_rgba8=0xff000000U;});
  rejects([](auto &s){s.materials[1U].use_vertex_color=false;});
  rejects([](auto &s){s.textures[1U].color_space=RenderSceneTextureColorSpaceV1::srgb;});
  rejects([](auto &s){s.materials[1U].mipmap_filter=RenderSceneMipmapFilterV1::linear;});
  rejects([](auto &s){s.materials[1U].color_math=RenderSceneColorMathV1::linear;});
  rejects([](auto &s){s.materials[1U].blend_mode=RenderSceneBlendModeV1::opaque;});
  rejects([](auto &s){s.materials[1U].texture_modulation_denominator=0U;});
  rejects([](auto &s){s.materials[1U].blend_denominator=0U;});
  rejects([](auto &s){s.materials[1U].alpha_mode=RenderSceneAlphaModeV1::opaque;s.materials[1U].alpha_cutoff_rgba8=0U;});
  rejects([](auto &s){s.materials[1U].alpha_cutoff_rgba8=0U;});
  rejects([](auto &s){s.materials[1U].mag_filter=RenderSceneFilterV1::linear;});
  rejects([](auto &s){s.meshes[0U].vertices[0U].u=std::nextafter(16777216.0F,INFINITY);});
  rejects([](auto &s){s.meshes[0U].vertices[0U].v=std::nextafter(-16777216.0F,-INFINITY);});
  auto boundary=decode_render_scene_v1(bytes,kLimits);
  boundary.meshes[0U].vertices[0U].u=16777216.0F;
  boundary.meshes[0U].vertices[0U].v=-16777216.0F;
  expect(decode_render_scene_v1(encode_render_scene_v1(boundary,kLimits),kLimits)==boundary,
      "Exact encoded UV envelope boundary was rejected");
  rejects([](auto &s){s.materials[2U].texture_modulation_denominator=128U;});
  auto untextured=legacy;auto &raw=untextured.materials[2U];
  raw.base_color_rgba8=UINT32_C(0xffffffff);raw.color_math=RenderSceneColorMathV1::encoded_integer;
  raw.interpolation=RenderSceneInterpolationV1::affine;raw.depth_test=RenderSceneDepthTestV1::always;raw.depth_write=false;
  expect(decode_render_scene_v1(encode_render_scene_v1(untextured,kLimits),kLimits)==untextured,
      "Untextured raw vertex color or independent depth policy was rejected");
}

void test_practical_triangle_payload() {
  constexpr std::uint32_t kTriangles = 2048U;
  openrc::RenderSceneMaterialV1 material;
  openrc::RenderSceneMeshV1 mesh;
  mesh.vertices.reserve(kTriangles * 3U);
  mesh.triangle_indices.reserve(kTriangles * 3U);
  for (std::uint32_t triangle = 0U; triangle < kTriangles; ++triangle) {
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    const auto x = static_cast<float>(triangle % 64U);
    const auto y = static_cast<float>(triangle / 64U);
    mesh.vertices.push_back({x, y, 0.0F, 0.0F, 0.0F,
                             UINT32_C(0xffffffff)});
    mesh.vertices.push_back({x + 0.5F, y, 0.0F, 1.0F, 0.0F,
                             UINT32_C(0xffffffff)});
    mesh.vertices.push_back({x, y + 0.5F, 0.0F, 0.0F, 1.0F,
                             UINT32_C(0xffffffff)});
    mesh.triangle_indices.insert(mesh.triangle_indices.end(),
                                 {first, first + 1U, first + 2U});
  }
  mesh.draw_ranges.push_back({0U, 0U, mesh.triangle_indices.size()});
  openrc::RenderSceneV1 scene;
  scene.materials.push_back(material);
  scene.meshes.push_back(std::move(mesh));
  scene.instances.push_back({});

  const auto bytes = openrc::encode_render_scene_v1(scene, kLimits);
  expect(bytes.size() == 172'456U,
         "practical RenderSceneV1 payload is unexpectedly inflated");
  expect(openrc::decode_render_scene_v1(bytes, kLimits) == scene,
         "practical RenderSceneV1 payload did not round-trip");
}

} // namespace

int main() {
  try {
    test_identity_layout_canonicalization_and_round_trip();
    test_header_and_float_corruption();
    test_texture_and_material_corruption();
    test_mesh_draw_instance_and_index_corruption();
    test_encoder_semantics_and_limits();
    test_encoded_color_extension();
    test_practical_triangle_payload();
    std::cout << "render scene I/O tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "render scene I/O test failure: " << error.what() << '\n';
    return 1;
  }
}
