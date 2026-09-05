#include "openrc/level_bootstrap.hpp"

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
    std::byte{'O'}, std::byte{'R'}, std::byte{'L'}, std::byte{'B'},
    std::byte{'O'}, std::byte{'O'}, std::byte{'T'}, std::byte{0U},
};

constexpr std::size_t kMagicOffset = 0U;
constexpr std::size_t kFormatVersionOffset = 8U;
constexpr std::size_t kHeaderBytesOffset = 12U;
constexpr std::size_t kTotalBytesOffset = 16U;
constexpr std::size_t kLevelIdOffset = 24U;
constexpr std::size_t kDefaultSpawnIdOffset = 28U;
constexpr std::size_t kDeathHeightOffset = 32U;
constexpr std::size_t kSpawnCountOffset = 40U;
constexpr std::size_t kSpawnPointBytesOffset = 44U;
constexpr std::size_t kHeaderReservedOffset = 48U;

constexpr std::size_t kSpawnIdOffset = 0U;
constexpr std::size_t kSpawnReservedOffset = 4U;
constexpr std::size_t kSpawnXOffset = 8U;
constexpr std::size_t kSpawnYOffset = 16U;
constexpr std::size_t kSpawnZOffset = 24U;
constexpr std::size_t kSpawnYawOffset = 32U;

static_assert(sizeof(double) == sizeof(std::uint64_t));
static_assert(std::numeric_limits<double>::is_iec559);
static_assert(std::numeric_limits<double>::radix == 2);
static_assert(std::numeric_limits<double>::digits == 53);

[[noreturn]] void fail(const std::string &message) {
  throw LevelBootstrapV1Error(message);
}

void validate_limits(const LevelBootstrapV1Limits limits) {
  if (limits.max_input_bytes < kLevelBootstrapHeaderBytesV1 ||
      limits.max_spawn_points == 0U) {
    fail("LevelBootstrapV1 caller limits must be non-zero and bounded");
  }
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_u32(const std::span<const std::byte> bytes,
                                     const std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t read_u64(const std::span<const std::byte> bytes,
                                     const std::size_t offset) noexcept {
  std::uint64_t result = 0U;
  for (std::size_t index = 0U; index < sizeof(result); ++index) {
    result |= static_cast<std::uint64_t>(byte_value(bytes[offset + index]))
              << (index * 8U);
  }
  return result;
}

void write_u32(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint32_t value) noexcept {
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void write_u64(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint64_t value) noexcept {
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

[[nodiscard]] double canonical_number(const double value,
                                      const char *const description) {
  if (!std::isfinite(value)) {
    fail(std::string("LevelBootstrapV1 has a non-finite ") + description);
  }
  return value == 0.0 ? 0.0 : value;
}

void write_f64(std::vector<std::byte> &bytes, const std::size_t offset,
               const double value, const char *const description) {
  write_u64(bytes, offset,
            std::bit_cast<std::uint64_t>(canonical_number(value, description)));
}

[[nodiscard]] double read_f64(const std::span<const std::byte> bytes,
                              const std::size_t offset,
                              const char *const description) {
  const auto bits = read_u64(bytes, offset);
  const auto value = std::bit_cast<double>(bits);
  if (!std::isfinite(value)) {
    fail(std::string("LevelBootstrapV1 has a non-finite ") + description);
  }
  if (value == 0.0 && std::signbit(value)) {
    fail(std::string("LevelBootstrapV1 has non-canonical signed zero in ") +
         description);
  }
  return value;
}

[[nodiscard]] bool all_zero(const std::span<const std::byte> bytes,
                            const std::size_t begin,
                            const std::size_t end) noexcept {
  return std::all_of(
      bytes.begin() + static_cast<std::ptrdiff_t>(begin),
      bytes.begin() + static_cast<std::ptrdiff_t>(end),
      [](const std::byte value) { return value == std::byte{0U}; });
}

[[nodiscard]] std::uint64_t encoded_size(const std::uint32_t spawn_count) {
  const auto records = static_cast<std::uint64_t>(spawn_count) *
                       kLevelBootstrapSpawnPointBytesV1;
  if (records > std::numeric_limits<std::uint64_t>::max() -
                    kLevelBootstrapHeaderBytesV1) {
    fail("LevelBootstrapV1 encoded size overflows uint64_t");
  }
  return kLevelBootstrapHeaderBytesV1 + records;
}

[[nodiscard]] std::size_t host_size(const std::uint64_t size) {
  if (size > std::numeric_limits<std::size_t>::max()) {
    fail("LevelBootstrapV1 exceeds host size_t");
  }
  return static_cast<std::size_t>(size);
}

[[nodiscard]] bool same_spawn_id(const LevelSpawnPointV1 &left,
                                 const LevelSpawnPointV1 &right) noexcept {
  return left.id == right.id;
}

void validate_spawn_values(const LevelSpawnPointV1 &spawn) {
  static_cast<void>(canonical_number(spawn.feet_position.x, "spawn X"));
  static_cast<void>(canonical_number(spawn.feet_position.y, "spawn Y"));
  static_cast<void>(canonical_number(spawn.feet_position.z, "spawn Z"));
  static_cast<void>(canonical_number(spawn.facing_yaw_radians, "spawn yaw"));
}

} // namespace

std::vector<std::byte>
encode_level_bootstrap_v1(const LevelBootstrapV1 &bootstrap,
                          const LevelBootstrapV1Limits limits) {
  validate_limits(limits);
  const auto death_height =
      canonical_number(bootstrap.death_height_world, "death height");
  if (bootstrap.spawn_points.empty() ||
      bootstrap.spawn_points.size() > limits.max_spawn_points ||
      bootstrap.spawn_points.size() >
          std::numeric_limits<std::uint32_t>::max()) {
    fail("LevelBootstrapV1 has an invalid spawn-point count");
  }

  auto spawns = bootstrap.spawn_points;
  std::sort(spawns.begin(), spawns.end(),
            [](const LevelSpawnPointV1 &left, const LevelSpawnPointV1 &right) {
              return left.id < right.id;
            });
  if (std::adjacent_find(spawns.begin(), spawns.end(), same_spawn_id) !=
      spawns.end()) {
    fail("LevelBootstrapV1 has duplicate spawn IDs");
  }
  for (const auto &spawn : spawns) {
    validate_spawn_values(spawn);
    if (!(spawn.feet_position.z > death_height)) {
      fail("LevelBootstrapV1 has a spawn at or below its death height");
    }
  }
  const auto default_spawn =
      std::lower_bound(spawns.begin(), spawns.end(), bootstrap.default_spawn_id,
                       [](const LevelSpawnPointV1 &spawn,
                          const std::uint32_t id) { return spawn.id < id; });
  if (default_spawn == spawns.end() ||
      default_spawn->id != bootstrap.default_spawn_id) {
    fail("LevelBootstrapV1 default spawn ID does not exist");
  }

  const auto count = static_cast<std::uint32_t>(spawns.size());
  const auto total_bytes = encoded_size(count);
  if (total_bytes > limits.max_input_bytes) {
    fail("LevelBootstrapV1 exceeds the caller's input-byte limit");
  }
  std::vector<std::byte> result(host_size(total_bytes), std::byte{0U});
  std::copy(kMagic.begin(), kMagic.end(), result.begin() + kMagicOffset);
  write_u32(result, kFormatVersionOffset, kLevelBootstrapFormatVersionV1);
  write_u32(result, kHeaderBytesOffset, kLevelBootstrapHeaderBytesV1);
  write_u64(result, kTotalBytesOffset, total_bytes);
  write_u32(result, kLevelIdOffset, bootstrap.level_id);
  write_u32(result, kDefaultSpawnIdOffset, bootstrap.default_spawn_id);
  write_f64(result, kDeathHeightOffset, bootstrap.death_height_world,
            "death height");
  write_u32(result, kSpawnCountOffset, count);
  write_u32(result, kSpawnPointBytesOffset, kLevelBootstrapSpawnPointBytesV1);

  for (std::size_t index = 0U; index < spawns.size(); ++index) {
    const auto offset =
        kLevelBootstrapHeaderBytesV1 + index * kLevelBootstrapSpawnPointBytesV1;
    const auto &spawn = spawns[index];
    write_u32(result, offset + kSpawnIdOffset, spawn.id);
    write_f64(result, offset + kSpawnXOffset, spawn.feet_position.x, "spawn X");
    write_f64(result, offset + kSpawnYOffset, spawn.feet_position.y, "spawn Y");
    write_f64(result, offset + kSpawnZOffset, spawn.feet_position.z, "spawn Z");
    write_f64(result, offset + kSpawnYawOffset, spawn.facing_yaw_radians,
              "spawn yaw");
  }
  return result;
}

LevelBootstrapV1
parse_level_bootstrap_v1(const std::span<const std::byte> bytes,
                         const LevelBootstrapV1Limits limits) {
  validate_limits(limits);
  if (bytes.size() > limits.max_input_bytes) {
    fail("LevelBootstrapV1 exceeds the caller's input-byte limit");
  }
  if (bytes.size() < kLevelBootstrapHeaderBytesV1) {
    fail("LevelBootstrapV1 header is truncated");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin() + kMagicOffset)) {
    fail("LevelBootstrapV1 magic is invalid");
  }
  if (read_u32(bytes, kFormatVersionOffset) != kLevelBootstrapFormatVersionV1 ||
      read_u32(bytes, kHeaderBytesOffset) != kLevelBootstrapHeaderBytesV1 ||
      read_u32(bytes, kSpawnPointBytesOffset) !=
          kLevelBootstrapSpawnPointBytesV1 ||
      !all_zero(bytes, kHeaderReservedOffset, kLevelBootstrapHeaderBytesV1)) {
    fail("LevelBootstrapV1 header schema or reserved bytes are invalid");
  }
  const auto total_bytes = read_u64(bytes, kTotalBytesOffset);
  if (total_bytes != bytes.size()) {
    fail("LevelBootstrapV1 total byte size is invalid");
  }
  const auto count = read_u32(bytes, kSpawnCountOffset);
  if (count == 0U || count > limits.max_spawn_points ||
      encoded_size(count) != total_bytes) {
    fail("LevelBootstrapV1 spawn count or exact size is invalid");
  }

  LevelBootstrapV1 result;
  result.level_id = read_u32(bytes, kLevelIdOffset);
  result.default_spawn_id = read_u32(bytes, kDefaultSpawnIdOffset);
  result.death_height_world =
      read_f64(bytes, kDeathHeightOffset, "death height");
  if (static_cast<std::size_t>(count) > result.spawn_points.max_size()) {
    fail("LevelBootstrapV1 spawn count exceeds vector capacity");
  }
  result.spawn_points.reserve(count);
  for (std::uint32_t index = 0U; index < count; ++index) {
    const auto offset =
        kLevelBootstrapHeaderBytesV1 +
        static_cast<std::size_t>(index) * kLevelBootstrapSpawnPointBytesV1;
    if (read_u32(bytes, offset + kSpawnReservedOffset) != 0U) {
      fail("LevelBootstrapV1 spawn reserved bytes are non-zero");
    }
    LevelSpawnPointV1 spawn;
    spawn.id = read_u32(bytes, offset + kSpawnIdOffset);
    if (!result.spawn_points.empty() &&
        result.spawn_points.back().id >= spawn.id) {
      fail("LevelBootstrapV1 spawn IDs are duplicate or out of order");
    }
    spawn.feet_position.x = read_f64(bytes, offset + kSpawnXOffset, "spawn X");
    spawn.feet_position.y = read_f64(bytes, offset + kSpawnYOffset, "spawn Y");
    spawn.feet_position.z = read_f64(bytes, offset + kSpawnZOffset, "spawn Z");
    spawn.facing_yaw_radians =
        read_f64(bytes, offset + kSpawnYawOffset, "spawn yaw");
    if (!(spawn.feet_position.z > result.death_height_world)) {
      fail("LevelBootstrapV1 has a spawn at or below its death height");
    }
    result.spawn_points.push_back(spawn);
  }
  if (find_level_spawn_point_v1(result, result.default_spawn_id) == nullptr) {
    fail("LevelBootstrapV1 default spawn ID does not exist");
  }
  return result;
}

const LevelSpawnPointV1 *
find_level_spawn_point_v1(const LevelBootstrapV1 &bootstrap,
                          const std::uint32_t spawn_id) noexcept {
  const auto found = std::lower_bound(
      bootstrap.spawn_points.begin(), bootstrap.spawn_points.end(), spawn_id,
      [](const LevelSpawnPointV1 &spawn, const std::uint32_t id) {
        return spawn.id < id;
      });
  return found != bootstrap.spawn_points.end() && found->id == spawn_id
             ? &*found
             : nullptr;
}

} // namespace openrc
