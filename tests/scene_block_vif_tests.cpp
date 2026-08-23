#include "openrc/scene_block_vif.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

constexpr openrc::SceneBlockVifLimits kGenerousLimits{
    1024U * 1024U,
    1024U,
    1024U * 1024U,
};

struct ExpectedCommand {
    openrc::SceneBlockVifOpcode opcode;
    std::uint64_t code_offset;
    std::uint64_t payload_bytes;
    std::uint64_t payload_data_bytes;
    std::uint8_t padding_bytes;
    std::uint32_t raw_code;
    std::uint8_t raw_num;
    std::uint16_t immediate;
    std::uint16_t output_vectors;
    std::uint16_t input_vectors;
    std::uint8_t components;
    std::uint8_t component_bits;
    std::uint16_t destination_address;
    bool unsigned_data;
    bool use_tops;
};

void expect(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable>
void expect_vif_error(Callable&& callable, const char* message) {
    try {
        callable();
    } catch (const openrc::SceneBlockVifError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] constexpr std::uint32_t make_code(
    const std::uint16_t immediate,
    const std::uint8_t raw_num,
    const std::uint8_t opcode,
    const bool irq = false) noexcept {
    return static_cast<std::uint32_t>(immediate) |
        (static_cast<std::uint32_t>(raw_num) << 16U) |
        (static_cast<std::uint32_t>(opcode) << 24U) |
        (irq ? 0x80000000U : 0U);
}

void append_le32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

void append_payload(
    std::vector<std::byte>& bytes,
    const std::size_t size,
    const std::uint8_t seed) {
    for (std::size_t index = 0U; index < size; ++index) {
        const auto value = static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(seed) + index * 29U) & 0xffU);
        bytes.push_back(static_cast<std::byte>(value));
    }
}

[[nodiscard]] std::vector<std::byte> valid_all_command_stream() {
    std::vector<std::byte> bytes;
    bytes.reserve(2148U);

    append_le32(bytes, make_code(0U, 0U, 0x00U));
    append_le32(bytes, make_code(0x0404U, 0U, 0x01U));
    append_le32(bytes, make_code(2U, 0U, 0x05U));

    append_le32(bytes, make_code(0U, 0U, 0x30U));
    append_payload(bytes, 16U, 0x11U);

    append_le32(bytes, make_code(0x4012U, 1U, 0x69U));
    append_payload(bytes, 6U, 0x22U);
    bytes.insert(bytes.end(), 2U, std::byte{0});

    append_le32(bytes, make_code(0x8023U, 2U, 0x6cU));
    append_payload(bytes, 32U, 0x33U);

    // Raw NUM zero is the canonical encoding of 256 vectors.
    append_le32(bytes, make_code(0xc034U, 0U, 0x6dU));
    append_payload(bytes, 256U * 8U, 0x44U);

    append_le32(bytes, make_code(0x03ffU, 3U, 0x6eU));
    append_payload(bytes, 3U * 4U, 0x55U);

    expect(bytes.size() == 2148U, "valid VIF fixture size is wrong");
    return bytes;
}

void expect_command(
    const openrc::SceneBlockVifCommandV1& command,
    const ExpectedCommand& expected) {
    expect(command.opcode == expected.opcode, "VIF opcode is wrong");
    expect(
        command.code_range == openrc::SceneBlockVifRange{
            expected.code_offset,
            openrc::kSceneBlockVifCodeSize},
        "VIF code range is wrong");
    expect(
        command.payload_range == openrc::SceneBlockVifRange{
            expected.code_offset + openrc::kSceneBlockVifCodeSize,
            expected.payload_bytes},
        "VIF payload range is wrong");
    expect(
        command.packet_range == openrc::SceneBlockVifRange{
            expected.code_offset,
            openrc::kSceneBlockVifCodeSize + expected.payload_bytes},
        "VIF packet range is wrong");
    expect(command.raw_code == expected.raw_code, "raw VIFcode was not preserved");
    expect(command.raw_num == expected.raw_num, "raw VIF NUM is wrong");
    expect(command.immediate == expected.immediate, "VIF immediate is wrong");
    expect(
        command.output_vector_count == expected.output_vectors,
        "VIF output-vector count is wrong");
    expect(
        command.input_vector_count == expected.input_vectors,
        "VIF input-vector count is wrong");
    expect(command.component_count == expected.components, "VIF component count is wrong");
    expect(command.component_bits == expected.component_bits, "VIF component width is wrong");
    expect(
        command.destination_address == expected.destination_address,
        "VIF destination address is wrong");
    expect(command.unsigned_data == expected.unsigned_data, "VIF unsigned flag is wrong");
    expect(command.use_tops == expected.use_tops, "VIF TOPS flag is wrong");
    expect(
        command.payload_data_bytes == expected.payload_data_bytes,
        "VIF logical payload size is wrong");
    expect(command.padding_bytes == expected.padding_bytes, "VIF padding size is wrong");
}

void test_all_commands_ranges_and_accounting() {
    auto bytes = valid_all_command_stream();
    const auto report = openrc::parse_scene_block_vif_stream_v1(
        bytes,
        kGenerousLimits);

    constexpr std::array<ExpectedCommand, 8U> kExpected{
        ExpectedCommand{
            openrc::SceneBlockVifOpcode::nop,
            0U, 0U, 0U, 0U, make_code(0U, 0U, 0x00U),
            0U, 0U, 0U, 0U, 0U, 0U, 0U, false, false},
        ExpectedCommand{
            openrc::SceneBlockVifOpcode::stcycl,
            4U, 0U, 0U, 0U, make_code(0x0404U, 0U, 0x01U),
            0U, 0x0404U, 0U, 0U, 0U, 0U, 0U, false, false},
        ExpectedCommand{
            openrc::SceneBlockVifOpcode::stmod,
            8U, 0U, 0U, 0U, make_code(2U, 0U, 0x05U),
            0U, 2U, 0U, 0U, 0U, 0U, 0U, false, false},
        ExpectedCommand{
            openrc::SceneBlockVifOpcode::strow,
            12U, 16U, 16U, 0U, make_code(0U, 0U, 0x30U),
            0U, 0U, 0U, 0U, 0U, 0U, 0U, false, false},
        ExpectedCommand{
            openrc::SceneBlockVifOpcode::unpack_v3_16,
            32U, 8U, 6U, 2U, make_code(0x4012U, 1U, 0x69U),
            1U, 0x4012U, 1U, 1U, 3U, 16U, 0x12U, true, false},
        ExpectedCommand{
            openrc::SceneBlockVifOpcode::unpack_v4_32,
            44U, 32U, 32U, 0U, make_code(0x8023U, 2U, 0x6cU),
            2U, 0x8023U, 2U, 2U, 4U, 32U, 0x23U, false, true},
        ExpectedCommand{
            openrc::SceneBlockVifOpcode::unpack_v4_16,
            80U, 2048U, 2048U, 0U, make_code(0xc034U, 0U, 0x6dU),
            0U, 0xc034U, 256U, 256U, 4U, 16U, 0x34U, true, true},
        ExpectedCommand{
            openrc::SceneBlockVifOpcode::unpack_v4_8,
            2132U, 12U, 12U, 0U, make_code(0x03ffU, 3U, 0x6eU),
            3U, 0x03ffU, 3U, 3U, 4U, 8U, 0x03ffU, false, false},
    };

    expect(report.input_bytes == bytes.size(), "VIF input size is wrong");
    expect(report.commands.size() == kExpected.size(), "VIF command count is wrong");
    expect(report.total_payload_bytes == 2116U, "VIF total payload bytes are wrong");
    expect(report.total_padding_bytes == 2U, "VIF total padding bytes are wrong");
    expect(report.final_cycle_length == 4U, "VIF final cycle length is wrong");
    expect(report.final_write_length == 4U, "VIF final write length is wrong");

    std::uint64_t packet_bytes = 0U;
    std::uint64_t payload_bytes = 0U;
    std::uint64_t padding_bytes = 0U;
    for (std::size_t index = 0U; index < kExpected.size(); ++index) {
        expect_command(report.commands[index], kExpected[index]);
        packet_bytes += report.commands[index].packet_range.size;
        payload_bytes += report.commands[index].payload_range.size;
        padding_bytes += report.commands[index].padding_bytes;
        if (index + 1U < report.commands.size()) {
            expect(
                report.commands[index].packet_range.offset +
                        report.commands[index].packet_range.size ==
                    report.commands[index + 1U].packet_range.offset,
                "VIF packets do not form an exact stream partition");
        }
    }
    expect(packet_bytes == bytes.size(), "VIF packet accounting is incomplete");
    expect(payload_bytes == report.total_payload_bytes, "VIF payload accounting disagrees");
    expect(padding_bytes == report.total_padding_bytes, "VIF padding accounting disagrees");

    // The public report is metadata-only. Destroying the input must leave all
    // returned fields usable and unchanged; no payload pointer or span is kept.
    std::fill(bytes.begin(), bytes.end(), std::byte{0xff});
    bytes.clear();
    bytes.shrink_to_fit();
    expect(
        report.commands.back().raw_code == make_code(0x03ffU, 3U, 0x6eU) &&
            report.commands.back().payload_range ==
                openrc::SceneBlockVifRange{2136U, 12U},
        "VIF report retained a dependency on its input storage");

    static_assert(std::is_trivially_copyable_v<openrc::SceneBlockVifCommandV1>);
}

void test_fill_cycle_and_num_zero() {
    std::vector<std::byte> bytes;
    append_le32(bytes, make_code(0x0402U, 0U, 0x01U));
    append_le32(bytes, make_code(0xc020U, 0U, 0x6eU));
    // WL=4 and CL=2 consume 128 input vectors for 256 output vectors.
    append_payload(bytes, 128U * 4U, 0x69U);

    const auto report = openrc::parse_scene_block_vif_stream_v1(
        bytes,
        kGenerousLimits);
    expect(report.commands.size() == 2U, "fill-mode VIF command count is wrong");
    const auto& unpack = report.commands[1];
    expect(unpack.raw_num == 0U, "fill-mode raw NUM was not preserved");
    expect(unpack.output_vector_count == 256U, "NUM zero did not expand to 256 vectors");
    expect(unpack.input_vector_count == 128U, "fill-mode input count is wrong");
    expect(unpack.payload_data_bytes == 512U, "fill-mode payload size is wrong");
    expect(unpack.payload_range == openrc::SceneBlockVifRange{8U, 512U},
           "fill-mode payload range is wrong");
    expect(report.final_cycle_length == 2U, "fill-mode cycle length is wrong");
    expect(report.final_write_length == 4U, "fill-mode write length is wrong");
}

void test_mandatory_and_aggregate_limits() {
    const auto bytes = valid_all_command_stream();
    for (const auto limits : std::array<openrc::SceneBlockVifLimits, 3U>{
             openrc::SceneBlockVifLimits{0U, 1024U, 1024U * 1024U},
             openrc::SceneBlockVifLimits{1024U * 1024U, 0U, 1024U * 1024U},
             openrc::SceneBlockVifLimits{1024U * 1024U, 1024U, 0U}}) {
        expect_vif_error(
            [&] { (void)openrc::parse_scene_block_vif_stream_v1(bytes, limits); },
            "a zero VIF parser limit was accepted");
    }

    expect_vif_error(
        [&] {
            (void)openrc::parse_scene_block_vif_stream_v1(
                bytes,
                openrc::SceneBlockVifLimits{bytes.size() - 1U, 1024U, 1024U * 1024U});
        },
        "the VIF input-byte limit was ignored");
    expect_vif_error(
        [&] {
            (void)openrc::parse_scene_block_vif_stream_v1(
                bytes,
                openrc::SceneBlockVifLimits{1024U * 1024U, 7U, 1024U * 1024U});
        },
        "the VIF command-count limit was ignored");
    expect_vif_error(
        [&] {
            (void)openrc::parse_scene_block_vif_stream_v1(
                bytes,
                openrc::SceneBlockVifLimits{1024U * 1024U, 1024U, 2115U});
        },
        "the VIF payload-byte limit was ignored");

    std::vector<std::byte> padded_v3;
    append_le32(padded_v3, make_code(0x800eU, 1U, 0x69U));
    append_payload(padded_v3, 6U, 0x7aU);
    padded_v3.insert(padded_v3.end(), 2U, std::byte{0});
    expect_vif_error(
        [&] {
            (void)openrc::parse_scene_block_vif_stream_v1(
                padded_v3,
                openrc::SceneBlockVifLimits{64U, 4U, 7U});
        },
        "the VIF payload limit did not include alignment bytes");
}

void test_opcode_and_reserved_field_rejections() {
    for (const auto raw_code : std::array<std::uint32_t, 10U>{
             make_code(0U, 0U, 0x00U, true),
             make_code(0U, 0U, 0x02U),
             make_code(0U, 0U, 0x6aU),
             make_code(1U, 0U, 0x00U),
             make_code(0U, 1U, 0x00U),
             make_code(0x0102U, 1U, 0x01U),
             make_code(0U, 1U, 0x05U),
             make_code(3U, 0U, 0x05U),
             make_code(1U, 0U, 0x30U),
             make_code(0U, 1U, 0x30U)}) {
        std::vector<std::byte> bytes;
        append_le32(bytes, raw_code);
        if (((raw_code >> 24U) & 0x7fU) == 0x30U) {
            append_payload(bytes, 16U, 0x31U);
        }
        expect_vif_error(
            [&] { (void)openrc::parse_scene_block_vif_stream_v1(bytes, kGenerousLimits); },
            "a malformed or unsupported VIFcode was accepted");
    }

    {
        std::vector<std::byte> bytes;
        append_le32(bytes, make_code(0x8400U, 1U, 0x69U));
        append_payload(bytes, 6U, 0x42U);
        bytes.insert(bytes.end(), 2U, std::byte{0});
        expect_vif_error(
            [&] { (void)openrc::parse_scene_block_vif_stream_v1(bytes, kGenerousLimits); },
            "an UNPACK with reserved immediate bits was accepted");
    }
    {
        std::vector<std::byte> bytes;
        append_le32(bytes, make_code(0x800eU, 1U, 0x79U));
        append_payload(bytes, 6U, 0x43U);
        bytes.insert(bytes.end(), 2U, std::byte{0});
        expect_vif_error(
            [&] { (void)openrc::parse_scene_block_vif_stream_v1(bytes, kGenerousLimits); },
            "a masked UNPACK opcode was accepted");
    }
}

void test_truncation_overshoot_and_padding_rejections() {
    for (std::size_t size = 0U; size < openrc::kSceneBlockVifCodeSize; ++size) {
        std::vector<std::byte> bytes(size, std::byte{0});
        expect_vif_error(
            [&] { (void)openrc::parse_scene_block_vif_stream_v1(bytes, kGenerousLimits); },
            "a truncated VIFcode was accepted");
    }
    {
        std::vector<std::byte> bytes;
        append_le32(bytes, make_code(0U, 0U, 0x30U));
        append_payload(bytes, 15U, 0x51U);
        expect_vif_error(
            [&] { (void)openrc::parse_scene_block_vif_stream_v1(bytes, kGenerousLimits); },
            "a truncated STROW payload was accepted");
    }
    {
        std::vector<std::byte> bytes;
        append_le32(bytes, make_code(0x800eU, 1U, 0x69U));
        append_payload(bytes, 6U, 0x52U);
        expect_vif_error(
            [&] { (void)openrc::parse_scene_block_vif_stream_v1(bytes, kGenerousLimits); },
            "a missing UNPACK alignment suffix was accepted");
    }
    {
        std::vector<std::byte> bytes;
        append_le32(bytes, make_code(0x800eU, 1U, 0x69U));
        append_payload(bytes, 6U, 0x53U);
        bytes.push_back(std::byte{0});
        bytes.push_back(std::byte{1});
        expect_vif_error(
            [&] { (void)openrc::parse_scene_block_vif_stream_v1(bytes, kGenerousLimits); },
            "a non-zero UNPACK alignment suffix was accepted");
    }
    {
        std::vector<std::byte> bytes;
        append_le32(bytes, make_code(0x8009U, 2U, 0x6cU));
        append_payload(bytes, 31U, 0x54U);
        expect_vif_error(
            [&] { (void)openrc::parse_scene_block_vif_stream_v1(bytes, kGenerousLimits); },
            "an UNPACK payload that overshoots the stream was accepted");
    }
    {
        auto bytes = valid_all_command_stream();
        bytes.push_back(std::byte{0});
        expect_vif_error(
            [&] { (void)openrc::parse_scene_block_vif_stream_v1(bytes, kGenerousLimits); },
            "a trailing partial VIFcode was accepted");
    }
}

} // namespace

int main() {
    try {
        test_all_commands_ranges_and_accounting();
        test_fill_cycle_and_num_zero();
        test_mandatory_and_aggregate_limits();
        test_opcode_and_reserved_field_rejections();
        test_truncation_overshoot_and_padding_rejections();
        std::cout << "SceneBlock VIF tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SceneBlock VIF tests failed: " << error.what() << '\n';
        return 1;
    }
}
