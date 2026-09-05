#include "openrc/entity_scene.hpp"
#include "openrc/entity_scene_io.hpp"

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

constexpr openrc::EntitySceneIoLimitsV1 kLimits{
    1024U * 1024U,
    {
        128U,
        128U,
        128U,
        128U,
        8U,
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
constexpr std::size_t kDefinitionCountOffset = 0x24U;
constexpr std::size_t kTransformCountOffset = 0x28U;
constexpr std::size_t kRenderBindingCountOffset = 0x2cU;
constexpr std::size_t kActorBindingCountOffset = 0x30U;
constexpr std::size_t kPlayerBindingCountOffset = 0x34U;
constexpr std::size_t kTotalKeyBytesOffset = 0x38U;
constexpr std::size_t kDefinitionRecordBytesOffset = 0x40U;
constexpr std::size_t kDefinitionTableOffset = 0x58U;
constexpr std::size_t kTransformTableOffset = 0x60U;
constexpr std::size_t kRenderBindingTableOffset = 0x68U;
constexpr std::size_t kActorBindingTableOffset = 0x70U;
constexpr std::size_t kPlayerBindingTableOffset = 0x78U;
constexpr std::size_t kKeyDataOffset = 0x80U;
constexpr std::size_t kHeaderReservedOffset = 0x88U;

constexpr std::size_t kFixtureDefinitionTable = 0xa0U;
constexpr std::size_t kFixtureTransformTable = 0x100U;
constexpr std::size_t kFixtureRenderBindingTable = 0x160U;
constexpr std::size_t kFixtureActorBindingTable = 0x170U;
constexpr std::size_t kFixturePlayerBindingTable = 0x1f0U;
constexpr std::size_t kFixtureKeyData = 0x200U;
constexpr std::size_t kFixtureTotalBytes = 605U;

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
  } catch (const openrc::EntitySceneIoError &) {
    return;
  }
  fail(message);
}

template <typename Function>
void expect_scene_error(Function &&function, const std::string &message) {
  try {
    std::forward<Function>(function)();
  } catch (const openrc::EntitySceneError &) {
    return;
  }
  fail(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_u32(
    const std::span<const std::byte> bytes, const std::size_t offset) {
  return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t read_u64(
    const std::span<const std::byte> bytes, const std::size_t offset) {
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

[[nodiscard]] openrc::ActorAffineTransformV1 identity_actor_transform() {
  openrc::ActorAffineTransformV1 result;
  result.values = {
      1.0F, 0.0F, 0.0F, -0.0F,
      0.0F, 1.0F, 0.0F, 0.0F,
      0.0F, 0.0F, 1.0F, 0.0F,
  };
  return result;
}

[[nodiscard]] openrc::EntitySceneV1 make_scene() {
  openrc::EntityDefinitionV1 prop;
  prop.authored_id = 900U;
  prop.archetype_key = "openrc.prop/crate";
  prop.flags = 0U;
  prop.authoring_group_id = 4U;

  openrc::EntityDefinitionV1 player;
  player.authored_id = 100U;
  player.archetype_key = "openrc.player/default";

  openrc::EntityDefinitionV1 npc;
  npc.authored_id = 7U;
  npc.archetype_key = "openrc.npc/vendor";

  openrc::EntityTransformComponentV1 prop_transform;
  prop_transform.authored_id = prop.authored_id;
  prop_transform.transform.position = {10.0F, -0.0F, 30.0F};
  prop_transform.transform.rotation = {0.0F, 0.0F, 0.0F, -1.0F};
  prop_transform.transform.scale = {1.0F, 2.0F, 1.0F};

  openrc::EntityTransformComponentV1 npc_transform;
  npc_transform.authored_id = npc.authored_id;
  npc_transform.transform.position = {-5.0F, 6.0F, 7.0F};
  npc_transform.transform.rotation = {-1.0F, -0.0F, 0.0F, -0.0F};

  openrc::EntityActorBindingV1 player_actor;
  player_actor.authored_id = player.authored_id;
  player_actor.model_key = "actors/player/default";
  player_actor.model_to_entity = identity_actor_transform();

  openrc::EntityActorBindingV1 npc_actor;
  npc_actor.authored_id = npc.authored_id;
  npc_actor.model_key = "actors/npc/vendor";
  npc_actor.model_to_entity = identity_actor_transform();

  openrc::EntitySceneV1 result;
  result.level_id = UINT32_C(0x12345678);
  result.definitions = {prop, player, npc};
  result.transforms = {prop_transform, npc_transform};
  result.render_bindings = {{prop.authored_id, 42U}};
  result.actor_bindings = {player_actor, npc_actor};
  result.player_bindings = {{player.authored_id, 0U}};
  return result;
}

void test_identity_layout_round_trip_and_determinism() {
  expect(openrc::kEntitySceneResourceIdV1 ==
                 std::string_view("world/entities") &&
             openrc::kEntitySceneResourceTypeIdV1 ==
                 std::string_view("openrc.entity-scene") &&
             openrc::kEntitySceneResourceSchemaVersionV1 == 1U,
         "EntitySceneV1 resource identity is wrong");

  const auto expected =
      openrc::canonicalize_entity_scene_v1(make_scene(), kLimits.scene);
  const auto bytes = openrc::encode_entity_scene_v1(make_scene(), kLimits);
  expect(bytes.size() == kFixtureTotalBytes,
         "EntitySceneV1 exact fixture size changed unexpectedly");
  expect(read_u32(bytes, kFormatVersionOffset) == 1U &&
             read_u32(bytes, kHeaderBytesOffset) == 0xa0U &&
             read_u64(bytes, kTotalBytesOffset) == bytes.size() &&
             read_u32(bytes, kPayloadTypeOffset) == 1U &&
             read_u32(bytes, kSchemaVersionOffset) == 1U &&
             read_u32(bytes, kLevelIdOffset) == UINT32_C(0x12345678),
         "EntitySceneV1 envelope fields are wrong");
  expect(byte_value(bytes[kLevelIdOffset]) == 0x78U &&
             byte_value(bytes[kLevelIdOffset + 1U]) == 0x56U &&
             byte_value(bytes[kLevelIdOffset + 2U]) == 0x34U &&
             byte_value(bytes[kLevelIdOffset + 3U]) == 0x12U,
         "EntitySceneV1 integers are not little-endian");
  expect(read_u32(bytes, kDefinitionCountOffset) == 3U &&
             read_u32(bytes, kTransformCountOffset) == 2U &&
             read_u32(bytes, kRenderBindingCountOffset) == 1U &&
             read_u32(bytes, kActorBindingCountOffset) == 2U &&
             read_u32(bytes, kPlayerBindingCountOffset) == 1U &&
             read_u64(bytes, kTotalKeyBytesOffset) == 93U,
         "EntitySceneV1 header counts are wrong");
  expect(read_u64(bytes, kDefinitionTableOffset) ==
                 kFixtureDefinitionTable &&
             read_u64(bytes, kTransformTableOffset) ==
                 kFixtureTransformTable &&
             read_u64(bytes, kRenderBindingTableOffset) ==
                 kFixtureRenderBindingTable &&
             read_u64(bytes, kActorBindingTableOffset) ==
                 kFixtureActorBindingTable &&
             read_u64(bytes, kPlayerBindingTableOffset) ==
                 kFixturePlayerBindingTable &&
             read_u64(bytes, kKeyDataOffset) == kFixtureKeyData,
         "EntitySceneV1 table offsets are not canonical and contiguous");
  expect(read_u32(bytes, kFixtureDefinitionTable) == 7U &&
             read_u32(bytes, kFixtureTransformTable) == 7U &&
             read_u32(bytes, kFixtureActorBindingTable) == 7U,
         "EntitySceneV1 sparse authored IDs were not sorted");

  const auto decoded = openrc::decode_entity_scene_v1(bytes, kLimits);
  expect(decoded == expected,
         "EntitySceneV1 canonical round trip changed logical content");
  expect(!std::signbit(decoded.transforms[0U].transform.rotation[3U]) &&
             decoded.transforms[0U].transform.rotation[0U] == 1.0F &&
             decoded.transforms[1U].transform.rotation[3U] == 1.0F &&
             !std::signbit(decoded.transforms[1U].transform.position[1U]) &&
             !std::signbit(
                 decoded.actor_bindings[0U].model_to_entity.values[3U]),
         "EntitySceneV1 writer did not canonicalize float or quaternion sign");
  expect(openrc::encode_entity_scene_v1(decoded, kLimits) == bytes,
         "EntitySceneV1 re-encoding is not byte deterministic");

  const openrc::EntitySceneV1 empty;
  const auto empty_bytes = openrc::encode_entity_scene_v1(empty, kLimits);
  expect(empty_bytes.size() == openrc::kEntitySceneIoHeaderBytesV1 &&
             openrc::decode_entity_scene_v1(empty_bytes, kLimits) == empty,
         "An empty EntitySceneV1 did not round-trip canonically");
}

void test_record_offsets_preserve_the_full_format_count_domain() {
  constexpr auto last_record_offset = [](const std::uint32_t record_bytes) {
    return static_cast<std::uint64_t>(UINT32_MAX) * record_bytes;
  };

  expect(last_record_offset(openrc::kEntitySceneIoDefinitionBytesV1) ==
                 UINT64_C(137438953440) &&
             last_record_offset(openrc::kEntitySceneIoTransformBytesV1) ==
                 UINT64_C(206158430160) &&
             last_record_offset(openrc::kEntitySceneIoRenderBindingBytesV1) ==
                 UINT64_C(68719476720) &&
             last_record_offset(openrc::kEntitySceneIoActorBindingBytesV1) ==
                 UINT64_C(274877906880) &&
             last_record_offset(openrc::kEntitySceneIoPlayerBindingBytesV1) ==
                 UINT64_C(68719476720),
         "EntitySceneV1 record-offset arithmetic discarded high bits");
}

template <typename Mutation>
void expect_corrupt_decode(Mutation &&mutation, const std::string &message) {
  auto bytes = openrc::encode_entity_scene_v1(make_scene(), kLimits);
  std::forward<Mutation>(mutation)(bytes);
  expect_io_error(
      [&] { static_cast<void>(openrc::decode_entity_scene_v1(bytes, kLimits)); },
      message);
}

void test_envelope_partitions_reserved_and_truncation() {
  expect_corrupt_decode(
      [](auto &bytes) { bytes[0U] ^= std::byte{1U}; },
      "EntitySceneV1 decoder accepted bad magic");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFormatVersionOffset, 2U); },
      "EntitySceneV1 decoder accepted an unknown format version");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kPayloadTypeOffset, 2U); },
      "EntitySceneV1 decoder accepted an unknown payload type");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kSchemaVersionOffset, 2U); },
      "EntitySceneV1 decoder accepted an unknown schema version");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kDefinitionRecordBytesOffset, 36U); },
      "EntitySceneV1 decoder accepted a wrong record width");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, kDefinitionTableOffset, 0U); },
      "EntitySceneV1 decoder accepted a non-canonical table offset");
  expect_corrupt_decode(
      [](auto &bytes) { write_u64(bytes, kTotalBytesOffset, bytes.size() - 1U); },
      "EntitySceneV1 decoder accepted a false total size");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[kHeaderReservedOffset] = std::byte{1U}; },
      "EntitySceneV1 decoder accepted non-zero header reserved data");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[kFixtureDefinitionTable + 24U] = std::byte{1U}; },
      "EntitySceneV1 decoder accepted definition reserved data");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[kFixtureTransformTable + 4U] = std::byte{1U}; },
      "EntitySceneV1 decoder accepted transform reserved data");
  expect_corrupt_decode(
      [](auto &bytes) {
        bytes[kFixtureRenderBindingTable + 8U] = std::byte{1U};
      },
      "EntitySceneV1 decoder accepted render-binding reserved data");
  expect_corrupt_decode(
      [](auto &bytes) {
        bytes[kFixturePlayerBindingTable + 8U] = std::byte{1U};
      },
      "EntitySceneV1 decoder accepted player-binding reserved data");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u64(bytes, kFixtureDefinitionTable + 16U,
                  kFixtureKeyData + 1U);
      },
      "EntitySceneV1 decoder accepted a gapped key partition");

  const auto bytes = openrc::encode_entity_scene_v1(make_scene(), kLimits);
  for (std::size_t length = 0U; length < bytes.size(); ++length) {
    expect_io_error(
        [&] {
          static_cast<void>(openrc::decode_entity_scene_v1(
              std::span<const std::byte>(bytes.data(), length), kLimits));
        },
        "EntitySceneV1 decoder accepted a truncated prefix");
  }
  auto trailing = bytes;
  trailing.push_back(std::byte{0U});
  expect_io_error(
      [&] {
        static_cast<void>(openrc::decode_entity_scene_v1(trailing, kLimits));
      },
      "EntitySceneV1 decoder accepted trailing data");
}

void test_decoder_semantic_and_float_rejections() {
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureDefinitionTable + 4U, 2U); },
      "EntitySceneV1 decoder accepted unknown definition flags");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes,
                  kFixtureDefinitionTable +
                      openrc::kEntitySceneIoDefinitionBytesV1,
                  7U);
      },
      "EntitySceneV1 decoder accepted duplicate definition IDs");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureTransformTable, 6U); },
      "EntitySceneV1 decoder accepted a transform with a bad reference");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureRenderBindingTable, 8U); },
      "EntitySceneV1 decoder accepted a render binding with a bad reference");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixtureActorBindingTable, 8U); },
      "EntitySceneV1 decoder accepted an actor binding with a bad reference");
  expect_corrupt_decode(
      [](auto &bytes) { write_u32(bytes, kFixturePlayerBindingTable, 8U); },
      "EntitySceneV1 decoder accepted a player binding with a bad reference");
  expect_corrupt_decode(
      [](auto &bytes) { bytes[kFixtureKeyData] = std::byte{'O'}; },
      "EntitySceneV1 decoder accepted a non-canonical semantic key");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureTransformTable + 8U,
                  UINT32_C(0x80000000));
      },
      "EntitySceneV1 decoder accepted negative zero");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureTransformTable + 8U,
                  UINT32_C(0x7fc00000));
      },
      "EntitySceneV1 decoder accepted NaN");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureActorBindingTable + 16U,
                  UINT32_C(0x7f800000));
      },
      "EntitySceneV1 decoder accepted infinity in an actor transform");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureTransformTable + 36U, 0U);
      },
      "EntitySceneV1 decoder accepted zero scale");
  expect_corrupt_decode(
      [](auto &bytes) {
        for (std::size_t component = 0U; component < 4U; ++component) {
          write_u32(bytes, kFixtureTransformTable + 20U + component * 4U, 0U);
        }
      },
      "EntitySceneV1 decoder accepted a zero quaternion");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureTransformTable + 20U,
                  std::bit_cast<std::uint32_t>(2.0F));
      },
      "EntitySceneV1 decoder accepted a non-unit quaternion");
  expect_corrupt_decode(
      [](auto &bytes) {
        write_u32(bytes, kFixtureTransformTable + 20U,
                  std::bit_cast<std::uint32_t>(-1.0F));
      },
      "EntitySceneV1 decoder accepted a non-canonical quaternion sign");
}

template <typename Mutation>
void expect_invalid_encode(Mutation &&mutation, const std::string &message) {
  auto scene = make_scene();
  std::forward<Mutation>(mutation)(scene);
  expect_io_error(
      [&] { static_cast<void>(openrc::encode_entity_scene_v1(scene, kLimits)); },
      message);
}

void test_model_relationship_and_key_rejections() {
  expect_scene_error(
      [&] { openrc::validate_entity_scene_v1(make_scene(), kLimits.scene); },
      "EntitySceneV1 validator accepted non-canonical table order");
  expect_invalid_encode(
      [](auto &scene) { scene.schema_version = 2U; },
      "EntitySceneV1 writer accepted an unknown schema version");
  expect_invalid_encode(
      [](auto &scene) {
        scene.definitions.push_back(scene.definitions.front());
      },
      "EntitySceneV1 writer accepted duplicate definitions");
  expect_invalid_encode(
      [](auto &scene) { scene.transforms.push_back(scene.transforms.front()); },
      "EntitySceneV1 writer accepted duplicate transforms");
  expect_invalid_encode(
      [](auto &scene) {
        scene.render_bindings.push_back(scene.render_bindings.front());
      },
      "EntitySceneV1 writer accepted duplicate render bindings");
  expect_invalid_encode(
      [](auto &scene) {
        scene.actor_bindings.push_back(scene.actor_bindings.front());
      },
      "EntitySceneV1 writer accepted duplicate actor bindings");
  expect_invalid_encode(
      [](auto &scene) {
        scene.player_bindings.push_back(scene.player_bindings.front());
      },
      "EntitySceneV1 writer accepted duplicate player bindings");
  expect_invalid_encode(
      [](auto &scene) { scene.transforms.front().authored_id = 123U; },
      "EntitySceneV1 writer accepted a missing transform reference");
  expect_invalid_encode(
      [](auto &scene) { scene.render_bindings.front().authored_id = 123U; },
      "EntitySceneV1 writer accepted a missing render reference");
  expect_invalid_encode(
      [](auto &scene) {
        scene.render_bindings.front().render_instance_id = UINT32_MAX;
      },
      "EntitySceneV1 writer accepted an absent render-instance sentinel");
  expect_invalid_encode(
      [](auto &scene) { scene.actor_bindings.front().authored_id = 123U; },
      "EntitySceneV1 writer accepted a missing actor reference");
  expect_invalid_encode(
      [](auto &scene) { scene.player_bindings.front().authored_id = 123U; },
      "EntitySceneV1 writer accepted a missing player reference");
  expect_invalid_encode(
      [](auto &scene) { scene.transforms.pop_back(); },
      "EntitySceneV1 writer accepted a non-player without a transform");
  expect_invalid_encode(
      [](auto &scene) {
        openrc::EntityTransformComponentV1 component;
        component.authored_id = 100U;
        scene.transforms.push_back(component);
      },
      "EntitySceneV1 writer accepted a transform on a player entity");
  expect_invalid_encode(
      [](auto &scene) { scene.actor_bindings.front().authored_id = 900U; },
      "EntitySceneV1 writer accepted both actor and render bindings");
  expect_invalid_encode(
      [](auto &scene) {
        openrc::EntityDefinitionV1 second_player;
        second_player.authored_id = 101U;
        second_player.archetype_key = "openrc.player/second";
        scene.definitions.push_back(second_player);
        scene.player_bindings.push_back({101U, 0U});
      },
      "EntitySceneV1 writer accepted a duplicate local-player slot");
  expect_invalid_encode(
      [](auto &scene) { scene.definitions.front().flags = 2U; },
      "EntitySceneV1 writer accepted unknown definition flags");
  expect_invalid_encode(
      [](auto &scene) {
        scene.definitions.front().archetype_key = "OpenRC.Bad";
      },
      "EntitySceneV1 writer accepted a non-canonical archetype key");
  expect_invalid_encode(
      [](auto &scene) { scene.actor_bindings.front().model_key = "actors/../x"; },
      "EntitySceneV1 writer accepted an unsafe actor model key");
  expect_invalid_encode(
      [](auto &scene) {
        scene.transforms.front().transform.position[0U] =
            std::numeric_limits<float>::quiet_NaN();
      },
      "EntitySceneV1 writer accepted a non-finite world transform");
  expect_invalid_encode(
      [](auto &scene) {
        scene.actor_bindings.front().model_to_entity.values[0U] =
            std::numeric_limits<float>::infinity();
      },
      "EntitySceneV1 writer accepted a non-finite actor transform");
  expect_invalid_encode(
      [](auto &scene) { scene.transforms.front().transform.rotation = {}; },
      "EntitySceneV1 writer accepted a zero quaternion");
  expect_invalid_encode(
      [](auto &scene) { scene.transforms.front().transform.scale[0U] = 0.0F; },
      "EntitySceneV1 writer accepted zero scale");
}

void test_limits_are_enforced_before_allocation() {
  const auto bytes = openrc::encode_entity_scene_v1(make_scene(), kLimits);

  auto byte_limited = kLimits;
  byte_limited.max_encoded_bytes = bytes.size() - 1U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::encode_entity_scene_v1(make_scene(), byte_limited));
      },
      "EntitySceneV1 writer ignored its byte limit");
  expect_io_error(
      [&] {
        static_cast<void>(openrc::decode_entity_scene_v1(bytes, byte_limited));
      },
      "EntitySceneV1 decoder ignored its byte limit");

  auto count_limited = kLimits;
  count_limited.scene.max_definitions = 2U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_entity_scene_v1(bytes, count_limited));
      },
      "EntitySceneV1 decoder ignored its definition limit");
  count_limited = kLimits;
  count_limited.scene.max_transforms = 1U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::encode_entity_scene_v1(make_scene(), count_limited));
      },
      "EntitySceneV1 writer ignored its transform limit");
  count_limited = kLimits;
  count_limited.scene.max_render_bindings = 0U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_entity_scene_v1(bytes, count_limited));
      },
      "EntitySceneV1 decoder accepted a zero table limit");

  auto archetype_limited = kLimits;
  archetype_limited.scene.max_archetype_key_bytes = 16U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_entity_scene_v1(bytes, archetype_limited));
      },
      "EntitySceneV1 decoder ignored its per-archetype-key limit");
  auto model_limited = kLimits;
  model_limited.scene.max_model_key_bytes = 16U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::encode_entity_scene_v1(make_scene(), model_limited));
      },
      "EntitySceneV1 writer ignored its per-model-key limit");
  auto aggregate_limited = kLimits;
  aggregate_limited.scene.max_total_key_bytes = 92U;
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_entity_scene_v1(bytes, aggregate_limited));
      },
      "EntitySceneV1 decoder ignored its aggregate key-byte limit");

  expect_io_error(
      [&] {
        static_cast<void>(openrc::decode_entity_scene_v1(
            bytes, openrc::EntitySceneIoLimitsV1{}));
      },
      "EntitySceneV1 decoder accepted absent caller limits");

  auto excessive_count = bytes;
  write_u32(excessive_count, kDefinitionCountOffset,
            kLimits.scene.max_definitions + 1U);
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_entity_scene_v1(excessive_count, kLimits));
      },
      "EntitySceneV1 decoder did not reject a count limit from the header");
  auto excessive_keys = bytes;
  write_u64(excessive_keys, kTotalKeyBytesOffset,
            kLimits.scene.max_total_key_bytes + 1U);
  expect_io_error(
      [&] {
        static_cast<void>(
            openrc::decode_entity_scene_v1(excessive_keys, kLimits));
      },
      "EntitySceneV1 decoder did not reject aggregate key bytes from the header");
}

} // namespace

int main() {
  try {
    test_identity_layout_round_trip_and_determinism();
    test_record_offsets_preserve_the_full_format_count_domain();
    test_envelope_partitions_reserved_and_truncation();
    test_decoder_semantic_and_float_rejections();
    test_model_relationship_and_key_rejections();
    test_limits_are_enforced_before_allocation();
    std::cout << "EntitySceneV1 tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "EntitySceneV1 tests failed: " << error.what() << '\n';
    return 1;
  }
}
