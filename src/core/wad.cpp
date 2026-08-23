#include "openrc/wad.hpp"

#include "openrc/hash.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace openrc {
namespace {

constexpr std::size_t kReadChunkSize = 64U * 1024U;
constexpr std::uint64_t kStreamAlignment = 0x1000U;

[[noreturn]] void fail(const std::string& message) {
    throw WadError(message);
}

[[nodiscard]] std::uint64_t checked_sector_bytes(
    const std::uint64_t sectors,
    const char* description) {
    if (sectors > std::numeric_limits<std::uint64_t>::max() / kWadSectorSize) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return sectors * kWadSectorSize;
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

[[nodiscard]] std::uint8_t byte_value(const std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_le32(const std::byte* bytes) {
    return static_cast<std::uint32_t>(byte_value(bytes[0])) |
        (static_cast<std::uint32_t>(byte_value(bytes[1])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[2])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[3])) << 24U);
}

void read_exact(
    std::ifstream& input,
    const std::span<std::byte> destination,
    const char* description) {
    input.read(
        reinterpret_cast<char*>(destination.data()),
        static_cast<std::streamsize>(destination.size()));
    if (input.gcount() != static_cast<std::streamsize>(destination.size())) {
        fail(std::string("Unexpected end of image while reading ") + description);
    }
}

void require_plain_regular_file(const std::filesystem::path& image_path) {
    std::error_code filesystem_error;
    const auto status = std::filesystem::symlink_status(image_path, filesystem_error);
    if (filesystem_error || status.type() != std::filesystem::file_type::regular) {
        fail("The WAD source is not a plain regular file");
    }
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(image_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        fail("The WAD source is not a plain regular file");
    }
#endif
}

class LogicalReader final {
public:
    LogicalReader(
        std::ifstream& input,
        const std::uint64_t byte_count,
        Sha256& logical_hash)
        : input_(input), remaining_(byte_count), logical_hash_(logical_hash) {}

    [[nodiscard]] std::uint64_t remaining() const noexcept { return remaining_; }
    [[nodiscard]] std::uint64_t position() const noexcept { return position_; }

    void read(const std::span<std::byte> destination, const char* description) {
        if (destination.size() > remaining_) {
            fail(std::string("Compressed WadV1 stream ended while reading ") + description);
        }
        read_exact(input_, destination, description);
        logical_hash_.update(std::span<const std::byte>(destination.data(), destination.size()));
        remaining_ -= destination.size();
        position_ += destination.size();
    }

    [[nodiscard]] std::uint8_t read_byte(const char* description) {
        std::array<std::byte, 1> byte{};
        read(byte, description);
        return byte_value(byte[0]);
    }

    [[nodiscard]] std::uint8_t peek_byte(const char* description) {
        if (remaining_ == 0U) {
            fail(std::string("Compressed WadV1 stream ended before ") + description);
        }
        const auto value = input_.peek();
        if (value == std::char_traits<char>::eof()) {
            fail(std::string("Unexpected end of image before ") + description);
        }
        return static_cast<std::uint8_t>(static_cast<unsigned char>(value));
    }

private:
    std::ifstream& input_;
    std::uint64_t remaining_ = 0;
    std::uint64_t position_ = 0;
    Sha256& logical_hash_;
};

class MemoryLogicalReader final {
public:
    explicit MemoryLogicalReader(const std::span<const std::byte> input)
        : input_(input) {}

    [[nodiscard]] std::uint64_t remaining() const noexcept {
        return static_cast<std::uint64_t>(input_.size() - position_);
    }
    [[nodiscard]] std::uint64_t position() const noexcept {
        return static_cast<std::uint64_t>(position_);
    }

    void read(const std::span<std::byte> destination, const char* description) {
        if (destination.size() > input_.size() - position_) {
            fail(std::string("Compressed WadV1 stream ended while reading ") + description);
        }
        std::copy_n(input_.data() + position_, destination.size(), destination.data());
        position_ += destination.size();
    }

    [[nodiscard]] std::uint8_t read_byte(const char* description) {
        std::array<std::byte, 1> byte{};
        read(byte, description);
        return byte_value(byte[0]);
    }

    [[nodiscard]] std::uint8_t peek_byte(const char* description) const {
        if (position_ == input_.size()) {
            fail(std::string("Compressed WadV1 stream ended before ") + description);
        }
        return byte_value(input_[position_]);
    }

private:
    std::span<const std::byte> input_;
    std::size_t position_ = 0;
};

void require_output_growth(
    const std::vector<std::byte>& output,
    const std::uint64_t amount,
    const std::uint64_t max_decoded_bytes) {
    const auto current = static_cast<std::uint64_t>(output.size());
    if (current > max_decoded_bytes || amount > max_decoded_bytes - current) {
        fail("Decoded WadV1 output exceeds the caller-provided size limit");
    }
    if (amount > output.max_size() - output.size()) {
        fail("Decoded WadV1 output exceeds the host container limit");
    }
}

template <typename Reader>
void append_literals(
    Reader& reader,
    std::vector<std::byte>& output,
    const std::size_t count,
    const std::uint64_t max_decoded_bytes) {
    require_output_growth(output, count, max_decoded_bytes);
    std::array<std::byte, 512> literals{};
    if (count > literals.size()) {
        fail("Internal WadV1 literal bound was exceeded");
    }
    auto bytes = std::span<std::byte>(literals.data(), count);
    reader.read(bytes, "literal bytes");
    output.insert(output.end(), bytes.begin(), bytes.end());
}

void append_match(
    std::vector<std::byte>& output,
    const std::uint64_t length,
    const std::uint64_t distance,
    const std::uint64_t max_decoded_bytes) {
    if (distance == 0U || distance > output.size()) {
        fail("WadV1 match distance lies outside the decoded output");
    }
    require_output_growth(output, length, max_decoded_bytes);
    const auto host_distance = static_cast<std::size_t>(distance);
    for (std::uint64_t index = 0; index < length; ++index) {
        output.push_back(output[output.size() - host_distance]);
    }
}

template <typename Reader>
void consume_alignment_padding(Reader& reader) {
    const auto skip = (kStreamAlignment - (reader.position() % kStreamAlignment)) %
        kStreamAlignment;
    std::array<std::byte, kReadChunkSize> buffer{};
    std::uint64_t remaining = skip;
    while (remaining != 0U) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            remaining,
            buffer.size()));
        auto chunk = std::span<std::byte>(buffer.data(), count);
        reader.read(chunk, "WadV1 alignment padding");
        if (std::any_of(chunk.begin(), chunk.end(), [](const std::byte value) {
                return value != std::byte{0xee};
            })) {
            fail("WadV1 alignment padding contains a byte other than 0xee");
        }
        remaining -= count;
    }
}

template <typename Reader>
[[nodiscard]] std::vector<std::byte> decode_stream(
    Reader& reader,
    const std::uint64_t max_decoded_bytes) {
    std::vector<std::byte> output;
    while (reader.remaining() != 0U) {
        const auto flag = reader.read_byte("a WadV1 flag");
        if (flag < 0x10U) {
            const auto length = flag != 0U
                ? static_cast<std::size_t>(flag) + 3U
                : static_cast<std::size_t>(reader.read_byte("an extended literal length")) + 18U;
            append_literals(reader, output, length, max_decoded_bytes);
            if (reader.remaining() != 0U &&
                reader.peek_byte("the next WadV1 flag") < 0x10U) {
                fail("WadV1 contains two consecutive literal runs");
            }
            continue;
        }

        if (flag >= 0x40U) {
            const auto next = reader.read_byte("a short-match distance");
            const auto length = static_cast<std::uint64_t>(flag >> 5U) + 1U;
            const auto distance = static_cast<std::uint64_t>(next) * 8U +
                ((flag >> 2U) & 7U) + 1U;
            append_match(output, length, distance, max_decoded_bytes);
            append_literals(reader, output, flag & 3U, max_decoded_bytes);
            continue;
        }

        if (flag >= 0x20U) {
            const auto code = flag & 31U;
            const auto length = code != 0U
                ? static_cast<std::uint64_t>(code) + 2U
                : static_cast<std::uint64_t>(
                      reader.read_byte("an extended medium-match length")) + 33U;
            const auto byte_0 = reader.read_byte("a medium-match distance byte");
            const auto byte_1 = reader.read_byte("a medium-match distance byte");
            const auto distance = static_cast<std::uint64_t>(byte_1) * 64U +
                (byte_0 >> 2U) + 1U;
            append_match(output, length, distance, max_decoded_bytes);
            append_literals(reader, output, byte_0 & 3U, max_decoded_bytes);
            continue;
        }

        const auto code = flag & 7U;
        const auto base_length = code != 0U
            ? static_cast<std::uint64_t>(code)
            : static_cast<std::uint64_t>(
                  reader.read_byte("an extended long-match length")) + 7U;
        const auto byte_0 = reader.read_byte("a long-match control byte");
        const auto byte_1 = reader.read_byte("a long-match distance byte");
        const auto base = static_cast<std::uint64_t>(flag & 8U) * 0x800U +
            static_cast<std::uint64_t>(byte_1) * 64U + (byte_0 >> 2U);
        if (base != 0U) {
            append_match(
                output,
                base_length + 2U,
                0x4000U + base,
                max_decoded_bytes);
            append_literals(reader, output, byte_0 & 3U, max_decoded_bytes);
            continue;
        }

        if (base_length == 1U) {
            if (flag != 0x11U || byte_1 != 0U || byte_0 >= 4U) {
                fail("Malformed WadV1 dummy command");
            }
            append_literals(reader, output, byte_0 & 3U, max_decoded_bytes);
            continue;
        }

        if (flag != 0x12U || byte_0 != 0U || byte_1 != 0U) {
            fail("Malformed WadV1 alignment command");
        }
        consume_alignment_padding(reader);
    }
    return output;
}

void consume_logical_stream(LogicalReader& reader) {
    std::array<std::byte, kReadChunkSize> buffer{};
    while (reader.remaining() != 0U) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            reader.remaining(),
            buffer.size()));
        reader.read(
            std::span<std::byte>(buffer.data(), count),
            "the WadV1 compressed stream");
    }
}

struct ProcessedWad {
    WadReport report;
    std::vector<std::byte> decoded;
};

[[nodiscard]] ProcessedWad process_wad(
    const std::filesystem::path& image_path,
    const std::uint64_t logical_block,
    const std::uint64_t sector_count,
    const bool decode,
    const std::uint64_t max_decoded_bytes) {
    require_plain_regular_file(image_path);
    std::error_code filesystem_error;
    const auto image_size = std::filesystem::file_size(image_path, filesystem_error);
    if (filesystem_error) {
        fail("Cannot determine the WAD source size: " + filesystem_error.message());
    }
    const auto initial_write_time =
        std::filesystem::last_write_time(image_path, filesystem_error);
    if (filesystem_error) {
        fail("Cannot determine the WAD source modification time");
    }

    const auto extent_offset = checked_sector_bytes(logical_block, "the WAD extent offset");
    const auto extent_bytes = checked_sector_bytes(sector_count, "the WAD extent size");
    const auto extent_end = checked_add(extent_offset, extent_bytes, "the WAD extent end");
    if (extent_bytes < kWadV1HeaderSize) {
        fail("The WAD extent is too small to contain a WadV1 header");
    }
    if (extent_end > image_size) {
        fail("The WAD extent lies outside the source image");
    }
    if (extent_offset > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max()) ||
        extent_end > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max())) {
        fail("The WAD extent cannot be represented by the host input stream");
    }

    std::ifstream input(image_path, std::ios::binary);
    if (!input) {
        fail("Cannot open the WAD source image");
    }
    input.seekg(static_cast<std::streamoff>(extent_offset), std::ios::beg);
    if (!input) {
        fail("Cannot seek to the WAD extent");
    }

    std::array<std::byte, kWadV1HeaderSize> header{};
    read_exact(input, header, "the WadV1 header");
    for (std::size_t index = 0; index < kWadV1Magic.size(); ++index) {
        if (byte_value(header[index]) != static_cast<std::uint8_t>(kWadV1Magic[index])) {
            fail("The extent does not have a WadV1 signature");
        }
    }
    const auto total_bytes = read_le32(header.data() + 3);
    if (total_bytes < kWadV1HeaderSize) {
        fail("The WadV1 logical size is smaller than its header");
    }
    if (total_bytes > extent_bytes) {
        fail("The WadV1 logical size exceeds its extent");
    }
    const auto required_sectors =
        (static_cast<std::uint64_t>(total_bytes) + kWadSectorSize - 1U) /
        kWadSectorSize;
    if (required_sectors != sector_count) {
        fail("The WadV1 extent has extra or missing sectors for its logical size");
    }

    Sha256 logical_hash;
    logical_hash.update(header);
    LogicalReader reader(
        input,
        total_bytes - kWadV1HeaderSize,
        logical_hash);
    ProcessedWad result;
    if (decode) {
        result.decoded = decode_stream(reader, max_decoded_bytes);
    } else {
        consume_logical_stream(reader);
    }
    if (reader.remaining() != 0U) {
        fail("The WadV1 compressed stream was not consumed exactly");
    }

    std::array<std::byte, kReadChunkSize> buffer{};
    std::uint64_t padding_remaining = extent_bytes - total_bytes;
    while (padding_remaining != 0U) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            padding_remaining,
            buffer.size()));
        auto chunk = std::span<std::byte>(buffer.data(), count);
        read_exact(input, chunk, "WadV1 extent padding");
        if (std::any_of(chunk.begin(), chunk.end(), [](const std::byte value) {
                return value != std::byte{0};
            })) {
            fail("The WadV1 extent has non-zero bytes after its logical end");
        }
        padding_remaining -= count;
    }

    require_plain_regular_file(image_path);
    const auto final_size = std::filesystem::file_size(image_path, filesystem_error);
    if (filesystem_error || final_size != image_size) {
        fail("The WAD source size changed while it was being inspected");
    }
    const auto final_write_time =
        std::filesystem::last_write_time(image_path, filesystem_error);
    if (filesystem_error || final_write_time != initial_write_time) {
        fail("The WAD source changed while it was being inspected");
    }

    result.report.image_path = image_path;
    result.report.logical_block = logical_block;
    result.report.sector_count = sector_count;
    result.report.extent_bytes = extent_bytes;
    result.report.total_bytes = total_bytes;
    result.report.compressed_payload_bytes = total_bytes - kWadV1HeaderSize;
    result.report.padding_bytes = extent_bytes - total_bytes;
    std::transform(
        header.begin() + 7,
        header.end(),
        result.report.auxiliary_bytes.begin(),
        [](const std::byte value) { return byte_value(value); });
    result.report.sha256 = hex_digest(logical_hash.finish());
    return result;
}

} // namespace

WadReport inspect_wad(
    const std::filesystem::path& image_path,
    const std::uint64_t logical_block,
    const std::uint64_t sector_count) {
    return process_wad(image_path, logical_block, sector_count, false, 0).report;
}

DecodedWad decode_wad(
    const std::filesystem::path& image_path,
    const std::uint64_t logical_block,
    const std::uint64_t sector_count,
    const std::uint64_t max_decoded_bytes) {
    auto processed = process_wad(
        image_path,
        logical_block,
        sector_count,
        true,
        max_decoded_bytes);
    Sha256 decoded_hash;
    decoded_hash.update(std::span<const std::byte>(
        processed.decoded.data(),
        processed.decoded.size()));

    DecodedWad result;
    result.source = std::move(processed.report);
    result.bytes = std::move(processed.decoded);
    result.sha256 = hex_digest(decoded_hash.finish());
    return result;
}

DecodedWadBytes decode_wad_bytes(
    const std::span<const std::byte> logical_wad,
    const std::uint64_t max_decoded_bytes) {
    if (logical_wad.size() < kWadV1HeaderSize) {
        fail("The WadV1 logical segment is too small to contain its header");
    }
    for (std::size_t index = 0; index < kWadV1Magic.size(); ++index) {
        if (byte_value(logical_wad[index]) !=
            static_cast<std::uint8_t>(kWadV1Magic[index])) {
            fail("The logical segment does not have a WadV1 signature");
        }
    }
    const auto total_bytes = read_le32(logical_wad.data() + 3);
    if (total_bytes < kWadV1HeaderSize) {
        fail("The WadV1 logical size is smaller than its header");
    }
    if (static_cast<std::uint64_t>(logical_wad.size()) != total_bytes) {
        fail("The WadV1 logical segment size does not match its header");
    }

    MemoryLogicalReader reader(logical_wad.subspan(kWadV1HeaderSize));
    auto decoded = decode_stream(reader, max_decoded_bytes);
    if (reader.remaining() != 0U) {
        fail("The WadV1 compressed stream was not consumed exactly");
    }

    Sha256 decoded_hash;
    decoded_hash.update(std::span<const std::byte>(decoded.data(), decoded.size()));
    DecodedWadBytes result;
    result.bytes = std::move(decoded);
    result.sha256 = hex_digest(decoded_hash.finish());
    return result;
}

} // namespace openrc
