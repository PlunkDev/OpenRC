#include "openrc/rac_initial_progress_template.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
struct Tag { std::uint32_t key; std::uint32_t size; };
constexpr std::array<Tag, 47U> kPrimary{{
    {0,4},{1,4},{2,4},{3,4},{4,8},{5,0x80},{7,0xc},{8,0x20},{9,0x94},
    {0xa,0x25},{0xb,0x25},{0xc,0xc},{0xd,0x20},{0x13,4},{0xe,0x14},
    {0x14,0x50},{0xf,0x790},{0x10,0x4a0},{0x11,0x120},{0x12,0x128},
    {0x15,4},{0x16,4},{0x17,4},{0x18,4},{0x19,4},{0x1a,4},{0x1b,4},
    {0x1c,1},{0x1d,1},{0x1e,0x28},{0x1f,4},{0x20,0x1c},{0x21,1},
    {0x22,4},{0x23,4},{0x24,4},{0x25,0xc},{0x3e8,0x94},{0x3e9,0x94},
    {0x3ea,0x94},{0x3eb,4},{0x3ec,4},{0x3ed,4},{0x3f0,4},{0x3f1,4},
    {0x3f2,0x96},{0x3f3,4},
}};
constexpr std::array<Tag, 11U> kRepeated{{
    {0xbb9,1},{0xbba,0x800},{0xbbb,4},{0xbbc,16},{0xbbd,256},{0xbbe,256},
    {0xbbf,8},{0xbc0,16},{0xfa0,4},{0xfa1,4},{0xfa2,4},
}};
constexpr openrc::Ps2SaveBundleLimits kLimits{
    1024U * 1024U, 1U, 20U, 267U, 1024U * 1024U};

void expect(const bool okay, const std::string& message) {
    if (!okay) { throw std::runtime_error(message); }
}

std::uint32_t read32(const Bytes& bytes, const std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned byte = 0U; byte < 4U; ++byte) {
        value |= std::to_integer<std::uint32_t>(bytes.at(offset + byte)) << (byte * 8U);
    }
    return value;
}

void write32(Bytes& bytes, const std::size_t offset, const std::uint32_t value) {
    for (unsigned byte = 0U; byte < 4U; ++byte) {
        bytes.at(offset + byte) = static_cast<std::byte>((value >> (byte * 8U)) & 255U);
    }
}

void append32(Bytes& bytes, const std::uint32_t value) {
    const auto offset = bytes.size();
    bytes.resize(offset + 4U);
    write32(bytes, offset, value);
}

// Independent 16-bit implementation for the synthetic fixture writer.
void checksum(Bytes& record) {
    std::uint16_t value = 0x8320U;
    for (std::size_t index = 8U; index < record.size(); ++index) {
        value = static_cast<std::uint16_t>(value ^
            (std::to_integer<unsigned>(record[index]) << 8U));
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const bool carry = (value & 0x8000U) != 0U;
            value = static_cast<std::uint16_t>(value << 1U);
            if (carry) { value ^= 0x1f45U; }
        }
    }
    expect(value != 0U, "fixture checksum accidentally uses the rejected zero value");
    write32(record, 4U, value);
}

std::uint8_t pattern(const std::uint32_t key, const std::size_t index,
    const std::size_t row) {
    return static_cast<std::uint8_t>((key * 13U + index * 37U + row * 17U) ^ 0xa5U);
}

Bytes make_record(const std::span<const Tag> layout, const std::size_t row) {
    Bytes record(8U);
    for (const auto& tag : layout) {
        append32(record, tag.key);
        append32(record, tag.size);
        for (std::size_t index = 0U; index < tag.size; ++index) {
            record.push_back(static_cast<std::byte>(
                tag.key == 0U ? 255U : pattern(tag.key, index, row)));
        }
        while ((record.size() & 3U) != 0U) { record.push_back(std::byte{0}); }
    }
    append32(record, 0xffffffffU);
    append32(record, 0U);
    write32(record, 0U, static_cast<std::uint32_t>(record.size() - 8U));
    checksum(record);
    return record;
}

std::size_t find_tag(const Bytes& record, const std::uint32_t wanted) {
    for (std::size_t cursor = 8U; cursor < record.size();) {
        const auto key = read32(record, cursor);
        if (key == wanted) { return cursor; }
        if (key == 0xffffffffU) { break; }
        cursor = (cursor + 8U + read32(record, cursor + 4U) + 3U) & ~std::size_t{3U};
    }
    throw std::runtime_error("fixture tag not found");
}

struct Fixture {
    Bytes primary = make_record(kPrimary, 0U);
    std::vector<Bytes> rows;
    Fixture() {
        for (std::size_t row = 0U; row < 20U; ++row) {
            rows.push_back(make_record(kRepeated, row));
        }
    }
};

Bytes envelope(const Fixture& fixture) {
    Bytes icon(openrc::kPs2IconSysSize);
    icon[0] = std::byte{'P'}; icon[1] = std::byte{'S'};
    icon[2] = std::byte{'2'}; icon[3] = std::byte{'D'};
    Bytes model(openrc::kPs2MemoryCardIconHeaderSize +
        openrc::kPs2MemoryCardIconVertexSize + openrc::kPs2MemoryCardIconAnimationSize +
        openrc::kPs2MemoryCardIconTextureSize);
    write32(model, 0U, openrc::kPs2MemoryCardIconVersion);
    write32(model, 4U, openrc::kPs2MemoryCardIconShapeCount);
    write32(model, 8U, openrc::kPs2MemoryCardIconTextureType);
    write32(model, 16U, 1U);
    Bytes state;
    append32(state, static_cast<std::uint32_t>(fixture.primary.size()));
    append32(state, 0xaa4U);
    state.insert(state.end(), fixture.primary.begin(), fixture.primary.end());
    for (const auto& row : fixture.rows) {
        state.insert(state.end(), row.begin(), row.end());
    }
    Bytes result(openrc::kPs2SaveBundleHeaderSize);
    const std::array<const Bytes*, 3U> records{&icon, &model, &state};
    for (std::size_t index = 0U; index < records.size(); ++index) {
        write32(result, index * 8U, static_cast<std::uint32_t>(result.size()));
        write32(result, index * 8U + 4U, static_cast<std::uint32_t>(records[index]->size()));
        result.insert(result.end(), records[index]->begin(), records[index]->end());
    }
    result.resize((result.size() + 2047U) / 2048U * 2048U);
    return result;
}

void rejected(const Bytes& bytes, const std::string& why,
    const openrc::Ps2SaveBundleLimits limits = kLimits) {
    try {
        (void)openrc::parse_rac_initial_progress_template_v1(bytes, limits);
    } catch (const openrc::RacInitialProgressTemplateError&) {
        return;
    }
    throw std::runtime_error("accepted " + why);
}

void verify_all_fields(const openrc::RacInitialProgressTemplateV1& actual) {
    expect(actual.encoded_source_level == -1, "source level -1 was normalized");
    for (std::size_t row = 0U; row < actual.rows.size(); ++row) {
        for (std::size_t byte = 0U; byte < 16U; ++byte) {
            expect(actual.rows[row].selector_bytes[byte] == pattern(0xbbcU, byte, row),
                "selector payload or row order lost");
        }
        for (std::size_t slot = 0U; slot < 64U; ++slot) {
            std::uint32_t bits = 0U;
            for (unsigned byte = 0U; byte < 4U; ++byte) {
                bits |= static_cast<std::uint32_t>(pattern(0xbbdU, slot * 4U + byte, row))
                    << (8U * byte);
            }
            expect(actual.rows[row].primary_bit_words[slot] == bits,
                "persistent bit word lost bytes or little-endian order");
            const auto key = static_cast<std::uint16_t>(
                pattern(0xbbeU, slot * 4U, row) |
                (static_cast<unsigned>(pattern(0xbbeU, slot * 4U + 1U, row)) << 8U));
            const auto auxiliary = static_cast<std::uint16_t>(
                pattern(0xbbeU, slot * 4U + 2U, row) |
                (static_cast<unsigned>(pattern(0xbbeU, slot * 4U + 3U, row)) << 8U));
            expect(actual.rows[row].registration_slots[slot].key ==
                std::bit_cast<std::int16_t>(key), "signed registration key changed");
            expect(actual.rows[row].registration_slots[slot].auxiliary_bits == auxiliary,
                "registration auxiliary halfword changed");
        }
    }
}

void test_complete_and_reordered() {
    Fixture fixture;
    verify_all_fields(openrc::parse_rac_initial_progress_template_v1(envelope(fixture), kLimits));
    auto primary = kPrimary;
    auto repeated = kRepeated;
    std::reverse(primary.begin(), primary.end());
    std::reverse(repeated.begin(), repeated.end());
    fixture.primary = make_record(primary, 0U);
    for (std::size_t row = 0U; row < fixture.rows.size(); ++row) {
        fixture.rows[row] = make_record(repeated, row);
    }
    verify_all_fields(openrc::parse_rac_initial_progress_template_v1(envelope(fixture), kLimits));
    write32(fixture.primary, find_tag(fixture.primary, 0U) + 8U, 0x80000000U);
    checksum(fixture.primary);
    expect(openrc::parse_rac_initial_progress_template_v1(envelope(fixture), kLimits)
        .encoded_source_level == std::numeric_limits<std::int32_t>::min(),
        "encoded level was clamped or forced to a selected row");
}

void test_layout_rejections() {
    Fixture fixture;
    write32(fixture.primary, find_tag(fixture.primary, 0U), 1U);
    checksum(fixture.primary);
    rejected(envelope(fixture), "duplicate primary tag with missing level tag");
    fixture = Fixture{};
    write32(fixture.rows[19], find_tag(fixture.rows[19], 0xbbcU), 0xbc0U);
    checksum(fixture.rows[19]);
    rejected(envelope(fixture), "duplicate final-row tag with missing selectors");
    fixture = Fixture{};
    write32(fixture.rows[10], find_tag(fixture.rows[10], 0xfa2U), 0xabcdefU);
    checksum(fixture.rows[10]);
    rejected(envelope(fixture), "unknown non-extracted tag");
    fixture = Fixture{};
    write32(fixture.rows[0], find_tag(fixture.rows[0], 0xbb9U) + 4U, 2U);
    checksum(fixture.rows[0]);
    rejected(envelope(fixture), "wrong-sized unrelated tag with unchanged aligned size");
    fixture = Fixture{};
    fixture.rows.pop_back();
    rejected(envelope(fixture), "missing level row");
    fixture.rows.push_back(make_record(kRepeated, 19U));
    fixture.rows.push_back(make_record(kRepeated, 20U));
    rejected(envelope(fixture), "extra level row");
    auto malformed = envelope(Fixture{});
    write32(malformed, 0x10U, 0xffffffffU);
    rejected(malformed, "out-of-bounds template offset");
    rejected(Bytes(23U), "truncated envelope header");
}

void test_checksums_and_limits() {
    Fixture fixture;
    fixture.primary[find_tag(fixture.primary, 0x3f3U) + 8U] ^= std::byte{1};
    rejected(envelope(fixture), "corrupt primary checksum outside extracted fields");
    fixture = Fixture{};
    fixture.rows[19][find_tag(fixture.rows[19], 0xbbeU) + 8U] ^= std::byte{1};
    rejected(envelope(fixture), "corrupt final-row checksum");
    fixture = Fixture{};
    write32(fixture.primary, 4U, 0U);
    rejected(envelope(fixture), "zero checksum");
    fixture = Fixture{};
    write32(fixture.rows[0], 4U, read32(fixture.rows[0], 4U) | 0x10000U);
    rejected(envelope(fixture), "nonzero checksum upper bits");
    fixture = Fixture{};
    const auto bytes = envelope(fixture);
    auto limits = kLimits;
    limits.max_input_bytes = bytes.size() - 1U;
    rejected(bytes, "input over limit", limits);
    limits = kLimits; limits.max_vertex_count = 0U;
    rejected(bytes, "icon vertex over limit", limits);
    limits = kLimits; limits.max_repeated_record_count = 19U;
    rejected(bytes, "row count over caller limit", limits);
    limits = kLimits; limits.max_tlv_entry_count = 266U;
    rejected(bytes, "entry count over caller limit", limits);
    limits = kLimits; limits.max_tlv_payload_bytes = 1U;
    rejected(bytes, "payload size over caller limit", limits);
}

} // namespace

int main() {
    try {
        test_complete_and_reordered();
        test_layout_rejections();
        test_checksums_and_limits();
        std::cout << "RAC initial-progress template tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RAC initial-progress template tests failed: " << error.what() << '\n';
        return 1;
    }
}
