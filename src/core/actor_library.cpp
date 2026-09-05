#include "openrc/actor_library.hpp"

#include "openrc/hash.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw ActorLibraryError(message);
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

void validate_limits(const ActorLibraryLimitsV1 &limits) {
  if (limits.max_rigs == 0U || limits.max_models == 0U ||
      limits.max_semantic_key_bytes == 0U ||
      limits.max_total_semantic_key_bytes == 0U ||
      limits.max_joints_per_rig == 0U || limits.max_total_joints == 0U ||
      limits.max_textures == 0U || limits.max_mips_per_texture == 0U ||
      limits.max_total_texture_mips == 0U || limits.max_materials == 0U ||
      limits.max_meshes == 0U || limits.max_draw_ranges == 0U ||
      limits.max_vertices == 0U || limits.max_triangle_indices == 0U ||
      limits.max_texture_width == 0U || limits.max_texture_height == 0U ||
      limits.max_texels_per_texture == 0U ||
      limits.max_total_rgba8_bytes == 0U) {
    fail("ActorLibraryV1 limits must all be positive");
  }
}

[[nodiscard]] bool key_character(const unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('a') &&
          value <= static_cast<unsigned char>('z')) ||
         (value >= static_cast<unsigned char>('0') &&
          value <= static_cast<unsigned char>('9')) ||
         value == static_cast<unsigned char>('.') ||
         value == static_cast<unsigned char>('_') ||
         value == static_cast<unsigned char>('-') ||
         value == static_cast<unsigned char>('/');
}

void validate_key(const std::string_view value,
                  const std::uint32_t maximum_bytes,
                  const char *const description) {
  if (value.empty() || value.size() > maximum_bytes || value.front() == '/' ||
      value.back() == '/') {
    fail(std::string(description) + " is not a canonical semantic key");
  }
  for (const char character : value) {
    if (!key_character(static_cast<unsigned char>(character))) {
      fail(std::string(description) + " contains a non-canonical character");
    }
  }
  std::size_t component_begin = 0U;
  while (component_begin < value.size()) {
    const auto separator = value.find('/', component_begin);
    const auto component_end =
        separator == std::string_view::npos ? value.size() : separator;
    const auto component =
        value.substr(component_begin, component_end - component_begin);
    if (component.empty() || component == "." || component == "..") {
      fail(std::string(description) + " contains an unsafe component");
    }
    if (separator == std::string_view::npos) {
      break;
    }
    component_begin = separator + 1U;
  }
}

void validate_canonical_float(const float value,
                              const char *const description) {
  if (!std::isfinite(value)) {
    fail(std::string("ActorLibraryV1 has a non-finite ") + description);
  }
  if (value == 0.0F && std::signbit(value)) {
    fail(std::string("ActorLibraryV1 has non-canonical signed zero in ") +
         description);
  }
}

[[nodiscard]] float canonical_float(const float value,
                                    const char *const description) {
  if (!std::isfinite(value)) {
    fail(std::string("ActorLibraryV1 has a non-finite ") + description);
  }
  return value == 0.0F ? 0.0F : value;
}

template <typename Item>
void require_dense_ids(const std::vector<Item> &items,
                       const char *const description) {
  for (std::size_t index = 0U; index < items.size(); ++index) {
    if (items[index].id != index) {
      fail(std::string("ActorLibraryV1 ") + description +
           " IDs are not canonical dense table indices");
    }
  }
}

template <typename Item> void sort_by_id(std::vector<Item> &items) {
  std::sort(
      items.begin(), items.end(),
      [](const Item &left, const Item &right) { return left.id < right.id; });
}

void require_u32_count(const std::size_t count, const std::uint32_t limit,
                       const char *const description) {
  if (count > limit || count > std::numeric_limits<std::uint32_t>::max()) {
    fail(std::string("ActorLibraryV1 ") + description +
         " count exceeds its caller or format limit");
  }
}

[[nodiscard]] bool
valid_color_space(const RenderSceneTextureColorSpaceV1 value) noexcept {
  return value == RenderSceneTextureColorSpaceV1::linear ||
         value == RenderSceneTextureColorSpaceV1::srgb;
}

[[nodiscard]] bool
valid_address_mode(const RenderSceneAddressModeV1 value) noexcept {
  return value == RenderSceneAddressModeV1::repeat ||
         value == RenderSceneAddressModeV1::clamp_to_edge;
}

[[nodiscard]] bool valid_filter(const RenderSceneFilterV1 value) noexcept {
  return value == RenderSceneFilterV1::nearest ||
         value == RenderSceneFilterV1::linear;
}

[[nodiscard]] bool
valid_mipmap_filter(const RenderSceneMipmapFilterV1 value) noexcept {
  return value == RenderSceneMipmapFilterV1::none ||
         value == RenderSceneMipmapFilterV1::nearest ||
         value == RenderSceneMipmapFilterV1::linear;
}

[[nodiscard]] bool
valid_alpha_mode(const RenderSceneAlphaModeV1 value) noexcept {
  return value == RenderSceneAlphaModeV1::opaque ||
         value == RenderSceneAlphaModeV1::mask;
}

[[nodiscard]] double
linear_determinant(const ActorAffineTransformV1 &transform) noexcept {
  const auto &v = transform.values;
  return static_cast<double>(v[0U]) * (static_cast<double>(v[5U]) * v[10U] -
                                       static_cast<double>(v[6U]) * v[9U]) -
         static_cast<double>(v[1U]) * (static_cast<double>(v[4U]) * v[10U] -
                                       static_cast<double>(v[6U]) * v[8U]) +
         static_cast<double>(v[2U]) * (static_cast<double>(v[4U]) * v[9U] -
                                       static_cast<double>(v[5U]) * v[8U]);
}

void validate_rig(const ActorRigV1 &rig, const std::uint32_t max_joints) {
  if (rig.joints.empty() || rig.joints.size() > max_joints ||
      rig.joints.size() > std::numeric_limits<std::uint16_t>::max()) {
    fail("ActorLibraryV1 rig has an invalid joint count");
  }
  for (std::size_t index = 0U; index < rig.joints.size(); ++index) {
    const auto &joint = rig.joints[index];
    if ((index == 0U && joint.parent_index != -1) ||
        (index != 0U &&
         (joint.parent_index < 0 ||
          static_cast<std::size_t>(joint.parent_index) >= index))) {
      fail("ActorLibraryV1 rig hierarchy is not one canonical parent-first "
           "tree");
    }
    for (const float value : joint.local_bind_transform.values) {
      validate_canonical_float(value, "local-bind transform");
    }
    for (const float value : joint.inverse_bind_transform.values) {
      validate_canonical_float(value, "inverse-bind transform");
    }
    const auto local_determinant =
        linear_determinant(joint.local_bind_transform);
    const auto inverse_determinant =
        linear_determinant(joint.inverse_bind_transform);
    if (!std::isfinite(local_determinant) || local_determinant == 0.0 ||
        !std::isfinite(inverse_determinant) || inverse_determinant == 0.0) {
      fail("ActorLibraryV1 joint contains a singular affine transform");
    }
  }
}

void canonicalize_rig(ActorRigV1 &rig) {
  for (auto &joint : rig.joints) {
    for (float &value : joint.local_bind_transform.values) {
      value = canonical_float(value, "local-bind transform");
    }
    for (float &value : joint.inverse_bind_transform.values) {
      value = canonical_float(value, "inverse-bind transform");
    }
  }
}

void canonicalize_skin_binding(ActorSkinBindingV1 &skin) {
  if (skin.influence_count == 0U ||
      skin.influence_count > kActorMaximumSkinInfluencesV1) {
    fail("ActorLibraryV1 vertex has an invalid skin influence count");
  }
  std::array<std::pair<std::uint16_t, std::uint16_t>,
             kActorMaximumSkinInfluencesV1>
      influences{};
  for (std::size_t index = 0U; index < skin.influence_count; ++index) {
    influences[index] = {skin.joint_indices[index],
                         skin.weight_numerators[index]};
  }
  std::sort(influences.begin(), influences.begin() + skin.influence_count,
            [](const auto &left, const auto &right) {
              return left.first < right.first;
            });
  skin.joint_indices = {};
  skin.weight_numerators = {};
  for (std::size_t index = 0U; index < skin.influence_count; ++index) {
    skin.joint_indices[index] = influences[index].first;
    skin.weight_numerators[index] = influences[index].second;
  }
}

void validate_skin_binding(const ActorSkinBindingV1 &skin,
                           const std::size_t joint_count) {
  if (skin.influence_count == 0U ||
      skin.influence_count > kActorMaximumSkinInfluencesV1 ||
      skin.weight_sum == 0U) {
    fail("ActorLibraryV1 vertex has an invalid skin influence envelope");
  }
  std::uint32_t sum = 0U;
  for (std::size_t index = 0U; index < skin.joint_indices.size(); ++index) {
    if (index < skin.influence_count) {
      if (skin.joint_indices[index] >= joint_count ||
          skin.weight_numerators[index] == 0U ||
          (index != 0U &&
           skin.joint_indices[index - 1U] >= skin.joint_indices[index])) {
        fail("ActorLibraryV1 vertex has invalid or non-canonical influences");
      }
      sum += skin.weight_numerators[index];
    } else if (skin.joint_indices[index] != 0U ||
               skin.weight_numerators[index] != 0U) {
      fail("ActorLibraryV1 vertex has non-zero unused skin influences");
    }
  }
  if (sum != skin.weight_sum) {
    fail("ActorLibraryV1 vertex skin weights do not equal their exact sum");
  }
}

class DigestWriter final {
public:
  void append_bytes(const std::span<const std::byte> bytes) {
    hash_.update(bytes);
  }

  void append_domain(const std::string_view domain) {
    append_u32(static_cast<std::uint32_t>(domain.size()));
    append_bytes(std::as_bytes(std::span(domain.data(), domain.size())));
  }

  void append_u8(const std::uint8_t value) {
    const std::array bytes{static_cast<std::byte>(value)};
    append_bytes(bytes);
  }

  void append_u16(const std::uint16_t value) {
    const std::array bytes{static_cast<std::byte>(value & 0xffU),
                           static_cast<std::byte>((value >> 8U) & 0xffU)};
    append_bytes(bytes);
  }

  void append_u32(const std::uint32_t value) {
    const std::array bytes{static_cast<std::byte>(value & 0xffU),
                           static_cast<std::byte>((value >> 8U) & 0xffU),
                           static_cast<std::byte>((value >> 16U) & 0xffU),
                           static_cast<std::byte>((value >> 24U) & 0xffU)};
    append_bytes(bytes);
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

  [[nodiscard]] PreparedContentDigestV1 finish() { return hash_.finish(); }

private:
  Sha256 hash_;
};

void digest_texture(DigestWriter &writer, const RenderSceneTextureV1 &texture) {
  writer.append_u32(texture.id);
  writer.append_u8(static_cast<std::uint8_t>(texture.color_space));
  writer.append_u32(static_cast<std::uint32_t>(texture.mips.size()));
  for (std::size_t level = 0U; level < texture.mips.size(); ++level) {
    const auto &mip = texture.mips[level];
    writer.append_u32(static_cast<std::uint32_t>(level));
    writer.append_u32(mip.width);
    writer.append_u32(mip.height);
    writer.append_u64(mip.rgba8.size());
    writer.append_bytes(mip.rgba8);
  }
}

void digest_material(DigestWriter &writer,
                     const RenderSceneMaterialV1 &material) {
  writer.append_u32(material.id);
  writer.append_u32(material.base_color_texture_id.value_or(UINT32_MAX));
  writer.append_u32(material.base_color_rgba8);
  writer.append_u8(material.use_vertex_color ? 1U : 0U);
  writer.append_u8(material.double_sided ? 1U : 0U);
  writer.append_u8(static_cast<std::uint8_t>(material.address_u));
  writer.append_u8(static_cast<std::uint8_t>(material.address_v));
  writer.append_u8(static_cast<std::uint8_t>(material.min_filter));
  writer.append_u8(static_cast<std::uint8_t>(material.mag_filter));
  writer.append_u8(static_cast<std::uint8_t>(material.mipmap_filter));
  writer.append_u8(static_cast<std::uint8_t>(material.alpha_mode));
  writer.append_u8(material.alpha_cutoff_rgba8);
}

void digest_skin(DigestWriter &writer, const ActorSkinBindingV1 &skin) {
  writer.append_u8(skin.influence_count);
  for (const auto value : skin.joint_indices) {
    writer.append_u16(value);
  }
  for (const auto value : skin.weight_numerators) {
    writer.append_u16(value);
  }
  writer.append_u16(skin.weight_sum);
}

void digest_mesh(DigestWriter &writer, const ActorSkinnedMeshV1 &mesh) {
  writer.append_u32(mesh.id);
  writer.append_u64(mesh.vertices.size());
  writer.append_u64(mesh.triangle_indices.size());
  writer.append_u64(mesh.draw_ranges.size());
  for (const auto &vertex : mesh.vertices) {
    writer.append_f32(vertex.x);
    writer.append_f32(vertex.y);
    writer.append_f32(vertex.z);
    writer.append_f32(vertex.nx);
    writer.append_f32(vertex.ny);
    writer.append_f32(vertex.nz);
    writer.append_f32(vertex.u);
    writer.append_f32(vertex.v);
    writer.append_u32(vertex.rgba8);
    digest_skin(writer, vertex.skin);
  }
  for (const auto &draw : mesh.draw_ranges) {
    writer.append_u32(draw.material_id);
    writer.append_u64(draw.first_index);
    writer.append_u64(draw.index_count);
  }
  for (const auto index : mesh.triangle_indices) {
    writer.append_u32(index);
  }
}

struct AggregateCounts {
  std::uint64_t semantic_key_bytes = 0U;
  std::uint64_t joints = 0U;
  std::uint64_t textures = 0U;
  std::uint64_t mips = 0U;
  std::uint64_t materials = 0U;
  std::uint64_t meshes = 0U;
  std::uint64_t draws = 0U;
  std::uint64_t vertices = 0U;
  std::uint64_t indices = 0U;
  std::uint64_t rgba8_bytes = 0U;
};

void validate_texture(const RenderSceneTextureV1 &texture,
                      const ActorLibraryLimitsV1 &limits,
                      AggregateCounts &counts) {
  if (!valid_color_space(texture.color_space) || texture.mips.empty() ||
      texture.mips.size() > limits.max_mips_per_texture ||
      texture.mips.size() > std::numeric_limits<std::uint32_t>::max()) {
    fail("ActorLibraryV1 texture has an invalid policy or mip count");
  }
  counts.mips =
      checked_add(counts.mips, texture.mips.size(), "ActorLibraryV1 mip count");
  if (counts.mips > limits.max_total_texture_mips) {
    fail("ActorLibraryV1 mip count exceeds its aggregate limit");
  }
  std::uint64_t texture_texels = 0U;
  std::uint32_t expected_width = 0U;
  std::uint32_t expected_height = 0U;
  for (std::size_t level = 0U; level < texture.mips.size(); ++level) {
    const auto &mip = texture.mips[level];
    if (mip.width == 0U || mip.height == 0U ||
        mip.width > limits.max_texture_width ||
        mip.height > limits.max_texture_height ||
        (level != 0U &&
         (mip.width != expected_width || mip.height != expected_height))) {
      fail("ActorLibraryV1 texture mip dimensions are invalid");
    }
    const auto texels = checked_multiply(mip.width, mip.height,
                                         "ActorLibraryV1 mip texel count");
    const auto byte_count =
        checked_multiply(texels, 4U, "ActorLibraryV1 mip byte count");
    if (byte_count != mip.rgba8.size()) {
      fail("ActorLibraryV1 texture mip has an inexact RGBA8 byte count");
    }
    texture_texels = checked_add(texture_texels, texels,
                                 "ActorLibraryV1 texture texel count");
    counts.rgba8_bytes =
        checked_add(counts.rgba8_bytes, byte_count,
                    "ActorLibraryV1 aggregate RGBA8 byte count");
    if (texture_texels > limits.max_texels_per_texture ||
        counts.rgba8_bytes > limits.max_total_rgba8_bytes) {
      fail("ActorLibraryV1 texture data exceeds caller limits");
    }
    if (mip.width == 1U && mip.height == 1U &&
        level + 1U != texture.mips.size()) {
      fail("ActorLibraryV1 mip chain continues past 1x1");
    }
    expected_width = std::max(UINT32_C(1), mip.width / 2U);
    expected_height = std::max(UINT32_C(1), mip.height / 2U);
  }
}

void validate_model(const ActorModelV1 &model, const ActorRigAssetV1 &rig,
                    const ActorLibraryLimitsV1 &limits,
                    AggregateCounts &counts) {
  require_u32_count(model.textures.size(), limits.max_textures, "texture");
  require_u32_count(model.materials.size(), limits.max_materials, "material");
  require_u32_count(model.meshes.size(), limits.max_meshes, "mesh");
  if (model.materials.empty() || model.meshes.empty()) {
    fail("ActorLibraryV1 model needs a material and a skinned mesh");
  }
  require_dense_ids(model.textures, "model texture");
  require_dense_ids(model.materials, "model material");
  require_dense_ids(model.meshes, "model mesh");
  counts.textures = checked_add(counts.textures, model.textures.size(),
                                "ActorLibraryV1 texture count");
  counts.materials = checked_add(counts.materials, model.materials.size(),
                                 "ActorLibraryV1 material count");
  counts.meshes = checked_add(counts.meshes, model.meshes.size(),
                              "ActorLibraryV1 mesh count");
  if (counts.textures > limits.max_textures ||
      counts.materials > limits.max_materials ||
      counts.meshes > limits.max_meshes) {
    fail("ActorLibraryV1 model tables exceed aggregate caller limits");
  }

  for (const auto &texture : model.textures) {
    validate_texture(texture, limits, counts);
  }
  std::vector<bool> used_textures(model.textures.size(), false);
  std::vector<bool> used_materials(model.materials.size(), false);
  for (const auto &material : model.materials) {
    if (!valid_address_mode(material.address_u) ||
        !valid_address_mode(material.address_v) ||
        !valid_filter(material.min_filter) ||
        !valid_filter(material.mag_filter) ||
        !valid_mipmap_filter(material.mipmap_filter) ||
        !valid_alpha_mode(material.alpha_mode)) {
      fail("ActorLibraryV1 material uses an unknown render policy");
    }
    if ((material.alpha_mode == RenderSceneAlphaModeV1::opaque &&
         material.alpha_cutoff_rgba8 != 0U) ||
        (material.alpha_mode == RenderSceneAlphaModeV1::mask &&
         material.alpha_cutoff_rgba8 == 0U)) {
      fail("ActorLibraryV1 material has an invalid alpha cutoff");
    }
    if (material.base_color_texture_id) {
      const auto texture_id = *material.base_color_texture_id;
      if (texture_id >= model.textures.size()) {
        fail("ActorLibraryV1 material references a missing texture");
      }
      used_textures[texture_id] = true;
      if (material.mipmap_filter != RenderSceneMipmapFilterV1::none &&
          model.textures[texture_id].mips.size() < 2U) {
        fail("ActorLibraryV1 material requests unavailable mip filtering");
      }
    } else if (material.address_u != RenderSceneAddressModeV1::repeat ||
               material.address_v != RenderSceneAddressModeV1::repeat ||
               material.min_filter != RenderSceneFilterV1::linear ||
               material.mag_filter != RenderSceneFilterV1::linear ||
               material.mipmap_filter != RenderSceneMipmapFilterV1::none) {
      fail(
          "ActorLibraryV1 untextured material has non-canonical sampler state");
    }
  }

  for (const auto &mesh : model.meshes) {
    if (mesh.vertices.empty() || mesh.triangle_indices.empty() ||
        mesh.draw_ranges.empty() ||
        mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        mesh.draw_ranges.size() > std::numeric_limits<std::uint32_t>::max() ||
        (mesh.triangle_indices.size() % 3U) != 0U) {
      fail("ActorLibraryV1 skinned mesh has an invalid geometry envelope");
    }
    counts.vertices = checked_add(counts.vertices, mesh.vertices.size(),
                                  "ActorLibraryV1 vertex count");
    counts.indices = checked_add(counts.indices, mesh.triangle_indices.size(),
                                 "ActorLibraryV1 index count");
    counts.draws = checked_add(counts.draws, mesh.draw_ranges.size(),
                               "ActorLibraryV1 draw count");
    if (counts.vertices > limits.max_vertices ||
        counts.indices > limits.max_triangle_indices ||
        counts.draws > limits.max_draw_ranges) {
      fail("ActorLibraryV1 geometry exceeds aggregate caller limits");
    }
    for (const auto &vertex : mesh.vertices) {
      validate_canonical_float(vertex.x, "vertex X");
      validate_canonical_float(vertex.y, "vertex Y");
      validate_canonical_float(vertex.z, "vertex Z");
      validate_canonical_float(vertex.nx, "vertex normal X");
      validate_canonical_float(vertex.ny, "vertex normal Y");
      validate_canonical_float(vertex.nz, "vertex normal Z");
      validate_canonical_float(vertex.u, "vertex U");
      validate_canonical_float(vertex.v, "vertex V");
      validate_skin_binding(vertex.skin, rig.rig.joints.size());
    }
    for (const auto index : mesh.triangle_indices) {
      if (index >= mesh.vertices.size()) {
        fail("ActorLibraryV1 triangle references a missing vertex");
      }
    }
    std::uint64_t expected_first = 0U;
    for (const auto &draw : mesh.draw_ranges) {
      if (draw.material_id >= model.materials.size() ||
          draw.first_index != expected_first || draw.index_count == 0U ||
          (draw.index_count % 3U) != 0U ||
          draw.index_count > mesh.triangle_indices.size() - expected_first) {
        fail("ActorLibraryV1 draw ranges are not a complete partition");
      }
      used_materials[draw.material_id] = true;
      expected_first = checked_add(expected_first, draw.index_count,
                                   "ActorLibraryV1 draw coverage");
    }
    if (expected_first != mesh.triangle_indices.size()) {
      fail("ActorLibraryV1 draw ranges do not cover their mesh");
    }
  }
  if (std::ranges::any_of(used_materials,
                          [](const bool used) { return !used; }) ||
      std::ranges::any_of(used_textures,
                          [](const bool used) { return !used; })) {
    fail("ActorLibraryV1 model contains an orphan material or texture");
  }
}

void canonicalize_model(ActorModelV1 &model) {
  sort_by_id(model.textures);
  sort_by_id(model.materials);
  sort_by_id(model.meshes);
  for (auto &mesh : model.meshes) {
    for (auto &vertex : mesh.vertices) {
      vertex.x = canonical_float(vertex.x, "vertex X");
      vertex.y = canonical_float(vertex.y, "vertex Y");
      vertex.z = canonical_float(vertex.z, "vertex Z");
      vertex.nx = canonical_float(vertex.nx, "vertex normal X");
      vertex.ny = canonical_float(vertex.ny, "vertex normal Y");
      vertex.nz = canonical_float(vertex.nz, "vertex normal Z");
      vertex.u = canonical_float(vertex.u, "vertex U");
      vertex.v = canonical_float(vertex.v, "vertex V");
      canonicalize_skin_binding(vertex.skin);
    }
  }
}

} // namespace

bool ActorRigAssetV1::operator==(const ActorRigAssetV1 &other) const {
  if (id != other.id || semantic_key != other.semantic_key ||
      content_sha256 != other.content_sha256 ||
      rig.joints.size() != other.rig.joints.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < rig.joints.size(); ++index) {
    const auto &left = rig.joints[index];
    const auto &right = other.rig.joints[index];
    if (left.parent_index != right.parent_index ||
        left.local_bind_transform.values != right.local_bind_transform.values ||
        left.inverse_bind_transform.values !=
            right.inverse_bind_transform.values) {
      return false;
    }
  }
  return true;
}

PreparedContentDigestV1 actor_rig_content_sha256_v1(const ActorRigV1 &rig) {
  // This unbounded validation only enforces intrinsic format constraints; the
  // library validator applies the caller's smaller joint envelope separately.
  validate_rig(rig, std::numeric_limits<std::uint16_t>::max());
  DigestWriter writer;
  writer.append_domain("openrc.actor-rig.v1");
  writer.append_u32(kActorLibrarySchemaVersionV1);
  writer.append_u32(static_cast<std::uint32_t>(rig.joints.size()));
  for (const auto &joint : rig.joints) {
    writer.append_i32(joint.parent_index);
    for (const float value : joint.local_bind_transform.values) {
      writer.append_f32(value);
    }
    for (const float value : joint.inverse_bind_transform.values) {
      writer.append_f32(value);
    }
  }
  return writer.finish();
}

PreparedContentDigestV1 actor_model_content_sha256_v1(
    const ActorModelV1 &model,
    const PreparedContentDigestV1 &rig_content_sha256) {
  if (is_zero_prepared_digest_v1(rig_content_sha256)) {
    fail("ActorLibraryV1 cannot address a model with a zero rig digest");
  }
  if (model.textures.size() > std::numeric_limits<std::uint32_t>::max() ||
      model.materials.size() > std::numeric_limits<std::uint32_t>::max() ||
      model.meshes.size() > std::numeric_limits<std::uint32_t>::max()) {
    fail("ActorLibraryV1 model table exceeds the content-address width");
  }
  for (const auto &texture : model.textures) {
    if (texture.mips.size() > std::numeric_limits<std::uint32_t>::max()) {
      fail("ActorLibraryV1 mip table exceeds the content-address width");
    }
  }
  DigestWriter writer;
  writer.append_domain("openrc.actor-model.v1");
  writer.append_u32(kActorLibrarySchemaVersionV1);
  writer.append_bytes(rig_content_sha256);
  writer.append_u32(static_cast<std::uint32_t>(model.textures.size()));
  writer.append_u32(static_cast<std::uint32_t>(model.materials.size()));
  writer.append_u32(static_cast<std::uint32_t>(model.meshes.size()));
  for (const auto &texture : model.textures) {
    digest_texture(writer, texture);
  }
  for (const auto &material : model.materials) {
    digest_material(writer, material);
  }
  for (const auto &mesh : model.meshes) {
    digest_mesh(writer, mesh);
  }
  return writer.finish();
}

void validate_actor_library_v1(const ActorLibraryV1 &library,
                               const ActorLibraryLimitsV1 limits) {
  validate_limits(limits);
  if (library.schema_version != kActorLibrarySchemaVersionV1) {
    fail("ActorLibraryV1 has an unsupported schema version");
  }
  require_u32_count(library.rigs.size(), limits.max_rigs, "rig");
  require_u32_count(library.models.size(), limits.max_models, "model");
  require_dense_ids(library.rigs, "rig");
  require_dense_ids(library.models, "model");

  AggregateCounts counts;
  std::map<std::string, const ActorRigAssetV1 *, std::less<>> rigs_by_key;
  for (const auto &rig : library.rigs) {
    validate_key(rig.semantic_key, limits.max_semantic_key_bytes,
                 "An actor rig key");
    counts.semantic_key_bytes =
        checked_add(counts.semantic_key_bytes, rig.semantic_key.size(),
                    "ActorLibraryV1 semantic key bytes");
    counts.joints = checked_add(counts.joints, rig.rig.joints.size(),
                                "ActorLibraryV1 joint count");
    if (counts.joints > limits.max_total_joints) {
      fail("ActorLibraryV1 joints exceed their aggregate limit");
    }
    validate_rig(rig.rig, limits.max_joints_per_rig);
    const auto expected_digest = actor_rig_content_sha256_v1(rig.rig);
    if (is_zero_prepared_digest_v1(rig.content_sha256) ||
        rig.content_sha256 != expected_digest) {
      fail("ActorLibraryV1 rig content digest is absent or stale");
    }
    if (!rigs_by_key.emplace(rig.semantic_key, &rig).second) {
      fail("ActorLibraryV1 repeats a rig semantic key");
    }
  }

  std::map<std::string, const ActorModelV1 *, std::less<>> models_by_key;
  for (const auto &model : library.models) {
    validate_key(model.semantic_key, limits.max_semantic_key_bytes,
                 "An actor model key");
    validate_key(model.rig_key, limits.max_semantic_key_bytes,
                 "An actor model rig key");
    counts.semantic_key_bytes =
        checked_add(counts.semantic_key_bytes,
                    checked_add(model.semantic_key.size(), model.rig_key.size(),
                                "ActorLibraryV1 model key bytes"),
                    "ActorLibraryV1 semantic key bytes");
    if (counts.semantic_key_bytes > limits.max_total_semantic_key_bytes) {
      fail("ActorLibraryV1 semantic keys exceed their aggregate limit");
    }
    const auto rig = rigs_by_key.find(model.rig_key);
    if (rig == rigs_by_key.end()) {
      fail("ActorLibraryV1 model references a missing rig key");
    }
    if (!models_by_key.emplace(model.semantic_key, &model).second) {
      fail("ActorLibraryV1 repeats a model semantic key");
    }
    validate_model(model, *rig->second, limits, counts);
    const auto expected_digest =
        actor_model_content_sha256_v1(model, rig->second->content_sha256);
    if (is_zero_prepared_digest_v1(model.content_sha256) ||
        model.content_sha256 != expected_digest) {
      fail("ActorLibraryV1 model content digest is absent or stale");
    }
  }
  if (counts.semantic_key_bytes > limits.max_total_semantic_key_bytes) {
    fail("ActorLibraryV1 semantic keys exceed their aggregate limit");
  }
}

ActorLibraryV1
canonicalize_actor_library_v1(ActorLibraryV1 library,
                              const ActorLibraryLimitsV1 limits) {
  validate_limits(limits);
  if (library.schema_version != kActorLibrarySchemaVersionV1) {
    fail("ActorLibraryV1 has an unsupported schema version");
  }
  sort_by_id(library.rigs);
  sort_by_id(library.models);
  for (auto &rig : library.rigs) {
    canonicalize_rig(rig.rig);
    const auto digest = actor_rig_content_sha256_v1(rig.rig);
    if (!is_zero_prepared_digest_v1(rig.content_sha256) &&
        rig.content_sha256 != digest) {
      fail("ActorLibraryV1 rig supplied a stale content digest");
    }
    rig.content_sha256 = digest;
  }
  std::map<std::string, const ActorRigAssetV1 *, std::less<>> rigs_by_key;
  for (const auto &rig : library.rigs) {
    rigs_by_key.emplace(rig.semantic_key, &rig);
  }
  for (auto &model : library.models) {
    canonicalize_model(model);
    const auto rig = rigs_by_key.find(model.rig_key);
    if (rig == rigs_by_key.end()) {
      fail("ActorLibraryV1 model references a missing rig key");
    }
    const auto digest =
        actor_model_content_sha256_v1(model, rig->second->content_sha256);
    if (!is_zero_prepared_digest_v1(model.content_sha256) &&
        model.content_sha256 != digest) {
      fail("ActorLibraryV1 model supplied a stale content digest");
    }
    model.content_sha256 = digest;
  }
  validate_actor_library_v1(library, limits);
  return library;
}

ActorLibraryV1
compose_actor_libraries_v1(const std::span<const ActorLibraryV1> libraries,
                           const ActorLibraryLimitsV1 limits) {
  validate_limits(limits);
  ActorLibraryV1 result;
  std::map<std::string, std::size_t, std::less<>> rig_indices;
  std::map<std::string, bool, std::less<>> model_keys;
  AggregateCounts counts;

  for (const auto &library : libraries) {
    validate_actor_library_v1(library, limits);
    for (const auto &rig : library.rigs) {
      const auto found = rig_indices.find(rig.semantic_key);
      if (found != rig_indices.end()) {
        if (result.rigs[found->second].content_sha256 != rig.content_sha256) {
          fail("ActorLibraryV1 composition found different rigs behind one "
               "semantic key");
        }
        continue;
      }
      if (result.rigs.size() >= limits.max_rigs ||
          result.rigs.size() >= std::numeric_limits<std::uint32_t>::max()) {
        fail("ActorLibraryV1 composition exceeds the top-level rig limit");
      }
      counts.semantic_key_bytes = checked_add(
          counts.semantic_key_bytes, rig.semantic_key.size(),
          "ActorLibraryV1 composition semantic-key bytes");
      counts.joints = checked_add(counts.joints, rig.rig.joints.size(),
                                  "ActorLibraryV1 composition joint count");
      if (counts.semantic_key_bytes > limits.max_total_semantic_key_bytes ||
          counts.joints > limits.max_total_joints) {
        fail("ActorLibraryV1 composition exceeds aggregate rig limits");
      }
      auto appended = rig;
      appended.id = static_cast<std::uint32_t>(result.rigs.size());
      rig_indices.emplace(appended.semantic_key, result.rigs.size());
      result.rigs.push_back(std::move(appended));
    }

    for (const auto &model : library.models) {
      if (!model_keys.emplace(model.semantic_key, true).second) {
        fail("ActorLibraryV1 composition repeats a model semantic key");
      }
      if (result.models.size() >= limits.max_models ||
          result.models.size() >= std::numeric_limits<std::uint32_t>::max()) {
        fail("ActorLibraryV1 composition exceeds the top-level model limit");
      }
      counts.semantic_key_bytes = checked_add(
          counts.semantic_key_bytes,
          checked_add(model.semantic_key.size(), model.rig_key.size(),
                      "ActorLibraryV1 composition model-key bytes"),
          "ActorLibraryV1 composition semantic-key bytes");
      if (counts.semantic_key_bytes > limits.max_total_semantic_key_bytes) {
        fail("ActorLibraryV1 composition exceeds aggregate key limits");
      }
      const auto rig = rig_indices.find(model.rig_key);
      if (rig == rig_indices.end()) {
        fail("ActorLibraryV1 composition found a model without its rig");
      }
      validate_model(model, result.rigs[rig->second], limits, counts);
      auto appended = model;
      appended.id = static_cast<std::uint32_t>(result.models.size());
      result.models.push_back(std::move(appended));
    }
  }

  return canonicalize_actor_library_v1(std::move(result), limits);
}

} // namespace openrc
