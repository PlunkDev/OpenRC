#include "openrc/render_scene_io.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::array<std::byte, 8U> kMagic{
    std::byte{'O'}, std::byte{'R'}, std::byte{'R'}, std::byte{'S'},
    std::byte{'C'}, std::byte{'N'}, std::byte{0U}, std::byte{0U},
};

constexpr std::uint32_t kRightHandedZUpWorldUnits = 1U;
constexpr std::uint32_t kPositionUvRgba8VertexFormat = 1U;
constexpr std::uint32_t kTriangleListTopology = 1U;
constexpr std::uint32_t kRgba8TextureFormat = 1U;

constexpr std::uint64_t kFormatVersionOffset = 0x08U;
constexpr std::uint64_t kHeaderBytesOffset = 0x0cU;
constexpr std::uint64_t kTotalBytesOffset = 0x10U;
constexpr std::uint64_t kSceneSchemaOffset = 0x18U;
constexpr std::uint64_t kCoordinateSystemOffset = 0x1cU;
constexpr std::uint64_t kVertexFormatOffset = 0x20U;
constexpr std::uint64_t kTopologyOffset = 0x24U;
constexpr std::uint64_t kTextureFormatOffset = 0x28U;
constexpr std::uint64_t kHeaderSchemaReservedOffset = 0x2cU;
constexpr std::uint64_t kTextureCountOffset = 0x30U;
constexpr std::uint64_t kHeaderTextureMipCountOffset = 0x34U;
constexpr std::uint64_t kMaterialCountOffset = 0x38U;
constexpr std::uint64_t kMeshCountOffset = 0x3cU;
constexpr std::uint64_t kDrawRangeCountOffset = 0x40U;
constexpr std::uint64_t kInstanceCountOffset = 0x44U;
constexpr std::uint64_t kVertexCountOffset = 0x48U;
constexpr std::uint64_t kIndexCountOffset = 0x50U;
constexpr std::uint64_t kPixelBytesOffset = 0x58U;
constexpr std::uint64_t kTextureRecordBytesOffset = 0x60U;
constexpr std::uint64_t kTextureMipRecordBytesOffset = 0x64U;
constexpr std::uint64_t kMaterialRecordBytesOffset = 0x68U;
constexpr std::uint64_t kMeshRecordBytesOffset = 0x6cU;
constexpr std::uint64_t kDrawRangeRecordBytesOffset = 0x70U;
constexpr std::uint64_t kVertexRecordBytesOffset = 0x74U;
constexpr std::uint64_t kInstanceRecordBytesOffset = 0x78U;
constexpr std::uint64_t kIndexRecordBytesOffset = 0x7cU;
constexpr std::uint64_t kTextureTableOffsetOffset = 0x80U;
constexpr std::uint64_t kTextureMipTableOffsetOffset = 0x88U;
constexpr std::uint64_t kMaterialTableOffsetOffset = 0x90U;
constexpr std::uint64_t kMeshTableOffsetOffset = 0x98U;
constexpr std::uint64_t kDrawRangeTableOffsetOffset = 0xa0U;
constexpr std::uint64_t kVertexTableOffsetOffset = 0xa8U;
constexpr std::uint64_t kInstanceTableOffsetOffset = 0xb0U;
constexpr std::uint64_t kIndexTableOffsetOffset = 0xb8U;
constexpr std::uint64_t kPixelDataOffsetOffset = 0xc0U;
constexpr std::uint64_t kHeaderReservedOffset = 0xc8U;

constexpr std::uint64_t kTextureColorSpaceOffset = 0x04U;
constexpr std::uint64_t kTextureFirstMipOffset = 0x08U;
constexpr std::uint64_t kTextureMipCountOffset = 0x0cU;
constexpr std::uint64_t kTextureWidthOffset = 0x10U;
constexpr std::uint64_t kTextureHeightOffset = 0x14U;
constexpr std::uint64_t kTextureReservedOffset = 0x18U;

constexpr std::uint64_t kMipLevelOffset = 0x04U;
constexpr std::uint64_t kMipWidthOffset = 0x08U;
constexpr std::uint64_t kMipHeightOffset = 0x0cU;
constexpr std::uint64_t kMipPixelOffsetOffset = 0x10U;
constexpr std::uint64_t kMipPixelBytesOffset = 0x18U;

constexpr std::uint64_t kMaterialTextureIdOffset = 0x04U;
constexpr std::uint64_t kMaterialBaseColorOffset = 0x08U;
constexpr std::uint64_t kMaterialFlagsOffset = 0x0cU;
constexpr std::uint64_t kMaterialAddressUOffset = 0x10U;
constexpr std::uint64_t kMaterialAddressVOffset = 0x11U;
constexpr std::uint64_t kMaterialMinFilterOffset = 0x12U;
constexpr std::uint64_t kMaterialMagFilterOffset = 0x13U;
constexpr std::uint64_t kMaterialMipmapFilterOffset = 0x14U;
constexpr std::uint64_t kMaterialAlphaModeOffset = 0x15U;
constexpr std::uint64_t kMaterialAlphaCutoffOffset = 0x16U;
constexpr std::uint64_t kMaterialReservedOffset = 0x17U;
constexpr std::uint32_t kMaterialUsesVertexColor = UINT32_C(1) << 0U;
constexpr std::uint32_t kMaterialDoubleSided = UINT32_C(1) << 1U;
constexpr std::uint32_t kKnownMaterialFlags =
    kMaterialUsesVertexColor | kMaterialDoubleSided;
constexpr std::uint32_t kNoTextureId = UINT32_MAX;

constexpr std::uint64_t kMeshReservedOffset = 0x04U;
constexpr std::uint64_t kMeshFirstVertexOffset = 0x08U;
constexpr std::uint64_t kMeshVertexCountOffset = 0x10U;
constexpr std::uint64_t kMeshFirstIndexOffset = 0x18U;
constexpr std::uint64_t kMeshIndexCountOffset = 0x20U;
constexpr std::uint64_t kMeshFirstDrawOffset = 0x28U;
constexpr std::uint64_t kMeshDrawCountOffset = 0x2cU;

constexpr std::uint64_t kDrawMaterialIdOffset = 0x04U;
constexpr std::uint64_t kDrawFirstIndexOffset = 0x08U;
constexpr std::uint64_t kDrawIndexCountOffset = 0x10U;

constexpr std::uint64_t kVertexXOffset = 0x00U;
constexpr std::uint64_t kVertexYOffset = 0x04U;
constexpr std::uint64_t kVertexZOffset = 0x08U;
constexpr std::uint64_t kVertexUOffset = 0x0cU;
constexpr std::uint64_t kVertexVOffset = 0x10U;
constexpr std::uint64_t kVertexColorOffset = 0x14U;

constexpr std::uint64_t kInstanceMeshIdOffset = 0x04U;
constexpr std::uint64_t kInstanceReservedOffset = 0x08U;
constexpr std::uint64_t kInstanceMatrixOffset = 0x10U;

static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<float>::radix == 2);
static_assert(std::numeric_limits<float>::digits == 24);

[[noreturn]] void fail(const std::string &message) {
  throw RenderSceneIoError(message);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left, const std::uint64_t right,
    const char *const description) {
  if (left != 0U &&
      right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left * right;
}

[[nodiscard]] std::size_t host_size(const std::uint64_t value,
                                    const char *const description) {
  if (value > std::numeric_limits<std::size_t>::max() ||
      value > static_cast<std::uint64_t>(
                  std::numeric_limits<std::ptrdiff_t>::max())) {
    fail(std::string(description) + " exceeds the host addressable range");
  }
  return static_cast<std::size_t>(value);
}

void validate_limits(const RenderSceneIoLimitsV1 limits) {
  if (limits.max_encoded_bytes < kRenderSceneIoHeaderBytesV1) {
    fail("RenderSceneV1 encoded-byte limit is smaller than its header");
  }
  try {
    validate_render_scene_v1(RenderSceneV1{}, limits.scene);
  } catch (const RenderSceneError &error) {
    fail(std::string("Invalid RenderSceneV1 caller limits: ") + error.what());
  }
}

void require_range(const std::span<const std::byte> bytes,
                   const std::uint64_t offset, const std::uint64_t size,
                   const char *const description) {
  if (offset > bytes.size() || size > bytes.size() - offset) {
    fail(std::string(description) + " lies outside RenderSceneV1");
  }
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint8_t read_u8(const std::span<const std::byte> bytes,
                                   const std::uint64_t offset,
                                   const char *const description) {
  require_range(bytes, offset, 1U, description);
  return byte_value(bytes[host_size(offset, description)]);
}

[[nodiscard]] std::uint32_t read_u32(
    const std::span<const std::byte> bytes, const std::uint64_t offset,
    const char *const description) {
  require_range(bytes, offset, 4U, description);
  const auto begin = host_size(offset, description);
  return static_cast<std::uint32_t>(byte_value(bytes[begin])) |
         (static_cast<std::uint32_t>(byte_value(bytes[begin + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[begin + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[begin + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t read_u64(
    const std::span<const std::byte> bytes, const std::uint64_t offset,
    const char *const description) {
  require_range(bytes, offset, 8U, description);
  const auto begin = host_size(offset, description);
  std::uint64_t result = 0U;
  for (std::size_t index = 0U; index < sizeof(result); ++index) {
    result |= static_cast<std::uint64_t>(byte_value(bytes[begin + index]))
              << (index * 8U);
  }
  return result;
}

[[nodiscard]] float read_f32(const std::span<const std::byte> bytes,
                             const std::uint64_t offset,
                             const char *const description) {
  const auto value = std::bit_cast<float>(read_u32(bytes, offset, description));
  if (!std::isfinite(value)) {
    fail(std::string("RenderSceneV1 has a non-finite ") + description);
  }
  if (value == 0.0F && std::signbit(value)) {
    fail(std::string("RenderSceneV1 has non-canonical signed zero in ") +
         description);
  }
  return value;
}

void write_u8(std::vector<std::byte> &bytes, const std::uint64_t offset,
              const std::uint8_t value) {
  bytes[host_size(offset, "A RenderSceneV1 output byte offset")] =
      static_cast<std::byte>(value);
}

void write_u32(std::vector<std::byte> &bytes, const std::uint64_t offset,
               const std::uint32_t value) {
  const auto begin = host_size(offset, "A RenderSceneV1 output word offset");
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes[begin + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT32_C(0xff));
  }
}

void write_u64(std::vector<std::byte> &bytes, const std::uint64_t offset,
               const std::uint64_t value) {
  const auto begin = host_size(offset, "A RenderSceneV1 output word offset");
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes[begin + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
  }
}

void write_f32(std::vector<std::byte> &bytes, const std::uint64_t offset,
               const float value) {
  write_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] bool all_zero(const std::span<const std::byte> bytes,
                            const std::uint64_t begin,
                            const std::uint64_t end,
                            const char *const description) {
  if (end < begin) {
    fail(std::string(description) + " has an inverted range");
  }
  require_range(bytes, begin, end - begin, description);
  const auto range = bytes.subspan(host_size(begin, description),
                                   host_size(end - begin, description));
  return std::all_of(range.begin(), range.end(), [](const std::byte value) {
    return value == std::byte{0U};
  });
}

struct SceneCounts final {
  std::uint32_t textures = 0U;
  std::uint32_t texture_mips = 0U;
  std::uint32_t materials = 0U;
  std::uint32_t meshes = 0U;
  std::uint32_t draw_ranges = 0U;
  std::uint32_t instances = 0U;
  std::uint64_t vertices = 0U;
  std::uint64_t indices = 0U;
  std::uint64_t pixel_bytes = 0U;
};

[[nodiscard]] std::uint32_t checked_u32(const std::uint64_t value,
                                        const char *const description) {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    fail(std::string(description) + " exceeds the V1 uint32 width");
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] SceneCounts scene_counts(const RenderSceneV1 &scene) {
  SceneCounts result;
  result.textures = checked_u32(scene.textures.size(), "The texture count");
  result.materials = checked_u32(scene.materials.size(), "The material count");
  result.meshes = checked_u32(scene.meshes.size(), "The mesh count");
  result.instances = checked_u32(scene.instances.size(), "The instance count");
  std::uint64_t mips = 0U;
  std::uint64_t draws = 0U;
  for (const auto &texture : scene.textures) {
    mips = checked_add(mips, texture.mips.size(), "The texture-mip count");
    for (const auto &mip : texture.mips) {
      result.pixel_bytes = checked_add(
          result.pixel_bytes, mip.rgba8.size(), "The texture-pixel byte count");
    }
  }
  for (const auto &mesh : scene.meshes) {
    result.vertices = checked_add(result.vertices, mesh.vertices.size(),
                                  "The vertex count");
    result.indices = checked_add(result.indices, mesh.triangle_indices.size(),
                                 "The index count");
    draws = checked_add(draws, mesh.draw_ranges.size(), "The draw-range count");
  }
  result.texture_mips = checked_u32(mips, "The texture-mip count");
  result.draw_ranges = checked_u32(draws, "The draw-range count");
  return result;
}

struct EncodedLayout final {
  std::uint64_t texture_offset = 0U;
  std::uint64_t texture_mip_offset = 0U;
  std::uint64_t material_offset = 0U;
  std::uint64_t mesh_offset = 0U;
  std::uint64_t draw_range_offset = 0U;
  std::uint64_t vertex_offset = 0U;
  std::uint64_t instance_offset = 0U;
  std::uint64_t index_offset = 0U;
  std::uint64_t pixel_offset = 0U;
  std::uint64_t total_bytes = 0U;
};

[[nodiscard]] EncodedLayout encoded_layout(const SceneCounts counts) {
  EncodedLayout result;
  result.texture_offset = kRenderSceneIoHeaderBytesV1;
  result.texture_mip_offset = checked_add(
      result.texture_offset,
      checked_multiply(counts.textures, kRenderSceneIoTextureBytesV1,
                       "The RenderSceneV1 texture table size"),
      "The RenderSceneV1 texture-mip table offset");
  result.material_offset = checked_add(
      result.texture_mip_offset,
      checked_multiply(counts.texture_mips, kRenderSceneIoTextureMipBytesV1,
                       "The RenderSceneV1 texture-mip table size"),
      "The RenderSceneV1 material table offset");
  result.mesh_offset = checked_add(
      result.material_offset,
      checked_multiply(counts.materials, kRenderSceneIoMaterialBytesV1,
                       "The RenderSceneV1 material table size"),
      "The RenderSceneV1 mesh table offset");
  result.draw_range_offset = checked_add(
      result.mesh_offset,
      checked_multiply(counts.meshes, kRenderSceneIoMeshBytesV1,
                       "The RenderSceneV1 mesh table size"),
      "The RenderSceneV1 draw-range table offset");
  result.vertex_offset = checked_add(
      result.draw_range_offset,
      checked_multiply(counts.draw_ranges, kRenderSceneIoDrawRangeBytesV1,
                       "The RenderSceneV1 draw-range table size"),
      "The RenderSceneV1 vertex table offset");
  result.instance_offset = checked_add(
      result.vertex_offset,
      checked_multiply(counts.vertices, kRenderSceneIoVertexBytesV1,
                       "The RenderSceneV1 vertex table size"),
      "The RenderSceneV1 instance table offset");
  result.index_offset = checked_add(
      result.instance_offset,
      checked_multiply(counts.instances, kRenderSceneIoInstanceBytesV1,
                       "The RenderSceneV1 instance table size"),
      "The RenderSceneV1 index table offset");
  result.pixel_offset = checked_add(
      result.index_offset,
      checked_multiply(counts.indices, kRenderSceneIoIndexBytesV1,
                       "The RenderSceneV1 index table size"),
      "The RenderSceneV1 pixel-data offset");
  result.total_bytes = checked_add(result.pixel_offset, counts.pixel_bytes,
                                   "The RenderSceneV1 total size");
  return result;
}

void require_count(const std::uint64_t count, const std::uint64_t limit,
                   const std::size_t container_max,
                   const char *const description) {
  if (count > limit || count > container_max ||
      count > std::numeric_limits<std::size_t>::max() ||
      count > static_cast<std::uint64_t>(
                  std::numeric_limits<std::ptrdiff_t>::max())) {
    fail(std::string(description) + " exceeds its caller or host limit");
  }
}

struct EncodedMeshRecord final {
  std::uint32_t id = 0U;
  std::uint64_t first_vertex = 0U;
  std::uint64_t vertex_count = 0U;
  std::uint64_t first_index = 0U;
  std::uint64_t index_count = 0U;
  std::uint32_t first_draw = 0U;
  std::uint32_t draw_count = 0U;
};

[[nodiscard]] RenderSceneV1 canonical_scene(
    RenderSceneV1 scene, const RenderSceneLimitsV1 limits,
    const char *const context) {
  try {
    return canonicalize_render_scene_v1(std::move(scene), limits);
  } catch (const RenderSceneError &error) {
    fail(std::string(context) + ": " + error.what());
  }
}

void validate_decoded_scene(const RenderSceneV1 &scene,
                            const RenderSceneLimitsV1 limits) {
  try {
    validate_render_scene_v1(scene, limits);
  } catch (const RenderSceneError &error) {
    fail(std::string("Decoded RenderSceneV1 is invalid: ") + error.what());
  }
}

} // namespace

std::vector<std::byte>
encode_render_scene_v1(const RenderSceneV1 &scene,
                       const RenderSceneIoLimitsV1 limits) {
  validate_limits(limits);
  const auto canonical = canonical_scene(
      scene, limits.scene, "Cannot encode invalid RenderSceneV1");
  const auto counts = scene_counts(canonical);
  const auto layout = encoded_layout(counts);
  if (layout.total_bytes > limits.max_encoded_bytes) {
    fail("RenderSceneV1 encoded size exceeds its caller limit");
  }

  std::vector<std::byte> result(
      host_size(layout.total_bytes, "The RenderSceneV1 encoded size"),
      std::byte{0U});
  std::copy(kMagic.begin(), kMagic.end(), result.begin());
  write_u32(result, kFormatVersionOffset, kRenderSceneIoFormatVersionV1);
  write_u32(result, kHeaderBytesOffset, kRenderSceneIoHeaderBytesV1);
  write_u64(result, kTotalBytesOffset, layout.total_bytes);
  write_u32(result, kSceneSchemaOffset, canonical.schema_version);
  write_u32(result, kCoordinateSystemOffset, kRightHandedZUpWorldUnits);
  write_u32(result, kVertexFormatOffset, kPositionUvRgba8VertexFormat);
  write_u32(result, kTopologyOffset, kTriangleListTopology);
  write_u32(result, kTextureFormatOffset, kRgba8TextureFormat);
  write_u32(result, kTextureCountOffset, counts.textures);
  write_u32(result, kHeaderTextureMipCountOffset, counts.texture_mips);
  write_u32(result, kMaterialCountOffset, counts.materials);
  write_u32(result, kMeshCountOffset, counts.meshes);
  write_u32(result, kDrawRangeCountOffset, counts.draw_ranges);
  write_u32(result, kInstanceCountOffset, counts.instances);
  write_u64(result, kVertexCountOffset, counts.vertices);
  write_u64(result, kIndexCountOffset, counts.indices);
  write_u64(result, kPixelBytesOffset, counts.pixel_bytes);
  write_u32(result, kTextureRecordBytesOffset, kRenderSceneIoTextureBytesV1);
  write_u32(result, kTextureMipRecordBytesOffset,
            kRenderSceneIoTextureMipBytesV1);
  write_u32(result, kMaterialRecordBytesOffset,
            kRenderSceneIoMaterialBytesV1);
  write_u32(result, kMeshRecordBytesOffset, kRenderSceneIoMeshBytesV1);
  write_u32(result, kDrawRangeRecordBytesOffset,
            kRenderSceneIoDrawRangeBytesV1);
  write_u32(result, kVertexRecordBytesOffset, kRenderSceneIoVertexBytesV1);
  write_u32(result, kInstanceRecordBytesOffset,
            kRenderSceneIoInstanceBytesV1);
  write_u32(result, kIndexRecordBytesOffset, kRenderSceneIoIndexBytesV1);
  write_u64(result, kTextureTableOffsetOffset, layout.texture_offset);
  write_u64(result, kTextureMipTableOffsetOffset, layout.texture_mip_offset);
  write_u64(result, kMaterialTableOffsetOffset, layout.material_offset);
  write_u64(result, kMeshTableOffsetOffset, layout.mesh_offset);
  write_u64(result, kDrawRangeTableOffsetOffset, layout.draw_range_offset);
  write_u64(result, kVertexTableOffsetOffset, layout.vertex_offset);
  write_u64(result, kInstanceTableOffsetOffset, layout.instance_offset);
  write_u64(result, kIndexTableOffsetOffset, layout.index_offset);
  write_u64(result, kPixelDataOffsetOffset, layout.pixel_offset);

  std::uint32_t global_mip = 0U;
  std::uint64_t global_pixel = 0U;
  for (const auto &texture : canonical.textures) {
    const auto texture_offset = checked_add(
        layout.texture_offset,
        checked_multiply(texture.id, kRenderSceneIoTextureBytesV1,
                         "A RenderSceneV1 texture record offset"),
        "A RenderSceneV1 texture record offset");
    write_u32(result, texture_offset, texture.id);
    write_u8(result, texture_offset + kTextureColorSpaceOffset,
             static_cast<std::uint8_t>(texture.color_space));
    write_u32(result, texture_offset + kTextureFirstMipOffset, global_mip);
    write_u32(result, texture_offset + kTextureMipCountOffset,
              checked_u32(texture.mips.size(), "A texture mip count"));
    write_u32(result, texture_offset + kTextureWidthOffset,
              texture.mips.front().width);
    write_u32(result, texture_offset + kTextureHeightOffset,
              texture.mips.front().height);

    for (std::size_t level = 0U; level < texture.mips.size(); ++level) {
      const auto &mip = texture.mips[level];
      const auto mip_offset = checked_add(
          layout.texture_mip_offset,
          checked_multiply(global_mip, kRenderSceneIoTextureMipBytesV1,
                           "A RenderSceneV1 mip record offset"),
          "A RenderSceneV1 mip record offset");
      write_u32(result, mip_offset, texture.id);
      write_u32(result, mip_offset + kMipLevelOffset,
                checked_u32(level, "A texture mip level"));
      write_u32(result, mip_offset + kMipWidthOffset, mip.width);
      write_u32(result, mip_offset + kMipHeightOffset, mip.height);
      write_u64(result, mip_offset + kMipPixelOffsetOffset, global_pixel);
      write_u64(result, mip_offset + kMipPixelBytesOffset, mip.rgba8.size());
      const auto output_offset = checked_add(
          layout.pixel_offset, global_pixel,
          "A RenderSceneV1 mip pixel output offset");
      std::copy(mip.rgba8.begin(), mip.rgba8.end(),
                result.begin() + static_cast<std::ptrdiff_t>(host_size(
                                     output_offset, "A mip pixel offset")));
      global_pixel = checked_add(global_pixel, mip.rgba8.size(),
                                 "The RenderSceneV1 pixel-data cursor");
      ++global_mip;
    }
  }

  for (const auto &material : canonical.materials) {
    const auto offset = checked_add(
        layout.material_offset,
        checked_multiply(material.id, kRenderSceneIoMaterialBytesV1,
                         "A RenderSceneV1 material record offset"),
        "A RenderSceneV1 material record offset");
    write_u32(result, offset, material.id);
    write_u32(result, offset + kMaterialTextureIdOffset,
              material.base_color_texture_id.value_or(kNoTextureId));
    write_u32(result, offset + kMaterialBaseColorOffset,
              material.base_color_rgba8);
    std::uint32_t flags = 0U;
    if (material.use_vertex_color) {
      flags |= kMaterialUsesVertexColor;
    }
    if (material.double_sided) {
      flags |= kMaterialDoubleSided;
    }
    write_u32(result, offset + kMaterialFlagsOffset, flags);
    write_u8(result, offset + kMaterialAddressUOffset,
             static_cast<std::uint8_t>(material.address_u));
    write_u8(result, offset + kMaterialAddressVOffset,
             static_cast<std::uint8_t>(material.address_v));
    write_u8(result, offset + kMaterialMinFilterOffset,
             static_cast<std::uint8_t>(material.min_filter));
    write_u8(result, offset + kMaterialMagFilterOffset,
             static_cast<std::uint8_t>(material.mag_filter));
    write_u8(result, offset + kMaterialMipmapFilterOffset,
             static_cast<std::uint8_t>(material.mipmap_filter));
    write_u8(result, offset + kMaterialAlphaModeOffset,
             static_cast<std::uint8_t>(material.alpha_mode));
    write_u8(result, offset + kMaterialAlphaCutoffOffset,
             material.alpha_cutoff_rgba8);
  }

  std::uint64_t global_vertex = 0U;
  std::uint64_t global_index = 0U;
  std::uint32_t global_draw = 0U;
  for (const auto &mesh : canonical.meshes) {
    const auto mesh_offset = checked_add(
        layout.mesh_offset,
        checked_multiply(mesh.id, kRenderSceneIoMeshBytesV1,
                         "A RenderSceneV1 mesh record offset"),
        "A RenderSceneV1 mesh record offset");
    write_u32(result, mesh_offset, mesh.id);
    write_u64(result, mesh_offset + kMeshFirstVertexOffset, global_vertex);
    write_u64(result, mesh_offset + kMeshVertexCountOffset,
              mesh.vertices.size());
    write_u64(result, mesh_offset + kMeshFirstIndexOffset, global_index);
    write_u64(result, mesh_offset + kMeshIndexCountOffset,
              mesh.triangle_indices.size());
    write_u32(result, mesh_offset + kMeshFirstDrawOffset, global_draw);
    write_u32(result, mesh_offset + kMeshDrawCountOffset,
              checked_u32(mesh.draw_ranges.size(), "A mesh draw count"));

    for (const auto &draw : mesh.draw_ranges) {
      const auto offset = checked_add(
          layout.draw_range_offset,
          checked_multiply(global_draw, kRenderSceneIoDrawRangeBytesV1,
                           "A RenderSceneV1 draw-range record offset"),
          "A RenderSceneV1 draw-range record offset");
      write_u32(result, offset, mesh.id);
      write_u32(result, offset + kDrawMaterialIdOffset, draw.material_id);
      write_u64(result, offset + kDrawFirstIndexOffset, draw.first_index);
      write_u64(result, offset + kDrawIndexCountOffset, draw.index_count);
      ++global_draw;
    }
    for (std::size_t index = 0U; index < mesh.vertices.size(); ++index) {
      const auto offset = checked_add(
          layout.vertex_offset,
          checked_multiply(global_vertex + index,
                           kRenderSceneIoVertexBytesV1,
                           "A RenderSceneV1 vertex record offset"),
          "A RenderSceneV1 vertex record offset");
      const auto &vertex = mesh.vertices[index];
      write_f32(result, offset + kVertexXOffset, vertex.x);
      write_f32(result, offset + kVertexYOffset, vertex.y);
      write_f32(result, offset + kVertexZOffset, vertex.z);
      write_f32(result, offset + kVertexUOffset, vertex.u);
      write_f32(result, offset + kVertexVOffset, vertex.v);
      write_u32(result, offset + kVertexColorOffset, vertex.rgba8);
    }
    for (std::size_t index = 0U; index < mesh.triangle_indices.size(); ++index) {
      const auto offset = checked_add(
          layout.index_offset,
          checked_multiply(global_index + index, kRenderSceneIoIndexBytesV1,
                           "A RenderSceneV1 index record offset"),
          "A RenderSceneV1 index record offset");
      write_u32(result, offset, mesh.triangle_indices[index]);
    }
    global_vertex = checked_add(global_vertex, mesh.vertices.size(),
                                "The RenderSceneV1 vertex cursor");
    global_index = checked_add(global_index, mesh.triangle_indices.size(),
                               "The RenderSceneV1 index cursor");
  }

  for (const auto &instance : canonical.instances) {
    const auto offset = checked_add(
        layout.instance_offset,
        checked_multiply(instance.id, kRenderSceneIoInstanceBytesV1,
                         "A RenderSceneV1 instance record offset"),
        "A RenderSceneV1 instance record offset");
    write_u32(result, offset, instance.id);
    write_u32(result, offset + kInstanceMeshIdOffset, instance.mesh_id);
    for (std::size_t element = 0U;
         element < instance.local_to_world.values.size(); ++element) {
      write_f32(result, offset + kInstanceMatrixOffset + element * 4U,
                instance.local_to_world.values[element]);
    }
  }
  return result;
}

RenderSceneV1 decode_render_scene_v1(const std::span<const std::byte> bytes,
                                     const RenderSceneIoLimitsV1 limits) {
  validate_limits(limits);
  if (bytes.size() > limits.max_encoded_bytes) {
    fail("RenderSceneV1 exceeds its caller encoded-byte limit");
  }
  if (bytes.size() < kRenderSceneIoHeaderBytesV1) {
    fail("RenderSceneV1 header is truncated");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    fail("RenderSceneV1 magic is invalid");
  }
  if (read_u32(bytes, kFormatVersionOffset, "The format version") !=
          kRenderSceneIoFormatVersionV1 ||
      read_u32(bytes, kHeaderBytesOffset, "The header size") !=
          kRenderSceneIoHeaderBytesV1 ||
      read_u32(bytes, kSceneSchemaOffset, "The scene schema") !=
          kRenderSceneSchemaVersionV1 ||
      read_u32(bytes, kCoordinateSystemOffset, "The coordinate system") !=
          kRightHandedZUpWorldUnits ||
      read_u32(bytes, kVertexFormatOffset, "The vertex format") !=
          kPositionUvRgba8VertexFormat ||
      read_u32(bytes, kTopologyOffset, "The primitive topology") !=
          kTriangleListTopology ||
      read_u32(bytes, kTextureFormatOffset, "The texture format") !=
          kRgba8TextureFormat ||
      read_u32(bytes, kTextureRecordBytesOffset, "The texture record size") !=
          kRenderSceneIoTextureBytesV1 ||
      read_u32(bytes, kTextureMipRecordBytesOffset,
               "The texture-mip record size") !=
          kRenderSceneIoTextureMipBytesV1 ||
      read_u32(bytes, kMaterialRecordBytesOffset,
               "The material record size") !=
          kRenderSceneIoMaterialBytesV1 ||
      read_u32(bytes, kMeshRecordBytesOffset, "The mesh record size") !=
          kRenderSceneIoMeshBytesV1 ||
      read_u32(bytes, kDrawRangeRecordBytesOffset,
               "The draw-range record size") !=
          kRenderSceneIoDrawRangeBytesV1 ||
      read_u32(bytes, kVertexRecordBytesOffset, "The vertex record size") !=
          kRenderSceneIoVertexBytesV1 ||
      read_u32(bytes, kInstanceRecordBytesOffset,
               "The instance record size") !=
          kRenderSceneIoInstanceBytesV1 ||
      read_u32(bytes, kIndexRecordBytesOffset, "The index record size") !=
          kRenderSceneIoIndexBytesV1 ||
      read_u32(bytes, kHeaderSchemaReservedOffset,
               "The header schema reserved word") != 0U ||
      !all_zero(bytes, kHeaderReservedOffset, kRenderSceneIoHeaderBytesV1,
                "The RenderSceneV1 reserved header")) {
    fail("RenderSceneV1 header schema, record sizes, or reserved bytes are invalid");
  }
  const auto total_bytes =
      read_u64(bytes, kTotalBytesOffset, "The total byte size");
  if (total_bytes != bytes.size()) {
    fail("RenderSceneV1 total byte size is invalid or has trailing data");
  }

  SceneCounts counts;
  counts.textures = read_u32(bytes, kTextureCountOffset, "The texture count");
  counts.texture_mips =
      read_u32(bytes, kHeaderTextureMipCountOffset,
               "The texture-mip count");
  counts.materials =
      read_u32(bytes, kMaterialCountOffset, "The material count");
  counts.meshes = read_u32(bytes, kMeshCountOffset, "The mesh count");
  counts.draw_ranges =
      read_u32(bytes, kDrawRangeCountOffset, "The draw-range count");
  counts.instances =
      read_u32(bytes, kInstanceCountOffset, "The instance count");
  counts.vertices = read_u64(bytes, kVertexCountOffset, "The vertex count");
  counts.indices = read_u64(bytes, kIndexCountOffset, "The index count");
  counts.pixel_bytes =
      read_u64(bytes, kPixelBytesOffset, "The pixel-data byte count");
  RenderSceneV1 result;
  std::vector<EncodedMeshRecord> mesh_records;
  require_count(counts.textures, limits.scene.max_textures,
                result.textures.max_size(),
                "The RenderSceneV1 texture count");
  require_count(counts.texture_mips, limits.scene.max_total_texture_mips,
                std::vector<RenderSceneTextureMipV1>{}.max_size(),
                "The RenderSceneV1 texture-mip count");
  require_count(counts.materials, limits.scene.max_materials,
                result.materials.max_size(),
                "The RenderSceneV1 material count");
  require_count(counts.meshes, limits.scene.max_meshes,
                std::min(result.meshes.max_size(), mesh_records.max_size()),
                "The RenderSceneV1 mesh count");
  require_count(counts.draw_ranges, limits.scene.max_draw_ranges,
                std::vector<RenderSceneDrawRangeV1>{}.max_size(),
                "The RenderSceneV1 draw-range count");
  require_count(counts.instances, limits.scene.max_instances,
                result.instances.max_size(),
                "The RenderSceneV1 instance count");
  require_count(counts.vertices, limits.scene.max_vertices,
                std::vector<RenderSceneVertexV1>{}.max_size(),
                "The RenderSceneV1 vertex count");
  require_count(counts.indices, limits.scene.max_triangle_indices,
                std::vector<std::uint32_t>{}.max_size(),
                "The RenderSceneV1 index count");
  require_count(counts.pixel_bytes, limits.scene.max_total_rgba8_bytes,
                std::vector<std::byte>{}.max_size(),
                "The RenderSceneV1 pixel-data byte count");

  const auto layout = encoded_layout(counts);
  if (layout.total_bytes != total_bytes ||
      read_u64(bytes, kTextureTableOffsetOffset, "The texture table offset") !=
          layout.texture_offset ||
      read_u64(bytes, kTextureMipTableOffsetOffset,
               "The texture-mip table offset") != layout.texture_mip_offset ||
      read_u64(bytes, kMaterialTableOffsetOffset,
               "The material table offset") != layout.material_offset ||
      read_u64(bytes, kMeshTableOffsetOffset, "The mesh table offset") !=
          layout.mesh_offset ||
      read_u64(bytes, kDrawRangeTableOffsetOffset,
               "The draw-range table offset") != layout.draw_range_offset ||
      read_u64(bytes, kVertexTableOffsetOffset, "The vertex table offset") !=
          layout.vertex_offset ||
      read_u64(bytes, kInstanceTableOffsetOffset,
               "The instance table offset") != layout.instance_offset ||
      read_u64(bytes, kIndexTableOffsetOffset, "The index table offset") !=
          layout.index_offset ||
      read_u64(bytes, kPixelDataOffsetOffset, "The pixel-data offset") !=
          layout.pixel_offset) {
    fail("RenderSceneV1 table offsets or exact encoded size are invalid");
  }

  result.textures.reserve(counts.textures);
  std::uint32_t expected_first_mip = 0U;
  std::uint64_t expected_pixel_offset = 0U;
  for (std::uint32_t texture_id = 0U; texture_id < counts.textures;
       ++texture_id) {
    const auto texture_offset =
        layout.texture_offset +
        static_cast<std::uint64_t>(texture_id) * kRenderSceneIoTextureBytesV1;
    if (read_u32(bytes, texture_offset, "A texture ID") != texture_id ||
        !all_zero(bytes, texture_offset + 5U, texture_offset + 8U,
                  "A texture reserved field") ||
        !all_zero(bytes, texture_offset + kTextureReservedOffset,
                  texture_offset + kRenderSceneIoTextureBytesV1,
                  "A texture reserved field")) {
      fail("RenderSceneV1 texture ID/order or reserved bytes are invalid");
    }
    const auto color_space = read_u8(
        bytes, texture_offset + kTextureColorSpaceOffset,
        "A texture color space");
    if (color_space >
        static_cast<std::uint8_t>(RenderSceneTextureColorSpaceV1::srgb)) {
      fail("RenderSceneV1 texture uses an unknown color space");
    }
    const auto first_mip = read_u32(
        bytes, texture_offset + kTextureFirstMipOffset,
        "A texture first-mip index");
    const auto mip_count = read_u32(
        bytes, texture_offset + kTextureMipCountOffset, "A texture mip count");
    if (first_mip != expected_first_mip || mip_count == 0U ||
        mip_count > limits.scene.max_mips_per_texture ||
        mip_count > counts.texture_mips - expected_first_mip) {
      fail("RenderSceneV1 texture mip ranges are not a complete canonical partition");
    }

    RenderSceneTextureV1 texture;
    texture.id = texture_id;
    texture.color_space =
        static_cast<RenderSceneTextureColorSpaceV1>(color_space);
    texture.mips.reserve(mip_count);
    std::uint64_t texture_texels = 0U;
    std::uint32_t expected_width = 0U;
    std::uint32_t expected_height = 0U;
    for (std::uint32_t level = 0U; level < mip_count; ++level) {
      const auto mip_id = expected_first_mip + level;
      const auto mip_offset = layout.texture_mip_offset +
                              static_cast<std::uint64_t>(mip_id) *
                                  kRenderSceneIoTextureMipBytesV1;
      if (read_u32(bytes, mip_offset, "A mip texture ID") != texture_id ||
          read_u32(bytes, mip_offset + kMipLevelOffset, "A mip level") !=
              level) {
        fail("RenderSceneV1 mip ownership or level order is invalid");
      }
      RenderSceneTextureMipV1 mip;
      mip.width =
          read_u32(bytes, mip_offset + kMipWidthOffset, "A mip width");
      mip.height =
          read_u32(bytes, mip_offset + kMipHeightOffset, "A mip height");
      if (mip.width == 0U || mip.height == 0U ||
          mip.width > limits.scene.max_texture_width ||
          mip.height > limits.scene.max_texture_height ||
          (level != 0U && (mip.width != expected_width ||
                          mip.height != expected_height))) {
        fail("RenderSceneV1 mip dimensions are invalid or non-canonical");
      }
      const auto texels = checked_multiply(
          mip.width, mip.height, "A RenderSceneV1 mip texel count");
      const auto exact_bytes = checked_multiply(
          texels, 4U, "A RenderSceneV1 mip RGBA8 byte count");
      const auto encoded_pixel_offset = read_u64(
          bytes, mip_offset + kMipPixelOffsetOffset, "A mip pixel offset");
      const auto encoded_pixel_bytes = read_u64(
          bytes, mip_offset + kMipPixelBytesOffset, "A mip pixel byte count");
      if (encoded_pixel_offset != expected_pixel_offset ||
          encoded_pixel_bytes != exact_bytes ||
          encoded_pixel_bytes > counts.pixel_bytes - expected_pixel_offset) {
        fail("RenderSceneV1 mip pixel ranges are not an exact canonical partition");
      }
      texture_texels = checked_add(texture_texels, texels,
                                   "A RenderSceneV1 texture texel count");
      if (texture_texels > limits.scene.max_texels_per_texture) {
        fail("RenderSceneV1 texture exceeds its texel limit");
      }
      const auto pixel_begin = checked_add(
          layout.pixel_offset, encoded_pixel_offset,
          "A RenderSceneV1 mip pixel input offset");
      const auto host_bytes = host_size(encoded_pixel_bytes, "A mip byte count");
      const auto host_begin = host_size(pixel_begin, "A mip pixel offset");
      mip.rgba8.assign(bytes.begin() + static_cast<std::ptrdiff_t>(host_begin),
                       bytes.begin() + static_cast<std::ptrdiff_t>(host_begin +
                                                                   host_bytes));
      texture.mips.push_back(std::move(mip));
      expected_pixel_offset = checked_add(
          expected_pixel_offset, encoded_pixel_bytes,
          "The RenderSceneV1 pixel-data cursor");
      expected_width =
          std::max(UINT32_C(1), texture.mips.back().width / 2U);
      expected_height =
          std::max(UINT32_C(1), texture.mips.back().height / 2U);
    }
    if (read_u32(bytes, texture_offset + kTextureWidthOffset,
                 "A texture base width") != texture.mips.front().width ||
        read_u32(bytes, texture_offset + kTextureHeightOffset,
                 "A texture base height") != texture.mips.front().height) {
      fail("RenderSceneV1 texture base dimensions disagree with mip zero");
    }
    expected_first_mip += mip_count;
    result.textures.push_back(std::move(texture));
  }
  if (expected_first_mip != counts.texture_mips ||
      expected_pixel_offset != counts.pixel_bytes) {
    fail("RenderSceneV1 mips or pixel bytes are not covered exactly");
  }

  result.materials.reserve(counts.materials);
  for (std::uint32_t material_id = 0U; material_id < counts.materials;
       ++material_id) {
    const auto offset = layout.material_offset +
                        static_cast<std::uint64_t>(material_id) *
                            kRenderSceneIoMaterialBytesV1;
    if (read_u32(bytes, offset, "A material ID") != material_id ||
        !all_zero(bytes, offset + kMaterialReservedOffset,
                  offset + kRenderSceneIoMaterialBytesV1,
                  "A material reserved field")) {
      fail("RenderSceneV1 material ID/order or reserved bytes are invalid");
    }
    const auto flags =
        read_u32(bytes, offset + kMaterialFlagsOffset, "Material flags");
    if ((flags & ~kKnownMaterialFlags) != 0U) {
      fail("RenderSceneV1 material contains unknown flags");
    }
    RenderSceneMaterialV1 material;
    material.id = material_id;
    const auto texture_id = read_u32(
        bytes, offset + kMaterialTextureIdOffset, "A material texture ID");
    if (texture_id != kNoTextureId) {
      material.base_color_texture_id = texture_id;
    }
    material.base_color_rgba8 = read_u32(
        bytes, offset + kMaterialBaseColorOffset, "A material base color");
    material.use_vertex_color = (flags & kMaterialUsesVertexColor) != 0U;
    material.double_sided = (flags & kMaterialDoubleSided) != 0U;
    material.address_u = static_cast<RenderSceneAddressModeV1>(read_u8(
        bytes, offset + kMaterialAddressUOffset, "A material U address mode"));
    material.address_v = static_cast<RenderSceneAddressModeV1>(read_u8(
        bytes, offset + kMaterialAddressVOffset, "A material V address mode"));
    material.min_filter = static_cast<RenderSceneFilterV1>(read_u8(
        bytes, offset + kMaterialMinFilterOffset, "A material min filter"));
    material.mag_filter = static_cast<RenderSceneFilterV1>(read_u8(
        bytes, offset + kMaterialMagFilterOffset, "A material mag filter"));
    material.mipmap_filter =
        static_cast<RenderSceneMipmapFilterV1>(read_u8(
            bytes, offset + kMaterialMipmapFilterOffset,
            "A material mipmap filter"));
    material.alpha_mode = static_cast<RenderSceneAlphaModeV1>(read_u8(
        bytes, offset + kMaterialAlphaModeOffset, "A material alpha mode"));
    material.alpha_cutoff_rgba8 = read_u8(
        bytes, offset + kMaterialAlphaCutoffOffset,
        "A material alpha cutoff");
    result.materials.push_back(material);
  }

  mesh_records.reserve(counts.meshes);
  std::uint64_t expected_vertex = 0U;
  std::uint64_t expected_index = 0U;
  std::uint32_t expected_draw = 0U;
  for (std::uint32_t mesh_id = 0U; mesh_id < counts.meshes; ++mesh_id) {
    const auto offset = layout.mesh_offset +
                        static_cast<std::uint64_t>(mesh_id) *
                            kRenderSceneIoMeshBytesV1;
    if (read_u32(bytes, offset, "A mesh ID") != mesh_id ||
        read_u32(bytes, offset + kMeshReservedOffset,
                 "A mesh reserved word") != 0U) {
      fail("RenderSceneV1 mesh ID/order or reserved bytes are invalid");
    }
    EncodedMeshRecord record;
    record.id = mesh_id;
    record.first_vertex = read_u64(
        bytes, offset + kMeshFirstVertexOffset, "A mesh first vertex");
    record.vertex_count = read_u64(
        bytes, offset + kMeshVertexCountOffset, "A mesh vertex count");
    record.first_index = read_u64(
        bytes, offset + kMeshFirstIndexOffset, "A mesh first index");
    record.index_count = read_u64(
        bytes, offset + kMeshIndexCountOffset, "A mesh index count");
    record.first_draw = read_u32(
        bytes, offset + kMeshFirstDrawOffset, "A mesh first draw");
    record.draw_count = read_u32(
        bytes, offset + kMeshDrawCountOffset, "A mesh draw count");
    if (record.first_vertex != expected_vertex || record.vertex_count == 0U ||
        record.vertex_count > std::numeric_limits<std::uint32_t>::max() ||
        record.vertex_count > counts.vertices - expected_vertex ||
        record.first_index != expected_index || record.index_count == 0U ||
        (record.index_count % 3U) != 0U ||
        record.index_count > counts.indices - expected_index ||
        record.first_draw != expected_draw || record.draw_count == 0U ||
        record.draw_count > counts.draw_ranges - expected_draw) {
      fail("RenderSceneV1 mesh table ranges are not complete canonical partitions");
    }
    expected_vertex += record.vertex_count;
    expected_index += record.index_count;
    expected_draw += record.draw_count;
    mesh_records.push_back(record);
  }
  if (expected_vertex != counts.vertices || expected_index != counts.indices ||
      expected_draw != counts.draw_ranges) {
    fail("RenderSceneV1 mesh table does not cover aggregate geometry exactly");
  }

  result.meshes.reserve(counts.meshes);
  for (const auto &record : mesh_records) {
    RenderSceneMeshV1 mesh;
    mesh.id = record.id;
    const auto host_vertex_count =
        host_size(record.vertex_count, "A mesh vertex count");
    const auto host_index_count =
        host_size(record.index_count, "A mesh index count");
    mesh.vertices.reserve(host_vertex_count);
    mesh.triangle_indices.reserve(host_index_count);
    mesh.draw_ranges.reserve(record.draw_count);
    for (std::uint64_t local = 0U; local < record.vertex_count; ++local) {
      const auto offset = layout.vertex_offset +
                          (record.first_vertex + local) *
                              kRenderSceneIoVertexBytesV1;
      mesh.vertices.push_back({
          read_f32(bytes, offset + kVertexXOffset, "vertex X"),
          read_f32(bytes, offset + kVertexYOffset, "vertex Y"),
          read_f32(bytes, offset + kVertexZOffset, "vertex Z"),
          read_f32(bytes, offset + kVertexUOffset, "vertex U"),
          read_f32(bytes, offset + kVertexVOffset, "vertex V"),
          read_u32(bytes, offset + kVertexColorOffset, "a vertex color"),
      });
    }
    for (std::uint64_t local = 0U; local < record.index_count; ++local) {
      const auto offset = layout.index_offset +
                          (record.first_index + local) *
                              kRenderSceneIoIndexBytesV1;
      mesh.triangle_indices.push_back(
          read_u32(bytes, offset, "A local mesh vertex index"));
    }
    for (std::uint32_t local = 0U; local < record.draw_count; ++local) {
      const auto offset = layout.draw_range_offset +
                          static_cast<std::uint64_t>(record.first_draw + local) *
                              kRenderSceneIoDrawRangeBytesV1;
      if (read_u32(bytes, offset, "A draw mesh ID") != record.id) {
        fail("RenderSceneV1 draw range belongs to the wrong mesh");
      }
      mesh.draw_ranges.push_back({
          read_u32(bytes, offset + kDrawMaterialIdOffset,
                   "A draw material ID"),
          read_u64(bytes, offset + kDrawFirstIndexOffset,
                   "A draw first index"),
          read_u64(bytes, offset + kDrawIndexCountOffset,
                   "A draw index count"),
      });
    }
    result.meshes.push_back(std::move(mesh));
  }

  result.instances.reserve(counts.instances);
  for (std::uint32_t instance_id = 0U; instance_id < counts.instances;
       ++instance_id) {
    const auto offset = layout.instance_offset +
                        static_cast<std::uint64_t>(instance_id) *
                            kRenderSceneIoInstanceBytesV1;
    if (read_u32(bytes, offset, "An instance ID") != instance_id ||
        !all_zero(bytes, offset + kInstanceReservedOffset,
                  offset + kInstanceMatrixOffset,
                  "An instance reserved field")) {
      fail("RenderSceneV1 instance ID/order or reserved bytes are invalid");
    }
    RenderSceneInstanceV1 instance;
    instance.id = instance_id;
    instance.mesh_id = read_u32(
        bytes, offset + kInstanceMeshIdOffset, "An instance mesh ID");
    for (std::size_t element = 0U;
         element < instance.local_to_world.values.size(); ++element) {
      instance.local_to_world.values[element] = read_f32(
          bytes, offset + kInstanceMatrixOffset + element * 4U,
          "instance affine matrix");
    }
    result.instances.push_back(instance);
  }

  validate_decoded_scene(result, limits.scene);
  return result;
}

} // namespace openrc
