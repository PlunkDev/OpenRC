#include "openrc/rac_initial_progress_template.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <string>
#include <utility>

namespace openrc {
namespace {

struct TagLayout {
    std::uint32_t key;
    std::uint32_t bytes;
};

// Recovered descriptor tag/size pairs, not payload defaults or runtime
// addresses. Input ordering may differ: the source searches each tag.
constexpr std::array<TagLayout, 47U> kPrimaryLayout{{
    {0U, 4U}, {1U, 4U}, {2U, 4U}, {3U, 4U}, {4U, 8U},
    {5U, 0x80U}, {7U, 0xcU}, {8U, 0x20U}, {9U, 0x94U},
    {0xaU, 0x25U}, {0xbU, 0x25U}, {0xcU, 0xcU}, {0xdU, 0x20U},
    {0x13U, 4U}, {0xeU, 0x14U}, {0x14U, 0x50U}, {0xfU, 0x790U},
    {0x10U, 0x4a0U}, {0x11U, 0x120U}, {0x12U, 0x128U},
    {0x15U, 4U}, {0x16U, 4U}, {0x17U, 4U}, {0x18U, 4U},
    {0x19U, 4U}, {0x1aU, 4U}, {0x1bU, 4U}, {0x1cU, 1U},
    {0x1dU, 1U}, {0x1eU, 0x28U}, {0x1fU, 4U}, {0x20U, 0x1cU},
    {0x21U, 1U}, {0x22U, 4U}, {0x23U, 4U}, {0x24U, 4U},
    {0x25U, 0xcU}, {0x3e8U, 0x94U}, {0x3e9U, 0x94U},
    {0x3eaU, 0x94U}, {0x3ebU, 4U}, {0x3ecU, 4U}, {0x3edU, 4U},
    {0x3f0U, 4U}, {0x3f1U, 4U}, {0x3f2U, 0x96U}, {0x3f3U, 4U},
}};

constexpr std::array<TagLayout, 11U> kRepeatedLayout{{
    {0xbb9U, 1U}, {0xbbaU, 0x800U}, {0xbbbU, 4U},
    {0xbbcU, 0x10U}, {0xbbdU, 0x100U}, {0xbbeU, 0x100U},
    {0xbbfU, 8U}, {0xbc0U, 0x10U}, {0xfa0U, 4U},
    {0xfa1U, 4U}, {0xfa2U, 4U},
}};

constexpr std::uint32_t kPrimaryBytes = 0x1530U;
constexpr std::uint32_t kRepeatedBytes = 0xaa4U;
constexpr std::uint32_t kTemplateBytes = 0xea08U;
static_assert(8U + kPrimaryBytes +
    kRacInitialProgressLevelRowsV1 * kRepeatedBytes == kTemplateBytes);

[[noreturn]] void fail(const std::string& message) {
    throw RacInitialProgressTemplateError(message);
}

[[nodiscard]] std::uint16_t read_le16(
    const std::span<const std::byte> bytes, const std::size_t offset) {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(bytes[offset]) |
        (std::to_integer<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes, const std::size_t offset) {
    return std::to_integer<std::uint32_t>(bytes[offset]) |
        (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

// Source record checksum includes the stream's entry headers, payloads,
// alignment padding and final sentinel, but excludes its own 8-byte header.
[[nodiscard]] std::uint32_t source_checksum(
    const std::span<const std::byte> stream) {
    std::uint32_t value = 0xedb88320U;
    for (const auto byte : stream) {
        value ^= std::to_integer<std::uint32_t>(byte) << 8U;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const auto polynomial = (value & 0x8000U) != 0U ? 0x1f45U : 0U;
            value = (value << 1U) ^ polynomial;
        }
    }
    return value & 0xffffU;
}

template<std::size_t Count>
void validate_record(
    const Ps2SaveTlvRecord& record,
    const std::span<const std::byte> template_bytes,
    const std::array<TagLayout, Count>& layout,
    const std::uint32_t expected_size) {
    if (record.size != expected_size || record.declared_stream_bytes > 0x1800U) {
        fail("Initial-progress record has an unsupported size");
    }
    const auto raw_record = template_bytes.subspan(record.offset, record.size);
    if (record.opaque_kind == 0U ||
        source_checksum(raw_record.subspan(8U)) != record.opaque_kind) {
        fail("Initial-progress record fails the source checksum");
    }
    if (record.entries.size() != layout.size()) {
        fail("Initial-progress record does not contain the complete tag layout");
    }
    std::array<bool, Count> seen{};
    for (const auto& entry : record.entries) {
        const auto found = std::find_if(layout.begin(), layout.end(),
            [&entry](const TagLayout& candidate) { return candidate.key == entry.key; });
        if (found == layout.end()) {
            fail("Initial-progress record contains an unsupported tag");
        }
        const auto index = static_cast<std::size_t>(found - layout.begin());
        if (seen[index] || entry.payload.size() != found->bytes) {
            fail("Initial-progress record contains a duplicate or wrong-sized tag");
        }
        seen[index] = true;
    }
}

[[nodiscard]] std::span<const std::byte> payload_for(
    const Ps2SaveTlvRecord& record, const std::uint32_t key) {
    const auto found = std::find_if(record.entries.begin(), record.entries.end(),
        [key](const Ps2SaveTlvEntry& entry) { return entry.key == key; });
    if (found == record.entries.end()) {
        fail("Initial-progress record is missing a required tag");
    }
    return found->payload;
}

} // namespace

RacInitialProgressTemplateV1 parse_rac_initial_progress_template_v1(
    const std::span<const std::byte> ps2d_envelope,
    Ps2SaveBundleLimits limits) {
    if (ps2d_envelope.size() > limits.max_input_bytes) {
        fail("Initial-progress bundle exceeds the input-size limit");
    }
    if (ps2d_envelope.size() < kPs2SaveBundleHeaderSize) {
        fail("Initial-progress bundle has no complete envelope header");
    }
    const auto template_offset = read_le32(ps2d_envelope, 0x10U);
    const auto template_size = read_le32(ps2d_envelope, 0x14U);
    if (template_size != kTemplateBytes ||
        template_offset > ps2d_envelope.size() ||
        template_size > ps2d_envelope.size() - template_offset) {
        fail("Initial-progress template has an unsupported or out-of-bounds extent");
    }
    const auto template_bytes = ps2d_envelope.subspan(template_offset, template_size);
    if (read_le32(template_bytes, 0U) != kPrimaryBytes ||
        read_le32(template_bytes, 4U) != kRepeatedBytes) {
        fail("Initial-progress template record sizes do not match source descriptors");
    }

    // Tighten, never widen, the caller's allocation limits before invoking the
    // structural parser. This profile cannot need more than these fixed counts.
    limits.max_repeated_record_count = std::min<std::uint64_t>(
        limits.max_repeated_record_count, kRacInitialProgressLevelRowsV1);
    limits.max_tlv_entry_count = std::min<std::uint64_t>(
        limits.max_tlv_entry_count,
        kPrimaryLayout.size() + kRepeatedLayout.size() * kRacInitialProgressLevelRowsV1);
    limits.max_tlv_payload_bytes = std::min<std::uint64_t>(
        limits.max_tlv_payload_bytes, kTemplateBytes);

    const auto bundle = [&]() {
        try {
            return parse_ps2_save_bundle(ps2d_envelope, limits);
        } catch (const Ps2SaveBundleError& error) {
            fail(std::string("Invalid initial-progress PS2D bundle: ") + error.what());
        }
    }();
    const auto& state = bundle.save_template;
    if (state.repeated_records.size() != kRacInitialProgressLevelRowsV1) {
        fail("Initial-progress template must contain all 20 source rows");
    }
    validate_record(state.primary_record, template_bytes, kPrimaryLayout, kPrimaryBytes);
    for (const auto& record : state.repeated_records) {
        validate_record(record, template_bytes, kRepeatedLayout, kRepeatedBytes);
    }

    RacInitialProgressTemplateV1 result;
    result.encoded_source_level = std::bit_cast<std::int32_t>(
        read_le32(payload_for(state.primary_record, 0U), 0U));
    for (std::size_t level = 0U; level < result.rows.size(); ++level) {
        const auto& record = state.repeated_records[level];
        auto& row = result.rows[level];
        const auto selectors = payload_for(record, 0xbbcU);
        std::transform(selectors.begin(), selectors.end(), row.selector_bytes.begin(),
            [](const std::byte byte) { return std::to_integer<std::uint8_t>(byte); });
        const auto bits = payload_for(record, 0xbbdU);
        const auto registration = payload_for(record, 0xbbeU);
        for (std::size_t slot = 0U; slot < row.primary_bit_words.size(); ++slot) {
            row.primary_bit_words[slot] = read_le32(bits, slot * 4U);
            row.registration_slots[slot].key = std::bit_cast<std::int16_t>(
                read_le16(registration, slot * 4U));
            row.registration_slots[slot].auxiliary_bits =
                read_le16(registration, slot * 4U + 2U);
        }
    }
    return result;
}

} // namespace openrc
