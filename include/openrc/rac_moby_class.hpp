#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kRacMobyClassHeaderBytesV1 = 0x48U;
inline constexpr std::uint32_t kRacMobyPacketEntryBytesV1 = 0x10U;
inline constexpr std::uint32_t kRacMobySoundDefinitionBytesV1 = 0x20U;
inline constexpr std::uint32_t kRacMobyDataAlignmentV1 = 0x10U;

enum class RacMobyPacketKindV1 : std::uint8_t {
  high_lod,
  low_lod,
  metal,
};

struct RacMobyClassLimitsV1 {
  std::uint64_t max_input_bytes = 0U;
  // Standalone companion-bank WADs observed on the reference image use
  // 0xff at header byte 0x0b. Local level-core classes legitimately use
  // other values, so only context-specific probes should require it.
  bool require_shared_bank_byte_b_ff = false;
};

struct RacMobyClassRangeV1 {
  std::uint64_t offset = 0U;
  std::uint64_t size = 0U;

  [[nodiscard]] bool operator==(const RacMobyClassRangeV1 &) const = default;
};

struct RacMobyPacketV1 {
  RacMobyPacketKindV1 kind = RacMobyPacketKindV1::high_lod;
  RacMobyClassRangeV1 table_entry_range;
  RacMobyClassRangeV1 vif_range;
  RacMobyClassRangeV1 vertex_range;
  std::uint16_t texture_unpack_offset_qwords = 0U;
  std::uint8_t vertex_data_qwords = 0U;
  std::uint8_t transfer_vertex_count = 0U;
};

struct RacMobyClassV1 {
  std::uint64_t input_bytes = 0U;
  RacMobyClassRangeV1 header_range;
  RacMobyClassRangeV1 sequence_offset_table_range;
  RacMobyClassRangeV1 packet_table_range;

  std::uint32_t packet_table_offset = 0U;
  std::uint8_t high_lod_packet_count = 0U;
  std::uint8_t low_lod_packet_count = 0U;
  std::uint8_t metal_packet_count = 0U;
  std::uint8_t metal_packet_begin = 0U;
  std::uint8_t joint_count = 0U;
  std::uint8_t unknown_09 = 0U;
  std::uint8_t rac1_byte_a = 0U;
  std::uint8_t rac12_format_byte = 0U;
  std::uint8_t sequence_count = 0U;
  std::uint8_t sound_count = 0U;
  std::uint8_t lod_transition = 0U;
  std::uint8_t shadow_qwords = 0U;

  std::uint32_t collision_offset = 0U;
  std::uint32_t skeleton_offset = 0U;
  std::uint32_t common_translation_offset = 0U;
  std::uint32_t joint_metadata_offset = 0U;
  std::uint32_t gif_usage_offset = 0U;
  std::uint32_t scale_bits = 0U;
  float scale = 0.0F;
  std::uint32_t sound_definitions_offset = 0U;
  std::uint8_t bangles_offset_qwords = 0U;
  std::uint8_t mip_distance = 0U;
  std::uint16_t rac1_short_2e = 0U;
  std::array<std::uint32_t, 4> bounding_sphere_bits{};
  std::array<float, 4> bounding_sphere{};
  std::uint32_t glow_rgba = 0U;
  std::uint16_t mode_bits = 0U;
  std::uint8_t type = 0U;
  std::uint8_t mode_bits_2 = 0U;

  RacMobyClassRangeV1 shadow_range;
  RacMobyClassRangeV1 skeleton_range;
  RacMobyClassRangeV1 common_translation_range;
  RacMobyClassRangeV1 sound_definitions_range;
  std::vector<std::uint32_t> sequence_offsets;
  std::vector<RacMobyPacketV1> packets;
};

class RacMobyClassError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Parses one complete decoded RAC1 MobyClass core. The fixed 0x48-byte
// header, animation-offset directory, mesh packet table, and every referenced
// VIF/vertex envelope are validated. Packet bodies remain borrowed ranges for
// the later geometry decoder.
[[nodiscard]] RacMobyClassV1
parse_rac_moby_class_v1(std::span<const std::byte> bytes,
                        RacMobyClassLimitsV1 limits);

} // namespace openrc
