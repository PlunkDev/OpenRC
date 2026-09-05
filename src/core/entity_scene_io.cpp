#include "openrc/entity_scene_io.hpp"

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
    std::byte{'O'}, std::byte{'R'}, std::byte{'E'}, std::byte{'N'},
    std::byte{'T'}, std::byte{'S'}, std::byte{'C'}, std::byte{'N'},
};

constexpr std::uint64_t kFormatVersionOffset = 0x08U;
constexpr std::uint64_t kHeaderBytesOffset = 0x0cU;
constexpr std::uint64_t kTotalBytesOffset = 0x10U;
constexpr std::uint64_t kPayloadTypeOffset = 0x18U;
constexpr std::uint64_t kSchemaVersionOffset = 0x1cU;
constexpr std::uint64_t kLevelIdOffset = 0x20U;
constexpr std::uint64_t kDefinitionCountOffset = 0x24U;
constexpr std::uint64_t kTransformCountOffset = 0x28U;
constexpr std::uint64_t kRenderBindingCountOffset = 0x2cU;
constexpr std::uint64_t kActorBindingCountOffset = 0x30U;
constexpr std::uint64_t kPlayerBindingCountOffset = 0x34U;
constexpr std::uint64_t kTotalKeyBytesOffset = 0x38U;
constexpr std::uint64_t kDefinitionRecordBytesOffset = 0x40U;
constexpr std::uint64_t kTransformRecordBytesOffset = 0x44U;
constexpr std::uint64_t kRenderBindingRecordBytesOffset = 0x48U;
constexpr std::uint64_t kActorBindingRecordBytesOffset = 0x4cU;
constexpr std::uint64_t kPlayerBindingRecordBytesOffset = 0x50U;
constexpr std::uint64_t kHeaderReservedWordOffset = 0x54U;
constexpr std::uint64_t kDefinitionTableOffset = 0x58U;
constexpr std::uint64_t kTransformTableOffset = 0x60U;
constexpr std::uint64_t kRenderBindingTableOffset = 0x68U;
constexpr std::uint64_t kActorBindingTableOffset = 0x70U;
constexpr std::uint64_t kPlayerBindingTableOffset = 0x78U;
constexpr std::uint64_t kKeyDataOffset = 0x80U;
constexpr std::uint64_t kHeaderReservedOffset = 0x88U;

constexpr std::uint64_t kDefinitionAuthoredIdOffset = 0x00U;
constexpr std::uint64_t kDefinitionFlagsOffset = 0x04U;
constexpr std::uint64_t kDefinitionAuthoringGroupOffset = 0x08U;
constexpr std::uint64_t kDefinitionKeyBytesOffset = 0x0cU;
constexpr std::uint64_t kDefinitionKeyOffsetOffset = 0x10U;
constexpr std::uint64_t kDefinitionReservedOffset = 0x18U;

constexpr std::uint64_t kComponentAuthoredIdOffset = 0x00U;
constexpr std::uint64_t kTransformReservedOffset = 0x04U;
constexpr std::uint64_t kTransformPositionOffset = 0x08U;
constexpr std::uint64_t kTransformRotationOffset = 0x14U;
constexpr std::uint64_t kTransformScaleOffset = 0x24U;

constexpr std::uint64_t kRenderInstanceIdOffset = 0x04U;
constexpr std::uint64_t kRenderReservedOffset = 0x08U;

constexpr std::uint64_t kActorKeyBytesOffset = 0x04U;
constexpr std::uint64_t kActorKeyOffsetOffset = 0x08U;
constexpr std::uint64_t kActorTransformOffset = 0x10U;

constexpr std::uint64_t kPlayerSlotOffset = 0x04U;
constexpr std::uint64_t kPlayerReservedOffset = 0x08U;

static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<float>::radix == 2);
static_assert(std::numeric_limits<float>::digits == 24);

[[noreturn]] void fail(const std::string &message) {
  throw EntitySceneIoError(message);
}

void validate_limits(const EntitySceneIoLimitsV1 &limits) {
  if (limits.max_encoded_bytes < kEntitySceneIoHeaderBytesV1 ||
      limits.scene.max_definitions == 0U ||
      limits.scene.max_transforms == 0U ||
      limits.scene.max_render_bindings == 0U ||
      limits.scene.max_actor_bindings == 0U ||
      limits.scene.max_player_bindings == 0U ||
      limits.scene.max_archetype_key_bytes == 0U ||
      limits.scene.max_model_key_bytes == 0U ||
      limits.scene.max_total_key_bytes == 0U) {
    fail("EntitySceneV1 I/O limits must all be non-zero and bounded");
  }
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
                                    const std::size_t maximum,
                                    const char *const description) {
  if (value > maximum ||
      value > static_cast<std::uint64_t>(
                  std::numeric_limits<std::size_t>::max())) {
    fail(std::string(description) + " exceeds the host container domain");
  }
  return static_cast<std::size_t>(value);
}

[[nodiscard]] std::uint32_t format_count(const std::size_t value,
                                         const char *const description) {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    fail(std::string(description) + " exceeds the format count width");
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_u32(const std::span<const std::byte> bytes,
                                     const std::uint64_t offset) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  return static_cast<std::uint32_t>(byte_value(bytes[begin])) |
         (static_cast<std::uint32_t>(byte_value(bytes[begin + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[begin + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[begin + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t read_u64(const std::span<const std::byte> bytes,
                                     const std::uint64_t offset) noexcept {
  std::uint64_t result = 0U;
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < sizeof(result); ++index) {
    result |= static_cast<std::uint64_t>(byte_value(bytes[begin + index]))
              << (index * 8U);
  }
  return result;
}

[[nodiscard]] float read_f32(const std::span<const std::byte> bytes,
                             const std::uint64_t offset,
                             const char *const description) {
  const auto value = std::bit_cast<float>(read_u32(bytes, offset));
  if (!std::isfinite(value)) {
    fail(std::string("EntitySceneV1 has a non-finite ") + description);
  }
  if (value == 0.0F && std::signbit(value)) {
    fail(std::string("EntitySceneV1 has non-canonical signed zero in ") +
         description);
  }
  return value;
}

void write_u32(std::vector<std::byte> &bytes, const std::uint64_t offset,
               const std::uint32_t value) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes[begin + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT32_C(0xff));
  }
}

void write_u64(std::vector<std::byte> &bytes, const std::uint64_t offset,
               const std::uint64_t value) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes[begin + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
  }
}

void write_f32(std::vector<std::byte> &bytes, const std::uint64_t offset,
               const float value) noexcept {
  write_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] bool all_zero(const std::span<const std::byte> bytes,
                            const std::uint64_t begin,
                            const std::uint64_t end) noexcept {
  return std::all_of(
      bytes.begin() + static_cast<std::ptrdiff_t>(begin),
      bytes.begin() + static_cast<std::ptrdiff_t>(end),
      [](const std::byte value) { return value == std::byte{0U}; });
}

void write_string(std::vector<std::byte> &bytes, const std::uint64_t offset,
                  const std::string &value) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < value.size(); ++index) {
    bytes[begin + index] =
        static_cast<std::byte>(static_cast<unsigned char>(value[index]));
  }
}

[[nodiscard]] std::string read_string(
    const std::span<const std::byte> bytes, const std::uint64_t offset,
    const std::uint32_t length, const std::uint32_t maximum_length,
    const char *const description) {
  if (length == 0U || length > maximum_length) {
    fail(std::string(description) + " has an invalid byte length");
  }
  std::string result;
  result.reserve(host_size(length, result.max_size(), description));
  const auto begin = static_cast<std::size_t>(offset);
  for (std::uint32_t index = 0U; index < length; ++index) {
    result.push_back(static_cast<char>(byte_value(bytes[begin + index])));
  }
  return result;
}

struct Counts {
  std::uint32_t definitions = 0U;
  std::uint32_t transforms = 0U;
  std::uint32_t render_bindings = 0U;
  std::uint32_t actor_bindings = 0U;
  std::uint32_t player_bindings = 0U;
  std::uint64_t key_bytes = 0U;
};

[[nodiscard]] Counts scene_counts(const EntitySceneV1 &scene) {
  Counts result;
  result.definitions =
      format_count(scene.definitions.size(), "Entity definition count");
  result.transforms =
      format_count(scene.transforms.size(), "Entity transform count");
  result.render_bindings = format_count(scene.render_bindings.size(),
                                        "Entity render-binding count");
  result.actor_bindings = format_count(scene.actor_bindings.size(),
                                       "Entity actor-binding count");
  result.player_bindings = format_count(scene.player_bindings.size(),
                                        "Entity player-binding count");
  for (const auto &definition : scene.definitions) {
    result.key_bytes = checked_add(result.key_bytes,
                                   definition.archetype_key.size(),
                                   "EntitySceneV1 key bytes");
  }
  for (const auto &binding : scene.actor_bindings) {
    result.key_bytes = checked_add(result.key_bytes, binding.model_key.size(),
                                   "EntitySceneV1 key bytes");
  }
  return result;
}

struct Layout {
  std::uint64_t definitions = 0U;
  std::uint64_t transforms = 0U;
  std::uint64_t render_bindings = 0U;
  std::uint64_t actor_bindings = 0U;
  std::uint64_t player_bindings = 0U;
  std::uint64_t keys = 0U;
  std::uint64_t total = 0U;
};

[[nodiscard]] Layout canonical_layout(const Counts &counts) {
  Layout result;
  result.definitions = kEntitySceneIoHeaderBytesV1;
  result.transforms = checked_add(
      result.definitions,
      checked_multiply(counts.definitions, kEntitySceneIoDefinitionBytesV1,
                       "Entity definition table"),
      "Entity definition table");
  result.render_bindings = checked_add(
      result.transforms,
      checked_multiply(counts.transforms, kEntitySceneIoTransformBytesV1,
                       "Entity transform table"),
      "Entity transform table");
  result.actor_bindings = checked_add(
      result.render_bindings,
      checked_multiply(counts.render_bindings,
                       kEntitySceneIoRenderBindingBytesV1,
                       "Entity render-binding table"),
      "Entity render-binding table");
  result.player_bindings = checked_add(
      result.actor_bindings,
      checked_multiply(counts.actor_bindings,
                       kEntitySceneIoActorBindingBytesV1,
                       "Entity actor-binding table"),
      "Entity actor-binding table");
  result.keys = checked_add(
      result.player_bindings,
      checked_multiply(counts.player_bindings,
                       kEntitySceneIoPlayerBindingBytesV1,
                       "Entity player-binding table"),
      "Entity player-binding table");
  result.total =
      checked_add(result.keys, counts.key_bytes, "EntitySceneV1 key data");
  return result;
}

[[nodiscard]] EntitySceneV1
canonical_scene(const EntitySceneV1 &scene,
                const EntitySceneLimitsV1 limits) {
  try {
    return canonicalize_entity_scene_v1(scene, limits);
  } catch (const EntitySceneError &error) {
    fail("Cannot encode EntitySceneV1: " + std::string(error.what()));
  }
}

void require_key_partition(const std::uint64_t declared_offset,
                           const std::uint32_t length,
                           std::uint64_t &expected_offset,
                           const std::uint64_t end,
                           const char *const description) {
  if (expected_offset > end || declared_offset != expected_offset ||
      length > end - expected_offset) {
    fail(std::string(description) +
         " is not part of the exact canonical key partition");
  }
  expected_offset += length;
}

} // namespace

std::vector<std::byte>
encode_entity_scene_v1(const EntitySceneV1 &scene,
                       const EntitySceneIoLimitsV1 limits) {
  validate_limits(limits);
  const auto canonical = canonical_scene(scene, limits.scene);
  const auto counts = scene_counts(canonical);
  const auto layout = canonical_layout(counts);
  if (layout.total > limits.max_encoded_bytes) {
    fail("EntitySceneV1 encoded bytes exceed the caller limit");
  }

  std::vector<std::byte> result(
      host_size(layout.total, std::vector<std::byte>{}.max_size(),
                "EntitySceneV1 encoded size"),
      std::byte{0U});
  std::copy(kMagic.begin(), kMagic.end(), result.begin());
  write_u32(result, kFormatVersionOffset, kEntitySceneIoFormatVersionV1);
  write_u32(result, kHeaderBytesOffset, kEntitySceneIoHeaderBytesV1);
  write_u64(result, kTotalBytesOffset, layout.total);
  write_u32(result, kPayloadTypeOffset, kEntitySceneIoPayloadTypeV1);
  write_u32(result, kSchemaVersionOffset, canonical.schema_version);
  write_u32(result, kLevelIdOffset, canonical.level_id);
  write_u32(result, kDefinitionCountOffset, counts.definitions);
  write_u32(result, kTransformCountOffset, counts.transforms);
  write_u32(result, kRenderBindingCountOffset, counts.render_bindings);
  write_u32(result, kActorBindingCountOffset, counts.actor_bindings);
  write_u32(result, kPlayerBindingCountOffset, counts.player_bindings);
  write_u64(result, kTotalKeyBytesOffset, counts.key_bytes);
  write_u32(result, kDefinitionRecordBytesOffset,
            kEntitySceneIoDefinitionBytesV1);
  write_u32(result, kTransformRecordBytesOffset,
            kEntitySceneIoTransformBytesV1);
  write_u32(result, kRenderBindingRecordBytesOffset,
            kEntitySceneIoRenderBindingBytesV1);
  write_u32(result, kActorBindingRecordBytesOffset,
            kEntitySceneIoActorBindingBytesV1);
  write_u32(result, kPlayerBindingRecordBytesOffset,
            kEntitySceneIoPlayerBindingBytesV1);
  write_u64(result, kDefinitionTableOffset, layout.definitions);
  write_u64(result, kTransformTableOffset, layout.transforms);
  write_u64(result, kRenderBindingTableOffset, layout.render_bindings);
  write_u64(result, kActorBindingTableOffset, layout.actor_bindings);
  write_u64(result, kPlayerBindingTableOffset, layout.player_bindings);
  write_u64(result, kKeyDataOffset, layout.keys);

  auto key_offset = layout.keys;
  for (std::size_t index = 0U; index < canonical.definitions.size(); ++index) {
    const auto offset = layout.definitions +
                        static_cast<std::uint64_t>(index) *
                            kEntitySceneIoDefinitionBytesV1;
    const auto &definition = canonical.definitions[index];
    write_u32(result, offset + kDefinitionAuthoredIdOffset,
              definition.authored_id);
    write_u32(result, offset + kDefinitionFlagsOffset, definition.flags);
    write_u32(result, offset + kDefinitionAuthoringGroupOffset,
              definition.authoring_group_id);
    write_u32(result, offset + kDefinitionKeyBytesOffset,
              format_count(definition.archetype_key.size(),
                           "Entity archetype key length"));
    write_u64(result, offset + kDefinitionKeyOffsetOffset, key_offset);
    write_string(result, key_offset, definition.archetype_key);
    key_offset += definition.archetype_key.size();
  }

  for (std::size_t index = 0U; index < canonical.transforms.size(); ++index) {
    const auto offset = layout.transforms +
                        static_cast<std::uint64_t>(index) *
                            kEntitySceneIoTransformBytesV1;
    const auto &component = canonical.transforms[index];
    write_u32(result, offset + kComponentAuthoredIdOffset,
              component.authored_id);
    for (std::size_t axis = 0U; axis < component.transform.position.size();
         ++axis) {
      write_f32(result, offset + kTransformPositionOffset + axis * 4U,
                component.transform.position[axis]);
    }
    for (std::size_t axis = 0U; axis < component.transform.rotation.size();
         ++axis) {
      write_f32(result, offset + kTransformRotationOffset + axis * 4U,
                component.transform.rotation[axis]);
    }
    for (std::size_t axis = 0U; axis < component.transform.scale.size();
         ++axis) {
      write_f32(result, offset + kTransformScaleOffset + axis * 4U,
                component.transform.scale[axis]);
    }
  }

  for (std::size_t index = 0U; index < canonical.render_bindings.size();
       ++index) {
    const auto offset = layout.render_bindings +
                        static_cast<std::uint64_t>(index) *
                            kEntitySceneIoRenderBindingBytesV1;
    const auto &binding = canonical.render_bindings[index];
    write_u32(result, offset + kComponentAuthoredIdOffset,
              binding.authored_id);
    write_u32(result, offset + kRenderInstanceIdOffset,
              binding.render_instance_id);
  }

  for (std::size_t index = 0U; index < canonical.actor_bindings.size();
       ++index) {
    const auto offset = layout.actor_bindings +
                        static_cast<std::uint64_t>(index) *
                            kEntitySceneIoActorBindingBytesV1;
    const auto &binding = canonical.actor_bindings[index];
    write_u32(result, offset + kComponentAuthoredIdOffset,
              binding.authored_id);
    write_u32(result, offset + kActorKeyBytesOffset,
              format_count(binding.model_key.size(),
                           "Entity actor model-key length"));
    write_u64(result, offset + kActorKeyOffsetOffset, key_offset);
    for (std::size_t value_index = 0U;
         value_index < binding.model_to_entity.values.size(); ++value_index) {
      write_f32(result, offset + kActorTransformOffset + value_index * 4U,
                binding.model_to_entity.values[value_index]);
    }
    write_string(result, key_offset, binding.model_key);
    key_offset += binding.model_key.size();
  }

  for (std::size_t index = 0U; index < canonical.player_bindings.size();
       ++index) {
    const auto offset = layout.player_bindings +
                        static_cast<std::uint64_t>(index) *
                            kEntitySceneIoPlayerBindingBytesV1;
    const auto &binding = canonical.player_bindings[index];
    write_u32(result, offset + kComponentAuthoredIdOffset,
              binding.authored_id);
    write_u32(result, offset + kPlayerSlotOffset, binding.local_player_slot);
  }
  if (key_offset != layout.total) {
    fail("EntitySceneV1 internal key partition is inconsistent");
  }
  return result;
}

EntitySceneV1 decode_entity_scene_v1(const std::span<const std::byte> bytes,
                                     const EntitySceneIoLimitsV1 limits) {
  validate_limits(limits);
  if (bytes.size() > limits.max_encoded_bytes) {
    fail("EntitySceneV1 encoded bytes exceed the caller limit");
  }
  if (bytes.size() < kEntitySceneIoHeaderBytesV1) {
    fail("EntitySceneV1 header is truncated");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    fail("EntitySceneV1 magic is invalid");
  }
  if (read_u32(bytes, kFormatVersionOffset) !=
      kEntitySceneIoFormatVersionV1) {
    fail("EntitySceneV1 format version is unknown");
  }
  if (read_u32(bytes, kPayloadTypeOffset) != kEntitySceneIoPayloadTypeV1) {
    fail("EntitySceneV1 payload type is unknown");
  }
  if (read_u32(bytes, kHeaderBytesOffset) != kEntitySceneIoHeaderBytesV1 ||
      read_u32(bytes, kSchemaVersionOffset) !=
          kEntitySceneSchemaVersionV1 ||
      read_u32(bytes, kDefinitionRecordBytesOffset) !=
          kEntitySceneIoDefinitionBytesV1 ||
      read_u32(bytes, kTransformRecordBytesOffset) !=
          kEntitySceneIoTransformBytesV1 ||
      read_u32(bytes, kRenderBindingRecordBytesOffset) !=
          kEntitySceneIoRenderBindingBytesV1 ||
      read_u32(bytes, kActorBindingRecordBytesOffset) !=
          kEntitySceneIoActorBindingBytesV1 ||
      read_u32(bytes, kPlayerBindingRecordBytesOffset) !=
          kEntitySceneIoPlayerBindingBytesV1 ||
      read_u32(bytes, kHeaderReservedWordOffset) != 0U ||
      !all_zero(bytes, kHeaderReservedOffset,
                kEntitySceneIoHeaderBytesV1)) {
    fail("EntitySceneV1 header schema, record sizes, or reserved data are invalid");
  }

  Counts counts;
  counts.definitions =
      read_u32(bytes, kDefinitionCountOffset);
  counts.transforms = read_u32(bytes, kTransformCountOffset);
  counts.render_bindings = read_u32(bytes, kRenderBindingCountOffset);
  counts.actor_bindings = read_u32(bytes, kActorBindingCountOffset);
  counts.player_bindings = read_u32(bytes, kPlayerBindingCountOffset);
  counts.key_bytes = read_u64(bytes, kTotalKeyBytesOffset);
  if (counts.definitions > limits.scene.max_definitions ||
      counts.transforms > limits.scene.max_transforms ||
      counts.render_bindings > limits.scene.max_render_bindings ||
      counts.actor_bindings > limits.scene.max_actor_bindings ||
      counts.player_bindings > limits.scene.max_player_bindings ||
      counts.key_bytes > limits.scene.max_total_key_bytes) {
    fail("EntitySceneV1 header exceeds a caller limit");
  }

  const auto layout = canonical_layout(counts);
  if (read_u64(bytes, kDefinitionTableOffset) != layout.definitions ||
      read_u64(bytes, kTransformTableOffset) != layout.transforms ||
      read_u64(bytes, kRenderBindingTableOffset) != layout.render_bindings ||
      read_u64(bytes, kActorBindingTableOffset) != layout.actor_bindings ||
      read_u64(bytes, kPlayerBindingTableOffset) != layout.player_bindings ||
      read_u64(bytes, kKeyDataOffset) != layout.keys ||
      read_u64(bytes, kTotalBytesOffset) != layout.total ||
      layout.total != bytes.size()) {
    fail("EntitySceneV1 table layout or exact byte size is invalid");
  }

  EntitySceneV1 result;
  result.schema_version = read_u32(bytes, kSchemaVersionOffset);
  result.level_id = read_u32(bytes, kLevelIdOffset);
  result.definitions.reserve(host_size(counts.definitions,
                                       result.definitions.max_size(),
                                       "Entity definition count"));
  result.transforms.reserve(host_size(counts.transforms,
                                      result.transforms.max_size(),
                                      "Entity transform count"));
  result.render_bindings.reserve(host_size(
      counts.render_bindings, result.render_bindings.max_size(),
      "Entity render-binding count"));
  result.actor_bindings.reserve(host_size(
      counts.actor_bindings, result.actor_bindings.max_size(),
      "Entity actor-binding count"));
  result.player_bindings.reserve(host_size(
      counts.player_bindings, result.player_bindings.max_size(),
      "Entity player-binding count"));

  auto expected_key_offset = layout.keys;
  for (std::uint32_t index = 0U; index < counts.definitions; ++index) {
    const auto offset = layout.definitions +
                        static_cast<std::uint64_t>(index) *
                            kEntitySceneIoDefinitionBytesV1;
    const auto key_length =
        read_u32(bytes, offset + kDefinitionKeyBytesOffset);
    const auto key_offset =
        read_u64(bytes, offset + kDefinitionKeyOffsetOffset);
    if (read_u64(bytes, offset + kDefinitionReservedOffset) != 0U) {
      fail("EntitySceneV1 definition reserved data is non-zero");
    }
    require_key_partition(key_offset, key_length, expected_key_offset,
                          layout.total, "An entity archetype key");
    EntityDefinitionV1 definition;
    definition.authored_id =
        read_u32(bytes, offset + kDefinitionAuthoredIdOffset);
    definition.flags = read_u32(bytes, offset + kDefinitionFlagsOffset);
    definition.authoring_group_id =
        read_u32(bytes, offset + kDefinitionAuthoringGroupOffset);
    definition.archetype_key =
        read_string(bytes, key_offset, key_length,
                    limits.scene.max_archetype_key_bytes,
                    "An entity archetype key");
    result.definitions.push_back(std::move(definition));
  }

  for (std::uint32_t index = 0U; index < counts.transforms; ++index) {
    const auto offset = layout.transforms +
                        static_cast<std::uint64_t>(index) *
                            kEntitySceneIoTransformBytesV1;
    if (read_u32(bytes, offset + kTransformReservedOffset) != 0U) {
      fail("EntitySceneV1 transform reserved data is non-zero");
    }
    EntityTransformComponentV1 component;
    component.authored_id =
        read_u32(bytes, offset + kComponentAuthoredIdOffset);
    for (std::size_t axis = 0U; axis < component.transform.position.size();
         ++axis) {
      component.transform.position[axis] = read_f32(
          bytes, offset + kTransformPositionOffset + axis * 4U,
          "world-transform position");
    }
    for (std::size_t axis = 0U; axis < component.transform.rotation.size();
         ++axis) {
      component.transform.rotation[axis] = read_f32(
          bytes, offset + kTransformRotationOffset + axis * 4U,
          "world-transform rotation");
    }
    for (std::size_t axis = 0U; axis < component.transform.scale.size();
         ++axis) {
      component.transform.scale[axis] = read_f32(
          bytes, offset + kTransformScaleOffset + axis * 4U,
          "world-transform scale");
    }
    result.transforms.push_back(component);
  }

  for (std::uint32_t index = 0U; index < counts.render_bindings; ++index) {
    const auto offset = layout.render_bindings +
                        static_cast<std::uint64_t>(index) *
                            kEntitySceneIoRenderBindingBytesV1;
    if (read_u64(bytes, offset + kRenderReservedOffset) != 0U) {
      fail("EntitySceneV1 render-binding reserved data is non-zero");
    }
    result.render_bindings.push_back(EntityRenderBindingV1{
        read_u32(bytes, offset + kComponentAuthoredIdOffset),
        read_u32(bytes, offset + kRenderInstanceIdOffset),
    });
  }

  for (std::uint32_t index = 0U; index < counts.actor_bindings; ++index) {
    const auto offset = layout.actor_bindings +
                        static_cast<std::uint64_t>(index) *
                            kEntitySceneIoActorBindingBytesV1;
    const auto key_length = read_u32(bytes, offset + kActorKeyBytesOffset);
    const auto key_offset = read_u64(bytes, offset + kActorKeyOffsetOffset);
    require_key_partition(key_offset, key_length, expected_key_offset,
                          layout.total, "An actor model key");
    EntityActorBindingV1 binding;
    binding.authored_id =
        read_u32(bytes, offset + kComponentAuthoredIdOffset);
    binding.model_key =
        read_string(bytes, key_offset, key_length,
                    limits.scene.max_model_key_bytes, "An actor model key");
    for (std::size_t value_index = 0U;
         value_index < binding.model_to_entity.values.size(); ++value_index) {
      binding.model_to_entity.values[value_index] = read_f32(
          bytes, offset + kActorTransformOffset + value_index * 4U,
          "actor model-to-entity transform");
    }
    result.actor_bindings.push_back(std::move(binding));
  }

  for (std::uint32_t index = 0U; index < counts.player_bindings; ++index) {
    const auto offset = layout.player_bindings +
                        static_cast<std::uint64_t>(index) *
                            kEntitySceneIoPlayerBindingBytesV1;
    if (read_u64(bytes, offset + kPlayerReservedOffset) != 0U) {
      fail("EntitySceneV1 player-binding reserved data is non-zero");
    }
    result.player_bindings.push_back(PlayerEntityBindingV1{
        read_u32(bytes, offset + kComponentAuthoredIdOffset),
        read_u32(bytes, offset + kPlayerSlotOffset),
    });
  }
  if (expected_key_offset != layout.total) {
    fail("EntitySceneV1 key records do not consume the declared key data");
  }

  try {
    validate_entity_scene_v1(result, limits.scene);
  } catch (const EntitySceneError &error) {
    fail("Decoded EntitySceneV1 is invalid: " + std::string(error.what()));
  }
  return result;
}

} // namespace openrc
