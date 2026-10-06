#include "openrc/actor_library_io.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::array<std::byte, 8U> kMagic{
    std::byte{'O'}, std::byte{'R'}, std::byte{'A'}, std::byte{'C'},
    std::byte{'T'}, std::byte{'L'}, std::byte{'B'}, std::byte{'1'},
};

constexpr std::uint64_t kRigRecordBytes = 48U;
constexpr std::uint64_t kJointRecordBytes = 104U;
constexpr std::uint64_t kModelRecordBytes = 112U;
constexpr std::uint64_t kTextureRecordBytes = 16U;
constexpr std::uint64_t kMipRecordBytes = 24U;
constexpr std::uint64_t kMaterialRecordBytes = 32U;
constexpr std::uint64_t kMeshRecordBytes = 32U;
constexpr std::uint64_t kVertexRecordBytes = 56U;
constexpr std::uint64_t kDrawRecordBytes = 24U;
constexpr std::uint64_t kIndexRecordBytes = 4U;

constexpr std::uint64_t kFormatVersionOffset = 0x08U;
constexpr std::uint64_t kHeaderBytesOffset = 0x0cU;
constexpr std::uint64_t kTotalBytesOffset = 0x10U;
constexpr std::uint64_t kSchemaVersionOffset = 0x18U;
constexpr std::uint64_t kRigCountOffset = 0x1cU;
constexpr std::uint64_t kModelCountOffset = 0x20U;
constexpr std::uint64_t kTextureCountOffset = 0x24U;
constexpr std::uint64_t kMipCountOffset = 0x28U;
constexpr std::uint64_t kMaterialCountOffset = 0x2cU;
constexpr std::uint64_t kMeshCountOffset = 0x30U;
constexpr std::uint64_t kJointCountOffset = 0x38U;
constexpr std::uint64_t kDrawCountOffset = 0x40U;
constexpr std::uint64_t kVertexCountOffset = 0x48U;
constexpr std::uint64_t kIndexCountOffset = 0x50U;
constexpr std::uint64_t kPixelBytesOffset = 0x58U;
constexpr std::uint64_t kSemanticKeyBytesOffset = 0x60U;
constexpr std::uint64_t kHeaderReservedOffset = 0x68U;

constexpr std::uint32_t kNoTextureId = UINT32_MAX;
constexpr std::uint32_t kMaterialUsesVertexColor = 1U << 0U;
constexpr std::uint32_t kMaterialDoubleSided = 1U << 1U;
constexpr std::uint32_t kKnownMaterialFlags =
    kMaterialUsesVertexColor | kMaterialDoubleSided;

[[noreturn]] void fail(const std::string &message) {
  throw ActorLibraryIoError(message);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(const std::uint64_t left,
                                             const std::uint64_t right,
                                             const char *const description) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left * right;
}

[[nodiscard]] std::size_t host_size(const std::uint64_t value,
                                    const std::size_t maximum,
                                    const char *const description) {
  if (value > maximum || value > static_cast<std::uint64_t>(
                                     std::numeric_limits<std::size_t>::max())) {
    fail(std::string(description) + " exceeds the host container domain");
  }
  return static_cast<std::size_t>(value);
}

struct Counts {
  std::uint32_t rigs = 0U;
  std::uint32_t models = 0U;
  std::uint64_t textures = 0U;
  std::uint64_t mips = 0U;
  std::uint64_t materials = 0U;
  std::uint64_t meshes = 0U;
  std::uint64_t joints = 0U;
  std::uint64_t draws = 0U;
  std::uint64_t vertices = 0U;
  std::uint64_t indices = 0U;
  std::uint64_t pixel_bytes = 0U;
  std::uint64_t semantic_key_bytes = 0U;
};

[[nodiscard]] std::uint32_t u32_count(const std::size_t count,
                                      const char *const description) {
  if (count > std::numeric_limits<std::uint32_t>::max()) {
    fail(std::string(description) + " exceeds the format count width");
  }
  return static_cast<std::uint32_t>(count);
}

[[nodiscard]] Counts count_library(const ActorLibraryV1 &library) {
  Counts result;
  result.rigs = u32_count(library.rigs.size(), "Actor rig count");
  result.models = u32_count(library.models.size(), "Actor model count");
  for (const auto &rig : library.rigs) {
    result.semantic_key_bytes =
        checked_add(result.semantic_key_bytes, rig.semantic_key.size(),
                    "ActorLibraryV1 semantic key bytes");
    result.joints = checked_add(result.joints, rig.rig.joints.size(),
                                "ActorLibraryV1 joint count");
  }
  for (const auto &model : library.models) {
    result.semantic_key_bytes =
        checked_add(result.semantic_key_bytes,
                    checked_add(model.semantic_key.size(), model.rig_key.size(),
                                "ActorLibraryV1 model key bytes"),
                    "ActorLibraryV1 semantic key bytes");
    result.textures = checked_add(result.textures, model.textures.size(),
                                  "ActorLibraryV1 texture count");
    result.materials = checked_add(result.materials, model.materials.size(),
                                   "ActorLibraryV1 material count");
    result.meshes = checked_add(result.meshes, model.meshes.size(),
                                "ActorLibraryV1 mesh count");
    for (const auto &texture : model.textures) {
      result.mips = checked_add(result.mips, texture.mips.size(),
                                "ActorLibraryV1 mip count");
      for (const auto &mip : texture.mips) {
        result.pixel_bytes = checked_add(result.pixel_bytes, mip.rgba8.size(),
                                         "ActorLibraryV1 pixel byte count");
      }
    }
    for (const auto &mesh : model.meshes) {
      result.draws = checked_add(result.draws, mesh.draw_ranges.size(),
                                 "ActorLibraryV1 draw count");
      result.vertices = checked_add(result.vertices, mesh.vertices.size(),
                                    "ActorLibraryV1 vertex count");
      result.indices = checked_add(result.indices, mesh.triangle_indices.size(),
                                   "ActorLibraryV1 index count");
    }
  }
  return result;
}

[[nodiscard]] std::uint64_t encoded_size(const Counts &counts) {
  std::uint64_t result = kActorLibraryIoHeaderBytesV1;
  const auto add_table = [&result](const std::uint64_t count,
                                   const std::uint64_t stride,
                                   const char *const description) {
    result = checked_add(result, checked_multiply(count, stride, description),
                         description);
  };
  add_table(counts.rigs, kRigRecordBytes, "Actor rig records");
  add_table(counts.joints, kJointRecordBytes, "Actor joint records");
  add_table(counts.models, kModelRecordBytes, "Actor model records");
  add_table(counts.textures, kTextureRecordBytes, "Actor texture records");
  add_table(counts.mips, kMipRecordBytes, "Actor mip records");
  add_table(counts.materials, kMaterialRecordBytes, "Actor material records");
  add_table(counts.meshes, kMeshRecordBytes, "Actor mesh records");
  add_table(counts.vertices, kVertexRecordBytes, "Actor vertex records");
  add_table(counts.draws, kDrawRecordBytes, "Actor draw records");
  add_table(counts.indices, kIndexRecordBytes, "Actor index records");
  result = checked_add(result, counts.pixel_bytes, "Actor pixel bytes");
  return checked_add(result, counts.semantic_key_bytes,
                     "Actor semantic key bytes");
}

class Writer final {
public:
  explicit Writer(const std::uint64_t maximum_bytes)
      : maximum_bytes_(maximum_bytes),
        bytes_(kActorLibraryIoHeaderBytesV1, std::byte{0}) {}

  void append_u8(const std::uint8_t value) {
    append_byte(static_cast<std::byte>(value));
  }

  void append_u16(const std::uint16_t value) {
    append_u8(static_cast<std::uint8_t>(value));
    append_u8(static_cast<std::uint8_t>(value >> 8U));
  }

  void append_u32(const std::uint32_t value) {
    append_u16(static_cast<std::uint16_t>(value));
    append_u16(static_cast<std::uint16_t>(value >> 16U));
  }

  void append_i32(const std::int32_t value) {
    append_u32(std::bit_cast<std::uint32_t>(value));
  }

  void append_u64(const std::uint64_t value) {
    append_u32(static_cast<std::uint32_t>(value));
    append_u32(static_cast<std::uint32_t>(value >> 32U));
  }

  void append_f32(const float value) {
    append_u32(std::bit_cast<std::uint32_t>(value));
  }

  void append_digest(const PreparedContentDigestV1 &digest) {
    append_bytes(digest);
  }

  void append_string(const std::string &value) {
    append_bytes(std::as_bytes(std::span(value.data(), value.size())));
  }

  void append_bytes(const std::span<const std::byte> values) {
    require_append(values.size());
    bytes_.insert(bytes_.end(), values.begin(), values.end());
  }

  void append_zero(const std::size_t count) {
    require_append(count);
    bytes_.insert(bytes_.end(), count, std::byte{0});
  }

  void patch_u32(const std::uint64_t offset, const std::uint32_t value) {
    patch_u8(offset, static_cast<std::uint8_t>(value));
    patch_u8(offset + 1U, static_cast<std::uint8_t>(value >> 8U));
    patch_u8(offset + 2U, static_cast<std::uint8_t>(value >> 16U));
    patch_u8(offset + 3U, static_cast<std::uint8_t>(value >> 24U));
  }

  void patch_u64(const std::uint64_t offset, const std::uint64_t value) {
    patch_u32(offset, static_cast<std::uint32_t>(value));
    patch_u32(offset + 4U, static_cast<std::uint32_t>(value >> 32U));
  }

  void patch_magic() {
    std::copy(kMagic.begin(), kMagic.end(), bytes_.begin());
  }

  [[nodiscard]] std::vector<std::byte> finish() && { return std::move(bytes_); }

  [[nodiscard]] std::uint64_t size() const noexcept { return bytes_.size(); }

private:
  void require_append(const std::size_t count) {
    if (bytes_.size() > maximum_bytes_ ||
        count > maximum_bytes_ - bytes_.size() ||
        count > bytes_.max_size() - bytes_.size()) {
      fail("ActorLibraryV1 encoded bytes exceed the caller limit");
    }
  }

  void append_byte(const std::byte value) {
    require_append(1U);
    bytes_.push_back(value);
  }

  void patch_u8(const std::uint64_t offset, const std::uint8_t value) {
    if (offset >= bytes_.size()) {
      fail("ActorLibraryV1 internal header patch is out of range");
    }
    bytes_[static_cast<std::size_t>(offset)] = static_cast<std::byte>(value);
  }

  std::uint64_t maximum_bytes_ = 0U;
  std::vector<std::byte> bytes_;
};

class Reader final {
public:
  explicit Reader(const std::span<const std::byte> bytes) : bytes_(bytes) {}

  [[nodiscard]] std::uint8_t read_u8(const char *const description) {
    require(1U, description);
    return std::to_integer<std::uint8_t>(bytes_[offset_++]);
  }

  [[nodiscard]] std::uint16_t read_u16(const char *const description) {
    const auto low = read_u8(description);
    const auto high = read_u8(description);
    return static_cast<std::uint16_t>(low |
                                      (static_cast<std::uint16_t>(high) << 8U));
  }

  [[nodiscard]] std::uint32_t read_u32(const char *const description) {
    const auto low = read_u16(description);
    const auto high = read_u16(description);
    return static_cast<std::uint32_t>(low) |
           (static_cast<std::uint32_t>(high) << 16U);
  }

  [[nodiscard]] std::int32_t read_i32(const char *const description) {
    return std::bit_cast<std::int32_t>(read_u32(description));
  }

  [[nodiscard]] std::uint64_t read_u64(const char *const description) {
    const auto low = read_u32(description);
    const auto high = read_u32(description);
    return static_cast<std::uint64_t>(low) |
           (static_cast<std::uint64_t>(high) << 32U);
  }

  [[nodiscard]] float read_f32(const char *const description) {
    return std::bit_cast<float>(read_u32(description));
  }

  [[nodiscard]] PreparedContentDigestV1
  read_digest(const char *const description) {
    PreparedContentDigestV1 result{};
    require(result.size(), description);
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                result.size(), result.begin());
    offset_ += result.size();
    return result;
  }

  [[nodiscard]] std::string read_string(const std::uint32_t byte_count,
                                        const char *const description) {
    require(byte_count, description);
    std::string result(byte_count, '\0');
    if (byte_count != 0U) {
      std::memcpy(result.data(), bytes_.data() + offset_, byte_count);
    }
    offset_ += byte_count;
    return result;
  }

  [[nodiscard]] std::vector<std::byte>
  read_bytes(const std::uint64_t byte_count, const std::size_t maximum,
             const char *const description) {
    const auto count = host_size(byte_count, maximum, description);
    require(count, description);
    std::vector<std::byte> result;
    result.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                  bytes_.begin() +
                      static_cast<std::ptrdiff_t>(offset_ + count));
    offset_ += count;
    return result;
  }

  void require_zero(const std::size_t byte_count,
                    const char *const description) {
    require(byte_count, description);
    for (std::size_t index = 0U; index < byte_count; ++index) {
      if (bytes_[offset_ + index] != std::byte{0}) {
        fail(std::string(description) + " is non-zero");
      }
    }
    offset_ += byte_count;
  }

  [[nodiscard]] bool finished() const noexcept {
    return offset_ == bytes_.size();
  }

private:
  void require(const std::size_t byte_count,
               const char *const description) const {
    if (offset_ > bytes_.size() || byte_count > bytes_.size() - offset_) {
      fail(std::string(description) + " is truncated");
    }
  }

  std::span<const std::byte> bytes_;
  std::size_t offset_ = 0U;
};

void require_limit(const std::uint64_t value, const std::uint64_t limit,
                   const char *const description) {
  if (value > limit) {
    fail(std::string(description) + " exceeds its caller limit");
  }
}

void add_partition(std::uint64_t &seen, const std::uint64_t count,
                   const std::uint64_t declared,
                   const char *const description) {
  if (seen > declared || count > declared - seen) {
    fail(std::string(description) + " exceeds its declared partition");
  }
  seen += count;
}

void write_header(Writer &writer, const Counts &counts,
                  const std::uint64_t total_bytes) {
  writer.patch_magic();
  writer.patch_u32(kFormatVersionOffset, kActorLibraryIoFormatVersionV1);
  writer.patch_u32(kHeaderBytesOffset, kActorLibraryIoHeaderBytesV1);
  writer.patch_u64(kTotalBytesOffset, total_bytes);
  writer.patch_u32(kSchemaVersionOffset, kActorLibrarySchemaVersionV1);
  writer.patch_u32(kRigCountOffset, counts.rigs);
  writer.patch_u32(kModelCountOffset, counts.models);
  writer.patch_u32(kTextureCountOffset,
                   static_cast<std::uint32_t>(counts.textures));
  writer.patch_u32(kMipCountOffset, static_cast<std::uint32_t>(counts.mips));
  writer.patch_u32(kMaterialCountOffset,
                   static_cast<std::uint32_t>(counts.materials));
  writer.patch_u32(kMeshCountOffset, static_cast<std::uint32_t>(counts.meshes));
  writer.patch_u64(kJointCountOffset, counts.joints);
  writer.patch_u64(kDrawCountOffset, counts.draws);
  writer.patch_u64(kVertexCountOffset, counts.vertices);
  writer.patch_u64(kIndexCountOffset, counts.indices);
  writer.patch_u64(kPixelBytesOffset, counts.pixel_bytes);
  writer.patch_u64(kSemanticKeyBytesOffset, counts.semantic_key_bytes);
}

void write_texture(Writer &writer, const RenderSceneTextureV1 &texture) {
  writer.append_u32(texture.id);
  writer.append_u8(static_cast<std::uint8_t>(texture.color_space));
  writer.append_zero(3U);
  writer.append_u32(static_cast<std::uint32_t>(texture.mips.size()));
  writer.append_u32(0U);
  for (std::size_t level = 0U; level < texture.mips.size(); ++level) {
    const auto &mip = texture.mips[level];
    writer.append_u32(static_cast<std::uint32_t>(level));
    writer.append_u32(mip.width);
    writer.append_u32(mip.height);
    writer.append_u32(0U);
    writer.append_u64(mip.rgba8.size());
    writer.append_bytes(mip.rgba8);
  }
}

void write_material(Writer &writer, const RenderSceneMaterialV1 &material) {
  writer.append_u32(material.id);
  writer.append_u32(material.base_color_texture_id.value_or(kNoTextureId));
  writer.append_u32(material.base_color_rgba8);
  std::uint32_t flags = 0U;
  flags |= material.use_vertex_color ? kMaterialUsesVertexColor : 0U;
  flags |= material.double_sided ? kMaterialDoubleSided : 0U;
  writer.append_u32(flags);
  writer.append_u8(static_cast<std::uint8_t>(material.address_u));
  writer.append_u8(static_cast<std::uint8_t>(material.address_v));
  writer.append_u8(static_cast<std::uint8_t>(material.min_filter));
  writer.append_u8(static_cast<std::uint8_t>(material.mag_filter));
  writer.append_u8(static_cast<std::uint8_t>(material.mipmap_filter));
  writer.append_u8(static_cast<std::uint8_t>(material.alpha_mode));
  writer.append_u8(material.alpha_cutoff_rgba8);
  writer.append_u8(static_cast<std::uint8_t>(material.color_math));
  writer.append_u8(static_cast<std::uint8_t>(material.blend_mode));
  writer.append_u8(static_cast<std::uint8_t>(material.interpolation));
  writer.append_u8(static_cast<std::uint8_t>(material.depth_test));
  writer.append_u8(material.depth_write?0U:1U);
  writer.append_u8(material.texture_modulation_denominator==255U?0U:material.texture_modulation_denominator);
  writer.append_u8(material.blend_denominator==255U?0U:material.blend_denominator);
  writer.append_u8(static_cast<std::uint8_t>(material.alpha_failure));
  writer.append_zero(1U);
}

void write_vertex(Writer &writer, const ActorSkinnedVertexV1 &vertex) {
  writer.append_f32(vertex.x);
  writer.append_f32(vertex.y);
  writer.append_f32(vertex.z);
  writer.append_f32(vertex.nx);
  writer.append_f32(vertex.ny);
  writer.append_f32(vertex.nz);
  writer.append_f32(vertex.u);
  writer.append_f32(vertex.v);
  writer.append_u32(vertex.rgba8);
  writer.append_u8(vertex.skin.influence_count);
  writer.append_zero(3U);
  for (const auto joint : vertex.skin.joint_indices) {
    writer.append_u16(joint);
  }
  for (const auto weight : vertex.skin.weight_numerators) {
    writer.append_u16(weight);
  }
  writer.append_u16(vertex.skin.weight_sum);
  writer.append_u16(0U);
}

void write_mesh(Writer &writer, const ActorSkinnedMeshV1 &mesh) {
  writer.append_u32(mesh.id);
  writer.append_u32(0U);
  writer.append_u64(mesh.vertices.size());
  writer.append_u64(mesh.triangle_indices.size());
  writer.append_u32(static_cast<std::uint32_t>(mesh.draw_ranges.size()));
  writer.append_u32(0U);
  for (const auto &vertex : mesh.vertices) {
    write_vertex(writer, vertex);
  }
  for (const auto &draw : mesh.draw_ranges) {
    writer.append_u32(mesh.id);
    writer.append_u32(draw.material_id);
    writer.append_u64(draw.first_index);
    writer.append_u64(draw.index_count);
  }
  for (const auto index : mesh.triangle_indices) {
    writer.append_u32(index);
  }
}

} // namespace

std::vector<std::byte>
encode_actor_library_v1(const ActorLibraryV1 &library,
                        const ActorLibraryIoLimitsV1 limits) {
  if (limits.max_encoded_bytes < kActorLibraryIoHeaderBytesV1) {
    fail("ActorLibraryV1 encoded-byte limit is too small");
  }
  ActorLibraryV1 canonical;
  try {
    canonical = canonicalize_actor_library_v1(library, limits.library);
  } catch (const ActorLibraryError &error) {
    fail("Cannot encode ActorLibraryV1: " + std::string(error.what()));
  }
  const auto counts = count_library(canonical);
  const auto exact_bytes = encoded_size(counts);
  if (exact_bytes > limits.max_encoded_bytes ||
      exact_bytes > std::vector<std::byte>{}.max_size()) {
    fail("ActorLibraryV1 encoded bytes exceed the caller or host limit");
  }
  Writer writer(limits.max_encoded_bytes);
  for (const auto &rig : canonical.rigs) {
    writer.append_u32(rig.id);
    writer.append_u32(static_cast<std::uint32_t>(rig.semantic_key.size()));
    writer.append_u32(static_cast<std::uint32_t>(rig.rig.joints.size()));
    writer.append_u32(0U);
    writer.append_digest(rig.content_sha256);
    writer.append_string(rig.semantic_key);
    for (const auto &joint : rig.rig.joints) {
      writer.append_i32(joint.parent_index);
      writer.append_u32(0U);
      for (const float value : joint.local_bind_transform.values) {
        writer.append_f32(value);
      }
      for (const float value : joint.inverse_bind_transform.values) {
        writer.append_f32(value);
      }
    }
  }
  for (const auto &model : canonical.models) {
    std::uint64_t model_mips = 0U;
    std::uint64_t model_draws = 0U;
    std::uint64_t model_vertices = 0U;
    std::uint64_t model_indices = 0U;
    std::uint64_t model_pixels = 0U;
    for (const auto &texture : model.textures) {
      model_mips += texture.mips.size();
      for (const auto &mip : texture.mips) {
        model_pixels += mip.rgba8.size();
      }
    }
    for (const auto &mesh : model.meshes) {
      model_draws += mesh.draw_ranges.size();
      model_vertices += mesh.vertices.size();
      model_indices += mesh.triangle_indices.size();
    }
    writer.append_u32(model.id);
    writer.append_u32(static_cast<std::uint32_t>(model.semantic_key.size()));
    writer.append_u32(static_cast<std::uint32_t>(model.rig_key.size()));
    writer.append_u32(static_cast<std::uint32_t>(model.textures.size()));
    writer.append_u32(static_cast<std::uint32_t>(model.materials.size()));
    writer.append_u32(static_cast<std::uint32_t>(model.meshes.size()));
    writer.append_u32(0U);
    writer.append_u32(0U);
    writer.append_u64(model_mips);
    writer.append_u64(model_draws);
    writer.append_u64(model_vertices);
    writer.append_u64(model_indices);
    writer.append_u64(model_pixels);
    writer.append_digest(model.content_sha256);
    writer.append_zero(8U);
    writer.append_string(model.semantic_key);
    writer.append_string(model.rig_key);
    for (const auto &texture : model.textures) {
      write_texture(writer, texture);
    }
    for (const auto &material : model.materials) {
      write_material(writer, material);
    }
    for (const auto &mesh : model.meshes) {
      write_mesh(writer, mesh);
    }
  }
  if (writer.size() != exact_bytes) {
    fail("ActorLibraryV1 internal encoded size accounting disagrees");
  }
  write_header(writer, counts, exact_bytes);
  return std::move(writer).finish();
}

ActorLibraryV1 decode_actor_library_v1(const std::span<const std::byte> bytes,
                                       const ActorLibraryIoLimitsV1 limits) {
  if (limits.max_encoded_bytes < kActorLibraryIoHeaderBytesV1) {
    fail("ActorLibraryV1 encoded-byte limit is too small");
  }
  try {
    validate_actor_library_v1(ActorLibraryV1{}, limits.library);
  } catch (const ActorLibraryError &error) {
    fail("ActorLibraryV1 caller limits are invalid: " +
         std::string(error.what()));
  }
  if (bytes.size() < kActorLibraryIoHeaderBytesV1 ||
      bytes.size() > limits.max_encoded_bytes) {
    fail("ActorLibraryV1 input size exceeds its exact caller envelope");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    fail("ActorLibraryV1 magic is invalid");
  }

  Reader header_fields(bytes.first(kActorLibraryIoHeaderBytesV1));
  for (std::size_t index = 0U; index < kMagic.size(); ++index) {
    static_cast<void>(header_fields.read_u8("ActorLibraryV1 magic"));
  }
  if (header_fields.read_u32("ActorLibraryV1 format version") !=
          kActorLibraryIoFormatVersionV1 ||
      header_fields.read_u32("ActorLibraryV1 header size") !=
          kActorLibraryIoHeaderBytesV1 ||
      header_fields.read_u64("ActorLibraryV1 total byte size") !=
          bytes.size() ||
      header_fields.read_u32("ActorLibraryV1 schema version") !=
          kActorLibrarySchemaVersionV1) {
    fail("ActorLibraryV1 header version or total byte size is invalid");
  }
  Counts declared;
  declared.rigs = header_fields.read_u32("ActorLibraryV1 rig count");
  declared.models = header_fields.read_u32("ActorLibraryV1 model count");
  declared.textures = header_fields.read_u32("ActorLibraryV1 texture count");
  declared.mips = header_fields.read_u32("ActorLibraryV1 mip count");
  declared.materials = header_fields.read_u32("ActorLibraryV1 material count");
  declared.meshes = header_fields.read_u32("ActorLibraryV1 mesh count");
  if (header_fields.read_u32("ActorLibraryV1 reserved header word") != 0U) {
    fail("ActorLibraryV1 reserved header word is non-zero");
  }
  declared.joints = header_fields.read_u64("ActorLibraryV1 joint count");
  declared.draws = header_fields.read_u64("ActorLibraryV1 draw count");
  declared.vertices = header_fields.read_u64("ActorLibraryV1 vertex count");
  declared.indices = header_fields.read_u64("ActorLibraryV1 index count");
  declared.pixel_bytes =
      header_fields.read_u64("ActorLibraryV1 pixel byte count");
  declared.semantic_key_bytes =
      header_fields.read_u64("ActorLibraryV1 semantic key byte count");
  header_fields.require_zero(kActorLibraryIoHeaderBytesV1 -
                                 kHeaderReservedOffset,
                             "ActorLibraryV1 reserved header bytes");
  if (!header_fields.finished()) {
    fail("ActorLibraryV1 header layout is inconsistent");
  }

  require_limit(declared.rigs, limits.library.max_rigs,
                "ActorLibraryV1 rig count");
  require_limit(declared.models, limits.library.max_models,
                "ActorLibraryV1 model count");
  require_limit(declared.textures, limits.library.max_textures,
                "ActorLibraryV1 texture count");
  require_limit(declared.mips, limits.library.max_total_texture_mips,
                "ActorLibraryV1 mip count");
  require_limit(declared.materials, limits.library.max_materials,
                "ActorLibraryV1 material count");
  require_limit(declared.meshes, limits.library.max_meshes,
                "ActorLibraryV1 mesh count");
  require_limit(declared.joints, limits.library.max_total_joints,
                "ActorLibraryV1 joint count");
  require_limit(declared.draws, limits.library.max_draw_ranges,
                "ActorLibraryV1 draw count");
  require_limit(declared.vertices, limits.library.max_vertices,
                "ActorLibraryV1 vertex count");
  require_limit(declared.indices, limits.library.max_triangle_indices,
                "ActorLibraryV1 index count");
  require_limit(declared.pixel_bytes, limits.library.max_total_rgba8_bytes,
                "ActorLibraryV1 pixel byte count");
  require_limit(declared.semantic_key_bytes,
                limits.library.max_total_semantic_key_bytes,
                "ActorLibraryV1 semantic key bytes");
  if (encoded_size(declared) != bytes.size()) {
    fail("ActorLibraryV1 aggregate counts do not describe its exact bytes");
  }

  ActorLibraryV1 result;
  result.rigs.reserve(host_size(declared.rigs, result.rigs.max_size(),
                                "ActorLibraryV1 rig count"));
  result.models.reserve(host_size(declared.models, result.models.max_size(),
                                  "ActorLibraryV1 model count"));
  Reader reader(bytes.subspan(kActorLibraryIoHeaderBytesV1));
  Counts seen;
  for (std::uint32_t rig_index = 0U; rig_index < declared.rigs; ++rig_index) {
    ActorRigAssetV1 rig;
    rig.id = reader.read_u32("An actor rig ID");
    const auto key_bytes = reader.read_u32("An actor rig key length");
    const auto joint_count = reader.read_u32("An actor rig joint count");
    if (reader.read_u32("An actor rig reserved word") != 0U ||
        key_bytes == 0U || key_bytes > limits.library.max_semantic_key_bytes ||
        joint_count == 0U || joint_count > limits.library.max_joints_per_rig) {
      fail("ActorLibraryV1 rig header is invalid");
    }
    add_partition(seen.semantic_key_bytes, key_bytes,
                  declared.semantic_key_bytes, "Actor rig key bytes");
    add_partition(seen.joints, joint_count, declared.joints,
                  "Actor rig joints");
    rig.content_sha256 = reader.read_digest("An actor rig content digest");
    rig.semantic_key = reader.read_string(key_bytes, "An actor rig key");
    rig.rig.joints.reserve(host_size(joint_count, rig.rig.joints.max_size(),
                                     "An actor rig joint count"));
    for (std::uint32_t joint_index = 0U; joint_index < joint_count;
         ++joint_index) {
      ActorRigJointV1 joint;
      joint.parent_index = reader.read_i32("An actor joint parent");
      if (reader.read_u32("An actor joint reserved word") != 0U) {
        fail("ActorLibraryV1 joint reserved word is non-zero");
      }
      for (float &value : joint.local_bind_transform.values) {
        value = reader.read_f32("An actor local-bind value");
      }
      for (float &value : joint.inverse_bind_transform.values) {
        value = reader.read_f32("An actor inverse-bind value");
      }
      rig.rig.joints.push_back(joint);
    }
    result.rigs.push_back(std::move(rig));
  }

  for (std::uint32_t model_index = 0U; model_index < declared.models;
       ++model_index) {
    ActorModelV1 model;
    model.id = reader.read_u32("An actor model ID");
    const auto key_bytes = reader.read_u32("An actor model key length");
    const auto rig_key_bytes = reader.read_u32("An actor model rig-key length");
    const auto texture_count = reader.read_u32("An actor model texture count");
    const auto material_count =
        reader.read_u32("An actor model material count");
    const auto mesh_count = reader.read_u32("An actor model mesh count");
    if (reader.read_u32("An actor model reserved word") != 0U ||
        reader.read_u32("An actor model reserved word") != 0U) {
      fail("ActorLibraryV1 model reserved word is non-zero");
    }
    const auto model_mips = reader.read_u64("An actor model mip count");
    const auto model_draws = reader.read_u64("An actor model draw count");
    const auto model_vertices = reader.read_u64("An actor model vertex count");
    const auto model_indices = reader.read_u64("An actor model index count");
    const auto model_pixels =
        reader.read_u64("An actor model pixel byte count");
    model.content_sha256 = reader.read_digest("An actor model content digest");
    reader.require_zero(8U, "ActorLibraryV1 model reserved bytes");
    if (key_bytes == 0U || rig_key_bytes == 0U ||
        key_bytes > limits.library.max_semantic_key_bytes ||
        rig_key_bytes > limits.library.max_semantic_key_bytes ||
        texture_count > limits.library.max_textures || material_count == 0U ||
        material_count > limits.library.max_materials || mesh_count == 0U ||
        mesh_count > limits.library.max_meshes ||
        model_mips > limits.library.max_total_texture_mips ||
        model_draws > limits.library.max_draw_ranges ||
        model_vertices > limits.library.max_vertices ||
        model_indices > limits.library.max_triangle_indices ||
        model_pixels > limits.library.max_total_rgba8_bytes) {
      fail("ActorLibraryV1 model header exceeds its caller limits");
    }
    add_partition(seen.semantic_key_bytes,
                  static_cast<std::uint64_t>(key_bytes) + rig_key_bytes,
                  declared.semantic_key_bytes, "Actor model key bytes");
    add_partition(seen.textures, texture_count, declared.textures,
                  "Actor model textures");
    add_partition(seen.materials, material_count, declared.materials,
                  "Actor model materials");
    add_partition(seen.meshes, mesh_count, declared.meshes,
                  "Actor model meshes");
    add_partition(seen.mips, model_mips, declared.mips, "Actor model mips");
    add_partition(seen.draws, model_draws, declared.draws, "Actor model draws");
    add_partition(seen.vertices, model_vertices, declared.vertices,
                  "Actor model vertices");
    add_partition(seen.indices, model_indices, declared.indices,
                  "Actor model indices");
    add_partition(seen.pixel_bytes, model_pixels, declared.pixel_bytes,
                  "Actor model pixels");
    model.semantic_key = reader.read_string(key_bytes, "An actor model key");
    model.rig_key = reader.read_string(rig_key_bytes, "An actor model rig key");
    model.textures.reserve(host_size(texture_count, model.textures.max_size(),
                                     "An actor model texture count"));
    model.materials.reserve(host_size(material_count,
                                      model.materials.max_size(),
                                      "An actor model material count"));
    model.meshes.reserve(host_size(mesh_count, model.meshes.max_size(),
                                   "An actor model mesh count"));

    std::uint64_t parsed_mips = 0U;
    std::uint64_t parsed_pixels = 0U;
    for (std::uint32_t texture_index = 0U; texture_index < texture_count;
         ++texture_index) {
      RenderSceneTextureV1 texture;
      texture.id = reader.read_u32("An actor texture ID");
      const auto color_space = reader.read_u8("An actor texture color space");
      reader.require_zero(3U, "Actor texture reserved bytes");
      const auto mip_count = reader.read_u32("An actor texture mip count");
      if (reader.read_u32("An actor texture reserved word") != 0U ||
          color_space >
              static_cast<std::uint8_t>(RenderSceneTextureColorSpaceV1::srgb) ||
          mip_count == 0U || mip_count > limits.library.max_mips_per_texture ||
          mip_count > model_mips - parsed_mips) {
        fail("ActorLibraryV1 texture header is invalid");
      }
      texture.color_space =
          static_cast<RenderSceneTextureColorSpaceV1>(color_space);
      texture.mips.reserve(host_size(mip_count, texture.mips.max_size(),
                                     "An actor texture mip count"));
      std::uint64_t texture_texels = 0U;
      std::uint32_t expected_width = 0U;
      std::uint32_t expected_height = 0U;
      for (std::uint32_t mip_index = 0U; mip_index < mip_count; ++mip_index) {
        if (reader.read_u32("An actor mip level") != mip_index) {
          fail("ActorLibraryV1 mip levels are not canonical");
        }
        RenderSceneTextureMipV1 mip;
        mip.width = reader.read_u32("An actor mip width");
        mip.height = reader.read_u32("An actor mip height");
        if (reader.read_u32("An actor mip reserved word") != 0U) {
          fail("ActorLibraryV1 mip reserved word is non-zero");
        }
        const auto pixel_bytes = reader.read_u64("An actor mip byte count");
        const auto texels =
            checked_multiply(mip.width, mip.height, "Actor mip texel count");
        texture_texels =
            checked_add(texture_texels, texels, "Actor texture texel count");
        if (mip.width == 0U || mip.height == 0U ||
            mip.width > limits.library.max_texture_width ||
            mip.height > limits.library.max_texture_height ||
            (mip_index != 0U &&
             (mip.width != expected_width || mip.height != expected_height)) ||
            texture_texels > limits.library.max_texels_per_texture ||
            checked_multiply(texels, 4U, "Actor mip byte count") !=
                pixel_bytes ||
            pixel_bytes > model_pixels - parsed_pixels ||
            (mip.width == 1U && mip.height == 1U &&
             mip_index + 1U != mip_count)) {
          fail("ActorLibraryV1 mip dimensions or byte count are invalid");
        }
        expected_width = std::max(UINT32_C(1), mip.width / 2U);
        expected_height = std::max(UINT32_C(1), mip.height / 2U);
        mip.rgba8 =
            reader.read_bytes(pixel_bytes, std::vector<std::byte>{}.max_size(),
                              "Actor mip pixels");
        parsed_pixels += pixel_bytes;
        texture.mips.push_back(std::move(mip));
      }
      parsed_mips += mip_count;
      model.textures.push_back(std::move(texture));
    }
    if (parsed_mips != model_mips || parsed_pixels != model_pixels) {
      fail("ActorLibraryV1 texture tables do not match model totals");
    }

    for (std::uint32_t material_index = 0U; material_index < material_count;
         ++material_index) {
      RenderSceneMaterialV1 material;
      material.id = reader.read_u32("An actor material ID");
      const auto texture_id = reader.read_u32("An actor material texture ID");
      if (texture_id != kNoTextureId) {
        material.base_color_texture_id = texture_id;
      }
      material.base_color_rgba8 =
          reader.read_u32("An actor material base color");
      const auto flags = reader.read_u32("An actor material flags");
      if ((flags & ~kKnownMaterialFlags) != 0U) {
        fail("ActorLibraryV1 material has unknown flags");
      }
      material.use_vertex_color = (flags & kMaterialUsesVertexColor) != 0U;
      material.double_sided = (flags & kMaterialDoubleSided) != 0U;
      material.address_u = static_cast<RenderSceneAddressModeV1>(
          reader.read_u8("An actor material U address mode"));
      material.address_v = static_cast<RenderSceneAddressModeV1>(
          reader.read_u8("An actor material V address mode"));
      material.min_filter = static_cast<RenderSceneFilterV1>(
          reader.read_u8("An actor material min filter"));
      material.mag_filter = static_cast<RenderSceneFilterV1>(
          reader.read_u8("An actor material mag filter"));
      material.mipmap_filter = static_cast<RenderSceneMipmapFilterV1>(
          reader.read_u8("An actor material mip filter"));
      material.alpha_mode = static_cast<RenderSceneAlphaModeV1>(
          reader.read_u8("An actor material alpha mode"));
      material.alpha_cutoff_rgba8 =
          reader.read_u8("An actor material alpha cutoff");
      material.color_math=static_cast<RenderSceneColorMathV1>(reader.read_u8("Actor material color math"));
      material.blend_mode=static_cast<RenderSceneBlendModeV1>(reader.read_u8("Actor material blend mode"));
      material.interpolation=static_cast<RenderSceneInterpolationV1>(reader.read_u8("Actor material interpolation"));
      material.depth_test=static_cast<RenderSceneDepthTestV1>(reader.read_u8("Actor material depth test"));
      const auto no_depth_write=reader.read_u8("Actor material depth write");
      const auto modulation=reader.read_u8("Actor material modulation denominator");
      const auto blend=reader.read_u8("Actor material blend denominator");
      if(no_depth_write>1U || modulation==255U || blend==255U)
        fail("ActorLibraryV1 material extension has noncanonical wire values");
      material.depth_write=no_depth_write==0U;
      material.texture_modulation_denominator=modulation?modulation:255U;
      material.blend_denominator=blend?blend:255U;
      material.alpha_failure=static_cast<RenderSceneAlphaFailureV1>(
          reader.read_u8("Actor material alpha failure"));
      reader.require_zero(1U, "Actor material reserved bytes");
      model.materials.push_back(material);
    }

    std::uint64_t parsed_draws = 0U;
    std::uint64_t parsed_vertices = 0U;
    std::uint64_t parsed_indices = 0U;
    for (std::uint32_t mesh_index = 0U; mesh_index < mesh_count; ++mesh_index) {
      ActorSkinnedMeshV1 mesh;
      mesh.id = reader.read_u32("An actor mesh ID");
      if (reader.read_u32("An actor mesh reserved word") != 0U) {
        fail("ActorLibraryV1 mesh reserved word is non-zero");
      }
      const auto vertex_count = reader.read_u64("An actor mesh vertex count");
      const auto index_count = reader.read_u64("An actor mesh index count");
      const auto draw_count = reader.read_u32("An actor mesh draw count");
      if (reader.read_u32("An actor mesh reserved word") != 0U ||
          vertex_count == 0U ||
          vertex_count > model_vertices - parsed_vertices ||
          vertex_count > std::numeric_limits<std::uint32_t>::max() ||
          index_count == 0U || index_count > model_indices - parsed_indices ||
          (index_count % 3U) != 0U || draw_count == 0U ||
          draw_count > model_draws - parsed_draws) {
        fail("ActorLibraryV1 mesh header is invalid");
      }
      mesh.vertices.reserve(host_size(vertex_count, mesh.vertices.max_size(),
                                      "An actor mesh vertex count"));
      mesh.triangle_indices.reserve(host_size(index_count,
                                              mesh.triangle_indices.max_size(),
                                              "An actor mesh index count"));
      mesh.draw_ranges.reserve(host_size(
          draw_count, mesh.draw_ranges.max_size(), "An actor mesh draw count"));
      for (std::uint64_t vertex_index = 0U; vertex_index < vertex_count;
           ++vertex_index) {
        ActorSkinnedVertexV1 vertex;
        vertex.x = reader.read_f32("An actor vertex X");
        vertex.y = reader.read_f32("An actor vertex Y");
        vertex.z = reader.read_f32("An actor vertex Z");
        vertex.nx = reader.read_f32("An actor vertex normal X");
        vertex.ny = reader.read_f32("An actor vertex normal Y");
        vertex.nz = reader.read_f32("An actor vertex normal Z");
        vertex.u = reader.read_f32("An actor vertex U");
        vertex.v = reader.read_f32("An actor vertex V");
        vertex.rgba8 = reader.read_u32("An actor vertex color");
        vertex.skin.influence_count =
            reader.read_u8("An actor skin influence count");
        reader.require_zero(3U, "Actor vertex reserved bytes");
        for (auto &joint : vertex.skin.joint_indices) {
          joint = reader.read_u16("An actor skin joint index");
        }
        for (auto &weight : vertex.skin.weight_numerators) {
          weight = reader.read_u16("An actor skin weight");
        }
        vertex.skin.weight_sum = reader.read_u16("An actor skin weight sum");
        if (reader.read_u16("An actor vertex reserved word") != 0U) {
          fail("ActorLibraryV1 vertex reserved word is non-zero");
        }
        mesh.vertices.push_back(vertex);
      }
      for (std::uint32_t draw_index = 0U; draw_index < draw_count;
           ++draw_index) {
        if (reader.read_u32("An actor draw mesh ID") != mesh.id) {
          fail("ActorLibraryV1 draw belongs to the wrong mesh");
        }
        mesh.draw_ranges.push_back(RenderSceneDrawRangeV1{
            reader.read_u32("An actor draw material ID"),
            reader.read_u64("An actor draw first index"),
            reader.read_u64("An actor draw index count"),
        });
      }
      for (std::uint64_t index = 0U; index < index_count; ++index) {
        mesh.triangle_indices.push_back(
            reader.read_u32("An actor triangle index"));
      }
      parsed_vertices += vertex_count;
      parsed_indices += index_count;
      parsed_draws += draw_count;
      model.meshes.push_back(std::move(mesh));
    }
    if (parsed_vertices != model_vertices || parsed_indices != model_indices ||
        parsed_draws != model_draws) {
      fail("ActorLibraryV1 geometry tables do not match model totals");
    }
    result.models.push_back(std::move(model));
  }
  if (seen.semantic_key_bytes != declared.semantic_key_bytes ||
      seen.joints != declared.joints || seen.textures != declared.textures ||
      seen.mips != declared.mips || seen.materials != declared.materials ||
      seen.meshes != declared.meshes || seen.draws != declared.draws ||
      seen.vertices != declared.vertices || seen.indices != declared.indices ||
      seen.pixel_bytes != declared.pixel_bytes || !reader.finished()) {
    fail("ActorLibraryV1 body partitions or trailing bytes are inconsistent");
  }
  try {
    validate_actor_library_v1(result, limits.library);
  } catch (const ActorLibraryError &error) {
    fail("Decoded ActorLibraryV1 is invalid: " + std::string(error.what()));
  }
  return result;
}

} // namespace openrc
