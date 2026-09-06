#include "openrc/session_state_io.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace openrc {
namespace {

constexpr std::array<std::byte, 8U> kMagic{
    std::byte{'O'}, std::byte{'R'}, std::byte{'S'}, std::byte{'S'},
    std::byte{'I'}, std::byte{'N'}, std::byte{'I'}, std::byte{'T'}};
constexpr std::uint64_t kSchemaDigest = 0x60U;
constexpr std::uint64_t kInitialDigest = 0x80U;

[[noreturn]] void fail(const std::string& message) {
    throw SessionStateIoError(message);
}

std::uint64_t add(const std::uint64_t a, const std::uint64_t b) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a) {
        fail("Session initial-state size or aggregate overflows uint64");
    }
    return a + b;
}

std::uint64_t multiply(const std::uint64_t a, const std::uint64_t b) {
    if (a != 0U && b > std::numeric_limits<std::uint64_t>::max() / a) {
        fail("Session initial-state table size overflows uint64");
    }
    return a * b;
}

void bounded(const std::uint64_t value, const std::uint64_t maximum,
             const char* description) {
    if (value > maximum) { fail(std::string(description) + " exceeds its limit"); }
}

void validate_limits(const SessionStateIoLimitsV1& limits) {
    const auto& state = limits.state;
    if (limits.max_input_bytes < kSessionStateIoHeaderBytesV1 ||
        state.max_buffers == 0U || state.max_views == 0U ||
        state.max_key_bytes == 0U || state.max_total_key_bytes == 0U ||
        state.max_buffer_bytes == 0U || state.max_total_buffer_bytes == 0U ||
        state.max_view_elements == 0U || state.max_total_view_elements == 0U ||
        state.max_batch_writes == 0U) {
        fail("Session initial-state I/O requires explicit positive limits");
    }
}

std::size_t host_size(const std::uint64_t value, const std::size_t maximum) {
    bounded(value, maximum, "Session initial-state host container size");
    bounded(value, std::numeric_limits<std::size_t>::max(), "Session initial-state host size");
    return static_cast<std::size_t>(value);
}

std::uint32_t read32(const std::span<const std::byte> bytes, const std::uint64_t offset) {
    std::uint32_t result = 0U;
    for (unsigned i = 0U; i < 4U; ++i) {
        result |= std::to_integer<std::uint32_t>(bytes[static_cast<std::size_t>(offset + i)]) << (i * 8U);
    }
    return result;
}

std::uint64_t read64(const std::span<const std::byte> bytes, const std::uint64_t offset) {
    std::uint64_t result = 0U;
    for (unsigned i = 0U; i < 8U; ++i) {
        result |= std::to_integer<std::uint64_t>(bytes[static_cast<std::size_t>(offset + i)]) << (i * 8U);
    }
    return result;
}

void write32(std::vector<std::byte>& bytes, const std::uint64_t offset, const std::uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i) {
        bytes[static_cast<std::size_t>(offset + i)] = static_cast<std::byte>((value >> (i * 8U)) & 255U);
    }
}

void write64(std::vector<std::byte>& bytes, const std::uint64_t offset, const std::uint64_t value) {
    for (unsigned i = 0U; i < 8U; ++i) {
        bytes[static_cast<std::size_t>(offset + i)] = static_cast<std::byte>((value >> (i * 8U)) & 255U);
    }
}

void require_zero(const std::span<const std::byte> bytes, const std::uint64_t begin,
                  const std::uint64_t end) {
    for (auto offset = begin; offset < end; ++offset) {
        if (bytes[static_cast<std::size_t>(offset)] != std::byte{0}) {
            fail("Session initial-state reserved bytes are nonzero");
        }
    }
}

struct Counts {
    std::uint32_t buffers = 0U;
    std::uint32_t views = 0U;
    std::uint64_t keys = 0U;
    std::uint64_t data = 0U;
};

struct Layout {
    std::uint64_t buffers;
    std::uint64_t views;
    std::uint64_t keys;
    std::uint64_t data;
    std::uint64_t total;
};

Layout layout_for(const Counts& counts) {
    Layout result{};
    result.buffers = kSessionStateIoHeaderBytesV1;
    result.views = add(result.buffers, multiply(counts.buffers, kSessionStateIoBufferRecordBytesV1));
    result.keys = add(result.views, multiply(counts.views, kSessionStateIoViewRecordBytesV1));
    result.data = add(result.keys, counts.keys);
    result.total = add(result.data, counts.data);
    return result;
}

Counts preflight_initial(const SessionStateInitialV1& initial, const SessionStateIoLimitsV1& limits) {
    bounded(initial.schema.buffers.size(), limits.state.max_buffers, "Session buffer count");
    bounded(initial.schema.views.size(), limits.state.max_views, "Session view count");
    if (initial.schema.buffers.empty() || initial.buffers.size() != initial.schema.buffers.size()) {
        fail("Session initial-state images must match the nonempty buffer declarations");
    }
    Counts counts;
    counts.buffers = static_cast<std::uint32_t>(initial.schema.buffers.size());
    counts.views = static_cast<std::uint32_t>(initial.schema.views.size());
    std::uint64_t logical_keys = 0U;
    const auto account_key = [&](const std::string& key, const bool encoded) {
        bounded(key.size(), limits.state.max_key_bytes, "Session key size");
        logical_keys = add(logical_keys, key.size());
        bounded(logical_keys, limits.state.max_total_key_bytes, "Session logical key bytes");
        if (encoded) { counts.keys = add(counts.keys, key.size()); }
    };
    account_key(initial.schema.identity_key, true);
    for (const auto& buffer : initial.schema.buffers) {
        account_key(buffer.key, true);
        bounded(buffer.byte_count, limits.state.max_buffer_bytes, "Session buffer size");
        counts.data = add(counts.data, buffer.byte_count);
        bounded(counts.data, limits.state.max_total_buffer_bytes, "Session total buffer bytes");
    }
    std::uint64_t elements = 0U;
    for (const auto& view : initial.schema.views) {
        account_key(view.key, true);
        account_key(view.buffer_key, false);
        bounded(view.element_count, limits.state.max_view_elements, "Session view element count");
        elements = add(elements, view.element_count);
        bounded(elements, limits.state.max_total_view_elements, "Session total view elements");
    }
    std::uint64_t image_bytes = 0U;
    for (const auto& image : initial.buffers) {
        account_key(image.buffer_key, false);
        bounded(image.bytes.size(), limits.state.max_buffer_bytes, "Session image size");
        image_bytes = add(image_bytes, image.bytes.size());
        bounded(image_bytes, limits.state.max_total_buffer_bytes, "Session total image bytes");
    }
    return counts;
}

void write_key(std::vector<std::byte>& bytes, std::uint64_t& cursor, const std::string& key) {
    for (const unsigned char ch : key) {
        bytes[static_cast<std::size_t>(cursor++)] = static_cast<std::byte>(ch);
    }
}

std::string_view key_at(const std::span<const std::byte> bytes,
                        const std::uint64_t offset, const std::uint32_t size) {
    return {reinterpret_cast<const char*>(bytes.data() + static_cast<std::size_t>(offset)), size};
}

std::string_view preflight_key(const std::span<const std::byte> bytes,
    const std::uint64_t offset, const std::uint32_t size, std::uint64_t& cursor,
    const Layout& layout, const SessionStateIoLimitsV1& limits) {
    if (size == 0U || size > limits.state.max_key_bytes || offset != cursor ||
        cursor > layout.data || size > layout.data - cursor) {
        fail("Session key is not in its exact canonical partition");
    }
    bounded(size, std::string{}.max_size(), "Session key host size");
    const auto key = key_at(bytes, offset, size);
    for (const unsigned char ch : key) {
        if (ch < 0x21U || ch > 0x7eU) { fail("Session key is not nonempty printable ASCII"); }
    }
    cursor += size;
    return key;
}

// Complete record preflight uses string views into bounded input: no storage,
// view, image, or copied string allocations occur before this succeeds.
void preflight_records(const std::span<const std::byte> bytes, const Counts& counts,
    const Layout& layout, const SessionStateIoLimitsV1& limits) {
    std::uint64_t key_cursor = layout.keys;
    const auto identity_size = read32(bytes, 0xa0U);
    (void)preflight_key(bytes, read64(bytes, 0xa8U), identity_size, key_cursor, layout, limits);
    std::uint64_t logical_keys = identity_size;
    std::uint64_t data_cursor = layout.data;
    std::string_view previous;
    for (std::uint32_t i = 0U; i < counts.buffers; ++i) {
        const auto record = layout.buffers + static_cast<std::uint64_t>(i) * kSessionStateIoBufferRecordBytesV1;
        require_zero(bytes, record + 4U, record + 8U);
        require_zero(bytes, record + 32U, record + 48U);
        const auto size = read32(bytes, record);
        const auto key = preflight_key(bytes, read64(bytes, record + 8U), size, key_cursor, layout, limits);
        if (i != 0U && !(previous < key)) { fail("Session buffer keys are not strictly canonical"); }
        previous = key;
        logical_keys = add(logical_keys, multiply(size, 2U)); // declaration + image label
        bounded(logical_keys, limits.state.max_total_key_bytes, "Session logical key bytes");
        const auto byte_count = read64(bytes, record + 16U);
        if (byte_count == 0U || byte_count > limits.state.max_buffer_bytes ||
            read64(bytes, record + 24U) != data_cursor || data_cursor > layout.total ||
            byte_count > layout.total - data_cursor) {
            fail("Session buffer image is not a complete canonical data partition");
        }
        bounded(byte_count, std::vector<std::byte>{}.max_size(), "Session image host size");
        data_cursor += byte_count;
    }
    if (data_cursor != layout.total) { fail("Session buffer images do not cover all data bytes"); }
    std::uint64_t elements = 0U;
    previous = {};
    for (std::uint32_t i = 0U; i < counts.views; ++i) {
        const auto record = layout.views + static_cast<std::uint64_t>(i) * kSessionStateIoViewRecordBytesV1;
        require_zero(bytes, record + 12U, record + 16U);
        require_zero(bytes, record + 48U, record + 64U);
        const auto key_size = read32(bytes, record);
        const auto key = preflight_key(bytes, read64(bytes, record + 16U), key_size, key_cursor, layout, limits);
        if (i != 0U && !(previous < key)) { fail("Session view keys are not strictly canonical"); }
        previous = key;
        const auto buffer_index = read32(bytes, record + 4U);
        const auto type = read32(bytes, record + 8U);
        if (buffer_index >= counts.buffers || type > 2U) { fail("Session view binding or type is unknown"); }
        const auto buffer_record = layout.buffers + static_cast<std::uint64_t>(buffer_index) * kSessionStateIoBufferRecordBytesV1;
        logical_keys = add(logical_keys, add(key_size, read32(bytes, buffer_record)));
        bounded(logical_keys, limits.state.max_total_key_bytes, "Session logical key bytes");
        const auto byte_count = read64(bytes, buffer_record + 16U);
        const auto offset = read64(bytes, record + 24U);
        const auto count = read64(bytes, record + 32U);
        const auto stride = read64(bytes, record + 40U);
        const std::uint64_t width = std::uint64_t{1U} << type;
        if (count == 0U || stride == 0U || count > limits.state.max_view_elements ||
            offset > byte_count || width > byte_count - offset ||
            count - 1U > (byte_count - offset - width) / stride) {
            fail("Session view range, count or stride is invalid");
        }
        elements = add(elements, count);
        bounded(elements, limits.state.max_total_view_elements, "Session total view elements");
    }
    if (key_cursor != layout.data) { fail("Session keys do not cover the exact string partition"); }
}

void write_digest(std::vector<std::byte>& bytes, const std::uint64_t offset,
                   const PreparedContentDigestV1& digest) {
    std::copy(digest.begin(), digest.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

void require_digest(const std::span<const std::byte> bytes, const std::uint64_t offset,
                     const PreparedContentDigestV1& digest) {
    if (!std::equal(digest.begin(), digest.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset))) {
        fail("Session initial-state schema or initial-data digest is stale");
    }
}

} // namespace

std::vector<std::byte> encode_session_state_initial_v1(
    const SessionStateInitialV1& initial, const SessionStateIoLimitsV1 limits) {
    validate_limits(limits);
    const auto counts = preflight_initial(initial, limits);
    const auto layout = layout_for(counts);
    bounded(layout.total, limits.max_input_bytes, "Session encoded bytes");
    const auto output_size = host_size(layout.total, std::vector<std::byte>{}.max_size());
    try {
        const auto canonical = canonicalize_session_state_initial_v1(initial, limits.state);
        const auto schema_digest = hash_session_state_schema_v1(canonical.schema, limits.state);
        const auto initial_digest = hash_session_state_initial_v1(canonical, limits.state);
        std::vector<std::byte> bytes(output_size, std::byte{0});
        std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
        write32(bytes, 0x08U, kSessionStateIoFormatVersionV1);
        write32(bytes, 0x0cU, kSessionStateIoHeaderBytesV1);
        write64(bytes, 0x10U, layout.total);
        write32(bytes, 0x18U, kSessionStateIoPayloadTypeV1);
        write32(bytes, 0x1cU, kSessionStateSchemaVersionV1);
        write32(bytes, 0x20U, counts.buffers);
        write32(bytes, 0x24U, counts.views);
        write64(bytes, 0x28U, counts.keys);
        write64(bytes, 0x30U, counts.data);
        write32(bytes, 0x38U, kSessionStateIoBufferRecordBytesV1);
        write32(bytes, 0x3cU, kSessionStateIoViewRecordBytesV1);
        write64(bytes, 0x40U, layout.buffers);
        write64(bytes, 0x48U, layout.views);
        write64(bytes, 0x50U, layout.keys);
        write64(bytes, 0x58U, layout.data);
        write_digest(bytes, kSchemaDigest, schema_digest);
        write_digest(bytes, kInitialDigest, initial_digest);
        write32(bytes, 0xa0U, static_cast<std::uint32_t>(canonical.schema.identity_key.size()));
        write64(bytes, 0xa8U, layout.keys);
        auto key_cursor = layout.keys;
        auto data_cursor = layout.data;
        write_key(bytes, key_cursor, canonical.schema.identity_key);
        for (std::size_t i = 0U; i < canonical.schema.buffers.size(); ++i) {
            const auto& buffer = canonical.schema.buffers[i];
            const auto record = layout.buffers + i * kSessionStateIoBufferRecordBytesV1;
            write32(bytes, record, static_cast<std::uint32_t>(buffer.key.size()));
            write64(bytes, record + 8U, key_cursor);
            write64(bytes, record + 16U, buffer.byte_count);
            write64(bytes, record + 24U, data_cursor);
            write_key(bytes, key_cursor, buffer.key);
            const auto& image = canonical.buffers[i].bytes;
            std::copy(image.begin(), image.end(), bytes.begin() + static_cast<std::ptrdiff_t>(data_cursor));
            data_cursor += buffer.byte_count;
        }
        for (std::size_t i = 0U; i < canonical.schema.views.size(); ++i) {
            const auto& view = canonical.schema.views[i];
            const auto buffer = std::lower_bound(canonical.schema.buffers.begin(), canonical.schema.buffers.end(),
                view.buffer_key, [](const SessionStateBufferV1& item, const std::string& key) { return item.key < key; });
            const auto record = layout.views + i * kSessionStateIoViewRecordBytesV1;
            write32(bytes, record, static_cast<std::uint32_t>(view.key.size()));
            write32(bytes, record + 4U, static_cast<std::uint32_t>(buffer - canonical.schema.buffers.begin()));
            write32(bytes, record + 8U, static_cast<std::uint32_t>(view.value_type));
            write64(bytes, record + 16U, key_cursor);
            write64(bytes, record + 24U, view.byte_offset);
            write64(bytes, record + 32U, view.element_count);
            write64(bytes, record + 40U, view.byte_stride);
            write_key(bytes, key_cursor, view.key);
        }
        if (key_cursor != layout.data || data_cursor != layout.total) {
            fail("Session initial-state internal canonical partition is inconsistent");
        }
        return bytes;
    } catch (const SessionStateError& error) {
        fail(std::string("Invalid session initial-state: ") + error.what());
    }
}

SessionStateInitialV1 decode_session_state_initial_v1(
    const std::span<const std::byte> bytes, const SessionStateIoLimitsV1 limits) {
    validate_limits(limits);
    bounded(bytes.size(), limits.max_input_bytes, "Session input bytes");
    if (bytes.size() < kSessionStateIoHeaderBytesV1 ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        fail("Session initial-state header is truncated or has invalid magic");
    }
    if (read32(bytes, 0x08U) != kSessionStateIoFormatVersionV1 ||
        read32(bytes, 0x0cU) != kSessionStateIoHeaderBytesV1 ||
        read32(bytes, 0x18U) != kSessionStateIoPayloadTypeV1 ||
        read32(bytes, 0x1cU) != kSessionStateSchemaVersionV1 ||
        read32(bytes, 0x38U) != kSessionStateIoBufferRecordBytesV1 ||
        read32(bytes, 0x3cU) != kSessionStateIoViewRecordBytesV1) {
        fail("Session initial-state version, payload or record size is unknown");
    }
    require_zero(bytes, 0xa4U, 0xa8U);
    require_zero(bytes, 0xb0U, kSessionStateIoHeaderBytesV1);
    const Counts counts{read32(bytes, 0x20U), read32(bytes, 0x24U),
        read64(bytes, 0x28U), read64(bytes, 0x30U)};
    if (counts.buffers == 0U) { fail("Session initial-state has no declared buffers"); }
    bounded(counts.buffers, limits.state.max_buffers, "Session buffer count");
    bounded(counts.views, limits.state.max_views, "Session view count");
    bounded(counts.keys, limits.state.max_total_key_bytes, "Session encoded key bytes");
    bounded(counts.data, limits.state.max_total_buffer_bytes, "Session encoded buffer bytes");
    const auto layout = layout_for(counts);
    if (read64(bytes, 0x10U) != layout.total || layout.total != bytes.size() ||
        read64(bytes, 0x40U) != layout.buffers || read64(bytes, 0x48U) != layout.views ||
        read64(bytes, 0x50U) != layout.keys || read64(bytes, 0x58U) != layout.data) {
        fail("Session initial-state tables or exact byte size are noncanonical");
    }
    preflight_records(bytes, counts, layout, limits);
    SessionStateInitialV1 result;
    const auto buffer_count = host_size(counts.buffers, result.schema.buffers.max_size());
    const auto image_count = host_size(counts.buffers, result.buffers.max_size());
    const auto view_count = host_size(counts.views, result.schema.views.max_size());
    result.schema.identity_key = key_at(bytes, read64(bytes, 0xa8U), read32(bytes, 0xa0U));
    result.schema.buffers.reserve(buffer_count);
    result.buffers.reserve(image_count);
    result.schema.views.reserve(view_count);
    for (std::uint32_t i = 0U; i < counts.buffers; ++i) {
        const auto record = layout.buffers + static_cast<std::uint64_t>(i) * kSessionStateIoBufferRecordBytesV1;
        SessionStateBufferV1 buffer;
        buffer.key = key_at(bytes, read64(bytes, record + 8U), read32(bytes, record));
        buffer.byte_count = read64(bytes, record + 16U);
        SessionStateBufferBytesV1 image;
        image.buffer_key = buffer.key;
        const auto size = host_size(buffer.byte_count, image.bytes.max_size());
        const auto data_offset = static_cast<std::size_t>(read64(bytes, record + 24U));
        const auto data = bytes.subspan(data_offset, size);
        image.bytes.assign(data.begin(), data.end());
        result.schema.buffers.push_back(std::move(buffer));
        result.buffers.push_back(std::move(image));
    }
    for (std::uint32_t i = 0U; i < counts.views; ++i) {
        const auto record = layout.views + static_cast<std::uint64_t>(i) * kSessionStateIoViewRecordBytesV1;
        SessionStateViewV1 view;
        view.key = key_at(bytes, read64(bytes, record + 16U), read32(bytes, record));
        view.buffer_key = result.schema.buffers[read32(bytes, record + 4U)].key;
        view.value_type = static_cast<SessionStateValueTypeV1>(read32(bytes, record + 8U));
        view.byte_offset = read64(bytes, record + 24U);
        view.element_count = read64(bytes, record + 32U);
        view.byte_stride = read64(bytes, record + 40U);
        result.schema.views.push_back(std::move(view));
    }
    try {
        require_digest(bytes, kSchemaDigest, hash_session_state_schema_v1(result.schema, limits.state));
        require_digest(bytes, kInitialDigest, hash_session_state_initial_v1(result, limits.state));
    } catch (const SessionStateError& error) {
        fail(std::string("Invalid decoded session initial-state: ") + error.what());
    }
    return result;
}

} // namespace openrc
