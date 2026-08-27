#include "openrc/scene_block_vif.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint32_t kInterruptBit = 0x80000000U;
constexpr std::uint16_t kUnpackAddressMask = 0x03ffU;
constexpr std::uint16_t kUnpackReservedMask = 0x3c00U;
constexpr std::uint16_t kUnpackUnsignedBit = 0x4000U;
constexpr std::uint16_t kUnpackTopsBit = 0x8000U;

[[noreturn]] void fail(const std::string& message) {
    throw SceneBlockVifError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
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

[[nodiscard]] std::uint64_t align_up_4(
    const std::uint64_t value,
    const char* description) {
    constexpr std::uint64_t kMask =
        static_cast<std::uint64_t>(kSceneBlockVifPayloadAlignment) - 1U;
    static_assert(
        (kSceneBlockVifPayloadAlignment & kMask) == 0U,
        "The SceneBlock VIF payload alignment must be a power of two");
    return checked_add(value, kMask, description) & ~kMask;
}

[[nodiscard]] std::uint16_t effective_write_length(
    const std::uint8_t raw_value) noexcept {
    return raw_value == 0U
        ? kSceneBlockVifMaximumVectorCount
        : raw_value;
}

[[nodiscard]] std::uint16_t effective_num(
    const std::uint8_t raw_num) noexcept {
    return raw_num == 0U
        ? kSceneBlockVifMaximumVectorCount
        : raw_num;
}

[[nodiscard]] std::uint16_t unpack_input_vector_count(
    const std::uint16_t output_vectors,
    const std::uint16_t cycle_length,
    const std::uint16_t write_length) {
    if (write_length <= cycle_length) {
        return output_vectors;
    }

    const auto complete_cycles =
        static_cast<std::uint64_t>(output_vectors) / write_length;
    const auto complete_input = checked_multiply(
        complete_cycles,
        cycle_length,
        "a SceneBlock VIF UNPACK fill-mode input count");
    const auto partial_input = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(output_vectors) % write_length,
        cycle_length);
    const auto input_vectors = checked_add(
        complete_input,
        partial_input,
        "a SceneBlock VIF UNPACK input count");
    if (input_vectors > kSceneBlockVifMaximumVectorCount) {
        fail("A SceneBlock VIF UNPACK input count exceeds 256 vectors");
    }
    return static_cast<std::uint16_t>(input_vectors);
}

void require_zero_padding(
    const std::span<const std::byte> bytes,
    const std::uint64_t offset,
    const std::uint8_t size) {
    if (size == 0U) {
        return;
    }
    const auto host_offset = static_cast<std::size_t>(offset);
    const auto host_size = static_cast<std::size_t>(size);
    const auto padding = bytes.subspan(host_offset, host_size);
    if (!std::all_of(
            padding.begin(),
            padding.end(),
            [](const std::byte value) { return value == std::byte{0}; })) {
        fail("A SceneBlock VIF UNPACK has non-zero alignment padding");
    }
}

} // namespace

SceneBlockVifStreamV1 parse_scene_block_vif_stream_v1(
    const std::span<const std::byte> bytes,
    const SceneBlockVifLimits limits) {
    return parse_scene_block_vif_stream_v1(
        bytes,
        SceneBlockVifCycleStateV1{},
        limits);
}

SceneBlockVifStreamV1 parse_scene_block_vif_stream_v1(
    const std::span<const std::byte> bytes,
    const SceneBlockVifCycleStateV1 initial_cycle,
    const SceneBlockVifLimits limits) {
    if (limits.max_input_bytes == 0U ||
        limits.max_commands == 0U ||
        limits.max_payload_bytes == 0U) {
        fail("SceneBlockVifStreamV1 caller limits must all be non-zero");
    }
    if (initial_cycle.cycle_length >
            static_cast<std::uint16_t>(
                std::numeric_limits<std::uint8_t>::max()) ||
        initial_cycle.write_length == 0U ||
        initial_cycle.write_length > kSceneBlockVifMaximumVectorCount) {
        fail("SceneBlockVifStreamV1 initial STCYCL state is invalid");
    }

    const auto input_bytes = static_cast<std::uint64_t>(bytes.size());
    if (input_bytes == 0U) {
        fail("SceneBlockVifStreamV1 input is empty");
    }
    if (input_bytes > limits.max_input_bytes) {
        fail("The SceneBlockVifStreamV1 input exceeds the caller's byte limit");
    }

    SceneBlockVifStreamV1 result;
    result.input_bytes = input_bytes;
    result.initial_cycle_length = initial_cycle.cycle_length;
    result.initial_write_length = initial_cycle.write_length;
    std::uint16_t cycle_length = initial_cycle.cycle_length;
    std::uint16_t write_length = initial_cycle.write_length;
    std::uint64_t position = 0U;

    while (position < input_bytes) {
        if (input_bytes - position < kSceneBlockVifCodeSize) {
            fail("SceneBlockVifStreamV1 ends with a truncated VIFcode");
        }
        if (static_cast<std::uint64_t>(result.commands.size()) >=
            limits.max_commands) {
            fail("SceneBlockVifStreamV1 command count exceeds the caller's limit");
        }
        if (result.commands.size() == result.commands.max_size()) {
            fail("SceneBlockVifStreamV1 command metadata exceeds the host container limit");
        }

        const auto host_position = static_cast<std::size_t>(position);
        SceneBlockVifCommandV1 command;
        command.raw_code = read_le32(bytes, host_position);
        command.raw_num = static_cast<std::uint8_t>(
            command.raw_code >> 16U);
        command.immediate = static_cast<std::uint16_t>(command.raw_code);
        command.code_range = SceneBlockVifRange{
            position,
            kSceneBlockVifCodeSize};

        if ((command.raw_code & kInterruptBit) != 0U) {
            fail("SceneBlockVifStreamV1 contains an unsupported IRQ VIFcode");
        }
        const auto raw_opcode = static_cast<std::uint8_t>(
            command.raw_code >> 24U);

        std::uint64_t payload_data_bytes = 0U;
        switch (raw_opcode) {
        case static_cast<std::uint8_t>(SceneBlockVifOpcode::nop):
            command.opcode = SceneBlockVifOpcode::nop;
            if (command.raw_num != 0U || command.immediate != 0U) {
                fail("A SceneBlock VIF NOP has non-zero reserved fields");
            }
            break;

        case static_cast<std::uint8_t>(SceneBlockVifOpcode::stcycl): {
            command.opcode = SceneBlockVifOpcode::stcycl;
            if (command.raw_num != 0U) {
                fail("A SceneBlock VIF STCYCL has a non-zero reserved NUM field");
            }
            cycle_length = static_cast<std::uint8_t>(command.immediate);
            write_length = effective_write_length(
                static_cast<std::uint8_t>(command.immediate >> 8U));
            break;
        }

        case static_cast<std::uint8_t>(SceneBlockVifOpcode::stmod):
            command.opcode = SceneBlockVifOpcode::stmod;
            if (command.raw_num != 0U) {
                fail("A SceneBlock VIF STMOD has a non-zero reserved NUM field");
            }
            if (command.immediate > 2U) {
                fail("A SceneBlock VIF STMOD uses a reserved mode");
            }
            break;

        case static_cast<std::uint8_t>(SceneBlockVifOpcode::strow):
            command.opcode = SceneBlockVifOpcode::strow;
            if (command.raw_num != 0U || command.immediate != 0U) {
                fail("A SceneBlock VIF STROW has non-zero reserved fields");
            }
            payload_data_bytes = 16U;
            break;

        case static_cast<std::uint8_t>(
                 SceneBlockVifOpcode::unpack_v3_16):
        case static_cast<std::uint8_t>(
                 SceneBlockVifOpcode::unpack_v4_32):
        case static_cast<std::uint8_t>(
                 SceneBlockVifOpcode::unpack_v4_16):
        case static_cast<std::uint8_t>(
                 SceneBlockVifOpcode::unpack_v4_8): {
            command.opcode = static_cast<SceneBlockVifOpcode>(raw_opcode);
            if ((command.immediate & kUnpackReservedMask) != 0U) {
                fail("A SceneBlock VIF UNPACK has non-zero reserved immediate bits");
            }
            command.output_vector_count = effective_num(command.raw_num);
            command.input_vector_count = unpack_input_vector_count(
                command.output_vector_count,
                cycle_length,
                write_length);
            command.destination_address =
                command.immediate & kUnpackAddressMask;
            command.unsigned_data =
                (command.immediate & kUnpackUnsignedBit) != 0U;
            command.use_tops =
                (command.immediate & kUnpackTopsBit) != 0U;

            switch (command.opcode) {
            case SceneBlockVifOpcode::unpack_v3_16:
                command.component_count = 3U;
                command.component_bits = 16U;
                break;
            case SceneBlockVifOpcode::unpack_v4_32:
                command.component_count = 4U;
                command.component_bits = 32U;
                break;
            case SceneBlockVifOpcode::unpack_v4_16:
                command.component_count = 4U;
                command.component_bits = 16U;
                break;
            case SceneBlockVifOpcode::unpack_v4_8:
                command.component_count = 4U;
                command.component_bits = 8U;
                break;
            default:
                fail("Internal SceneBlock VIF UNPACK opcode mismatch");
            }

            const auto bits_per_vector = checked_multiply(
                command.component_count,
                command.component_bits,
                "a SceneBlock VIF UNPACK vector size");
            const auto payload_bits = checked_multiply(
                command.input_vector_count,
                bits_per_vector,
                "a SceneBlock VIF UNPACK payload size");
            payload_data_bytes = checked_add(
                payload_bits,
                7U,
                "a SceneBlock VIF UNPACK byte size") / 8U;
            break;
        }

        default:
            fail("SceneBlockVifStreamV1 contains an unsupported VIF opcode");
        }

        const auto stored_payload_bytes = align_up_4(
            payload_data_bytes,
            "a SceneBlock VIF stored payload size");
        const auto padding_bytes =
            stored_payload_bytes - payload_data_bytes;
        if (padding_bytes >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint8_t>::max())) {
            fail("A SceneBlock VIF payload alignment suffix is too large");
        }

        const auto payload_offset = checked_add(
            position,
            kSceneBlockVifCodeSize,
            "a SceneBlock VIF payload offset");
        const auto packet_end = checked_add(
            payload_offset,
            stored_payload_bytes,
            "a SceneBlock VIF packet end");
        if (packet_end > input_bytes) {
            fail("A SceneBlock VIF payload exceeds the bounded stream");
        }

        result.total_payload_bytes = checked_add(
            result.total_payload_bytes,
            stored_payload_bytes,
            "the SceneBlock VIF aggregate payload size");
        if (result.total_payload_bytes > limits.max_payload_bytes) {
            fail("SceneBlockVifStreamV1 payload bytes exceed the caller's limit");
        }
        result.total_padding_bytes = checked_add(
            result.total_padding_bytes,
            padding_bytes,
            "the SceneBlock VIF aggregate padding size");

        command.payload_data_bytes = payload_data_bytes;
        command.padding_bytes = static_cast<std::uint8_t>(padding_bytes);
        command.payload_range = SceneBlockVifRange{
            payload_offset,
            stored_payload_bytes};
        command.packet_range = SceneBlockVifRange{
            position,
            packet_end - position};

        const auto padding_offset = checked_add(
            payload_offset,
            payload_data_bytes,
            "a SceneBlock VIF padding offset");
        require_zero_padding(
            bytes,
            padding_offset,
            command.padding_bytes);

        result.commands.push_back(std::move(command));
        position = packet_end;
    }

    if (position != input_bytes) {
        fail("SceneBlockVifStreamV1 does not end exactly at its input boundary");
    }
    result.final_cycle_length = cycle_length;
    result.final_write_length = write_length;
    return result;
}

} // namespace openrc
