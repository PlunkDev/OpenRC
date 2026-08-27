#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kSceneBlockVifCodeSize = 4U;
inline constexpr std::uint32_t kSceneBlockVifPayloadAlignment = 4U;
inline constexpr std::uint16_t kSceneBlockVifMaximumVectorCount = 256U;
// The contiguous leading remainder sections [0, 5) form one VIF stream.
inline constexpr std::size_t kSceneBlockVifSectionCount = 5U;

enum class SceneBlockVifOpcode : std::uint8_t {
    nop = 0x00,
    stcycl = 0x01,
    stmod = 0x05,
    strow = 0x30,
    unpack_v3_16 = 0x69,
    unpack_v4_32 = 0x6c,
    unpack_v4_16 = 0x6d,
    unpack_v4_8 = 0x6e,
};

struct SceneBlockVifLimits {
    std::uint64_t max_input_bytes = 0;
    std::uint64_t max_commands = 0;
    // Counts each command's complete stored payload range, including zero
    // alignment bytes after its logical data.
    std::uint64_t max_payload_bytes = 0;
};

struct SceneBlockVifCycleStateV1 {
    // CL retains its raw eight-bit value, including zero. WL uses the
    // effective value 256 for a raw zero STCYCL field.
    std::uint16_t cycle_length = 1;
    std::uint16_t write_length = 1;

    [[nodiscard]] bool
    operator==(const SceneBlockVifCycleStateV1&) const = default;
};

struct SceneBlockVifRange {
    // Offsets are relative to the beginning of the passed VIF stream.
    std::uint64_t offset = 0;
    std::uint64_t size = 0;

    [[nodiscard]] bool operator==(const SceneBlockVifRange&) const = default;
};

struct SceneBlockVifCommandV1 {
    SceneBlockVifRange code_range;
    // payload_range includes any required zero bytes that align the next
    // VIFcode to four bytes. packet_range covers code_range and payload_range.
    SceneBlockVifRange payload_range;
    SceneBlockVifRange packet_range;

    std::uint32_t raw_code = 0;
    SceneBlockVifOpcode opcode = SceneBlockVifOpcode::nop;
    std::uint8_t raw_num = 0;
    std::uint16_t immediate = 0;

    // UNPACK-only fields. A raw NUM of zero expands to 256 output vectors.
    // input_vector_count also accounts for the current STCYCL fill mode.
    // Non-UNPACK commands leave these fields at zero/false.
    std::uint16_t output_vector_count = 0;
    std::uint16_t input_vector_count = 0;
    std::uint8_t component_count = 0;
    std::uint8_t component_bits = 0;
    std::uint16_t destination_address = 0;
    bool unsigned_data = false;
    bool use_tops = false;

    // Logical data excludes the zero alignment suffix retained by
    // payload_range.
    std::uint64_t payload_data_bytes = 0;
    std::uint8_t padding_bytes = 0;
};

struct SceneBlockVifStreamV1 {
    std::uint64_t input_bytes = 0;
    std::uint64_t total_payload_bytes = 0;
    std::uint64_t total_padding_bytes = 0;
    std::uint16_t final_cycle_length = 1;
    std::uint16_t final_write_length = 1;
    std::vector<SceneBlockVifCommandV1> commands;
    // Appended for source compatibility with positional V1 aggregate users.
    std::uint16_t initial_cycle_length = 1;
    std::uint16_t initial_write_length = 1;
};

class SceneBlockVifError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses exactly one bounded VIF stream spanning scene-block remainder
// sections 0-4. The returned report contains only metadata and relative
// ranges: it neither copies payload bytes nor stores pointers into input.
[[nodiscard]] SceneBlockVifStreamV1 parse_scene_block_vif_stream_v1(
    std::span<const std::byte> bytes,
    SceneBlockVifLimits limits);

// Parses a continuation whose first UNPACK observes inherited STCYCL state.
[[nodiscard]] SceneBlockVifStreamV1 parse_scene_block_vif_stream_v1(
    std::span<const std::byte> bytes,
    SceneBlockVifCycleStateV1 initial_cycle,
    SceneBlockVifLimits limits);

} // namespace openrc
