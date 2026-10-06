#include "openrc/audio_gain_table.hpp"

#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <bit>
#include <utility>

namespace openrc {
namespace {
constexpr std::array<std::byte, 8> magic{std::byte{'O'}, std::byte{'R'}, std::byte{'A'}, std::byte{'G'},
    std::byte{'A'}, std::byte{'I'}, std::byte{'N'}, std::byte{'1'}};
constexpr std::uint64_t fixed_bytes = 96, cell_bytes = 12;
[[noreturn]] void fail(const char* message) { throw AudioGainTableError(message); }
void put(std::vector<std::byte>& bytes, std::uint64_t value, unsigned width = 4) {
    for (unsigned i = 0; i < width; ++i) { bytes.push_back(static_cast<std::byte>(value & 255U)); value >>= 8; }
}
struct Reader {
    std::span<const std::byte> bytes;
    std::size_t at = 0;
    std::uint64_t get(unsigned width = 4) {
        if (at > bytes.size() || width > bytes.size() - at) fail("Truncated audio gain table");
        std::uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i) value |= std::uint64_t{std::to_integer<unsigned>(bytes[at++])} << (8U * i);
        return value;
    }
    std::uint32_t word() { return static_cast<std::uint32_t>(get()); }
};
std::uint64_t count(const AudioGainTableV1& value, AudioGainTableLimitsV1 limits) {
    if (!value.state_count || value.state_count > limits.max_states ||
        !value.level_count || value.level_count > limits.max_levels ||
        !value.pan_count || value.pan_count > limits.max_pans ||
        value.initial_state >= value.state_count || !value.gain_denominator || value.gain_denominator > (1U << 30U) ||
        limits.max_bytes < fixed_bytes || !limits.max_cells)
        fail("Audio gain table shape exceeds its neutral limits");
    const auto maximum = std::min(limits.max_cells, (limits.max_bytes - fixed_bytes) / cell_bytes);
    // Check every factor before multiplication, including caller-supplied
    // permissive limits; three uint32 dimensions need not fit uint64.
    if (value.level_count > maximum / value.state_count)
        fail("Audio gain table dimensions exceed its cell limit");
    const auto plane = std::uint64_t{value.state_count} * value.level_count;
    if (value.pan_count > maximum / plane)
        fail("Audio gain table dimensions exceed its cell limit");
    return plane * value.pan_count;
}
void cell(const AudioGainTableCellV1& value, const AudioGainTableV1& table) {
    const auto bound = static_cast<std::int32_t>(table.gain_denominator);
    if (value.next_state >= table.state_count || value.gains[0] < -bound || value.gains[0] > bound ||
        value.gains[1] < -bound || value.gains[1] > bound)
        fail("Audio gain table cell leaves its state or signed gain domain");
}
} // namespace

void validate_audio_gain_table_v1(const AudioGainTableV1& value, AudioGainTableLimitsV1 limits) {
    if (count(value, limits) != value.cells.size()) fail("Audio gain table cells do not partition its dimensions");
    for (const auto& entry : value.cells) cell(entry, value);
}
std::vector<std::byte> encode_audio_gain_table_v1(const AudioGainTableV1& value, AudioGainTableLimitsV1 limits) {
    validate_audio_gain_table_v1(value, limits);
    std::vector<std::byte> body;
    body.reserve(static_cast<std::size_t>(32U + cell_bytes * value.cells.size()));
    put(body, value.state_count); put(body, value.level_count); put(body, value.pan_count);
    put(body, value.initial_state); put(body, value.gain_denominator); put(body, 0);
    put(body, value.cells.size(), 8);
    for (const auto& entry : value.cells) {
        for (const auto gain : entry.gains) put(body, std::bit_cast<std::uint32_t>(gain));
        put(body, entry.next_state);
    }
    std::vector<std::byte> output(magic.begin(), magic.end());
    put(output, 1); put(output, 64); put(output, 64U + body.size(), 8); put(output, 0, 8);
    const auto digest = prepared_content_sha256_v1(body);
    output.insert(output.end(), digest.begin(), digest.end());
    output.insert(output.end(), body.begin(), body.end());
    return output;
}
AudioGainTableV1 decode_audio_gain_table_v1(std::span<const std::byte> bytes, AudioGainTableLimitsV1 limits) {
    if (bytes.size() < fixed_bytes || bytes.size() > limits.max_bytes || !std::equal(magic.begin(), magic.end(), bytes.begin()))
        fail("Invalid audio gain table envelope");
    Reader input{bytes, 8};
    if (input.get() != 1 || input.get() != 64 || input.get(8) != bytes.size() || input.get(8))
        fail("Invalid audio gain table header");
    const auto digest = prepared_content_sha256_v1(bytes.subspan(64));
    if (!std::equal(digest.begin(), digest.end(), bytes.begin() + 32)) fail("Audio gain table digest mismatch");
    input.at = 64; AudioGainTableV1 output;
    output.state_count = input.word(); output.level_count = input.word(); output.pan_count = input.word();
    output.initial_state = input.word(); output.gain_denominator = input.word();
    if (input.get()) fail("Invalid audio gain table flags");
    const auto declared = input.get(8), expected = count(output, limits);
    if (declared != expected || (bytes.size() - fixed_bytes) % cell_bytes ||
        expected != (bytes.size() - fixed_bytes) / cell_bytes || expected > output.cells.max_size())
        fail("Audio gain table cells do not partition its payload");
    output.cells.resize(static_cast<std::size_t>(expected));
    for (auto& entry : output.cells) {
        for (auto& gain : entry.gains) gain = std::bit_cast<std::int32_t>(input.word());
        entry.next_state = input.word(); cell(entry, output);
    }
    return output;
}

AudioGainTableResourceV1::AudioGainTableResourceV1(AudioGainTableV1 value, AudioGainTableLimitsV1 limits) {
    validate_audio_gain_table_v1(value, limits);
    table_ = std::make_shared<const AudioGainTableV1>(std::move(value));
}
AudioGainTablePlayerV1::AudioGainTablePlayerV1(AudioGainTableResourceV1 resource) : resource_(resource) { reset(); }
std::uint32_t AudioGainTablePlayerV1::gain_denominator() const noexcept { return resource_.definition().gain_denominator; }
void AudioGainTablePlayerV1::reset() noexcept { state_ = resource_.definition().initial_state; }
std::array<std::int32_t, 2> AudioGainTablePlayerV1::step(std::uint32_t level, std::uint32_t pan) {
    const auto& table = resource_.definition();
    if (level >= table.level_count || pan >= table.pan_count) fail("Audio gain table input index is outside its domain");
    const auto offset = (std::uint64_t{state_} * table.level_count + level) * table.pan_count + pan;
    const auto& entry = table.cells[static_cast<std::size_t>(offset)];
    state_ = entry.next_state;
    return entry.gains;
}

} // namespace openrc
