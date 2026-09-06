#include "openrc/session_state_io.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

namespace {

using Bytes = std::vector<std::byte>;
constexpr openrc::SessionStateIoLimitsV1 kLimits{
    4096U, {4U, 8U, 64U, 1024U, 64U, 128U, 64U, 128U, 16U}};

void expect(const bool value, const std::string& message) {
    if (!value) { throw std::runtime_error(message); }
}

std::uint64_t read64(const Bytes& bytes, const std::size_t offset) {
    std::uint64_t value = 0U;
    for (unsigned i = 0U; i < 8U; ++i) {
        value |= std::to_integer<std::uint64_t>(bytes.at(offset + i)) << (i * 8U);
    }
    return value;
}

std::uint32_t read32(const Bytes& bytes, const std::size_t offset) {
    std::uint32_t value = 0U;
    for (unsigned i = 0U; i < 4U; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes.at(offset + i)) << (i * 8U);
    }
    return value;
}

void write32(Bytes& bytes, const std::size_t offset, const std::uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i) {
        bytes.at(offset + i) = static_cast<std::byte>((value >> (i * 8U)) & 255U);
    }
}

void write64(Bytes& bytes, const std::size_t offset, const std::uint64_t value) {
    for (unsigned i = 0U; i < 8U; ++i) {
        bytes.at(offset + i) = static_cast<std::byte>((value >> (i * 8U)) & 255U);
    }
}

openrc::SessionStateInitialV1 make_initial() {
    using Type = openrc::SessionStateValueTypeV1;
    openrc::SessionStateInitialV1 result;
    result.schema.identity_key = "test/session";
    result.schema.buffers = {{"z-level", 4U}, {"a-state", 32U}, {"m-extra", 3U}};
    result.schema.views = {
        {"word", "a-state", Type::u32, 1U, 3U, 4U},
        {"keys", "a-state", Type::u16, 1U, 3U, 4U},
        {"aux", "a-state", Type::u16, 3U, 3U, 4U},
        {"packed", "a-state", Type::u16, 2U, 4U, 1U},
        {"bytes", "a-state", Type::u8, 0U, 32U, 1U},
        {"level", "z-level", Type::u32, 0U, 1U, 4U},
    };
    Bytes data;
    for (unsigned i = 0U; i < 32U; ++i) {
        data.push_back(static_cast<std::byte>((17U * i + 3U) & 255U));
    }
    result.buffers = {
        {"m-extra", {std::byte{0x87}, std::byte{0x54}, std::byte{0xc9}}},
        {"z-level", {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}}},
        {"a-state", data},
    };
    return result;
}

template<class Operation>
void rejected(Operation operation, const std::string& description) {
    try {
        operation();
    } catch (const openrc::SessionStateIoError&) {
        return;
    }
    throw std::runtime_error("Accepted " + description);
}

void rejects_bytes(const Bytes& bytes, const std::string& why,
                    const openrc::SessionStateIoLimitsV1 limits = kLimits) {
    rejected([&] { (void)openrc::decode_session_state_initial_v1(bytes, limits); }, why);
}

template<class Mutation>
void rejects_mutation(Mutation mutation, const std::string& why) {
    auto bytes = openrc::encode_session_state_initial_v1(make_initial(), kLimits);
    mutation(bytes);
    rejects_bytes(bytes, why);
}

void test_roundtrip_canonicalization_and_aliases() {
    const auto initial = make_initial();
    const auto saved_input = initial;
    const auto canonical = openrc::canonicalize_session_state_initial_v1(initial, kLimits.state);
    const auto bytes = openrc::encode_session_state_initial_v1(initial, kLimits);
    expect(initial == saved_input, "Encoding mutated caller data");
    const auto decoded = openrc::decode_session_state_initial_v1(bytes, kLimits);
    expect(decoded == canonical, "Round trip lost a schema field or canonical byte");
    expect(openrc::encode_session_state_initial_v1(decoded, kLimits) == bytes,
        "Canonical bytes changed after round trip");
    auto reordered = initial;
    std::reverse(reordered.schema.buffers.begin(), reordered.schema.buffers.end());
    std::reverse(reordered.schema.views.begin(), reordered.schema.views.end());
    std::reverse(reordered.buffers.begin(), reordered.buffers.end());
    expect(openrc::encode_session_state_initial_v1(reordered, kLimits) == bytes,
        "Equivalent declaration/image orders encoded differently");
    expect(read64(bytes, 0x10U) == bytes.size() && read32(bytes, 0x20U) == 3U &&
        read32(bytes, 0x24U) == 6U, "Wire header counts or total are incorrect");
    const auto schema_hash = openrc::hash_session_state_schema_v1(decoded.schema, kLimits.state);
    const auto initial_hash = openrc::hash_session_state_initial_v1(decoded, kLimits.state);
    expect(std::equal(schema_hash.begin(), schema_hash.end(), bytes.begin() + 0x60U) &&
        std::equal(initial_hash.begin(), initial_hash.end(), bytes.begin() + 0x80U),
        "Wire digests do not bind the canonical schema and complete initial bytes");

    openrc::game::SessionStateV1 session(decoded, kLimits.state);
    const auto old_word = session.read_u32("word", 1U);
    const auto old_aux = session.read_u16("aux", 1U);
    const std::array<openrc::game::SessionStateWriteV1, 1U> writes{{
        {"keys", 1U, openrc::SessionStateValueTypeV1::u16, 0xbbaaU}}};
    session.apply_batch(writes, 0U);
    expect(session.read_u32("word", 1U) == ((old_word & 0xffff0000U) | 0xbbaaU) &&
        session.read_u16("aux", 1U) == old_aux,
        "Decoded alias views became separate storage or lost their offset/stride");
    expect(session.read_u8("bytes", 5U) == 0xaaU && session.read_u8("bytes", 6U) == 0xbbU,
        "Decoded shared storage does not preserve little-endian alias writes");
    expect(session.read_u32("level", 0U) == 0xffffffffU,
        "Encoded unsigned bits were normalized to a default level");
    const auto extra = session.buffer_bytes("m-extra");
    expect(extra.size() == 3U && extra[2] == std::byte{0xc9}, "Unviewed bytes were dropped");
}

void test_unviewed_storage_and_missing_images() {
    auto initial = make_initial();
    initial.schema.views.clear();
    const auto bytes = openrc::encode_session_state_initial_v1(initial, kLimits);
    expect(openrc::decode_session_state_initial_v1(bytes, kLimits) ==
        openrc::canonicalize_session_state_initial_v1(initial, kLimits.state),
        "Storage-only artifact does not preserve its explicit bytes");
    initial.buffers.pop_back();
    rejected([&] { (void)openrc::encode_session_state_initial_v1(initial, kLimits); },
        "missing storage image");
    initial = make_initial();
    initial.buffers[0].bytes.pop_back();
    rejected([&] { (void)openrc::encode_session_state_initial_v1(initial, kLimits); },
        "short storage image with implicit zero tail");
    initial = make_initial();
    initial.buffers[0].buffer_key = initial.buffers[1].buffer_key;
    rejected([&] { (void)openrc::encode_session_state_initial_v1(initial, kLimits); },
        "duplicate image label");
    initial = make_initial();
    initial.schema.views[1].key = initial.schema.views[0].key;
    rejected([&] { (void)openrc::encode_session_state_initial_v1(initial, kLimits); },
        "duplicate view key");
}

void test_header_and_partitions() {
    for (const auto offset : {0U, 8U, 12U, 24U, 28U, 56U, 60U, 164U, 176U, 191U}) {
        rejects_mutation([offset](Bytes& bytes) { bytes[offset] ^= std::byte{1}; },
            "unknown header metadata or reserved bytes");
    }
    for (const auto offset : {0x10U, 0x28U, 0x30U, 0x40U, 0x48U, 0x50U, 0x58U, 0xa8U}) {
        rejects_mutation([offset](Bytes& bytes) { write64(bytes, offset, read64(bytes, offset) + 1U); },
            "noncanonical header partition");
    }
    rejects_mutation([](Bytes& bytes) { bytes.push_back(std::byte{0}); }, "trailing byte");
    rejects_mutation([](Bytes& bytes) { bytes.pop_back(); }, "truncated final image");
    rejects_mutation([](Bytes& bytes) { bytes.resize(191U); }, "truncated header");
    rejects_mutation([](Bytes& bytes) { write32(bytes, 0x20U, 0U); }, "zero buffer count");
    rejects_mutation([](Bytes& bytes) { write32(bytes, 0x20U, 0xffffffffU); }, "unbounded buffer count");
    rejects_mutation([](Bytes& bytes) { write32(bytes, 0x24U, 0xffffffffU); }, "unbounded view count");
    rejects_mutation([](Bytes& bytes) { write64(bytes, 0x28U, ~std::uint64_t{0}); }, "overflowing key partition");
    rejects_mutation([](Bytes& bytes) { write64(bytes, 0x30U, ~std::uint64_t{0}); }, "overflowing data partition");

    for (const auto relative : {4U, 32U, 47U}) {
        rejects_mutation([relative](Bytes& bytes) { bytes[0xc0U + relative] = std::byte{1}; },
            "buffer reserved bytes");
    }
    for (const auto relative : {8U, 24U}) {
        rejects_mutation([relative](Bytes& bytes) { write64(bytes, 0xc0U + relative,
            read64(bytes, 0xc0U + relative) + 1U); }, "buffer partition gap");
    }
    rejects_mutation([](Bytes& bytes) { write64(bytes, 0xc0U + 16U, 31U); }, "short declared storage");
    rejects_mutation([](Bytes& bytes) { write64(bytes, 0xc0U + 16U, 0U); }, "empty storage declaration");
    rejects_mutation([](Bytes& bytes) { write32(bytes, 0xc0U, 0U); }, "empty buffer key");
    rejects_mutation([](Bytes& bytes) {
        const auto first = static_cast<std::size_t>(read64(bytes, 0xc0U + 8U));
        const auto second = static_cast<std::size_t>(read64(bytes, 0xc0U + 48U + 8U));
        std::swap_ranges(bytes.begin() + static_cast<std::ptrdiff_t>(first),
            bytes.begin() + static_cast<std::ptrdiff_t>(first + 7U),
            bytes.begin() + static_cast<std::ptrdiff_t>(second));
    }, "noncanonical buffer key order");
}

void test_view_metadata_strings_and_digests() {
    for (const auto relative : {12U, 48U, 63U}) {
        rejects_mutation([relative](Bytes& bytes) {
            bytes[static_cast<std::size_t>(read64(bytes, 0x48U)) + relative] = std::byte{1};
        }, "view reserved bytes");
    }
    rejects_mutation([](Bytes& bytes) {
        write32(bytes, static_cast<std::size_t>(read64(bytes, 0x48U)) + 4U, 3U);
    }, "missing view storage");
    rejects_mutation([](Bytes& bytes) {
        write32(bytes, static_cast<std::size_t>(read64(bytes, 0x48U)) + 8U, 3U);
    }, "unknown view type");
    for (const auto relative : {32U, 40U}) {
        rejects_mutation([relative](Bytes& bytes) {
            write64(bytes, static_cast<std::size_t>(read64(bytes, 0x48U)) + relative, 0U);
        }, "zero view count or stride");
    }
    for (const auto relative : {24U, 32U, 40U}) {
        rejects_mutation([relative](Bytes& bytes) {
            write64(bytes, static_cast<std::size_t>(read64(bytes, 0x48U)) + relative, ~std::uint64_t{0});
        }, "overflowing view range");
    }
    rejects_mutation([](Bytes& bytes) {
        const auto record = static_cast<std::size_t>(read64(bytes, 0x48U));
        write64(bytes, record + 16U, read64(bytes, record + 16U) + 1U);
    }, "noncanonical view key partition");
    rejects_mutation([](Bytes& bytes) {
        bytes[static_cast<std::size_t>(read64(bytes, 0x50U))] = std::byte{' '};
    }, "whitespace key");
    rejects_mutation([](Bytes& bytes) {
        bytes[static_cast<std::size_t>(read64(bytes, 0x50U))] = std::byte{0x80};
    }, "non-ASCII key");
    rejects_mutation([](Bytes& bytes) {
        bytes[static_cast<std::size_t>(read64(bytes, 0x50U))] = std::byte{'T'};
    }, "stale schema digest after valid identity change");
    for (const auto offset : {0x60U, 0x80U}) {
        rejects_mutation([offset](Bytes& bytes) { bytes[offset] ^= std::byte{1}; }, "stale digest");
        rejects_mutation([offset](Bytes& bytes) {
            std::fill_n(bytes.begin() + offset, 32U, std::byte{0});
        }, "missing digest");
    }
    rejects_mutation([](Bytes& bytes) {
        const auto unseen = static_cast<std::size_t>(read64(bytes, 0xc0U + 48U + 24U));
        bytes[unseen + 2U] ^= std::byte{1};
    }, "corruption of bytes not covered by any view");
}

void test_limits() {
    const auto initial = make_initial();
    const auto bytes = openrc::encode_session_state_initial_v1(initial, kLimits);
    const auto check = [&](const openrc::SessionStateIoLimitsV1 limits, const std::string& why) {
        rejected([&] { (void)openrc::encode_session_state_initial_v1(initial, limits); }, why + " encoder");
        rejects_bytes(bytes, why + " decoder", limits);
    };
    auto limited = kLimits;
    limited.max_input_bytes = bytes.size() - 1U; check(limited, "encoded-byte limit");
    limited = kLimits; limited.state.max_buffers = 2U; check(limited, "buffer-count limit");
    limited = kLimits; limited.state.max_views = 5U; check(limited, "view-count limit");
    limited = kLimits; limited.state.max_key_bytes = 7U; check(limited, "individual key limit");
    limited = kLimits; limited.state.max_buffer_bytes = 31U; check(limited, "individual buffer limit");
    limited = kLimits; limited.state.max_total_buffer_bytes = 38U; check(limited, "aggregate storage limit");
    limited = kLimits; limited.state.max_view_elements = 31U; check(limited, "view-element limit");
    limited = kLimits; limited.state.max_total_view_elements = 45U; check(limited, "aggregate view-element limit");
    std::uint64_t all_keys = initial.schema.identity_key.size();
    for (const auto& buffer : initial.schema.buffers) { all_keys += buffer.key.size(); }
    for (const auto& view : initial.schema.views) { all_keys += view.key.size() + view.buffer_key.size(); }
    for (const auto& image : initial.buffers) { all_keys += image.buffer_key.size(); }
    limited = kLimits; limited.state.max_total_key_bytes = all_keys - 1U;
    expect(read64(bytes, 0x28U) < limited.state.max_total_key_bytes,
        "Fixture does not distinguish encoded and logical key budgets");
    check(limited, "logical alias/image key aggregate limit");
    limited.state.max_total_key_bytes = all_keys;
    expect(openrc::encode_session_state_initial_v1(initial, limited) == bytes &&
        openrc::decode_session_state_initial_v1(bytes, limited) ==
            openrc::canonicalize_session_state_initial_v1(initial, limited.state),
        "Exact logical key limit was rejected");
    limited = kLimits; limited.state.max_batch_writes = 0U; check(limited, "invalid mandatory core limits");
    check({}, "absent explicit I/O limits");
}

} // namespace

int main() {
    try {
        test_roundtrip_canonicalization_and_aliases();
        test_unviewed_storage_and_missing_images();
        test_header_and_partitions();
        test_view_metadata_strings_and_digests();
        test_limits();
        std::cout << "Session initial-state I/O tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Session initial-state I/O tests failed: " << error.what() << '\n';
        return 1;
    }
}
