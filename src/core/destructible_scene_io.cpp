#include "openrc/destructible_scene_io.hpp"

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
    std::byte{'O'}, std::byte{'R'}, std::byte{'D'}, std::byte{'S'},
    std::byte{'T'}, std::byte{'R'}, std::byte{'C'}, std::byte{'1'},
};

constexpr std::uint64_t kFormatVersionOffset = 0x08U;
constexpr std::uint64_t kHeaderBytesOffset = 0x0cU;
constexpr std::uint64_t kTotalBytesOffset = 0x10U;
constexpr std::uint64_t kPayloadTypeOffset = 0x18U;
constexpr std::uint64_t kSchemaVersionOffset = 0x1cU;
constexpr std::uint64_t kLevelIdOffset = 0x20U;
constexpr std::uint64_t kDestructibleCountOffset = 0x24U;
constexpr std::uint64_t kTotalDropCountOffset = 0x28U;
constexpr std::uint64_t kHeaderFlagsOffset = 0x2cU;
constexpr std::uint64_t kTotalKeyBytesOffset = 0x30U;
constexpr std::uint64_t kDestructibleRecordBytesOffset = 0x38U;
constexpr std::uint64_t kDropRecordBytesOffset = 0x3cU;
constexpr std::uint64_t kDestructibleTableOffset = 0x40U;
constexpr std::uint64_t kDropTableOffset = 0x48U;
constexpr std::uint64_t kKeyDataOffset = 0x50U;
constexpr std::uint64_t kHeaderReservedOffset = 0x58U;

constexpr std::uint64_t kDestructibleAuthoredIdOffset = 0x00U;
constexpr std::uint64_t kDestructibleFlagsOffset = 0x04U;
constexpr std::uint64_t kDestructibleMaxHealthOffset = 0x08U;
constexpr std::uint64_t kDestructibleDamageChannelsOffset = 0x0cU;
constexpr std::uint64_t kDestructibleLocalHitCenterOffset = 0x10U;
constexpr std::uint64_t kDestructibleHitRadiusOffset = 0x1cU;
constexpr std::uint64_t kDestructibleFirstDropOffset = 0x20U;
constexpr std::uint64_t kDestructibleDropCountOffset = 0x24U;
constexpr std::uint64_t kDestructibleReservedOffset = 0x28U;

constexpr std::uint64_t kDropKeyBytesOffset = 0x00U;
constexpr std::uint64_t kDropAmountOffset = 0x04U;
constexpr std::uint64_t kDropKeyOffsetOffset = 0x08U;
constexpr std::uint64_t kDropFlagsOffset = 0x10U;
constexpr std::uint64_t kDropReserved32Offset = 0x14U;
constexpr std::uint64_t kDropReserved64Offset = 0x18U;

static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<float>::radix == 2);
static_assert(std::numeric_limits<float>::digits == 24);

[[noreturn]] void fail(const std::string &message) {
  throw DestructibleSceneIoError(message);
}

void validate_limits(const DestructibleSceneIoLimitsV1 &limits) {
  if (limits.max_encoded_bytes < kDestructibleSceneIoHeaderBytesV1 ||
      limits.scene.max_destructibles == 0U ||
      limits.scene.max_total_drops == 0U ||
      limits.scene.max_drops_per_destructible == 0U ||
      limits.scene.max_item_key_bytes == 0U ||
      limits.scene.max_total_key_bytes == 0U ||
      limits.scene.max_health == 0U ||
      limits.scene.max_drop_amount == 0U ||
      !std::isfinite(limits.scene.max_absolute_local_hit_center) ||
      !(limits.scene.max_absolute_local_hit_center > 0.0F) ||
      !std::isfinite(limits.scene.max_hit_radius) ||
      !(limits.scene.max_hit_radius > 0.0F)) {
    fail("DestructibleSceneV1 I/O limits must all be non-zero and bounded");
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

[[nodiscard]] std::uint32_t format_count(const std::uint64_t value,
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
    fail(std::string("DestructibleSceneV1 definition has a non-finite ") +
         description);
  }
  if (value == 0.0F && std::signbit(value)) {
    fail(std::string("DestructibleSceneV1 definition has non-canonical signed "
                     "zero in its ") +
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
  for (auto offset = begin; offset < end; ++offset) {
    if (bytes[static_cast<std::size_t>(offset)] != std::byte{0U}) {
      return false;
    }
  }
  return true;
}

void write_string(std::vector<std::byte> &bytes, const std::uint64_t offset,
                  const std::string &value) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < value.size(); ++index) {
    bytes[begin + index] =
        static_cast<std::byte>(static_cast<unsigned char>(value[index]));
  }
}

[[nodiscard]] std::string read_string(const std::span<const std::byte> bytes,
                                      const std::uint64_t offset,
                                      const std::uint32_t length,
                                      const std::uint32_t maximum_length) {
  if (length == 0U || length > maximum_length) {
    fail("A destructible drop item key has an invalid byte length");
  }
  std::string result;
  result.reserve(
      host_size(length, result.max_size(), "A destructible drop item key"));
  const auto begin = static_cast<std::size_t>(offset);
  for (std::uint32_t index = 0U; index < length; ++index) {
    result.push_back(static_cast<char>(byte_value(bytes[begin + index])));
  }
  return result;
}

struct Counts {
  std::uint32_t destructibles = 0U;
  std::uint32_t drops = 0U;
  std::uint64_t key_bytes = 0U;
};

[[nodiscard]] Counts scene_counts(const DestructibleSceneV1 &scene) {
  Counts result;
  result.destructibles =
      format_count(scene.destructibles.size(), "Destructible count");
  std::uint64_t total_drops = 0U;
  for (const auto &destructible : scene.destructibles) {
    total_drops = checked_add(total_drops, destructible.drops.size(),
                              "DestructibleSceneV1 drop count");
    for (const auto &drop : destructible.drops) {
      result.key_bytes =
          checked_add(result.key_bytes, drop.item_key.size(),
                      "DestructibleSceneV1 key bytes");
    }
  }
  result.drops = format_count(total_drops, "Destructible drop count");
  return result;
}

struct Layout {
  std::uint64_t destructibles = 0U;
  std::uint64_t drops = 0U;
  std::uint64_t keys = 0U;
  std::uint64_t total = 0U;
};

[[nodiscard]] Layout canonical_layout(const Counts &counts) {
  Layout result;
  result.destructibles = kDestructibleSceneIoHeaderBytesV1;
  result.drops = checked_add(
      result.destructibles,
      checked_multiply(counts.destructibles,
                       kDestructibleSceneIoDestructibleBytesV1,
                       "DestructibleSceneV1 definition table"),
      "DestructibleSceneV1 definition table");
  result.keys = checked_add(
      result.drops,
      checked_multiply(counts.drops, kDestructibleSceneIoDropBytesV1,
                       "DestructibleSceneV1 drop table"),
      "DestructibleSceneV1 drop table");
  result.total = checked_add(result.keys, counts.key_bytes,
                             "DestructibleSceneV1 key data");
  return result;
}

[[nodiscard]] DestructibleSceneV1
canonical_scene(const DestructibleSceneV1 &scene,
                const DestructibleSceneLimitsV1 limits) {
  try {
    return canonicalize_destructible_scene_v1(scene, limits);
  } catch (const DestructibleSceneError &error) {
    fail("Cannot encode DestructibleSceneV1: " + std::string(error.what()));
  }
}

void require_key_partition(const std::uint64_t declared_offset,
                           const std::uint32_t length,
                           std::uint64_t &expected_offset,
                           const std::uint64_t end) {
  if (expected_offset > end || declared_offset != expected_offset ||
      length > end - expected_offset) {
    fail("A destructible drop item key is not part of the exact canonical key "
         "partition");
  }
  expected_offset += length;
}

} // namespace

std::vector<std::byte>
encode_destructible_scene_v1(const DestructibleSceneV1 &scene,
                             const DestructibleSceneIoLimitsV1 limits) {
  validate_limits(limits);
  const auto canonical = canonical_scene(scene, limits.scene);
  const auto counts = scene_counts(canonical);
  const auto layout = canonical_layout(counts);
  if (layout.total > limits.max_encoded_bytes) {
    fail("DestructibleSceneV1 encoded bytes exceed the caller limit");
  }

  std::vector<std::byte> result(
      host_size(layout.total, std::vector<std::byte>{}.max_size(),
                "DestructibleSceneV1 encoded size"),
      std::byte{0U});
  std::copy(kMagic.begin(), kMagic.end(), result.begin());
  write_u32(result, kFormatVersionOffset,
            kDestructibleSceneIoFormatVersionV1);
  write_u32(result, kHeaderBytesOffset, kDestructibleSceneIoHeaderBytesV1);
  write_u64(result, kTotalBytesOffset, layout.total);
  write_u32(result, kPayloadTypeOffset,
            kDestructibleSceneIoPayloadTypeV1);
  write_u32(result, kSchemaVersionOffset, canonical.schema_version);
  write_u32(result, kLevelIdOffset, canonical.level_id);
  write_u32(result, kDestructibleCountOffset, counts.destructibles);
  write_u32(result, kTotalDropCountOffset, counts.drops);
  write_u64(result, kTotalKeyBytesOffset, counts.key_bytes);
  write_u32(result, kDestructibleRecordBytesOffset,
            kDestructibleSceneIoDestructibleBytesV1);
  write_u32(result, kDropRecordBytesOffset,
            kDestructibleSceneIoDropBytesV1);
  write_u64(result, kDestructibleTableOffset, layout.destructibles);
  write_u64(result, kDropTableOffset, layout.drops);
  write_u64(result, kKeyDataOffset, layout.keys);

  std::uint32_t first_drop = 0U;
  auto key_offset = layout.keys;
  for (std::size_t index = 0U; index < canonical.destructibles.size();
       ++index) {
    const auto definition_offset =
        layout.destructibles + static_cast<std::uint64_t>(index) *
                                   kDestructibleSceneIoDestructibleBytesV1;
    const auto &destructible = canonical.destructibles[index];
    const auto drop_count =
        format_count(destructible.drops.size(), "Per-destructible drop count");
    write_u32(result, definition_offset + kDestructibleAuthoredIdOffset,
              destructible.authored_id);
    write_u32(result, definition_offset + kDestructibleFlagsOffset,
              destructible.flags);
    write_u32(result, definition_offset + kDestructibleMaxHealthOffset,
              destructible.max_health);
    write_u32(result, definition_offset + kDestructibleDamageChannelsOffset,
              destructible.accepted_damage_channels);
    for (std::size_t axis = 0U; axis < destructible.local_hit_center.size();
         ++axis) {
      write_f32(result,
                definition_offset + kDestructibleLocalHitCenterOffset +
                    axis * 4U,
                destructible.local_hit_center[axis]);
    }
    write_f32(result, definition_offset + kDestructibleHitRadiusOffset,
              destructible.hit_radius);
    write_u32(result, definition_offset + kDestructibleFirstDropOffset,
              first_drop);
    write_u32(result, definition_offset + kDestructibleDropCountOffset,
              drop_count);

    for (std::size_t local = 0U; local < destructible.drops.size(); ++local) {
      const auto drop_index =
          static_cast<std::uint64_t>(first_drop) +
          static_cast<std::uint64_t>(local);
      const auto drop_offset =
          layout.drops + drop_index * kDestructibleSceneIoDropBytesV1;
      const auto &drop = destructible.drops[local];
      write_u32(result, drop_offset + kDropFlagsOffset, drop.flags);
      write_u32(result, drop_offset + kDropAmountOffset, drop.amount);
      write_u32(result, drop_offset + kDropKeyBytesOffset,
                format_count(drop.item_key.size(), "Drop item-key length"));
      write_u64(result, drop_offset + kDropKeyOffsetOffset, key_offset);
      write_string(result, key_offset, drop.item_key);
      key_offset = checked_add(key_offset, drop.item_key.size(),
                               "DestructibleSceneV1 key partition");
    }
    first_drop = format_count(
        static_cast<std::uint64_t>(first_drop) + drop_count,
        "DestructibleSceneV1 flattened drop count");
  }
  if (first_drop != counts.drops || key_offset != layout.total) {
    fail("DestructibleSceneV1 internal table partition is inconsistent");
  }
  return result;
}

DestructibleSceneV1 decode_destructible_scene_v1(
    const std::span<const std::byte> bytes,
    const DestructibleSceneIoLimitsV1 limits) {
  validate_limits(limits);
  if (bytes.size() > limits.max_encoded_bytes) {
    fail("DestructibleSceneV1 encoded bytes exceed the caller limit");
  }
  if (bytes.size() < kDestructibleSceneIoHeaderBytesV1) {
    fail("DestructibleSceneV1 header is truncated");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    fail("DestructibleSceneV1 magic is invalid");
  }
  if (read_u32(bytes, kFormatVersionOffset) !=
      kDestructibleSceneIoFormatVersionV1) {
    fail("DestructibleSceneV1 format version is unknown");
  }
  if (read_u32(bytes, kPayloadTypeOffset) !=
      kDestructibleSceneIoPayloadTypeV1) {
    fail("DestructibleSceneV1 payload type is unknown");
  }
  if (read_u32(bytes, kHeaderBytesOffset) !=
          kDestructibleSceneIoHeaderBytesV1 ||
      read_u32(bytes, kSchemaVersionOffset) !=
          kDestructibleSceneSchemaVersionV1 ||
      read_u32(bytes, kDestructibleRecordBytesOffset) !=
          kDestructibleSceneIoDestructibleBytesV1 ||
      read_u32(bytes, kDropRecordBytesOffset) !=
          kDestructibleSceneIoDropBytesV1 ||
      read_u32(bytes, kHeaderFlagsOffset) != 0U ||
      !all_zero(bytes, kHeaderReservedOffset,
                kDestructibleSceneIoHeaderBytesV1)) {
    fail("DestructibleSceneV1 header schema, record sizes, flags, or reserved "
         "data are invalid");
  }

  Counts counts;
  counts.destructibles = read_u32(bytes, kDestructibleCountOffset);
  counts.drops = read_u32(bytes, kTotalDropCountOffset);
  counts.key_bytes = read_u64(bytes, kTotalKeyBytesOffset);
  if (counts.destructibles > limits.scene.max_destructibles ||
      counts.drops > limits.scene.max_total_drops ||
      counts.key_bytes > limits.scene.max_total_key_bytes) {
    fail("DestructibleSceneV1 header exceeds a caller limit");
  }

  const auto layout = canonical_layout(counts);
  if (read_u64(bytes, kDestructibleTableOffset) != layout.destructibles ||
      read_u64(bytes, kDropTableOffset) != layout.drops ||
      read_u64(bytes, kKeyDataOffset) != layout.keys ||
      read_u64(bytes, kTotalBytesOffset) != layout.total ||
      layout.total != bytes.size()) {
    fail("DestructibleSceneV1 table layout or exact byte size is invalid");
  }

  DestructibleSceneV1 result;
  result.schema_version = read_u32(bytes, kSchemaVersionOffset);
  result.level_id = read_u32(bytes, kLevelIdOffset);
  result.destructibles.reserve(host_size(
      counts.destructibles, result.destructibles.max_size(),
      "Destructible definition count"));

  std::uint64_t expected_first_drop = 0U;
  auto expected_key_offset = layout.keys;
  for (std::uint32_t index = 0U; index < counts.destructibles; ++index) {
    const auto definition_offset =
        layout.destructibles + static_cast<std::uint64_t>(index) *
                                   kDestructibleSceneIoDestructibleBytesV1;
    if (read_u64(bytes, definition_offset + kDestructibleReservedOffset) !=
        0U) {
      fail("DestructibleSceneV1 definition reserved data is non-zero");
    }
    const auto first_drop =
        read_u32(bytes, definition_offset + kDestructibleFirstDropOffset);
    const auto drop_count =
        read_u32(bytes, definition_offset + kDestructibleDropCountOffset);
    if (first_drop != expected_first_drop ||
        drop_count > limits.scene.max_drops_per_destructible ||
        expected_first_drop > counts.drops ||
        drop_count > counts.drops - expected_first_drop) {
      fail("DestructibleSceneV1 drop ranges are not a complete canonical "
           "partition");
    }

    DestructibleDefinitionV1 destructible;
    destructible.authored_id =
        read_u32(bytes, definition_offset + kDestructibleAuthoredIdOffset);
    destructible.flags =
        read_u32(bytes, definition_offset + kDestructibleFlagsOffset);
    destructible.max_health =
        read_u32(bytes, definition_offset + kDestructibleMaxHealthOffset);
    destructible.accepted_damage_channels = read_u32(
        bytes, definition_offset + kDestructibleDamageChannelsOffset);
    for (std::size_t axis = 0U; axis < destructible.local_hit_center.size();
         ++axis) {
      destructible.local_hit_center[axis] = read_f32(
          bytes,
          definition_offset + kDestructibleLocalHitCenterOffset + axis * 4U,
          "local hit center");
    }
    destructible.hit_radius = read_f32(
        bytes, definition_offset + kDestructibleHitRadiusOffset,
        "hit radius");
    destructible.drops.reserve(host_size(
        drop_count, destructible.drops.max_size(),
        "Per-destructible drop count"));

    for (std::uint32_t local = 0U; local < drop_count; ++local) {
      const auto drop_index = expected_first_drop + local;
      const auto drop_offset =
          layout.drops + drop_index * kDestructibleSceneIoDropBytesV1;
      if (read_u32(bytes, drop_offset + kDropReserved32Offset) != 0U ||
          read_u64(bytes, drop_offset + kDropReserved64Offset) != 0U) {
        fail("DestructibleSceneV1 drop reserved data is non-zero");
      }
      const auto key_length =
          read_u32(bytes, drop_offset + kDropKeyBytesOffset);
      const auto key_offset =
          read_u64(bytes, drop_offset + kDropKeyOffsetOffset);
      require_key_partition(key_offset, key_length, expected_key_offset,
                            layout.total);

      DestructibleDropV1 drop;
      drop.flags = read_u32(bytes, drop_offset + kDropFlagsOffset);
      drop.amount = read_u32(bytes, drop_offset + kDropAmountOffset);
      drop.item_key = read_string(bytes, key_offset, key_length,
                                  limits.scene.max_item_key_bytes);
      destructible.drops.push_back(std::move(drop));
    }
    expected_first_drop = checked_add(expected_first_drop, drop_count,
                                      "DestructibleSceneV1 drop partition");
    result.destructibles.push_back(std::move(destructible));
  }
  if (expected_first_drop != counts.drops ||
      expected_key_offset != layout.total) {
    fail("DestructibleSceneV1 records do not consume their declared table "
         "partitions");
  }

  try {
    validate_destructible_scene_v1(result, limits.scene);
  } catch (const DestructibleSceneError &error) {
    fail("Decoded DestructibleSceneV1 is invalid: " +
         std::string(error.what()));
  }
  return result;
}

} // namespace openrc
