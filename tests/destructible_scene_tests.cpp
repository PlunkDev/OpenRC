#include "openrc/destructible_scene.hpp"
#include "openrc/destructible_scene_io.hpp"

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
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr openrc::DestructibleSceneIoLimitsV1 kLimits{
    1024U * 1024U,
    {
        128U,
        1024U,
        32U,
        128U,
        16U * 1024U,
        10'000U,
        10'000U,
        1000.0F,
        1000.0F,
    },
};

constexpr std::size_t kFormatVersionOffset = 0x08U;
constexpr std::size_t kHeaderBytesOffset = 0x0cU;
constexpr std::size_t kTotalBytesOffset = 0x10U;
constexpr std::size_t kPayloadTypeOffset = 0x18U;
constexpr std::size_t kSchemaVersionOffset = 0x1cU;
constexpr std::size_t kLevelIdOffset = 0x20U;
constexpr std::size_t kDestructibleCountOffset = 0x24U;
constexpr std::size_t kTotalDropCountOffset = 0x28U;
constexpr std::size_t kHeaderFlagsOffset = 0x2cU;
constexpr std::size_t kTotalKeyBytesOffset = 0x30U;
constexpr std::size_t kDestructibleRecordBytesOffset = 0x38U;
constexpr std::size_t kDropRecordBytesOffset = 0x3cU;
constexpr std::size_t kDestructibleTableOffset = 0x40U;
constexpr std::size_t kDropTableOffset = 0x48U;
constexpr std::size_t kKeyDataOffset = 0x50U;
constexpr std::size_t kHeaderReservedOffset = 0x58U;

constexpr std::size_t kFixtureDestructibleTable = 0x80U;
constexpr std::size_t kFixtureSecondDestructible = 0xb0U;
constexpr std::size_t kFixtureDropTable = 0xe0U;
constexpr std::size_t kFixtureSecondDrop = 0x100U;
constexpr std::size_t kFixtureThirdDrop = 0x120U;
constexpr std::size_t kFixtureKeyData = 0x140U;
constexpr std::size_t kFixtureTotalBytes = 364U;

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
  } catch (const openrc::DestructibleSceneIoError &) {
    return;
  }
  fail(message);
}

template <typename Function>
void expect_scene_error(Function &&function, const std::string &message) {
  try {
    std::forward<Function>(function)();
  } catch (const openrc::DestructibleSceneError &) {
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

void write_u64(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::uint64_t value) {
  for (std::size_t index = 0U; index < 8U; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
  }
}

[[nodiscard]] openrc::DestructibleSceneV1 make_scene() {
  openrc::DestructibleDefinitionV1 crate;
  crate.authored_id = 900U;
  crate.max_health = 100U;
  crate.accepted_damage_channels =
      openrc::game::kDamageChannelProjectileV1 |
      openrc::game::kDamageChannelExplosiveV1;
  crate.local_hit_center = {0.25F, -0.5F, 1.0F};
  crate.hit_radius = 1.5F;
  crate.drops = {{"ammo/standard", 4U, 0U}};

  openrc::DestructibleDefinitionV1 statue;
  statue.authored_id = 7U;
  statue.max_health = 250U;
  statue.accepted_damage_channels =
      openrc::game::kDamageChannelMeleeV1 |
      openrc::game::kDamageChannelExplosiveV1;
  statue.local_hit_center = {1.0F, -0.0F, 2.0F};
  statue.hit_radius = 2.25F;
  statue.drops = {
      {"progress/gold-bolt", 1U, 0U},
      {"currency/bolt", 25U, 0U},
  };

  openrc::DestructibleSceneV1 result;
  result.level_id = UINT32_C(0x12345678);
  result.destructibles = {crate, statue};
  return result;
}

void test_identity_layout_round_trip_and_determinism() {
  expect(openrc::kDestructibleSceneResourceIdV1 ==
                 std::string_view("world/destructibles") &&
             openrc::kDestructibleSceneResourceTypeIdV1 ==
                 std::string_view("openrc.destructible-scene") &&
             openrc::kDestructibleSceneResourceSchemaVersionV1 == 1U,
         "DestructibleSceneV1 resource identity is wrong");

  const auto expected =
      openrc::canonicalize_destructible_scene_v1(make_scene(), kLimits.scene);
  const auto bytes =
      openrc::encode_destructible_scene_v1(make_scene(), kLimits);
  expect(bytes.size() == kFixtureTotalBytes,
         "DestructibleSceneV1 exact fixture size changed unexpectedly");
  expect(byte_value(bytes[0U]) == static_cast<std::uint8_t>('O') &&
             byte_value(bytes[1U]) == static_cast<std::uint8_t>('R') &&
             byte_value(bytes[2U]) == static_cast<std::uint8_t>('D') &&
             byte_value(bytes[3U]) == static_cast<std::uint8_t>('S') &&
             byte_value(bytes[4U]) == static_cast<std::uint8_t>('T') &&
             byte_value(bytes[5U]) == static_cast<std::uint8_t>('R') &&
             byte_value(bytes[6U]) == static_cast<std::uint8_t>('C') &&
             byte_value(bytes[7U]) == static_cast<std::uint8_t>('1'),
         "DestructibleSceneV1 magic changed unexpectedly");
  expect(read_u32(bytes, kFormatVersionOffset) == 1U &&
             read_u32(bytes, kHeaderBytesOffset) == 0x80U &&
             read_u64(bytes, kTotalBytesOffset) == bytes.size() &&
             read_u32(bytes, kPayloadTypeOffset) == 1U &&
             read_u32(bytes, kSchemaVersionOffset) == 1U &&
             read_u32(bytes, kLevelIdOffset) == UINT32_C(0x12345678),
         "DestructibleSceneV1 envelope fields are wrong");
  expect(byte_value(bytes[kLevelIdOffset]) == 0x78U &&
             byte_value(bytes[kLevelIdOffset + 1U]) == 0x56U &&
             byte_value(bytes[kLevelIdOffset + 2U]) == 0x34U &&
             byte_value(bytes[kLevelIdOffset + 3U]) == 0x12U,
         "DestructibleSceneV1 integers are not little-endian");
  expect(read_u32(bytes, kDestructibleCountOffset) == 2U &&
             read_u32(bytes, kTotalDropCountOffset) == 3U &&
             read_u64(bytes, kTotalKeyBytesOffset) == 44U &&
             read_u32(bytes, kDestructibleRecordBytesOffset) == 48U &&
             read_u32(bytes, kDropRecordBytesOffset) == 32U &&
             read_u64(bytes, kDestructibleTableOffset) ==
                 kFixtureDestructibleTable &&
             read_u64(bytes, kDropTableOffset) == kFixtureDropTable &&
             read_u64(bytes, kKeyDataOffset) == kFixtureKeyData,
         "DestructibleSceneV1 counts or table offsets are wrong");
  expect(read_u32(bytes, kFixtureDestructibleTable) == 7U &&
             read_u32(bytes, kFixtureSecondDestructible) == 900U &&
             read_u32(bytes, kFixtureDestructibleTable + 8U) == 250U &&
             read_u32(bytes, kFixtureDestructibleTable + 12U) ==
                 (openrc::game::kDamageChannelMeleeV1 |
                  openrc::game::kDamageChannelExplosiveV1) &&
             read_u32(bytes, kFixtureDestructibleTable + 32U) == 0U &&
             read_u32(bytes, kFixtureDestructibleTable + 36U) == 2U &&
             read_u32(bytes, kFixtureSecondDestructible + 32U) == 2U &&
             read_u32(bytes, kFixtureSecondDestructible + 36U) == 1U,
         "DestructibleSceneV1 definition/drop partition is wrong");
  expect(read_u32(bytes, kFixtureDropTable) == 13U &&
             read_u32(bytes, kFixtureDropTable + 4U) == 25U &&
             read_u64(bytes, kFixtureDropTable + 8U) == kFixtureKeyData &&
             read_u32(bytes, kFixtureDropTable + 16U) == 0U &&
             read_u32(bytes, kFixtureSecondDrop) == 18U &&
             read_u32(bytes, kFixtureSecondDrop + 4U) == 1U &&
             read_u64(bytes, kFixtureSecondDrop + 8U) ==
                 kFixtureKeyData + 13U &&
             read_u32(bytes, kFixtureThirdDrop) == 13U &&
             read_u32(bytes, kFixtureThirdDrop + 4U) == 4U &&
             read_u64(bytes, kFixtureThirdDrop + 8U) ==
                 kFixtureKeyData + 31U,
         "DestructibleSceneV1 drop records or keys are not exact");

  const auto decoded = openrc::decode_destructible_scene_v1(bytes, kLimits);
  expect(decoded == expected,
         "DestructibleSceneV1 canonical round trip changed logical content");
  expect(decoded.destructibles[0U].local_hit_center[1U] == 0.0F &&
             !std::signbit(
                 decoded.destructibles[0U].local_hit_center[1U]),
         "DestructibleSceneV1 writer did not canonicalize signed zero");
  expect(openrc::encode_destructible_scene_v1(decoded, kLimits) == bytes,
         "DestructibleSceneV1 re-encoding is not byte deterministic");

  const openrc::DestructibleSceneV1 empty;
  const auto empty_bytes =
      openrc::encode_destructible_scene_v1(empty, kLimits);
  expect(empty_bytes.size() == openrc::kDestructibleSceneIoHeaderBytesV1 &&
             openrc::decode_destructible_scene_v1(empty_bytes, kLimits) ==
                 empty,
         "An empty DestructibleSceneV1 did not round-trip canonically");
}

void test_record_offsets_preserve_the_full_format_count_domain() {
  const auto last_definition_offset =
      static_cast<std::uint64_t>(UINT32_MAX) *
      openrc::kDestructibleSceneIoDestructibleBytesV1;
  const auto last_drop_offset = static_cast<std::uint64_t>(UINT32_MAX) *
                                openrc::kDestructibleSceneIoDropBytesV1;
  expect(last_definition_offset == UINT64_C(206158430160) &&
             last_drop_offset == UINT64_C(137438953440),
         "DestructibleSceneV1 record-offset arithmetic discarded high bits");
}

template <typename Mutation>
void expect_corrupt_decode(Mutation &&mutation, const std::string &message) {
  auto bytes = openrc::encode_destructible_scene_v1(make_scene(), kLimits);
  std::forward<Mutation>(mutation)(bytes);
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_destructible_scene_v1(bytes, kLimits));
      },
      message);
}

void test_envelope_partitions_reserved_and_truncation() {
  expect_corrupt_decode([](auto &bytes) { bytes[0U] ^= std::byte{1U}; },
                        "DestructibleSceneV1 decoder accepted bad magic");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFormatVersionOffset, 2U); },
      "DestructibleSceneV1 decoder accepted an unknown format version");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kHeaderBytesOffset, 0x7cU); },
      "DestructibleSceneV1 decoder accepted a wrong header size");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kPayloadTypeOffset, 2U); },
      "DestructibleSceneV1 decoder accepted an unknown payload type");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kSchemaVersionOffset, 2U); },
      "DestructibleSceneV1 decoder accepted an unknown schema version");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kDestructibleRecordBytesOffset, 44U);
      },
      "DestructibleSceneV1 decoder accepted a wrong definition width");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kDropRecordBytesOffset, 28U); },
      "DestructibleSceneV1 decoder accepted a wrong drop width");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kHeaderFlagsOffset, 1U); },
      "DestructibleSceneV1 decoder accepted unknown header flags");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[kHeaderReservedOffset] = std::byte{1U}; },
      "DestructibleSceneV1 decoder accepted header reserved data");
  expect_corrupt_decode(
      [](auto &bytes) {
        bytes[kFixtureDestructibleTable + 40U] = std::byte{1U};
      },
      "DestructibleSceneV1 decoder accepted definition reserved data");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[kFixtureDropTable + 20U] = std::byte{1U}; },
      "DestructibleSceneV1 decoder accepted drop reserved u32 data");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[kFixtureDropTable + 24U] = std::byte{1U}; },
      "DestructibleSceneV1 decoder accepted drop reserved u64 data");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, kDestructibleTableOffset, 0U); },
      "DestructibleSceneV1 decoder accepted a bad definition-table offset");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, kDropTableOffset, 0U); },
      "DestructibleSceneV1 decoder accepted a bad drop-table offset");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, kKeyDataOffset, 0U); },
      "DestructibleSceneV1 decoder accepted a bad key-data offset");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureDestructibleTable + 32U, 1U);
      },
      "DestructibleSceneV1 decoder accepted a gapped drop partition");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u64(bytes, kFixtureDropTable + 8U, kFixtureKeyData + 1U);
      },
      "DestructibleSceneV1 decoder accepted a gapped key partition");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u64(bytes, kTotalBytesOffset, bytes.size() - 1U);
      },
      "DestructibleSceneV1 decoder accepted a false total size");

  const auto bytes =
      openrc::encode_destructible_scene_v1(make_scene(), kLimits);
  for (std::size_t length = 0U; length < bytes.size(); ++length) {
    expect_io_error(
        [&] {
          static_cast<void>(openrc::decode_destructible_scene_v1(
              std::span<const std::byte>(bytes.data(), length), kLimits));
        },
        "DestructibleSceneV1 decoder accepted a truncated prefix");
  }
  auto trailing = bytes;
  trailing.push_back(std::byte{0U});
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_destructible_scene_v1(trailing, kLimits));
      },
      "DestructibleSceneV1 decoder accepted trailing data");
}

void test_decoder_semantic_order_channel_and_float_rejections() {
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureDestructibleTable + 4U, 1U);
      },
      "DestructibleSceneV1 decoder accepted unknown definition flags");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureSecondDestructible, 7U); },
      "DestructibleSceneV1 decoder accepted duplicate authored IDs");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureSecondDestructible, 6U); },
      "DestructibleSceneV1 decoder accepted out-of-order authored IDs");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureDestructibleTable + 8U, 0U);
      },
      "DestructibleSceneV1 decoder accepted zero max health");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureDestructibleTable + 12U, 0U);
      },
      "DestructibleSceneV1 decoder accepted zero damage channels");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureDestructibleTable + 12U,
                  UINT32_C(1) << 31U);
      },
      "DestructibleSceneV1 decoder accepted an unknown damage channel");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureDestructibleTable + 16U,
                  UINT32_C(0x80000000));
      },
      "DestructibleSceneV1 decoder accepted negative zero in a hit center");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureDestructibleTable + 16U,
                  UINT32_C(0x7fc00000));
      },
      "DestructibleSceneV1 decoder accepted NaN in a hit center");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureDestructibleTable + 28U,
                  UINT32_C(0x7f800000));
      },
      "DestructibleSceneV1 decoder accepted infinite hit radius");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureDestructibleTable + 28U, 0U);
      },
      "DestructibleSceneV1 decoder accepted zero hit radius");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureDestructibleTable + 28U,
                  std::bit_cast<std::uint32_t>(-1.0F));
      },
      "DestructibleSceneV1 decoder accepted negative hit radius");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureDropTable + 16U, 1U); },
      "DestructibleSceneV1 decoder accepted unknown drop flags");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureDropTable + 4U, 0U); },
      "DestructibleSceneV1 decoder accepted zero drop amount");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[kFixtureKeyData] = std::byte{'C'}; },
      "DestructibleSceneV1 decoder accepted a non-canonical item key");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[kFixtureKeyData + 13U] = std::byte{'a'}; },
      "DestructibleSceneV1 decoder accepted out-of-order drop keys");
}

template <typename Mutation>
void expect_invalid_encode(Mutation &&mutation, const std::string &message) {
  auto scene = make_scene();
  std::forward<Mutation>(mutation)(scene);
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::encode_destructible_scene_v1(scene, kLimits));
      },
      message);
}

void test_model_canonicalization_and_rejections() {
  expect_scene_error(
      [&] {
        openrc::validate_destructible_scene_v1(make_scene(), kLimits.scene);
      },
      "DestructibleSceneV1 validator accepted non-canonical definition order");

  auto drop_disordered = openrc::canonicalize_destructible_scene_v1(
      make_scene(), kLimits.scene);
  std::swap(drop_disordered.destructibles.front().drops[0U],
            drop_disordered.destructibles.front().drops[1U]);
  expect_scene_error(
      [&] {
        openrc::validate_destructible_scene_v1(drop_disordered,
                                               kLimits.scene);
      },
      "DestructibleSceneV1 validator accepted non-canonical drop order");

  expect_invalid_encode(
      [](auto &scene) { scene.schema_version = 2U; },
      "DestructibleSceneV1 writer accepted an unknown schema version");
  expect_invalid_encode(
      [](auto &scene) {
        scene.destructibles.push_back(scene.destructibles.front());
      },
      "DestructibleSceneV1 writer accepted duplicate definitions");
  expect_invalid_encode(
      [](auto &scene) {
        scene.destructibles.back().drops.push_back(
            scene.destructibles.back().drops.front());
      },
      "DestructibleSceneV1 writer accepted duplicate per-definition drops");
  expect_invalid_encode(
      [](auto &scene) { scene.destructibles.front().flags = 1U; },
      "DestructibleSceneV1 writer accepted unknown definition flags");
  expect_invalid_encode(
      [](auto &scene) { scene.destructibles.front().max_health = 0U; },
      "DestructibleSceneV1 writer accepted zero max health");
  expect_invalid_encode(
      [](auto &scene) {
        scene.destructibles.front().accepted_damage_channels = 0U;
      },
      "DestructibleSceneV1 writer accepted zero damage channels");
  expect_invalid_encode(
      [](auto &scene) {
        scene.destructibles.front().accepted_damage_channels =
            UINT32_C(1) << 31U;
      },
      "DestructibleSceneV1 writer accepted an unknown damage channel");
  expect_invalid_encode(
      [](auto &scene) { scene.destructibles.front().drops.front().flags = 1U; },
      "DestructibleSceneV1 writer accepted unknown drop flags");
  expect_invalid_encode(
      [](auto &scene) { scene.destructibles.front().drops.front().amount = 0U; },
      "DestructibleSceneV1 writer accepted zero drop amount");
  expect_invalid_encode(
      [](auto &scene) {
        scene.destructibles.front().drops.front().item_key = "Currency/Bolt";
      },
      "DestructibleSceneV1 writer accepted a non-canonical item key");
  expect_invalid_encode(
      [](auto &scene) {
        scene.destructibles.front().drops.front().item_key = "item/../bolt";
      },
      "DestructibleSceneV1 writer accepted an unsafe item key");
  expect_invalid_encode(
      [](auto &scene) {
        scene.destructibles.front().local_hit_center[0U] =
            std::numeric_limits<float>::infinity();
      },
      "DestructibleSceneV1 writer accepted a non-finite hit center");
  expect_invalid_encode(
      [](auto &scene) {
        scene.destructibles.front().hit_radius =
            std::numeric_limits<float>::quiet_NaN();
      },
      "DestructibleSceneV1 writer accepted a non-finite hit radius");
  expect_invalid_encode(
      [](auto &scene) { scene.destructibles.front().hit_radius = -0.0F; },
      "DestructibleSceneV1 writer accepted negative-zero hit radius");
  expect_invalid_encode(
      [](auto &scene) { scene.destructibles.front().hit_radius = -1.0F; },
      "DestructibleSceneV1 writer accepted negative hit radius");
}

void test_all_caller_limits_are_enforced() {
  const auto bytes =
      openrc::encode_destructible_scene_v1(make_scene(), kLimits);

  auto limited = kLimits;
  limited.max_encoded_bytes = bytes.size() - 1U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::encode_destructible_scene_v1(make_scene(), limited));
      },
      "DestructibleSceneV1 writer ignored its encoded-byte limit");
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_destructible_scene_v1(bytes, limited));
      },
      "DestructibleSceneV1 decoder ignored its encoded-byte limit");

  limited = kLimits;
  limited.scene.max_destructibles = 1U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_destructible_scene_v1(bytes, limited));
      },
      "DestructibleSceneV1 decoder ignored its definition limit");
  limited = kLimits;
  limited.scene.max_total_drops = 2U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_destructible_scene_v1(bytes, limited));
      },
      "DestructibleSceneV1 decoder ignored its aggregate drop limit");
  limited = kLimits;
  limited.scene.max_drops_per_destructible = 1U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_destructible_scene_v1(bytes, limited));
      },
      "DestructibleSceneV1 decoder ignored its per-definition drop limit");
  limited = kLimits;
  limited.scene.max_item_key_bytes = 17U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::encode_destructible_scene_v1(make_scene(), limited));
      },
      "DestructibleSceneV1 writer ignored its per-key limit");
  limited = kLimits;
  limited.scene.max_total_key_bytes = 43U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_destructible_scene_v1(bytes, limited));
      },
      "DestructibleSceneV1 decoder ignored its aggregate key-byte limit");
  limited = kLimits;
  limited.scene.max_health = 249U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_destructible_scene_v1(bytes, limited));
      },
      "DestructibleSceneV1 decoder ignored its health limit");
  limited = kLimits;
  limited.scene.max_drop_amount = 24U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::encode_destructible_scene_v1(make_scene(), limited));
      },
      "DestructibleSceneV1 writer ignored its drop-amount limit");
  limited = kLimits;
  limited.scene.max_absolute_local_hit_center = 1.99F;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_destructible_scene_v1(bytes, limited));
      },
      "DestructibleSceneV1 decoder ignored its local hit-center limit");
  limited = kLimits;
  limited.scene.max_hit_radius = 2.0F;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::encode_destructible_scene_v1(make_scene(), limited));
      },
      "DestructibleSceneV1 writer ignored its hit-radius limit");

  expect_io_error(
      [&] {
        static_cast<void>(openrc::decode_destructible_scene_v1(
            bytes, openrc::DestructibleSceneIoLimitsV1{}));
      },
      "DestructibleSceneV1 decoder accepted absent caller limits");

  auto excessive_count = bytes;
  write_u32(excessive_count, kDestructibleCountOffset,
            kLimits.scene.max_destructibles + 1U);
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_destructible_scene_v1(excessive_count, kLimits));
      },
      "DestructibleSceneV1 decoder did not reject a header definition limit");
  auto excessive_drops = bytes;
  write_u32(excessive_drops, kTotalDropCountOffset,
            kLimits.scene.max_total_drops + 1U);
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_destructible_scene_v1(excessive_drops, kLimits));
      },
      "DestructibleSceneV1 decoder did not reject a header drop limit");
  auto excessive_keys = bytes;
  write_u64(excessive_keys, kTotalKeyBytesOffset,
            kLimits.scene.max_total_key_bytes + 1U);
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_destructible_scene_v1(excessive_keys, kLimits));
      },
      "DestructibleSceneV1 decoder did not reject a header key-byte limit");
}

} // namespace

int main() {
  try {
    test_identity_layout_round_trip_and_determinism();
    test_record_offsets_preserve_the_full_format_count_domain();
    test_envelope_partitions_reserved_and_truncation();
    test_decoder_semantic_order_channel_and_float_rejections();
    test_model_canonicalization_and_rejections();
    test_all_caller_limits_are_enforced();
    std::cout << "DestructibleSceneV1 tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "DestructibleSceneV1 tests failed: " << error.what() << '\n';
    return 1;
  }
}
