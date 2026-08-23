#include "openrc/scene_block_vu_phase.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint64_t kVuAddressMask =
    static_cast<std::uint64_t>(kSceneBlockVuMemoryQwordCount) - 1U;
static_assert((kSceneBlockVuMemoryQwordCount & kVuAddressMask) == 0U,
              "The SceneBlock VU memory qword count must be a power of two");

struct PhaseBounds {
    std::size_t first = 0;
    std::size_t end = 0;
};

[[noreturn]] void fail(const std::string& message) {
    throw SceneBlockVuPhaseError(message);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char* description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(const std::uint64_t left,
                                             const std::uint64_t right,
                                             const char* description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left * right;
}

[[nodiscard]] std::uint64_t host_size_as_u64(const std::size_t value,
                                             const char* description) {
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (value > static_cast<std::size_t>(
                        std::numeric_limits<std::uint64_t>::max())) {
            fail(std::string(description) + " exceeds the public index range");
        }
    }
    return static_cast<std::uint64_t>(value);
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

[[nodiscard]] bool is_control(const SceneBlockVifOpcode opcode) noexcept {
    switch (opcode) {
    case SceneBlockVifOpcode::stcycl:
    case SceneBlockVifOpcode::stmod:
    case SceneBlockVifOpcode::strow:
        return true;
    default:
        return false;
    }
}

void validate_value(const SceneBlockVuValueV1 value, const char* description) {
    switch (value.state) {
    case SceneBlockVuValueState::indeterminate:
    case SceneBlockVuValueState::known:
        return;
    default:
        fail(std::string(description) + " has an invalid value state");
    }
}

[[nodiscard]] SceneBlockVuValueV1
canonical_value(const SceneBlockVuValueV1 value) noexcept {
    return value.state == SceneBlockVuValueState::known ? value
                                                        : SceneBlockVuValueV1{};
}

[[nodiscard]] bool values_equal(const SceneBlockVuValueV1 left,
                                const SceneBlockVuValueV1 right) noexcept {
    if (left.state != right.state) {
        return false;
    }
    return left.state == SceneBlockVuValueState::indeterminate ||
           left.bits == right.bits;
}

[[nodiscard]] bool states_equal(const SceneBlockVuStateV1& left,
                                const SceneBlockVuStateV1& right) noexcept {
    if (left.tops_qword != right.tops_qword ||
        left.cycle_length != right.cycle_length ||
        left.write_length != right.write_length ||
        left.addition_mode != right.addition_mode) {
        return false;
    }
    for (std::size_t lane = 0; lane < left.row.size(); ++lane) {
        if (!values_equal(left.row[lane], right.row[lane])) {
            return false;
        }
    }
    return true;
}

void validate_addition_mode(const SceneBlockVuAdditionMode mode,
                            const char* description) {
    switch (mode) {
    case SceneBlockVuAdditionMode::normal:
    case SceneBlockVuAdditionMode::offset:
    case SceneBlockVuAdditionMode::difference:
        return;
    default:
        fail(std::string(description) + " has an invalid addition mode");
    }
}

void validate_state(const SceneBlockVuStateV1& state, const char* description) {
    if (state.tops_qword >= kSceneBlockVuMemoryQwordCount) {
        fail(std::string(description) + " has TOPS outside VU1 memory");
    }
    if (state.cycle_length > static_cast<std::uint16_t>(
                                 std::numeric_limits<std::uint8_t>::max()) ||
        state.write_length == 0U ||
        state.write_length > kSceneBlockVifMaximumVectorCount) {
        fail(std::string(description) + " has invalid STCYCL state");
    }
    validate_addition_mode(state.addition_mode, description);
    for (const auto value : state.row) {
        validate_value(value, description);
    }
}

[[nodiscard]] std::uint16_t effective_num(const std::uint8_t raw_num) noexcept {
    return raw_num == 0U ? kSceneBlockVifMaximumVectorCount : raw_num;
}

[[nodiscard]] std::uint16_t
effective_write_length(const std::uint8_t raw_write_length) noexcept {
    return raw_write_length == 0U ? kSceneBlockVifMaximumVectorCount
                                  : raw_write_length;
}

[[nodiscard]] std::uint16_t
expected_input_vectors(const std::uint16_t output_vectors,
                       const SceneBlockVuStateV1& state) {
    if (state.write_length <= state.cycle_length) {
        return output_vectors;
    }
    const auto complete_blocks =
        static_cast<std::uint64_t>(output_vectors) / state.write_length;
    const auto partial = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(output_vectors) % state.write_length,
        state.cycle_length);
    const auto result =
        checked_add(checked_multiply(complete_blocks, state.cycle_length,
                                     "a phase UNPACK fill input count"),
                    partial, "a phase UNPACK input count");
    if (result > kSceneBlockVifMaximumVectorCount) {
        fail("A phase UNPACK input count exceeds 256 vectors");
    }
    return static_cast<std::uint16_t>(result);
}

void validate_command_encoding(const SceneBlockVifCommandV1& command) {
    if (static_cast<std::uint16_t>(command.raw_code & 0xffffU) !=
            command.immediate ||
        static_cast<std::uint8_t>((command.raw_code >> 16U) & 0xffU) !=
            command.raw_num ||
        static_cast<std::uint8_t>(command.raw_code >> 24U) !=
            static_cast<std::uint8_t>(command.opcode)) {
        fail("A phase command disagrees with its raw VIFcode");
    }

    switch (command.opcode) {
    case SceneBlockVifOpcode::nop:
    case SceneBlockVifOpcode::stcycl:
    case SceneBlockVifOpcode::stmod:
    case SceneBlockVifOpcode::strow:
    case SceneBlockVifOpcode::unpack_v3_16:
    case SceneBlockVifOpcode::unpack_v4_32:
    case SceneBlockVifOpcode::unpack_v4_16:
    case SceneBlockVifOpcode::unpack_v4_8:
        break;
    default:
        fail("A phase command has an unsupported VIF opcode");
    }
}

void validate_stream_layout(const SceneBlockVifStreamV1& stream) {
    std::uint64_t next_packet = 0U;
    std::uint64_t total_payload = 0U;
    std::uint64_t total_padding = 0U;

    for (const auto& command : stream.commands) {
        validate_command_encoding(command);
        if (command.code_range.size != kSceneBlockVifCodeSize ||
            command.code_range.offset != next_packet ||
            command.packet_range.offset != next_packet) {
            fail("Phase command packets do not form a contiguous stream");
        }
        const auto code_end =
            checked_add(command.code_range.offset, command.code_range.size,
                        "a phase VIFcode end");
        if (command.payload_range.offset != code_end) {
            fail("A phase command payload does not follow its VIFcode");
        }
        const auto payload_end =
            checked_add(command.payload_range.offset,
                        command.payload_range.size, "a phase payload end");
        const auto packet_end =
            checked_add(command.packet_range.offset, command.packet_range.size,
                        "a phase packet end");
        if (packet_end != payload_end ||
            command.packet_range.size != checked_add(command.code_range.size,
                                                     command.payload_range.size,
                                                     "a phase packet size") ||
            command.payload_data_bytes > command.payload_range.size ||
            command.padding_bytes !=
                command.payload_range.size - command.payload_data_bytes) {
            fail("A phase command has inconsistent packet metadata");
        }
        if (packet_end > stream.input_bytes) {
            fail("A phase command packet exceeds the bounded stream");
        }
        next_packet = packet_end;
        total_payload = checked_add(total_payload, command.payload_range.size,
                                    "the phase stream payload total");
        total_padding = checked_add(total_padding, command.padding_bytes,
                                    "the phase stream padding total");
    }

    if (next_packet != stream.input_bytes ||
        total_payload != stream.total_payload_bytes ||
        total_padding != stream.total_padding_bytes) {
        fail("The phase stream aggregate metadata is inconsistent");
    }
}

void validate_non_unpack_metadata(
    const SceneBlockVifCommandV1& command,
    const std::uint64_t expected_payload_data_bytes) {
    if (command.raw_num != 0U || command.output_vector_count != 0U ||
        command.input_vector_count != 0U || command.component_count != 0U ||
        command.component_bits != 0U || command.destination_address != 0U ||
        command.unsigned_data || command.use_tops ||
        command.payload_data_bytes != expected_payload_data_bytes ||
        command.payload_range.size != expected_payload_data_bytes ||
        command.padding_bytes != 0U) {
        fail("A phase control command has inconsistent metadata");
    }
}

[[nodiscard]] std::pair<std::uint8_t, std::uint8_t>
unpack_format(const SceneBlockVifOpcode opcode) {
    switch (opcode) {
    case SceneBlockVifOpcode::unpack_v3_16:
        return {3U, 16U};
    case SceneBlockVifOpcode::unpack_v4_32:
        return {4U, 32U};
    case SceneBlockVifOpcode::unpack_v4_16:
        return {4U, 16U};
    case SceneBlockVifOpcode::unpack_v4_8:
        return {4U, 8U};
    default:
        fail("Internal phase UNPACK opcode mismatch");
    }
}

void validate_unpack_metadata(const SceneBlockVifCommandV1& command,
                              const SceneBlockVuStateV1& state) {
    const auto [components, component_bits] = unpack_format(command.opcode);
    const auto output_vectors = effective_num(command.raw_num);
    const auto input_vectors = expected_input_vectors(output_vectors, state);
    if ((command.immediate & 0x3c00U) != 0U ||
        command.output_vector_count != output_vectors ||
        command.input_vector_count != input_vectors ||
        command.component_count != components ||
        command.component_bits != component_bits ||
        command.destination_address != (command.immediate & 0x03ffU) ||
        command.unsigned_data != ((command.immediate & 0x4000U) != 0U) ||
        command.use_tops != ((command.immediate & 0x8000U) != 0U)) {
        fail("A phase UNPACK has inconsistent format metadata");
    }

    const auto vector_bits = checked_multiply(components, component_bits,
                                              "a phase UNPACK vector width");
    if ((vector_bits & 7U) != 0U) {
        fail("A phase UNPACK vector is not byte aligned");
    }
    const auto logical_payload = checked_multiply(
        input_vectors, vector_bits / 8U, "a phase UNPACK payload size");
    const auto aligned_payload =
        checked_add(logical_payload, kSceneBlockVifPayloadAlignment - 1U,
                    "an aligned phase UNPACK payload size") /
        kSceneBlockVifPayloadAlignment * kSceneBlockVifPayloadAlignment;
    const auto padding = aligned_payload - logical_payload;
    if (command.payload_data_bytes != logical_payload ||
        command.payload_range.size != aligned_payload ||
        command.padding_bytes != padding) {
        fail("A phase UNPACK has inconsistent payload metadata");
    }
}

[[nodiscard]] std::vector<PhaseBounds>
make_phase_bounds(const std::vector<SceneBlockVifCommandV1>& commands) {
    std::vector<PhaseBounds> result;
    if (commands.empty()) {
        return result;
    }
    result.reserve(commands.size());
    std::size_t first = 0U;
    bool saw_unpack = false;
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        const auto opcode = commands[index].opcode;
        if (opcode == SceneBlockVifOpcode::nop) {
            continue;
        }
        if (is_unpack(opcode)) {
            saw_unpack = true;
            continue;
        }
        if (!is_control(opcode)) {
            fail("A phase command has no neutral command class");
        }
        if (saw_unpack) {
            result.push_back(PhaseBounds{first, index});
            first = index;
            saw_unpack = false;
        }
    }
    result.push_back(PhaseBounds{first, commands.size()});
    return result;
}

[[nodiscard]] SceneBlockVuValueV1
add_values(const SceneBlockVuValueV1 left,
           const SceneBlockVuValueV1 right) noexcept {
    if (left.state != SceneBlockVuValueState::known ||
        right.state != SceneBlockVuValueState::known) {
        return {};
    }
    return SceneBlockVuValueV1{SceneBlockVuValueState::known,
                               left.bits + right.bits};
}

[[nodiscard]] std::uint64_t
input_index_for_output(const std::uint64_t output,
                       const SceneBlockVuStateV1& state, const bool fill_mode) {
    if (!fill_mode) {
        return output;
    }
    return checked_add(checked_multiply(output / state.write_length,
                                        state.cycle_length,
                                        "a phase fill input index"),
                       output % state.write_length, "a phase fill input index");
}

[[nodiscard]] std::uint64_t
destination_progression(const std::uint64_t output,
                        const SceneBlockVuStateV1& state,
                        const bool fill_mode) {
    if (fill_mode) {
        return output;
    }
    return checked_add(checked_multiply(output / state.write_length,
                                        state.cycle_length,
                                        "a phase skip destination"),
                       output % state.write_length, "a phase skip destination");
}

void validate_write(const SceneBlockVifStreamV1& stream,
                    const SceneBlockVifCommandV1& command,
                    const std::uint64_t command_index,
                    const std::uint16_t output_index,
                    const SceneBlockVuVectorWriteV1& write,
                    SceneBlockVuStateV1& state) {
    if (write.command_index != command_index ||
        write.output_vector_index != output_index ||
        write.addition_mode != state.addition_mode) {
        fail("A phase write is outside its exact command/output order");
    }
    validate_addition_mode(write.addition_mode, "A phase write");

    const bool fill_mode = state.cycle_length < state.write_length;
    const auto position =
        static_cast<std::uint64_t>(output_index) % state.write_length;
    const bool consumes_input = !fill_mode || position < state.cycle_length;
    const auto input_index =
        input_index_for_output(output_index, state, fill_mode);
    if (consumes_input) {
        if (input_index > std::numeric_limits<std::uint16_t>::max() ||
            write.input_vector_index !=
                std::optional<std::uint16_t>{
                    static_cast<std::uint16_t>(input_index)}) {
            fail("A phase write has an inconsistent input vector index");
        }
    } else if (write.input_vector_index.has_value()) {
        fail("A phase fill write unexpectedly consumes an input vector");
    }

    std::uint64_t base = command.destination_address;
    if (command.use_tops) {
        base = checked_add(base, state.tops_qword, "a phase TOPS address");
    }
    const auto expected_unwrapped = checked_add(
        base, destination_progression(output_index, state, fill_mode),
        "a phase destination address");
    const auto expected_destination =
        static_cast<std::uint16_t>(expected_unwrapped & kVuAddressMask);
    const bool expected_wrapped =
        expected_unwrapped >= kSceneBlockVuMemoryQwordCount;
    if (write.unwrapped_destination_qword != expected_unwrapped ||
        write.destination_qword != expected_destination ||
        write.wrapped != expected_wrapped) {
        fail("A phase write has inconsistent destination provenance");
    }

    const auto component_bytes =
        static_cast<std::uint64_t>(command.component_bits) / 8U;
    const auto vector_bytes =
        checked_multiply(command.component_count, component_bytes,
                         "a phase source vector width");
    for (std::size_t lane_index = 0U; lane_index < write.lanes.size();
         ++lane_index) {
        const auto& lane = write.lanes[lane_index];
        validate_value(lane.unpacked_value, "A phase unpacked lane");
        validate_value(lane.row_before, "A phase ROW lane");
        validate_value(lane.written_value, "A phase written lane");
        if (!values_equal(lane.row_before, state.row[lane_index])) {
            fail("A phase write disagrees with the active ROW state");
        }

        SceneBlockVuValueV1 expected_unpacked;
        SceneBlockVifRange expected_source;
        SceneBlockVuLaneSource expected_lane_source;
        if (!consumes_input) {
            expected_lane_source =
                SceneBlockVuLaneSource::cycle_fill_indeterminate;
        } else if (command.opcode == SceneBlockVifOpcode::unpack_v3_16 &&
                   lane_index == 3U) {
            expected_lane_source = SceneBlockVuLaneSource::v3_w_indeterminate;
        } else {
            expected_lane_source = SceneBlockVuLaneSource::payload;
            expected_unpacked = lane.unpacked_value;
            if (expected_unpacked.state != SceneBlockVuValueState::known) {
                fail("A phase payload lane is unexpectedly indeterminate");
            }
            expected_source.offset = checked_add(
                command.payload_range.offset,
                checked_add(checked_multiply(input_index, vector_bytes,
                                             "a phase source vector offset"),
                            checked_multiply(lane_index, component_bytes,
                                             "a phase source component offset"),
                            "a phase source component offset"),
                "a phase source offset");
            expected_source.size = component_bytes;
            const auto source_end =
                checked_add(expected_source.offset, expected_source.size,
                            "a phase source end");
            const auto logical_end = checked_add(command.payload_range.offset,
                                                 command.payload_data_bytes,
                                                 "a phase logical payload end");
            if (source_end > logical_end || source_end > stream.input_bytes) {
                fail("A phase lane source exceeds its logical payload");
            }
        }
        if (lane.source != expected_lane_source ||
            lane.source_range != expected_source ||
            !values_equal(lane.unpacked_value, expected_unpacked)) {
            fail("A phase lane has inconsistent source provenance");
        }

        SceneBlockVuValueV1 expected_written;
        switch (state.addition_mode) {
        case SceneBlockVuAdditionMode::normal:
            expected_written = expected_unpacked;
            break;
        case SceneBlockVuAdditionMode::offset:
        case SceneBlockVuAdditionMode::difference:
            expected_written =
                add_values(expected_unpacked, state.row[lane_index]);
            break;
        default:
            fail("A phase write uses an unsupported addition mode");
        }
        if (!values_equal(lane.written_value, expected_written)) {
            fail("A phase lane has an inconsistent written value");
        }
        if (state.addition_mode == SceneBlockVuAdditionMode::difference) {
            state.row[lane_index] = canonical_value(lane.written_value);
        }
    }
}

[[nodiscard]] std::vector<SceneBlockVuQwordRunV1>
make_qword_runs(const std::array<bool, kSceneBlockVuMemoryQwordCount>& seen) {
    std::vector<SceneBlockVuQwordRunV1> result;
    std::size_t address = 0U;
    while (address < seen.size()) {
        if (!seen[address]) {
            ++address;
            continue;
        }
        const auto first = address;
        while (address < seen.size() && seen[address]) {
            ++address;
        }
        result.push_back(SceneBlockVuQwordRunV1{
            static_cast<std::uint16_t>(first),
            static_cast<std::uint16_t>(address - first)});
    }
    return result;
}

} // namespace

std::vector<SceneBlockVuCommandPhaseV1>
group_scene_block_vu_phases_v1(const SceneBlockVuSnapshotV1& snapshot) {
    validate_stream_layout(snapshot.stream);
    validate_state(snapshot.final_state, "The final phase snapshot state");

    const auto write_size = host_size_as_u64(snapshot.writes.size(),
                                             "The phase snapshot write count");
    if (snapshot.total_vector_writes != write_size) {
        fail("The phase snapshot has inconsistent total write metadata");
    }

    const auto bounds = make_phase_bounds(snapshot.stream.commands);
    std::vector<SceneBlockVuCommandPhaseV1> phases;
    phases.reserve(bounds.size());

    SceneBlockVuStateV1 state;
    state.tops_qword = snapshot.final_state.tops_qword;
    std::size_t write_cursor = 0U;
    std::array<bool, kSceneBlockVuMemoryQwordCount> globally_seen{};
    std::array<std::uint64_t, kSceneBlockVuMemoryQwordCount> write_counts{};
    std::array<std::optional<std::uint64_t>, kSceneBlockVuMemoryQwordCount>
        last_write_indices{};
    std::uint64_t computed_unique = 0U;
    std::uint64_t computed_overwrites = 0U;
    std::uint64_t computed_wrapped = 0U;
    std::uint64_t grouped_overwrites = 0U;

    for (std::size_t phase_index = 0U; phase_index < bounds.size();
         ++phase_index) {
        const auto phase_bounds = bounds[phase_index];
        SceneBlockVuCommandPhaseV1 phase;
        phase.first_command_index =
            host_size_as_u64(phase_bounds.first, "A phase first command index");
        phase.command_count = host_size_as_u64(
            phase_bounds.end - phase_bounds.first, "A phase command count");
        phase.first_write_index =
            host_size_as_u64(write_cursor, "A phase first write index");
        phase.state_before = state;

        const auto& first_command =
            snapshot.stream.commands[phase_bounds.first];
        const auto& last_command =
            snapshot.stream.commands[phase_bounds.end - 1U];
        const auto stream_end = checked_add(last_command.packet_range.offset,
                                            last_command.packet_range.size,
                                            "a phase stream range end");
        phase.stream_range =
            SceneBlockVifRange{first_command.packet_range.offset,
                               stream_end - first_command.packet_range.offset};

        std::array<bool, kSceneBlockVuMemoryQwordCount> locally_seen{};
        bool saw_unpack = false;
        bool row_replaced = false;

        for (std::size_t command_index = phase_bounds.first;
             command_index < phase_bounds.end; ++command_index) {
            const auto& command = snapshot.stream.commands[command_index];
            switch (command.opcode) {
            case SceneBlockVifOpcode::nop:
                validate_non_unpack_metadata(command, 0U);
                if (command.immediate != 0U) {
                    fail("A phase NOP has a non-zero immediate");
                }
                phase.nop_command_count = checked_add(phase.nop_command_count,
                                                      1U, "a phase NOP count");
                break;
            case SceneBlockVifOpcode::stcycl:
                if (saw_unpack) {
                    fail("A phase control appears after phase data");
                }
                validate_non_unpack_metadata(command, 0U);
                state.cycle_length =
                    static_cast<std::uint8_t>(command.immediate);
                state.write_length = effective_write_length(
                    static_cast<std::uint8_t>(command.immediate >> 8U));
                phase.control_command_count = checked_add(
                    phase.control_command_count, 1U, "a phase control count");
                break;
            case SceneBlockVifOpcode::stmod:
                if (saw_unpack) {
                    fail("A phase control appears after phase data");
                }
                validate_non_unpack_metadata(command, 0U);
                if (command.immediate > 2U) {
                    fail("A phase STMOD uses an unsupported mode");
                }
                state.addition_mode =
                    static_cast<SceneBlockVuAdditionMode>(command.immediate);
                phase.control_command_count = checked_add(
                    phase.control_command_count, 1U, "a phase control count");
                break;
            case SceneBlockVifOpcode::strow:
                if (saw_unpack) {
                    fail("A phase control appears after phase data");
                }
                validate_non_unpack_metadata(command, 16U);
                if (command.immediate != 0U) {
                    fail("A phase STROW has a non-zero immediate");
                }
                row_replaced = true;
                phase.control_command_count = checked_add(
                    phase.control_command_count, 1U, "a phase control count");
                break;
            case SceneBlockVifOpcode::unpack_v3_16:
            case SceneBlockVifOpcode::unpack_v4_32:
            case SceneBlockVifOpcode::unpack_v4_16:
            case SceneBlockVifOpcode::unpack_v4_8: {
                saw_unpack = true;
                validate_unpack_metadata(command, state);
                if (write_cursor >= snapshot.writes.size()) {
                    fail("A phase UNPACK is missing its first write");
                }
                if (row_replaced) {
                    const auto& first_write = snapshot.writes[write_cursor];
                    for (std::size_t lane = 0U; lane < state.row.size();
                         ++lane) {
                        validate_value(first_write.lanes[lane].row_before,
                                       "A phase STROW observation");
                        state.row[lane] =
                            canonical_value(first_write.lanes[lane].row_before);
                    }
                    row_replaced = false;
                }

                const auto command_index_u64 = host_size_as_u64(
                    command_index, "A phase write command index");
                for (std::uint16_t output = 0U;
                     output < command.output_vector_count; ++output) {
                    if (write_cursor >= snapshot.writes.size()) {
                        fail("A phase UNPACK has fewer writes than declared");
                    }
                    const auto& write = snapshot.writes[write_cursor];
                    validate_write(snapshot.stream, command, command_index_u64,
                                   output, write, state);

                    const auto address =
                        static_cast<std::size_t>(write.destination_qword);
                    if (locally_seen[address]) {
                        phase.internal_overwrite_count =
                            checked_add(phase.internal_overwrite_count, 1U,
                                        "a phase internal overwrite count");
                    } else {
                        locally_seen[address] = true;
                        phase.unique_qword_count =
                            checked_add(phase.unique_qword_count, 1U,
                                        "a phase unique qword count");
                        if (globally_seen[address]) {
                            phase.prior_phase_overwrite_count = checked_add(
                                phase.prior_phase_overwrite_count, 1U,
                                "a prior-phase overwrite count");
                        }
                    }

                    if (globally_seen[address]) {
                        computed_overwrites =
                            checked_add(computed_overwrites, 1U,
                                        "the snapshot overwrite count");
                    } else {
                        globally_seen[address] = true;
                        computed_unique =
                            checked_add(computed_unique, 1U,
                                        "the snapshot unique qword count");
                    }
                    if (write.wrapped) {
                        computed_wrapped =
                            checked_add(computed_wrapped, 1U,
                                        "the snapshot wrapped write count");
                    }
                    write_counts[address] =
                        checked_add(write_counts[address], 1U,
                                    "a snapshot memory qword write count");
                    last_write_indices[address] = host_size_as_u64(
                        write_cursor, "A snapshot last-write index");
                    ++write_cursor;
                }
                phase.unpack_command_count = checked_add(
                    phase.unpack_command_count, 1U, "a phase UNPACK count");
                break;
            }
            default:
                fail("A phase command has an unsupported opcode");
            }
        }

        if (row_replaced) {
            if (phase_index + 1U != bounds.size()) {
                fail("A non-terminal phase has an unobservable STROW state");
            }
            for (std::size_t lane = 0U; lane < state.row.size(); ++lane) {
                state.row[lane] =
                    canonical_value(snapshot.final_state.row[lane]);
            }
        }
        phase.state_after = state;
        phase.write_count =
            host_size_as_u64(write_cursor, "A phase write end") -
            phase.first_write_index;
        if (phase.unique_qword_count + phase.internal_overwrite_count !=
            phase.write_count) {
            fail("A phase has inconsistent internal write accounting");
        }
        grouped_overwrites =
            checked_add(grouped_overwrites,
                        checked_add(phase.internal_overwrite_count,
                                    phase.prior_phase_overwrite_count,
                                    "a phase overwrite subtotal"),
                        "the grouped phase overwrite total");
        phase.unique_destination_runs = make_qword_runs(locally_seen);
        phases.push_back(std::move(phase));
    }

    if (write_cursor != snapshot.writes.size()) {
        fail("The phase write ranges do not exactly partition snapshot writes");
    }
    if (!states_equal(state, snapshot.final_state) ||
        state.cycle_length != snapshot.stream.final_cycle_length ||
        state.write_length != snapshot.stream.final_write_length) {
        fail("The grouped final state disagrees with the snapshot");
    }
    if (computed_unique != snapshot.unique_qword_writes ||
        computed_overwrites != snapshot.overwrite_vector_writes ||
        computed_wrapped != snapshot.wrapped_vector_writes ||
        computed_unique + computed_overwrites != snapshot.total_vector_writes ||
        grouped_overwrites != computed_overwrites) {
        fail("The grouped write totals disagree with snapshot aggregates");
    }

    for (std::size_t address = 0U; address < snapshot.memory.size();
         ++address) {
        const auto& memory = snapshot.memory[address];
        if (memory.write_count != write_counts[address] ||
            memory.last_write_index != last_write_indices[address]) {
            fail("The grouped writes disagree with memory provenance");
        }
        if (last_write_indices[address].has_value()) {
            const auto write_index =
                static_cast<std::size_t>(*last_write_indices[address]);
            for (std::size_t lane = 0U; lane < memory.lanes.size(); ++lane) {
                if (!values_equal(memory.lanes[lane],
                                  snapshot.writes[write_index]
                                      .lanes[lane]
                                      .written_value)) {
                    fail("The grouped writes disagree with final VU memory");
                }
            }
        } else {
            for (const auto value : memory.lanes) {
                validate_value(value, "An untouched VU memory lane");
                if (value.state != SceneBlockVuValueState::indeterminate) {
                    fail("Untouched VU memory is unexpectedly known");
                }
            }
        }
    }

    return phases;
}

} // namespace openrc
