#include "openrc/scene_block_vu.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint64_t kVuAddressMask =
    static_cast<std::uint64_t>(kSceneBlockVuMemoryQwordCount) - 1U;
static_assert(
    (kSceneBlockVuMemoryQwordCount & kVuAddressMask) == 0U,
    "The SceneBlock VU memory qword count must be a power of two");

[[noreturn]] void fail(const std::string& message) {
    throw SceneBlockVuError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t read_le16(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(byte_value(bytes[offset])) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(byte_value(bytes[offset + 1U]))
            << 8U);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left * right;
}

[[nodiscard]] std::size_t checked_host_size(
    const std::uint64_t value,
    const char* description) {
    if (value > std::numeric_limits<std::size_t>::max()) {
        fail(std::string(description) + " exceeds the host address space");
    }
    return static_cast<std::size_t>(value);
}

void validate_range(
    const std::span<const std::byte> bytes,
    const SceneBlockVifRange range,
    const char* description) {
    const auto input_bytes = static_cast<std::uint64_t>(bytes.size());
    if (range.offset > input_bytes ||
        range.size > input_bytes - range.offset) {
        fail(std::string(description) + " exceeds the bounded VIF stream");
    }
    (void)checked_host_size(range.offset, description);
    (void)checked_host_size(range.size, description);
}

[[nodiscard]] SceneBlockVuValueV1 known_value(
    const std::uint32_t bits) noexcept {
    return SceneBlockVuValueV1{SceneBlockVuValueState::known, bits};
}

[[nodiscard]] SceneBlockVuValueV1 add_values(
    const SceneBlockVuValueV1 left,
    const SceneBlockVuValueV1 right) noexcept {
    if (left.state != SceneBlockVuValueState::known ||
        right.state != SceneBlockVuValueState::known) {
        return {};
    }
    // Unsigned addition gives the VIF's lane-wise modulo-2^32 result.
    return known_value(left.bits + right.bits);
}

[[nodiscard]] std::uint16_t effective_num(
    const std::uint8_t raw_num) noexcept {
    return raw_num == 0U
        ? kSceneBlockVifMaximumVectorCount
        : raw_num;
}

[[nodiscard]] std::uint16_t effective_write_length(
    const std::uint8_t raw_write_length) noexcept {
    return raw_write_length == 0U
        ? kSceneBlockVifMaximumVectorCount
        : raw_write_length;
}

[[nodiscard]] std::uint16_t expected_input_vectors(
    const std::uint16_t output_vectors,
    const std::uint16_t cycle_length,
    const std::uint16_t write_length) {
    if (write_length <= cycle_length) {
        return output_vectors;
    }

    const auto complete_blocks =
        static_cast<std::uint64_t>(output_vectors) / write_length;
    const auto complete_input = checked_multiply(
        complete_blocks,
        cycle_length,
        "a SceneBlock VU fill-mode input count");
    const auto partial_input = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(output_vectors) % write_length,
        cycle_length);
    const auto total = checked_add(
        complete_input,
        partial_input,
        "a SceneBlock VU input count");
    if (total > kSceneBlockVifMaximumVectorCount) {
        fail("A SceneBlock VU input count exceeds 256 vectors");
    }
    return static_cast<std::uint16_t>(total);
}

[[nodiscard]] bool is_unpack(const SceneBlockVifOpcode opcode) noexcept {
    switch (opcode) {
    case SceneBlockVifOpcode::unpack_v3_16:
    case SceneBlockVifOpcode::unpack_v4_32:
    case SceneBlockVifOpcode::unpack_v4_16:
    case SceneBlockVifOpcode::unpack_v4_8:
        return true;
    default:
        return false;
    }
}

void validate_command_ranges(
    const std::span<const std::byte> bytes,
    const SceneBlockVifCommandV1& command) {
    validate_range(bytes, command.code_range, "A SceneBlock VU code range");
    validate_range(
        bytes,
        command.payload_range,
        "A SceneBlock VU payload range");
    validate_range(
        bytes,
        command.packet_range,
        "A SceneBlock VU packet range");

    if (command.code_range.size != kSceneBlockVifCodeSize) {
        fail("A SceneBlock VU command has an invalid VIFcode range size");
    }
    const auto code_end = checked_add(
        command.code_range.offset,
        command.code_range.size,
        "a SceneBlock VU VIFcode end");
    const auto payload_end = checked_add(
        command.payload_range.offset,
        command.payload_range.size,
        "a SceneBlock VU payload end");
    const auto packet_end = checked_add(
        command.packet_range.offset,
        command.packet_range.size,
        "a SceneBlock VU packet end");
    if (command.packet_range.offset != command.code_range.offset ||
        command.payload_range.offset != code_end ||
        packet_end != payload_end) {
        fail("A SceneBlock VU command has inconsistent packet ranges");
    }
    if (command.payload_data_bytes > command.payload_range.size ||
        command.padding_bytes !=
            command.payload_range.size - command.payload_data_bytes) {
        fail("A SceneBlock VU command has inconsistent payload metadata");
    }

    const auto host_code_offset = checked_host_size(
        command.code_range.offset,
        "A SceneBlock VU VIFcode offset");
    if (read_le32(bytes, host_code_offset) != command.raw_code) {
        fail("A SceneBlock VU VIFcode does not match its parsed metadata");
    }
}

[[nodiscard]] std::uint64_t vector_bytes(
    const SceneBlockVifCommandV1& command) {
    const auto vector_bits = checked_multiply(
        command.component_count,
        command.component_bits,
        "a SceneBlock VU input vector width");
    if ((vector_bits & 7U) != 0U) {
        fail("A SceneBlock VU input vector is not byte-aligned");
    }
    return vector_bits / 8U;
}

void validate_unpack_metadata(
    const SceneBlockVifCommandV1& command,
    const SceneBlockVuStateV1& state) {
    if (command.output_vector_count != effective_num(command.raw_num)) {
        fail("A SceneBlock VU UNPACK has inconsistent output metadata");
    }
    if (state.write_length == 0U ||
        state.write_length > kSceneBlockVifMaximumVectorCount ||
        state.cycle_length >
            static_cast<std::uint16_t>(
                std::numeric_limits<std::uint8_t>::max())) {
        fail("SceneBlock VU cycle state is outside the VIF register domain");
    }

    const auto expected_input = expected_input_vectors(
        command.output_vector_count,
        state.cycle_length,
        state.write_length);
    if (command.input_vector_count != expected_input) {
        fail("A SceneBlock VU UNPACK disagrees with the active STCYCL state");
    }

    switch (command.opcode) {
    case SceneBlockVifOpcode::unpack_v3_16:
        if (command.component_count != 3U ||
            command.component_bits != 16U) {
            fail("A SceneBlock VU V3-16 UNPACK has invalid format metadata");
        }
        break;
    case SceneBlockVifOpcode::unpack_v4_32:
        if (command.component_count != 4U ||
            command.component_bits != 32U) {
            fail("A SceneBlock VU V4-32 UNPACK has invalid format metadata");
        }
        break;
    case SceneBlockVifOpcode::unpack_v4_16:
        if (command.component_count != 4U ||
            command.component_bits != 16U) {
            fail("A SceneBlock VU V4-16 UNPACK has invalid format metadata");
        }
        break;
    case SceneBlockVifOpcode::unpack_v4_8:
        if (command.component_count != 4U ||
            command.component_bits != 8U) {
            fail("A SceneBlock VU V4-8 UNPACK has invalid format metadata");
        }
        break;
    default:
        fail("Internal SceneBlock VU UNPACK opcode mismatch");
    }

    const auto expected_payload = checked_multiply(
        command.input_vector_count,
        vector_bytes(command),
        "a SceneBlock VU UNPACK payload size");
    if (command.payload_data_bytes != expected_payload) {
        fail("A SceneBlock VU UNPACK has inconsistent logical payload size");
    }
}

[[nodiscard]] SceneBlockVuValueV1 read_component(
    const std::span<const std::byte> bytes,
    const SceneBlockVifRange source_range,
    const std::uint8_t component_bits,
    const bool unsigned_data) {
    validate_range(
        bytes,
        source_range,
        "A SceneBlock VU component source range");
    const auto offset = checked_host_size(
        source_range.offset,
        "A SceneBlock VU component source offset");

    switch (component_bits) {
    case 8U: {
        if (source_range.size != 1U) {
            fail("A SceneBlock VU 8-bit component has an invalid source size");
        }
        std::uint32_t value = byte_value(bytes[offset]);
        if (!unsigned_data && (value & 0x80U) != 0U) {
            value |= 0xffffff00U;
        }
        return known_value(value);
    }
    case 16U: {
        if (source_range.size != 2U) {
            fail("A SceneBlock VU 16-bit component has an invalid source size");
        }
        std::uint32_t value = read_le16(bytes, offset);
        if (!unsigned_data && (value & 0x8000U) != 0U) {
            value |= 0xffff0000U;
        }
        return known_value(value);
    }
    case 32U:
        if (source_range.size != 4U) {
            fail("A SceneBlock VU 32-bit component has an invalid source size");
        }
        return known_value(read_le32(bytes, offset));
    default:
        fail("A SceneBlock VU component uses an unsupported bit width");
    }
}

[[nodiscard]] std::uint64_t source_component_bytes(
    const SceneBlockVifCommandV1& command) {
    if (command.component_bits != 8U &&
        command.component_bits != 16U &&
        command.component_bits != 32U) {
        fail("A SceneBlock VU component uses an unsupported bit width");
    }
    return command.component_bits / 8U;
}

[[nodiscard]] std::uint64_t input_vector_index_for_output(
    const std::uint64_t output_index,
    const SceneBlockVuStateV1& state,
    const bool fill_mode) {
    if (!fill_mode) {
        return output_index;
    }
    const auto complete_blocks = output_index / state.write_length;
    const auto position_in_block = output_index % state.write_length;
    return checked_add(
        checked_multiply(
            complete_blocks,
            state.cycle_length,
            "a SceneBlock VU fill-mode input vector index"),
        position_in_block,
        "a SceneBlock VU input vector index");
}

[[nodiscard]] std::uint64_t destination_progression(
    const std::uint64_t output_index,
    const SceneBlockVuStateV1& state,
    const bool fill_mode) {
    if (fill_mode) {
        return output_index;
    }
    return checked_add(
        checked_multiply(
            output_index / state.write_length,
            state.cycle_length,
            "a SceneBlock VU skipping-write destination"),
        output_index % state.write_length,
        "a SceneBlock VU destination progression");
}

void execute_strow(
    const std::span<const std::byte> bytes,
    const SceneBlockVifCommandV1& command,
    SceneBlockVuStateV1& state) {
    if (command.payload_data_bytes != 16U) {
        fail("A SceneBlock VU STROW has an invalid logical payload size");
    }
    const auto logical_end = checked_add(
        command.payload_range.offset,
        command.payload_data_bytes,
        "a SceneBlock VU STROW payload end");
    if (logical_end >
        checked_add(
            command.payload_range.offset,
            command.payload_range.size,
            "a SceneBlock VU STROW stored payload end")) {
        fail("A SceneBlock VU STROW logical payload exceeds its stored range");
    }

    for (std::size_t lane = 0; lane < state.row.size(); ++lane) {
        const auto lane_offset = checked_add(
            command.payload_range.offset,
            checked_multiply(
                lane,
                sizeof(std::uint32_t),
                "a SceneBlock VU STROW lane offset"),
            "a SceneBlock VU STROW source offset");
        const SceneBlockVifRange source{
            lane_offset,
            sizeof(std::uint32_t)};
        validate_range(bytes, source, "A SceneBlock VU STROW lane");
        state.row[lane] = known_value(read_le32(
            bytes,
            checked_host_size(
                source.offset,
                "A SceneBlock VU STROW source offset")));
    }
}

void apply_addition(
    SceneBlockVuLaneWriteV1& lane,
    SceneBlockVuStateV1& state,
    const std::size_t lane_index) {
    lane.row_before = state.row[lane_index];
    switch (state.addition_mode) {
    case SceneBlockVuAdditionMode::normal:
        lane.written_value = lane.unpacked_value;
        break;
    case SceneBlockVuAdditionMode::offset:
        lane.written_value = add_values(
            lane.unpacked_value,
            lane.row_before);
        break;
    case SceneBlockVuAdditionMode::difference:
        lane.written_value = add_values(
            lane.unpacked_value,
            lane.row_before);
        state.row[lane_index] = lane.written_value;
        break;
    default:
        fail("SceneBlock VU state contains an unsupported addition mode");
    }
}

void execute_unpack(
    const std::span<const std::byte> bytes,
    const SceneBlockVifCommandV1& command,
    const std::uint64_t command_index,
    SceneBlockVuSnapshotV1& result,
    SceneBlockVuStateV1& state) {
    validate_unpack_metadata(command, state);

    const bool fill_mode = state.cycle_length < state.write_length;
    const auto input_bytes_per_vector = vector_bytes(command);
    const auto component_bytes = source_component_bytes(command);
    const auto logical_payload_end = checked_add(
        command.payload_range.offset,
        command.payload_data_bytes,
        "a SceneBlock VU logical UNPACK payload end");

    std::uint64_t base_address = command.destination_address;
    if (command.use_tops) {
        base_address = checked_add(
            base_address,
            state.tops_qword,
            "a SceneBlock VU TOPS-relative destination");
    }

    for (std::uint32_t output = 0;
         output < command.output_vector_count;
         ++output) {
        const auto output_index = static_cast<std::uint64_t>(output);
        const auto position_in_block =
            output_index % state.write_length;
        const bool consumes_input =
            !fill_mode || position_in_block < state.cycle_length;

        SceneBlockVuVectorWriteV1 write;
        write.command_index = command_index;
        write.output_vector_index = static_cast<std::uint16_t>(output);
        write.addition_mode = state.addition_mode;

        std::uint64_t input_index = 0U;
        std::uint64_t input_offset = 0U;
        if (consumes_input) {
            input_index = input_vector_index_for_output(
                output_index,
                state,
                fill_mode);
            if (input_index >= command.input_vector_count) {
                fail("A SceneBlock VU UNPACK input vector index is out of range");
            }
            write.input_vector_index =
                static_cast<std::uint16_t>(input_index);
            input_offset = checked_add(
                command.payload_range.offset,
                checked_multiply(
                    input_index,
                    input_bytes_per_vector,
                    "a SceneBlock VU input vector byte offset"),
                "a SceneBlock VU input vector source");
            const auto input_end = checked_add(
                input_offset,
                input_bytes_per_vector,
                "a SceneBlock VU input vector end");
            if (input_end > logical_payload_end) {
                fail("A SceneBlock VU input vector exceeds the logical payload");
            }
        }

        const auto progression = destination_progression(
            output_index,
            state,
            fill_mode);
        write.unwrapped_destination_qword = checked_add(
            base_address,
            progression,
            "a SceneBlock VU unwrapped destination");
        write.destination_qword = static_cast<std::uint16_t>(
            write.unwrapped_destination_qword & kVuAddressMask);
        write.wrapped =
            write.unwrapped_destination_qword >=
            kSceneBlockVuMemoryQwordCount;

        for (std::size_t lane_index = 0;
             lane_index < write.lanes.size();
             ++lane_index) {
            auto& lane = write.lanes[lane_index];
            if (!consumes_input) {
                lane.source =
                    SceneBlockVuLaneSource::cycle_fill_indeterminate;
            } else if (
                command.opcode == SceneBlockVifOpcode::unpack_v3_16 &&
                lane_index == 3U) {
                lane.source =
                    SceneBlockVuLaneSource::v3_w_indeterminate;
            } else {
                if (lane_index >= command.component_count) {
                    fail("A SceneBlock VU lane exceeds its UNPACK component count");
                }
                lane.source = SceneBlockVuLaneSource::payload;
                lane.source_range = SceneBlockVifRange{
                    checked_add(
                        input_offset,
                        checked_multiply(
                            lane_index,
                            component_bytes,
                            "a SceneBlock VU component offset"),
                        "a SceneBlock VU component source"),
                    component_bytes};
                const auto component_end = checked_add(
                    lane.source_range.offset,
                    lane.source_range.size,
                    "a SceneBlock VU component end");
                if (component_end > logical_payload_end) {
                    fail("A SceneBlock VU component exceeds the logical payload");
                }
                lane.unpacked_value = read_component(
                    bytes,
                    lane.source_range,
                    command.component_bits,
                    command.unsigned_data);
            }
            apply_addition(lane, state, lane_index);
        }

        const auto destination =
            static_cast<std::size_t>(write.destination_qword);
        auto& memory_qword = result.memory[destination];
        if (memory_qword.write_count == 0U) {
            result.unique_qword_writes = checked_add(
                result.unique_qword_writes,
                1U,
                "the SceneBlock VU unique-write count");
        } else {
            result.overwrite_vector_writes = checked_add(
                result.overwrite_vector_writes,
                1U,
                "the SceneBlock VU overwrite count");
        }
        if (write.wrapped) {
            result.wrapped_vector_writes = checked_add(
                result.wrapped_vector_writes,
                1U,
                "the SceneBlock VU wrapped-write count");
        }

        const auto write_index =
            static_cast<std::uint64_t>(result.writes.size());
        for (std::size_t lane = 0;
             lane < memory_qword.lanes.size();
             ++lane) {
            memory_qword.lanes[lane] = write.lanes[lane].written_value;
        }
        memory_qword.write_count = checked_add(
            memory_qword.write_count,
            1U,
            "a SceneBlock VU qword write count");
        memory_qword.last_write_index = write_index;
        result.writes.push_back(std::move(write));
    }
}

[[nodiscard]] std::uint64_t preflight_write_count(
    const SceneBlockVifStreamV1& stream,
    const SceneBlockVuLimits limits,
    const std::size_t host_max_writes) {
    std::uint64_t total = 0U;
    for (const auto& command : stream.commands) {
        if (!is_unpack(command.opcode)) {
            continue;
        }
        total = checked_add(
            total,
            command.output_vector_count,
            "the SceneBlock VU vector-write count");
        if (total > limits.max_vector_writes) {
            fail("SceneBlock VU vector writes exceed the caller's limit");
        }
    }
    if (total > host_max_writes) {
        fail("SceneBlock VU writes exceed the host container limit");
    }
    return total;
}

} // namespace

SceneBlockVuSnapshotV1 execute_scene_block_vu_v1(
    const std::span<const std::byte> bytes,
    const SceneBlockVuExecutionOptionsV1 options,
    const SceneBlockVuLimits limits) {
    if (options.tops_qword >= kSceneBlockVuMemoryQwordCount) {
        fail("SceneBlock VU TOPS must be in the 0..1023 qword range");
    }
    if (limits.max_vector_writes == 0U) {
        fail("SceneBlock VU max_vector_writes must be non-zero");
    }

    SceneBlockVuSnapshotV1 result;
    try {
        result.stream = parse_scene_block_vif_stream_v1(bytes, limits.vif);
    } catch (const SceneBlockVifError& error) {
        fail(
            "Invalid SceneBlock VIF stream for VU execution: " +
            std::string(error.what()));
    }

    result.total_vector_writes = preflight_write_count(
        result.stream,
        limits,
        result.writes.max_size());
    result.writes.reserve(checked_host_size(
        result.total_vector_writes,
        "The SceneBlock VU vector-write count"));

    SceneBlockVuStateV1 state;
    state.tops_qword = options.tops_qword;

    for (std::size_t index = 0;
         index < result.stream.commands.size();
         ++index) {
        const auto& command = result.stream.commands[index];
        validate_command_ranges(bytes, command);

        switch (command.opcode) {
        case SceneBlockVifOpcode::nop:
            break;
        case SceneBlockVifOpcode::stcycl:
            state.cycle_length =
                static_cast<std::uint8_t>(command.immediate);
            state.write_length = effective_write_length(
                static_cast<std::uint8_t>(command.immediate >> 8U));
            break;
        case SceneBlockVifOpcode::stmod:
            if (command.immediate > 2U) {
                fail("A SceneBlock VU STMOD uses an unsupported mode");
            }
            state.addition_mode =
                static_cast<SceneBlockVuAdditionMode>(command.immediate);
            break;
        case SceneBlockVifOpcode::strow:
            execute_strow(bytes, command, state);
            break;
        case SceneBlockVifOpcode::unpack_v3_16:
        case SceneBlockVifOpcode::unpack_v4_32:
        case SceneBlockVifOpcode::unpack_v4_16:
        case SceneBlockVifOpcode::unpack_v4_8:
            execute_unpack(
                bytes,
                command,
                static_cast<std::uint64_t>(index),
                result,
                state);
            break;
        default:
            fail("SceneBlock VU execution encountered an unsupported opcode");
        }
    }

    if (result.writes.size() != result.total_vector_writes) {
        fail("SceneBlock VU execution produced an inconsistent write count");
    }
    if (result.unique_qword_writes + result.overwrite_vector_writes !=
        result.total_vector_writes) {
        fail("SceneBlock VU execution produced inconsistent write summaries");
    }
    if (state.cycle_length != result.stream.final_cycle_length ||
        state.write_length != result.stream.final_write_length) {
        fail("SceneBlock VU final cycle state disagrees with VIF metadata");
    }

    result.final_state = state;
    return result;
}

} // namespace openrc
