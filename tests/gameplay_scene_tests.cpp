#include "openrc/gameplay_scene.hpp"
#include "openrc/gameplay_scene_io.hpp"

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

constexpr openrc::GameplaySceneIoLimitsV1 kLimits{
    1024U * 1024U,
    {
        128U,
        128U,
        16U * 1024U,
    },
};

constexpr std::size_t kFormatVersionOffset = 0x08U;
constexpr std::size_t kHeaderBytesOffset = 0x0cU;
constexpr std::size_t kTotalBytesOffset = 0x10U;
constexpr std::size_t kPayloadTypeOffset = 0x18U;
constexpr std::size_t kSchemaVersionOffset = 0x1cU;
constexpr std::size_t kLevelIdOffset = 0x20U;
constexpr std::size_t kCollectibleCountOffset = 0x24U;
constexpr std::size_t kTotalKeyBytesOffset = 0x28U;
constexpr std::size_t kCollectibleRecordBytesOffset = 0x30U;
constexpr std::size_t kHeaderFlagsOffset = 0x34U;
constexpr std::size_t kCollectibleTableOffset = 0x38U;
constexpr std::size_t kKeyDataOffset = 0x40U;
constexpr std::size_t kHeaderReservedOffset = 0x48U;

constexpr std::size_t kFixtureCollectibleTable = 0x60U;
constexpr std::size_t kFixtureSecondCollectible = 0x90U;
constexpr std::size_t kFixtureKeyData = 0xc0U;
constexpr std::size_t kFixtureTotalBytes = 223U;

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
  } catch (const openrc::GameplaySceneIoError &) {
    return;
  }
  fail(message);
}

template <typename Function>
void expect_scene_error(Function &&function, const std::string &message) {
  try {
    std::forward<Function>(function)();
  } catch (const openrc::GameplaySceneError &) {
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

[[nodiscard]] openrc::GameplaySceneV1 make_scene() {
  openrc::GameplayCollectibleV1 common;
  common.authored_id = 900U;
  common.item_key = "currency/bolt";
  common.local_center = {0.25F, -0.5F, 1.0F};
  common.amount = 25U;
  common.collection_radius = 1.5F;

  openrc::GameplayCollectibleV1 progression;
  progression.authored_id = 7U;
  progression.item_key = "progress/gold-bolt";
  progression.local_center = {1.0F, -0.0F, 2.0F};
  progression.amount = 1U;
  progression.collection_radius = 2.25F;

  openrc::GameplaySceneV1 result;
  result.level_id = UINT32_C(0x12345678);
  result.collectibles = {common, progression};
  return result;
}

void test_identity_layout_round_trip_and_determinism() {
  expect(openrc::kGameplaySceneResourceIdV1 ==
                 std::string_view("world/gameplay") &&
             openrc::kGameplaySceneResourceTypeIdV1 ==
                 std::string_view("openrc.gameplay-scene") &&
             openrc::kGameplaySceneResourceSchemaVersionV1 == 1U,
         "GameplaySceneV1 resource identity is wrong");

  const auto expected =
      openrc::canonicalize_gameplay_scene_v1(make_scene(), kLimits.scene);
  const auto bytes = openrc::encode_gameplay_scene_v1(make_scene(), kLimits);
  expect(bytes.size() == kFixtureTotalBytes,
         "GameplaySceneV1 exact fixture size changed unexpectedly");
  expect(read_u32(bytes, kFormatVersionOffset) == 1U &&
             read_u32(bytes, kHeaderBytesOffset) == 0x60U &&
             read_u64(bytes, kTotalBytesOffset) == bytes.size() &&
             read_u32(bytes, kPayloadTypeOffset) == 1U &&
             read_u32(bytes, kSchemaVersionOffset) == 1U &&
             read_u32(bytes, kLevelIdOffset) == UINT32_C(0x12345678),
         "GameplaySceneV1 envelope fields are wrong");
  expect(byte_value(bytes[kLevelIdOffset]) == 0x78U &&
             byte_value(bytes[kLevelIdOffset + 1U]) == 0x56U &&
             byte_value(bytes[kLevelIdOffset + 2U]) == 0x34U &&
             byte_value(bytes[kLevelIdOffset + 3U]) == 0x12U,
         "GameplaySceneV1 integers are not little-endian");
  expect(read_u32(bytes, kCollectibleCountOffset) == 2U &&
             read_u64(bytes, kTotalKeyBytesOffset) == 31U &&
             read_u32(bytes, kCollectibleRecordBytesOffset) == 48U &&
             read_u64(bytes, kCollectibleTableOffset) ==
                 kFixtureCollectibleTable &&
             read_u64(bytes, kKeyDataOffset) == kFixtureKeyData,
         "GameplaySceneV1 counts or canonical table offsets are wrong");
  expect(read_u32(bytes, kFixtureCollectibleTable) == 7U &&
             read_u32(bytes, kFixtureSecondCollectible) == 900U,
         "GameplaySceneV1 collectible authored IDs were not sorted");
  expect(read_u32(bytes, kFixtureCollectibleTable + 8U) == 1U &&
             read_u32(bytes, kFixtureSecondCollectible + 8U) == 25U,
         "GameplaySceneV1 collectible amounts changed in the envelope");

  const auto decoded = openrc::decode_gameplay_scene_v1(bytes, kLimits);
  expect(decoded == expected,
         "GameplaySceneV1 canonical round trip changed logical content");
  expect(
      decoded.collectibles[0U].local_center[1U] == 0.0F &&
          !std::signbit(decoded.collectibles[0U].local_center[1U]),
      "GameplaySceneV1 writer did not canonicalize local-center signed zero");
  expect(openrc::encode_gameplay_scene_v1(decoded, kLimits) == bytes,
         "GameplaySceneV1 re-encoding is not byte deterministic");

  const openrc::GameplaySceneV1 empty;
  const auto empty_bytes = openrc::encode_gameplay_scene_v1(empty, kLimits);
  expect(empty_bytes.size() == openrc::kGameplaySceneIoHeaderBytesV1 &&
             openrc::decode_gameplay_scene_v1(empty_bytes, kLimits) == empty,
         "An empty GameplaySceneV1 did not round-trip canonically");
}

void test_record_offsets_preserve_the_full_format_count_domain() {
  const auto last_record_offset = static_cast<std::uint64_t>(UINT32_MAX) *
                                  openrc::kGameplaySceneIoCollectibleBytesV1;
  expect(last_record_offset == UINT64_C(206158430160),
         "GameplaySceneV1 record-offset arithmetic discarded high bits");
}

template <typename Mutation>
void expect_corrupt_decode(Mutation &&mutation, const std::string &message) {
  auto bytes = openrc::encode_gameplay_scene_v1(make_scene(), kLimits);
  std::forward<Mutation>(mutation)(bytes);
  expect_io_error(
      [&] {
        static_cast<void>(openrc::decode_gameplay_scene_v1(bytes, kLimits));
      },
      message);
}

void test_envelope_partitions_reserved_and_truncation() {
  expect_corrupt_decode([](auto &bytes) { bytes[0U] ^= std::byte{1U}; },
                        "GameplaySceneV1 decoder accepted bad magic");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFormatVersionOffset, 2U); },
      "GameplaySceneV1 decoder accepted an unknown format version");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kHeaderBytesOffset, 0x5cU); },
      "GameplaySceneV1 decoder accepted a wrong header size");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kPayloadTypeOffset, 2U); },
      "GameplaySceneV1 decoder accepted an unknown payload type");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kSchemaVersionOffset, 2U); },
      "GameplaySceneV1 decoder accepted an unknown schema version");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kCollectibleRecordBytesOffset, 36U); },
      "GameplaySceneV1 decoder accepted a wrong record width");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kHeaderFlagsOffset, 1U); },
      "GameplaySceneV1 decoder accepted unknown header flags");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[kHeaderReservedOffset] = std::byte{1U}; },
      "GameplaySceneV1 decoder accepted non-zero header reserved data");
  expect_corrupt_decode(
      [](auto &bytes) {
        bytes[kFixtureCollectibleTable + 40U] = std::byte{1U};
      },
      "GameplaySceneV1 decoder accepted collectible reserved data");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, kCollectibleTableOffset, 0U); },
      "GameplaySceneV1 decoder accepted a non-canonical table offset");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u64(bytes, kFixtureCollectibleTable + 32U, kFixtureKeyData + 1U);
      },
      "GameplaySceneV1 decoder accepted a gapped key partition");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u64(bytes, kTotalBytesOffset, bytes.size() - 1U);
      },
      "GameplaySceneV1 decoder accepted a false total size");

  const auto bytes = openrc::encode_gameplay_scene_v1(make_scene(), kLimits);
  for (std::size_t length = 0U; length < bytes.size(); ++length) {
    expect_io_error(
        [&] {
          static_cast<void>(openrc::decode_gameplay_scene_v1(
              std::span<const std::byte>(bytes.data(), length), kLimits));
        },
        "GameplaySceneV1 decoder accepted a truncated prefix");
  }
  auto trailing = bytes;
  trailing.push_back(std::byte{0U});
  expect_io_error(
      [&] {
        static_cast<void>(openrc::decode_gameplay_scene_v1(trailing, kLimits));
      },
      "GameplaySceneV1 decoder accepted trailing data");
}

void test_decoder_semantic_and_float_rejections() {
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureCollectibleTable + 4U, 1U); },
      "GameplaySceneV1 decoder accepted unknown collectible flags");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureSecondCollectible, 7U); },
      "GameplaySceneV1 decoder accepted duplicate collectible IDs");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureSecondCollectible, 6U); },
      "GameplaySceneV1 decoder accepted out-of-order collectible IDs");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[kFixtureKeyData] = std::byte{'P'}; },
      "GameplaySceneV1 decoder accepted a non-canonical semantic key");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureCollectibleTable + 16U, UINT32_C(0x80000000));
      },
      "GameplaySceneV1 decoder accepted negative zero in a local center");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureCollectibleTable + 16U, UINT32_C(0x7fc00000));
      },
      "GameplaySceneV1 decoder accepted NaN in a local center");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureCollectibleTable + 28U, UINT32_C(0x7f800000));
      },
      "GameplaySceneV1 decoder accepted infinity in a collection radius");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureCollectibleTable + 28U, 0U); },
      "GameplaySceneV1 decoder accepted a zero collection radius");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureCollectibleTable + 28U,
                  std::bit_cast<std::uint32_t>(-1.0F));
      },
      "GameplaySceneV1 decoder accepted a negative collection radius");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureCollectibleTable + 8U, 0U); },
      "GameplaySceneV1 decoder accepted a zero collectible amount");
}

template <typename Mutation>
void expect_invalid_encode(Mutation &&mutation, const std::string &message) {
  auto scene = make_scene();
  std::forward<Mutation>(mutation)(scene);
  expect_io_error(
      [&] {
        static_cast<void>(openrc::encode_gameplay_scene_v1(scene, kLimits));
      },
      message);
}

void test_model_and_key_rejections() {
  expect_scene_error(
      [&] { openrc::validate_gameplay_scene_v1(make_scene(), kLimits.scene); },
      "GameplaySceneV1 validator accepted non-canonical table order");
  expect_invalid_encode(
      [](auto &scene) { scene.schema_version = 2U; },
      "GameplaySceneV1 writer accepted an unknown schema version");
  expect_invalid_encode(
      [](auto &scene) {
        scene.collectibles.push_back(scene.collectibles.front());
      },
      "GameplaySceneV1 writer accepted duplicate collectibles");
  expect_invalid_encode(
      [](auto &scene) { scene.collectibles.front().flags = 1U; },
      "GameplaySceneV1 writer accepted unknown collectible flags");
  expect_invalid_encode(
      [](auto &scene) { scene.collectibles.front().amount = 0U; },
      "GameplaySceneV1 writer accepted a zero collectible amount");
  expect_invalid_encode(
      [](auto &scene) {
        scene.collectibles.front().item_key = "Currency/Bolt";
      },
      "GameplaySceneV1 writer accepted a non-canonical item key");
  expect_invalid_encode(
      [](auto &scene) { scene.collectibles.front().item_key = "item/../bolt"; },
      "GameplaySceneV1 writer accepted an unsafe item key");
  expect_invalid_encode(
      [](auto &scene) {
        scene.collectibles.front().local_center[0U] =
            std::numeric_limits<float>::infinity();
      },
      "GameplaySceneV1 writer accepted a non-finite local center");
  expect_invalid_encode(
      [](auto &scene) {
        scene.collectibles.front().collection_radius =
            std::numeric_limits<float>::quiet_NaN();
      },
      "GameplaySceneV1 writer accepted a non-finite collection radius");
  expect_invalid_encode(
      [](auto &scene) { scene.collectibles.front().collection_radius = -0.0F; },
      "GameplaySceneV1 writer accepted negative zero");
  expect_invalid_encode(
      [](auto &scene) { scene.collectibles.front().collection_radius = 0.0F; },
      "GameplaySceneV1 writer accepted a zero collection radius");
  expect_invalid_encode(
      [](auto &scene) { scene.collectibles.front().collection_radius = -1.0F; },
      "GameplaySceneV1 writer accepted a negative collection radius");
}

void test_limits_are_enforced_before_allocation() {
  const auto bytes = openrc::encode_gameplay_scene_v1(make_scene(), kLimits);

  auto byte_limited = kLimits;
  byte_limited.max_encoded_bytes = bytes.size() - 1U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::encode_gameplay_scene_v1(make_scene(), byte_limited));
      },
      "GameplaySceneV1 writer ignored its byte limit");
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_gameplay_scene_v1(bytes, byte_limited));
      },
      "GameplaySceneV1 decoder ignored its byte limit");

  auto count_limited = kLimits;
  count_limited.scene.max_collectibles = 1U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_gameplay_scene_v1(bytes, count_limited));
      },
      "GameplaySceneV1 decoder ignored its collectible limit");

  auto key_limited = kLimits;
  key_limited.scene.max_item_key_bytes = 17U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::encode_gameplay_scene_v1(make_scene(), key_limited));
      },
      "GameplaySceneV1 writer ignored its per-key limit");
  auto aggregate_limited = kLimits;
  aggregate_limited.scene.max_total_key_bytes = 30U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_gameplay_scene_v1(bytes, aggregate_limited));
      },
      "GameplaySceneV1 decoder ignored its aggregate key-byte limit");

  expect_io_error(
      [&] {
        static_cast<void>(openrc::decode_gameplay_scene_v1(
            bytes, openrc::GameplaySceneIoLimitsV1{}));
      },
      "GameplaySceneV1 decoder accepted absent caller limits");

  auto excessive_count = bytes;
  write_u32(excessive_count, kCollectibleCountOffset,
            kLimits.scene.max_collectibles + 1U);
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_gameplay_scene_v1(excessive_count, kLimits));
      },
      "GameplaySceneV1 decoder did not reject a count limit from the header");
  auto excessive_keys = bytes;
  write_u64(excessive_keys, kTotalKeyBytesOffset,
            kLimits.scene.max_total_key_bytes + 1U);
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_gameplay_scene_v1(excessive_keys, kLimits));
      },
      "GameplaySceneV1 decoder did not reject aggregate key bytes from the "
      "header");
}

} // namespace

int main() {
  try {
    test_identity_layout_round_trip_and_determinism();
    test_record_offsets_preserve_the_full_format_count_domain();
    test_envelope_partitions_reserved_and_truncation();
    test_decoder_semantic_and_float_rejections();
    test_model_and_key_rejections();
    test_limits_are_enforced_before_allocation();
    std::cout << "GameplaySceneV1 tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "GameplaySceneV1 tests failed: " << error.what() << '\n';
    return 1;
  }
}
