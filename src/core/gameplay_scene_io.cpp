#include "openrc/gameplay_scene_io.hpp"

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
    std::byte{'O'}, std::byte{'R'}, std::byte{'G'}, std::byte{'M'},
    std::byte{'P'}, std::byte{'S'}, std::byte{'C'}, std::byte{'N'},
};

constexpr std::uint64_t kFormatVersionOffset = 0x08U;
constexpr std::uint64_t kHeaderBytesOffset = 0x0cU;
constexpr std::uint64_t kTotalBytesOffset = 0x10U;
constexpr std::uint64_t kPayloadTypeOffset = 0x18U;
constexpr std::uint64_t kSchemaVersionOffset = 0x1cU;
constexpr std::uint64_t kLevelIdOffset = 0x20U;
constexpr std::uint64_t kCollectibleCountOffset = 0x24U;
constexpr std::uint64_t kTotalKeyBytesOffset = 0x28U;
constexpr std::uint64_t kCollectibleRecordBytesOffset = 0x30U;
constexpr std::uint64_t kHeaderFlagsOffset = 0x34U;
constexpr std::uint64_t kCollectibleTableOffset = 0x38U;
constexpr std::uint64_t kKeyDataOffset = 0x40U;
constexpr std::uint64_t kHeaderReservedOffset = 0x48U;

constexpr std::uint64_t kCollectibleAuthoredIdOffset = 0x00U;
constexpr std::uint64_t kCollectibleFlagsOffset = 0x04U;
constexpr std::uint64_t kCollectibleAmountOffset = 0x08U;
constexpr std::uint64_t kCollectibleKeyBytesOffset = 0x0cU;
constexpr std::uint64_t kCollectibleLocalCenterOffset = 0x10U;
constexpr std::uint64_t kCollectibleRadiusOffset = 0x1cU;
constexpr std::uint64_t kCollectibleKeyOffsetOffset = 0x20U;
constexpr std::uint64_t kCollectibleReservedOffset = 0x28U;

static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<float>::radix == 2);
static_assert(std::numeric_limits<float>::digits == 24);

[[noreturn]] void fail(const std::string &message) {
  throw GameplaySceneIoError(message);
}

void validate_limits(const GameplaySceneIoLimitsV1 &limits) {
  if (limits.max_encoded_bytes < kGameplaySceneIoHeaderBytesV1 ||
      limits.scene.max_collectibles == 0U ||
      limits.scene.max_item_key_bytes == 0U ||
      limits.scene.max_total_key_bytes == 0U) {
    fail("GameplaySceneV1 I/O limits must all be non-zero and bounded");
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
    fail(std::string("GameplaySceneV1 collectible has a non-finite ") +
         description);
  }
  if (value == 0.0F && std::signbit(value)) {
    fail(std::string("GameplaySceneV1 collectible has non-canonical signed "
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

[[nodiscard]] std::string read_string(const std::span<const std::byte> bytes,
                                      const std::uint64_t offset,
                                      const std::uint32_t length,
                                      const std::uint32_t maximum_length) {
  if (length == 0U || length > maximum_length) {
    fail("A gameplay item key has an invalid byte length");
  }
  std::string result;
  result.reserve(host_size(length, result.max_size(), "A gameplay item key"));
  const auto begin = static_cast<std::size_t>(offset);
  for (std::uint32_t index = 0U; index < length; ++index) {
    result.push_back(static_cast<char>(byte_value(bytes[begin + index])));
  }
  return result;
}

struct Counts {
  std::uint32_t collectibles = 0U;
  std::uint64_t key_bytes = 0U;
};

[[nodiscard]] Counts scene_counts(const GameplaySceneV1 &scene) {
  Counts result;
  result.collectibles =
      format_count(scene.collectibles.size(), "Gameplay collectible count");
  for (const auto &collectible : scene.collectibles) {
    result.key_bytes =
        checked_add(result.key_bytes, collectible.item_key.size(),
                    "GameplaySceneV1 key bytes");
  }
  return result;
}

struct Layout {
  std::uint64_t collectibles = 0U;
  std::uint64_t keys = 0U;
  std::uint64_t total = 0U;
};

[[nodiscard]] Layout canonical_layout(const Counts &counts) {
  Layout result;
  result.collectibles = kGameplaySceneIoHeaderBytesV1;
  result.keys = checked_add(result.collectibles,
                            checked_multiply(counts.collectibles,
                                             kGameplaySceneIoCollectibleBytesV1,
                                             "Gameplay collectible table"),
                            "Gameplay collectible table");
  result.total =
      checked_add(result.keys, counts.key_bytes, "GameplaySceneV1 key data");
  return result;
}

[[nodiscard]] GameplaySceneV1
canonical_scene(const GameplaySceneV1 &scene,
                const GameplaySceneLimitsV1 limits) {
  try {
    return canonicalize_gameplay_scene_v1(scene, limits);
  } catch (const GameplaySceneError &error) {
    fail("Cannot encode GameplaySceneV1: " + std::string(error.what()));
  }
}

void require_key_partition(const std::uint64_t declared_offset,
                           const std::uint32_t length,
                           std::uint64_t &expected_offset,
                           const std::uint64_t end) {
  if (expected_offset > end || declared_offset != expected_offset ||
      length > end - expected_offset) {
    fail(
        "A gameplay item key is not part of the exact canonical key partition");
  }
  expected_offset += length;
}

} // namespace

std::vector<std::byte>
encode_gameplay_scene_v1(const GameplaySceneV1 &scene,
                         const GameplaySceneIoLimitsV1 limits) {
  validate_limits(limits);
  const auto canonical = canonical_scene(scene, limits.scene);
  const auto counts = scene_counts(canonical);
  const auto layout = canonical_layout(counts);
  if (layout.total > limits.max_encoded_bytes) {
    fail("GameplaySceneV1 encoded bytes exceed the caller limit");
  }

  std::vector<std::byte> result(host_size(layout.total,
                                          std::vector<std::byte>{}.max_size(),
                                          "GameplaySceneV1 encoded size"),
                                std::byte{0U});
  std::copy(kMagic.begin(), kMagic.end(), result.begin());
  write_u32(result, kFormatVersionOffset, kGameplaySceneIoFormatVersionV1);
  write_u32(result, kHeaderBytesOffset, kGameplaySceneIoHeaderBytesV1);
  write_u64(result, kTotalBytesOffset, layout.total);
  write_u32(result, kPayloadTypeOffset, kGameplaySceneIoPayloadTypeV1);
  write_u32(result, kSchemaVersionOffset, canonical.schema_version);
  write_u32(result, kLevelIdOffset, canonical.level_id);
  write_u32(result, kCollectibleCountOffset, counts.collectibles);
  write_u64(result, kTotalKeyBytesOffset, counts.key_bytes);
  write_u32(result, kCollectibleRecordBytesOffset,
            kGameplaySceneIoCollectibleBytesV1);
  write_u64(result, kCollectibleTableOffset, layout.collectibles);
  write_u64(result, kKeyDataOffset, layout.keys);

  auto key_offset = layout.keys;
  for (std::size_t index = 0U; index < canonical.collectibles.size(); ++index) {
    const auto offset =
        layout.collectibles +
        static_cast<std::uint64_t>(index) * kGameplaySceneIoCollectibleBytesV1;
    const auto &collectible = canonical.collectibles[index];
    write_u32(result, offset + kCollectibleAuthoredIdOffset,
              collectible.authored_id);
    write_u32(result, offset + kCollectibleFlagsOffset, collectible.flags);
    write_u32(result, offset + kCollectibleAmountOffset, collectible.amount);
    write_u32(
        result, offset + kCollectibleKeyBytesOffset,
        format_count(collectible.item_key.size(), "Gameplay item-key length"));
    for (std::size_t axis = 0U; axis < collectible.local_center.size();
         ++axis) {
      write_f32(result, offset + kCollectibleLocalCenterOffset + axis * 4U,
                collectible.local_center[axis]);
    }
    write_f32(result, offset + kCollectibleRadiusOffset,
              collectible.collection_radius);
    write_u64(result, offset + kCollectibleKeyOffsetOffset, key_offset);
    write_string(result, key_offset, collectible.item_key);
    key_offset += collectible.item_key.size();
  }
  if (key_offset != layout.total) {
    fail("GameplaySceneV1 internal key partition is inconsistent");
  }
  return result;
}

GameplaySceneV1 decode_gameplay_scene_v1(const std::span<const std::byte> bytes,
                                         const GameplaySceneIoLimitsV1 limits) {
  validate_limits(limits);
  if (bytes.size() > limits.max_encoded_bytes) {
    fail("GameplaySceneV1 encoded bytes exceed the caller limit");
  }
  if (bytes.size() < kGameplaySceneIoHeaderBytesV1) {
    fail("GameplaySceneV1 header is truncated");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    fail("GameplaySceneV1 magic is invalid");
  }
  if (read_u32(bytes, kFormatVersionOffset) !=
      kGameplaySceneIoFormatVersionV1) {
    fail("GameplaySceneV1 format version is unknown");
  }
  if (read_u32(bytes, kPayloadTypeOffset) != kGameplaySceneIoPayloadTypeV1) {
    fail("GameplaySceneV1 payload type is unknown");
  }
  if (read_u32(bytes, kHeaderBytesOffset) != kGameplaySceneIoHeaderBytesV1 ||
      read_u32(bytes, kSchemaVersionOffset) != kGameplaySceneSchemaVersionV1 ||
      read_u32(bytes, kCollectibleRecordBytesOffset) !=
          kGameplaySceneIoCollectibleBytesV1 ||
      read_u32(bytes, kHeaderFlagsOffset) != 0U ||
      !all_zero(bytes, kHeaderReservedOffset, kGameplaySceneIoHeaderBytesV1)) {
    fail("GameplaySceneV1 header schema, record size, flags, or reserved data "
         "are invalid");
  }

  Counts counts;
  counts.collectibles = read_u32(bytes, kCollectibleCountOffset);
  counts.key_bytes = read_u64(bytes, kTotalKeyBytesOffset);
  if (counts.collectibles > limits.scene.max_collectibles ||
      counts.key_bytes > limits.scene.max_total_key_bytes) {
    fail("GameplaySceneV1 header exceeds a caller limit");
  }

  const auto layout = canonical_layout(counts);
  if (read_u64(bytes, kCollectibleTableOffset) != layout.collectibles ||
      read_u64(bytes, kKeyDataOffset) != layout.keys ||
      read_u64(bytes, kTotalBytesOffset) != layout.total ||
      layout.total != bytes.size()) {
    fail("GameplaySceneV1 table layout or exact byte size is invalid");
  }

  GameplaySceneV1 result;
  result.schema_version = read_u32(bytes, kSchemaVersionOffset);
  result.level_id = read_u32(bytes, kLevelIdOffset);
  result.collectibles.reserve(host_size(counts.collectibles,
                                        result.collectibles.max_size(),
                                        "Gameplay collectible count"));

  auto expected_key_offset = layout.keys;
  for (std::uint32_t index = 0U; index < counts.collectibles; ++index) {
    const auto offset =
        layout.collectibles +
        static_cast<std::uint64_t>(index) * kGameplaySceneIoCollectibleBytesV1;
    const auto key_length =
        read_u32(bytes, offset + kCollectibleKeyBytesOffset);
    const auto key_offset =
        read_u64(bytes, offset + kCollectibleKeyOffsetOffset);
    if (read_u64(bytes, offset + kCollectibleReservedOffset) != 0U) {
      fail("GameplaySceneV1 collectible reserved data is non-zero");
    }
    require_key_partition(key_offset, key_length, expected_key_offset,
                          layout.total);

    GameplayCollectibleV1 collectible;
    collectible.authored_id =
        read_u32(bytes, offset + kCollectibleAuthoredIdOffset);
    collectible.flags = read_u32(bytes, offset + kCollectibleFlagsOffset);
    collectible.amount = read_u32(bytes, offset + kCollectibleAmountOffset);
    collectible.item_key = read_string(bytes, key_offset, key_length,
                                       limits.scene.max_item_key_bytes);
    for (std::size_t axis = 0U; axis < collectible.local_center.size();
         ++axis) {
      collectible.local_center[axis] =
          read_f32(bytes, offset + kCollectibleLocalCenterOffset + axis * 4U,
                   "local center");
    }
    collectible.collection_radius =
        read_f32(bytes, offset + kCollectibleRadiusOffset, "collection radius");
    result.collectibles.push_back(std::move(collectible));
  }
  if (expected_key_offset != layout.total) {
    fail("GameplaySceneV1 key records do not consume the declared key data");
  }

  try {
    validate_gameplay_scene_v1(result, limits.scene);
  } catch (const GameplaySceneError &error) {
    fail("Decoded GameplaySceneV1 is invalid: " + std::string(error.what()));
  }
  return result;
}

} // namespace openrc
