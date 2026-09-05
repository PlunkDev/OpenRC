#include "openrc/level_bootstrap.hpp"
#include "openrc/rac_level_bootstrap_compile.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
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

constexpr openrc::LevelBootstrapV1Limits kLimits{
    16U * 1024U,
    128U,
};

constexpr std::size_t kFormatVersionOffset = 8U;
constexpr std::size_t kHeaderBytesOffset = 12U;
constexpr std::size_t kTotalBytesOffset = 16U;
constexpr std::size_t kDefaultSpawnIdOffset = 28U;
constexpr std::size_t kDeathHeightOffset = 32U;
constexpr std::size_t kSpawnCountOffset = 40U;
constexpr std::size_t kSpawnPointBytesOffset = 44U;
constexpr std::size_t kHeaderReservedOffset = 48U;
constexpr std::size_t kFirstSpawnOffset = openrc::kLevelBootstrapHeaderBytesV1;
constexpr std::size_t kSecondSpawnOffset =
    kFirstSpawnOffset + openrc::kLevelBootstrapSpawnPointBytesV1;
constexpr std::size_t kSpawnReservedOffset = 4U;
constexpr std::size_t kSpawnXOffset = 8U;
constexpr std::size_t kSpawnYawOffset = 32U;

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Function>
void expect_bootstrap_rejected(Function &&function,
                               const std::string &message) {
  try {
    std::invoke(std::forward<Function>(function));
  } catch (const openrc::LevelBootstrapV1Error &) {
    return;
  }
  throw std::runtime_error(message);
}

template <typename Function>
void expect_adapter_rejected(Function &&function, const std::string &message) {
  try {
    std::invoke(std::forward<Function>(function));
  } catch (const openrc::RacLevelBootstrapCompileError &) {
    return;
  }
  throw std::runtime_error(message);
}

void write_u32(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint32_t value) {
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes.at(offset + index) =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

void write_u64(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint64_t value) {
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes.at(offset + index) =
        static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
  }
}

[[nodiscard]] std::uint32_t read_u32(const std::span<const std::byte> bytes,
                                     const std::size_t offset) {
  std::uint32_t result = 0U;
  for (std::size_t index = 0U; index < sizeof(result); ++index) {
    result |= std::to_integer<std::uint32_t>(bytes[offset + index])
              << (index * 8U);
  }
  return result;
}

[[nodiscard]] std::uint64_t read_u64(const std::span<const std::byte> bytes,
                                     const std::size_t offset) {
  std::uint64_t result = 0U;
  for (std::size_t index = 0U; index < sizeof(result); ++index) {
    result |= std::to_integer<std::uint64_t>(bytes[offset + index])
              << (index * 8U);
  }
  return result;
}

[[nodiscard]] openrc::LevelBootstrapV1 make_bootstrap() {
  openrc::LevelBootstrapV1 result;
  result.level_id = 37U;
  result.death_height_world = -2048.5;
  result.default_spawn_id = 4U;
  result.spawn_points = {
      {9U, {90.0, 91.0, 92.0}, 0.9},
      {0U, {-0.0, 1.0, 2.0}, -0.0},
      {4U, {40.0, -0.0, 42.0}, -1.25},
  };
  return result;
}

void test_round_trip_sorting_identity_and_positive_zero() {
  const auto source = make_bootstrap();
  const auto encoded = openrc::encode_level_bootstrap_v1(source, kLimits);
  expect(encoded.size() == openrc::kLevelBootstrapHeaderBytesV1 +
                               3U * openrc::kLevelBootstrapSpawnPointBytesV1,
         "LevelBootstrapV1 encoded size is wrong");
  expect(read_u32(encoded, kFirstSpawnOffset) == 0U &&
             read_u32(encoded, kSecondSpawnOffset) == 4U &&
             read_u32(encoded, kSecondSpawnOffset +
                                   openrc::kLevelBootstrapSpawnPointBytesV1) ==
                 9U,
         "LevelBootstrapV1 writer did not sort spawn IDs");
  expect(read_u64(encoded, kFirstSpawnOffset + kSpawnXOffset) == 0U &&
             read_u64(encoded, kFirstSpawnOffset + kSpawnYawOffset) == 0U,
         "LevelBootstrapV1 writer did not canonicalize signed zero");

  const auto parsed = openrc::parse_level_bootstrap_v1(encoded, kLimits);
  expect(parsed.level_id == 37U && parsed.death_height_world == -2048.5 &&
             parsed.default_spawn_id == 4U &&
             parsed.spawn_points.size() == 3U &&
             parsed.spawn_points[0U].id == 0U &&
             parsed.spawn_points[1U].id == 4U &&
             parsed.spawn_points[2U].id == 9U &&
             !std::signbit(parsed.spawn_points[0U].feet_position.x) &&
             !std::signbit(parsed.spawn_points[0U].facing_yaw_radians),
         "LevelBootstrapV1 round trip lost neutral data");
  expect(openrc::encode_level_bootstrap_v1(parsed, kLimits) == encoded,
         "LevelBootstrapV1 canonical encoding is not deterministic");

  const auto *const default_spawn =
      openrc::find_level_spawn_point_v1(parsed, 4U);
  expect(default_spawn != nullptr && default_spawn->feet_position.x == 40.0 &&
             openrc::find_level_spawn_point_v1(parsed, 3U) == nullptr,
         "LevelBootstrapV1 spawn lookup is wrong");
  expect(openrc::kLevelBootstrapResourceIdV1 ==
                 std::string_view("world/bootstrap") &&
             openrc::kLevelBootstrapResourceTypeIdV1 ==
                 std::string_view("openrc.level-bootstrap") &&
             openrc::kLevelBootstrapResourceSchemaVersionV1 == 1U,
         "LevelBootstrapV1 LevelPackage resource identity is wrong");
}

template <typename Mutation>
void expect_mutated_parse_rejected(const std::vector<std::byte> &valid,
                                   Mutation &&mutation,
                                   const std::string &message) {
  auto malformed = valid;
  std::invoke(std::forward<Mutation>(mutation), malformed);
  expect_bootstrap_rejected(
      [&] {
        static_cast<void>(openrc::parse_level_bootstrap_v1(malformed, kLimits));
      },
      message);
}

void test_envelope_reserved_and_order_rejections() {
  const auto valid =
      openrc::encode_level_bootstrap_v1(make_bootstrap(), kLimits);
  expect_mutated_parse_rejected(
      valid, [](auto &bytes) { bytes[0U] ^= std::byte{1U}; },
      "LevelBootstrapV1 accepted bad magic");
  expect_mutated_parse_rejected(
      valid, [](auto &bytes) { write_u32(bytes, kFormatVersionOffset, 2U); },
      "LevelBootstrapV1 accepted an unknown version");
  expect_mutated_parse_rejected(
      valid, [](auto &bytes) { write_u32(bytes, kHeaderBytesOffset, 60U); },
      "LevelBootstrapV1 accepted a wrong header width");
  expect_mutated_parse_rejected(
      valid,
      [](auto &bytes) {
        write_u64(bytes, kTotalBytesOffset, bytes.size() - 1U);
      },
      "LevelBootstrapV1 accepted a false total size");
  expect_mutated_parse_rejected(
      valid, [](auto &bytes) { write_u32(bytes, kSpawnPointBytesOffset, 48U); },
      "LevelBootstrapV1 accepted a wrong record width");
  expect_mutated_parse_rejected(
      valid, [](auto &bytes) { bytes[kHeaderReservedOffset] = std::byte{1U}; },
      "LevelBootstrapV1 accepted non-zero header reserved bytes");
  expect_mutated_parse_rejected(
      valid,
      [](auto &bytes) {
        bytes[kFirstSpawnOffset + kSpawnReservedOffset] = std::byte{1U};
      },
      "LevelBootstrapV1 accepted non-zero record reserved bytes");
  expect_mutated_parse_rejected(
      valid, [](auto &bytes) { bytes.pop_back(); },
      "LevelBootstrapV1 accepted truncation");
  expect_mutated_parse_rejected(
      valid, [](auto &bytes) { bytes.push_back(std::byte{0U}); },
      "LevelBootstrapV1 accepted trailing data");
  expect_mutated_parse_rejected(
      valid, [](auto &bytes) { write_u32(bytes, kSpawnCountOffset, 2U); },
      "LevelBootstrapV1 accepted count/size disagreement");
  expect_mutated_parse_rejected(
      valid, [](auto &bytes) { write_u32(bytes, kSecondSpawnOffset, 0U); },
      "LevelBootstrapV1 accepted duplicate spawn IDs");
  expect_mutated_parse_rejected(
      valid,
      [](auto &bytes) {
        write_u32(bytes, kFirstSpawnOffset, 4U);
        write_u32(bytes, kSecondSpawnOffset, 0U);
      },
      "LevelBootstrapV1 accepted out-of-order spawn IDs");
  expect_mutated_parse_rejected(
      valid, [](auto &bytes) { write_u32(bytes, kDefaultSpawnIdOffset, 3U); },
      "LevelBootstrapV1 accepted an absent default spawn ID");
}

void test_non_finite_signed_zero_and_limits() {
  const auto valid =
      openrc::encode_level_bootstrap_v1(make_bootstrap(), kLimits);
  expect_mutated_parse_rejected(
      valid,
      [](auto &bytes) {
        write_u64(bytes, kDeathHeightOffset,
                  std::bit_cast<std::uint64_t>(
                      std::numeric_limits<double>::infinity()));
      },
      "LevelBootstrapV1 accepted infinite death height");
  expect_mutated_parse_rejected(
      valid,
      [](auto &bytes) {
        write_u64(bytes, kFirstSpawnOffset + kSpawnXOffset,
                  std::bit_cast<std::uint64_t>(
                      std::numeric_limits<double>::quiet_NaN()));
      },
      "LevelBootstrapV1 accepted a NaN spawn coordinate");
  expect_mutated_parse_rejected(
      valid,
      [](auto &bytes) {
        write_u64(bytes, kFirstSpawnOffset + kSpawnYawOffset,
                  std::bit_cast<std::uint64_t>(
                      std::numeric_limits<double>::infinity()));
      },
      "LevelBootstrapV1 accepted infinite yaw");
  expect_mutated_parse_rejected(
      valid,
      [](auto &bytes) {
        write_u64(bytes, kFirstSpawnOffset + kSpawnXOffset,
                  UINT64_C(0x8000000000000000));
      },
      "LevelBootstrapV1 accepted non-canonical negative zero");
  expect_mutated_parse_rejected(
      valid,
      [](auto &bytes) {
        write_u64(bytes, kDeathHeightOffset,
                  std::bit_cast<std::uint64_t>(2.0));
      },
      "LevelBootstrapV1 accepted a spawn on its death plane");

  auto byte_limited = kLimits;
  byte_limited.max_input_bytes = valid.size() - 1U;
  expect_bootstrap_rejected(
      [&] {
        static_cast<void>(
            openrc::parse_level_bootstrap_v1(valid, byte_limited));
      },
      "LevelBootstrapV1 parser ignored its byte limit");
  auto count_limited = kLimits;
  count_limited.max_spawn_points = 2U;
  expect_bootstrap_rejected(
      [&] {
        static_cast<void>(
            openrc::parse_level_bootstrap_v1(valid, count_limited));
      },
      "LevelBootstrapV1 parser ignored its spawn limit");
  expect_bootstrap_rejected(
      [&] {
        static_cast<void>(openrc::parse_level_bootstrap_v1(
            valid, openrc::LevelBootstrapV1Limits{}));
      },
      "LevelBootstrapV1 parser accepted absent caller limits");
}

void test_writer_validation() {
  auto duplicate = make_bootstrap();
  duplicate.spawn_points.push_back(duplicate.spawn_points.front());
  expect_bootstrap_rejected(
      [&] {
        static_cast<void>(
            openrc::encode_level_bootstrap_v1(duplicate, kLimits));
      },
      "LevelBootstrapV1 writer accepted duplicate spawn IDs");

  auto absent_default = make_bootstrap();
  absent_default.default_spawn_id = 99U;
  expect_bootstrap_rejected(
      [&] {
        static_cast<void>(
            openrc::encode_level_bootstrap_v1(absent_default, kLimits));
      },
      "LevelBootstrapV1 writer accepted an absent default spawn");

  auto non_finite = make_bootstrap();
  non_finite.spawn_points.front().feet_position.z =
      std::numeric_limits<double>::quiet_NaN();
  expect_bootstrap_rejected(
      [&] {
        static_cast<void>(
            openrc::encode_level_bootstrap_v1(non_finite, kLimits));
      },
      "LevelBootstrapV1 writer accepted a non-finite coordinate");

  auto dead_spawn = make_bootstrap();
  dead_spawn.spawn_points.front().feet_position.z =
      dead_spawn.death_height_world;
  expect_bootstrap_rejected(
      [&] {
        static_cast<void>(
            openrc::encode_level_bootstrap_v1(dead_spawn, kLimits));
      },
      "LevelBootstrapV1 writer accepted a spawn on its death plane");

  auto count_limited = kLimits;
  count_limited.max_spawn_points = 2U;
  expect_bootstrap_rejected(
      [&] {
        static_cast<void>(
            openrc::encode_level_bootstrap_v1(make_bootstrap(), count_limited));
      },
      "LevelBootstrapV1 writer ignored its spawn limit");
}

[[nodiscard]] openrc::RacGameplayBankV1 make_gameplay_bank() {
  openrc::RacGameplayBankV1 gameplay;
  gameplay.level_settings.death_height = -500.25F;
  gameplay.level_settings.death_height_bits = 0xdeadbeefU;
  gameplay.level_settings.ship_position = {900.0F, 901.0F, 902.0F};
  gameplay.level_settings.ship_rotation_z = 3.0F;

  openrc::RacGameplayMobyInstanceV1 player;
  player.class_id = 0U;
  player.position_bits = {1U, 2U, 3U};
  player.position = {10.5F, -20.25F, 30.75F};
  player.rotation_bits = {4U, 5U, 6U};
  player.rotation = {0.1F, 0.2F, 1.25F};

  openrc::RacGameplayMobyInstanceV1 scenery;
  scenery.class_id = 10U;
  scenery.position = {100.0F, 200.0F, 300.0F};
  gameplay.static_mobies = {scenery, player};
  gameplay.static_moby_count =
      static_cast<std::uint32_t>(gameplay.static_mobies.size());
  return gameplay;
}

void test_clean_room_rac_adapter() {
  const auto gameplay = make_gameplay_bank();
  const auto bootstrap = openrc::compile_rac_level_bootstrap_v1(gameplay, 12U);
  expect(bootstrap.level_id == 12U && bootstrap.death_height_world == -500.25 &&
             bootstrap.default_spawn_id == 0U &&
             bootstrap.spawn_points.size() == 1U &&
             bootstrap.spawn_points.front().id == 0U &&
             bootstrap.spawn_points.front().feet_position ==
                 openrc::CollisionVectorV1{10.5, -20.25, 30.75} &&
             bootstrap.spawn_points.front().facing_yaw_radians == 1.25,
         "RAC1 bootstrap adapter did not preserve typed player data");
  expect(bootstrap.spawn_points.front().feet_position !=
                 openrc::CollisionVectorV1{900.0, 901.0, 902.0} &&
             bootstrap.spawn_points.front().facing_yaw_radians != 3.0,
         "RAC1 bootstrap adapter incorrectly used ship metadata");

  const auto encoded = openrc::encode_level_bootstrap_v1(bootstrap, kLimits);
  expect(openrc::parse_level_bootstrap_v1(encoded, kLimits) == bootstrap,
         "RAC1 adapter output is not a valid neutral bootstrap payload");
}

void test_rac_adapter_rejections() {
  auto missing = make_gameplay_bank();
  missing.static_mobies.back().class_id = 11U;
  expect_adapter_rejected(
      [&] {
        static_cast<void>(openrc::compile_rac_level_bootstrap_v1(missing, 0U));
      },
      "RAC1 bootstrap adapter accepted a missing class-0 Moby");

  auto duplicate = make_gameplay_bank();
  duplicate.static_mobies.push_back(duplicate.static_mobies.back());
  duplicate.static_moby_count =
      static_cast<std::uint32_t>(duplicate.static_mobies.size());
  expect_adapter_rejected(
      [&] {
        static_cast<void>(
            openrc::compile_rac_level_bootstrap_v1(duplicate, 0U));
      },
      "RAC1 bootstrap adapter accepted duplicate class-0 Mobies");

  auto mismatched_count = make_gameplay_bank();
  ++mismatched_count.static_moby_count;
  expect_adapter_rejected(
      [&] {
        static_cast<void>(
            openrc::compile_rac_level_bootstrap_v1(mismatched_count, 0U));
      },
      "RAC1 bootstrap adapter accepted a mismatched typed count");

  auto bad_death_height = make_gameplay_bank();
  bad_death_height.level_settings.death_height =
      std::numeric_limits<float>::infinity();
  expect_adapter_rejected(
      [&] {
        static_cast<void>(
            openrc::compile_rac_level_bootstrap_v1(bad_death_height, 0U));
      },
      "RAC1 bootstrap adapter accepted infinite death height");

  auto bad_spawn = make_gameplay_bank();
  bad_spawn.static_mobies.back().position[1U] =
      std::numeric_limits<float>::quiet_NaN();
  expect_adapter_rejected(
      [&] {
        static_cast<void>(
            openrc::compile_rac_level_bootstrap_v1(bad_spawn, 0U));
      },
      "RAC1 bootstrap adapter accepted a NaN player spawn");
}

} // namespace

int main() {
  try {
    test_round_trip_sorting_identity_and_positive_zero();
    test_envelope_reserved_and_order_rejections();
    test_non_finite_signed_zero_and_limits();
    test_writer_validation();
    test_clean_room_rac_adapter();
    test_rac_adapter_rejections();
    std::cout << "OpenRC LevelBootstrapV1 tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "OpenRC LevelBootstrapV1 tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
