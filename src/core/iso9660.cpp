#include "iso9660.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace openrc::iso9660 {
namespace {

constexpr std::uint32_t kFirstDescriptorSector = 16;
constexpr std::uint32_t kMaxDescriptorCount = 128;
constexpr std::size_t kMaxDepth = 64;
constexpr std::size_t kMaxDirectoryCount = 65'536;
constexpr std::size_t kMaxRecordCount = 1'000'000;
constexpr std::uint64_t kMaxSingleDirectoryBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxTotalDirectoryBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::size_t kStreamChunkBytes = 64U * 1024U;

using Sector = std::array<std::uint8_t, kLogicalBlockSize>;

[[noreturn]] void fail(const std::string& message) {
    throw Error(message);
}

std::uint16_t read_le16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint16_t read_be16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[0]) << 8U) |
           static_cast<std::uint16_t>(bytes[1]);
}

std::uint32_t read_le32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint32_t read_be32(const std::uint8_t* bytes) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

std::uint16_t read_both16(const std::uint8_t* bytes, std::string_view field) {
    const auto little = read_le16(bytes);
    const auto big = read_be16(bytes + 2);
    if (little != big) {
        fail("ISO9660 little/big-endian mismatch in " + std::string(field));
    }
    return little;
}

std::uint32_t read_both32(const std::uint8_t* bytes, std::string_view field) {
    const auto little = read_le32(bytes);
    const auto big = read_be32(bytes + 4);
    if (little != big) {
        fail("ISO9660 little/big-endian mismatch in " + std::string(field));
    }
    return little;
}

std::uint64_t checked_add(
    std::uint64_t left,
    std::uint64_t right,
    std::string_view description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail("integer overflow while calculating " + std::string(description));
    }
    return left + right;
}

std::uint64_t block_offset(std::uint64_t block, std::string_view description) {
    if (block > std::numeric_limits<std::uint64_t>::max() / kLogicalBlockSize) {
        fail("integer overflow while calculating " + std::string(description));
    }
    return block * kLogicalBlockSize;
}

void read_exact_at(
    std::ifstream& input,
    std::uint64_t offset,
    void* destination,
    std::size_t byte_count) {
    constexpr auto max_stream_offset =
        static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max());
    if (offset > max_stream_offset ||
        byte_count > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        fail("image offset cannot be represented by the host I/O library");
    }

    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        fail("failed to seek in ISO image");
    }
    input.read(
        static_cast<char*>(destination),
        static_cast<std::streamsize>(byte_count));
    if (input.gcount() != static_cast<std::streamsize>(byte_count)) {
        fail("unexpected end of ISO image");
    }
}

Sector read_sector(std::ifstream& input, std::uint32_t sector, std::uint64_t image_bytes) {
    const auto offset = block_offset(sector, "descriptor offset");
    const auto end = checked_add(offset, kLogicalBlockSize, "descriptor end");
    if (end > image_bytes) {
        fail("volume descriptor lies outside the physical image");
    }
    Sector result{};
    read_exact_at(input, offset, result.data(), result.size());
    return result;
}

std::string trim_descriptor_text(
    const std::uint8_t* bytes,
    std::size_t length,
    std::string_view field) {
    std::size_t used = length;
    while (used != 0U && bytes[used - 1U] == static_cast<std::uint8_t>(' ')) {
        --used;
    }

    std::string result;
    result.reserve(used);
    for (std::size_t index = 0; index < used; ++index) {
        const auto value = bytes[index];
        if (value < 0x20U || value > 0x7eU) {
            fail("non-ASCII data in " + std::string(field));
        }
        result.push_back(static_cast<char>(value));
    }
    return result;
}

struct Record {
    std::uint32_t extent_block{};
    std::uint32_t data_length{};
    std::uint16_t volume_sequence{};
    std::uint8_t extended_attribute_blocks{};
    std::uint8_t flags{};
    std::vector<std::uint8_t> identifier;
};

bool is_directory(const Record& record) noexcept {
    return (record.flags & 0x02U) != 0U;
}

bool has_more_extents(const Record& record) noexcept {
    return (record.flags & 0x80U) != 0U;
}

std::uint64_t data_block(const Record& record) {
    return checked_add(
        record.extent_block,
        record.extended_attribute_blocks,
        "extent data block");
}

void validate_record_extent(
    const Record& record,
    std::uint32_t volume_blocks,
    std::uint64_t declared_bytes,
    std::uint64_t image_bytes) {
    const auto start_block = data_block(record);
    if (start_block > volume_blocks) {
        fail("record extent starts outside the declared volume");
    }
    if (record.data_length != 0U && start_block >= volume_blocks) {
        fail("non-empty record extent starts at the end of the declared volume");
    }

    const auto start = block_offset(start_block, "record extent offset");
    const auto end = checked_add(start, record.data_length, "record extent end");
    if (end > declared_bytes || end > image_bytes) {
        fail("record extent exceeds the declared or physical image boundary");
    }

    const auto raw_start = block_offset(record.extent_block, "raw extent offset");
    if (raw_start > declared_bytes || raw_start > image_bytes) {
        fail("extended attribute extent starts outside the image");
    }
}

Record parse_record(
    const std::uint8_t* bytes,
    std::size_t available,
    std::uint32_t volume_blocks,
    std::uint64_t declared_bytes,
    std::uint64_t image_bytes) {
    if (available < 34U) {
        fail("ISO9660 directory record is too short");
    }

    const auto record_length = static_cast<std::size_t>(bytes[0]);
    if (record_length < 34U || record_length > available) {
        fail("invalid ISO9660 directory record length");
    }

    const auto identifier_length = static_cast<std::size_t>(bytes[32]);
    if (identifier_length == 0U) {
        fail("empty ISO9660 file identifier");
    }
    const auto padding = (identifier_length % 2U == 0U) ? 1U : 0U;
    const auto minimum_length = checked_add(
        checked_add(33U, identifier_length, "directory record identifier"),
        padding,
        "directory record padding");
    if (minimum_length > record_length) {
        fail("file identifier exceeds its directory record");
    }
    if (padding != 0U && bytes[33U + identifier_length] != 0U) {
        fail("non-zero ISO9660 file identifier padding");
    }

    Record result;
    result.extended_attribute_blocks = bytes[1];
    result.extent_block = read_both32(bytes + 2, "directory record extent");
    result.data_length = read_both32(bytes + 10, "directory record data length");
    result.flags = bytes[25];
    result.volume_sequence = read_both16(bytes + 28, "directory record volume sequence");
    result.identifier.assign(bytes + 33, bytes + 33 + identifier_length);

    if (result.volume_sequence != 1U) {
        fail("multi-volume ISO9660 records are unsupported");
    }
    if (bytes[26] != 0U || bytes[27] != 0U) {
        fail("interleaved ISO9660 files are unsupported");
    }
    constexpr std::uint8_t supported_flags = 0x01U | 0x02U | 0x80U;
    if ((result.flags & 0x04U) != 0U) {
        fail("associated ISO9660 files are unsupported");
    }
    if ((result.flags & static_cast<std::uint8_t>(~supported_flags)) != 0U) {
        fail("unsupported ISO9660 directory-record flags");
    }
    if (is_directory(result) && has_more_extents(result)) {
        fail("multi-extent directories are unsupported");
    }

    validate_record_extent(result, volume_blocks, declared_bytes, image_bytes);
    return result;
}

bool is_current_directory(const Record& record) noexcept {
    return record.identifier.size() == 1U && record.identifier[0] == 0U;
}

bool is_parent_directory(const Record& record) noexcept {
    return record.identifier.size() == 1U && record.identifier[0] == 1U;
}

std::string raw_identifier(const Record& record) {
    std::string result;
    result.reserve(record.identifier.size());
    for (const auto value : record.identifier) {
        if (value < 0x20U || value > 0x7eU) {
            fail("non-ASCII ISO9660 file identifier");
        }
        result.push_back(static_cast<char>(value));
    }
    return result;
}

char ascii_upper(char value) noexcept {
    if (value >= 'a' && value <= 'z') {
        return static_cast<char>(value - ('a' - 'A'));
    }
    return value;
}

char ascii_lower(char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value + ('a' - 'A'));
    }
    return value;
}

bool is_reserved_windows_name(std::string_view component) {
    const auto dot = component.find('.');
    const auto base = component.substr(0, dot);
    std::string upper;
    upper.reserve(base.size());
    for (const auto character : base) {
        upper.push_back(ascii_upper(character));
    }

    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
        upper == "CLOCK$" || upper == "CONIN$" || upper == "CONOUT$") {
        return true;
    }
    if (upper.size() == 4U && (upper.starts_with("COM") || upper.starts_with("LPT")) &&
        upper[3] >= '1' && upper[3] <= '9') {
        return true;
    }
    return false;
}

std::string normalize_component(std::string_view raw) {
    if (raw.empty()) {
        fail("empty output path component");
    }

    for (const auto character : raw) {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x20U || value > 0x7eU) {
            fail("non-ASCII output path component");
        }
        switch (character) {
            case '<':
            case '>':
            case ':':
            case '"':
            case '/':
            case '\\':
            case '|':
            case '?':
            case '*':
                fail("hostile character in ISO9660 path component");
            default:
                break;
        }
    }

    std::string result(raw);
    const auto semicolon = result.rfind(';');
    if (semicolon != std::string::npos && semicolon + 1U < result.size()) {
        const bool numeric_suffix = std::all_of(
            result.begin() + static_cast<std::string::difference_type>(semicolon + 1U),
            result.end(),
            [](char value) { return value >= '0' && value <= '9'; });
        if (numeric_suffix) {
            result.erase(semicolon);
        }
    }

    if (result.empty() || result == "." || result == "..") {
        fail("unsafe ISO9660 output path component");
    }
    if (result.back() == '.' || result.back() == ' ') {
        fail("ISO9660 output path component has a trailing dot or space");
    }
    if (result.size() > 255U) {
        fail("ISO9660 output path component is too long");
    }
    if (is_reserved_windows_name(result)) {
        fail("Windows-reserved ISO9660 output path component");
    }
    return result;
}

std::string join_raw(std::string_view directory, std::string_view component) {
    std::string result;
    result.reserve(directory.size() + component.size() + 2U);
    result.push_back('/');
    if (!directory.empty()) {
        result.append(directory);
        result.push_back('/');
    }
    result.append(component);
    if (result.size() > 4096U) {
        fail("ISO9660 path is too long");
    }
    return result;
}

std::string join_output(std::string_view directory, std::string_view component) {
    std::string result;
    result.reserve(directory.size() + component.size() + 1U);
    if (!directory.empty()) {
        result.append(directory);
        result.push_back('/');
    }
    result.append(component);
    if (result.size() > 4096U) {
        fail("normalized output path is too long");
    }
    return result;
}

std::string lower_ascii(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        result.push_back(ascii_lower(character));
    }
    return result;
}

struct DirectoryIdentity {
    std::uint64_t start_block{};
    std::uint32_t byte_length{};
};

bool operator==(const DirectoryIdentity& left, const DirectoryIdentity& right) noexcept {
    return left.start_block == right.start_block && left.byte_length == right.byte_length;
}

DirectoryIdentity directory_identity(const Record& record) {
    return DirectoryIdentity{data_block(record), record.data_length};
}

struct DirectoryJob {
    Record record;
    DirectoryIdentity identity;
    DirectoryIdentity parent_identity;
    std::string raw_directory;
    std::string output_directory;
    std::size_t depth{};
};

std::vector<std::uint8_t> read_directory(
    std::ifstream& input,
    const Record& record,
    std::uint64_t& total_directory_bytes) {
    if (record.data_length > kMaxSingleDirectoryBytes) {
        fail("ISO9660 directory exceeds the per-directory size limit");
    }
    total_directory_bytes = checked_add(
        total_directory_bytes,
        record.data_length,
        "total directory bytes");
    if (total_directory_bytes > kMaxTotalDirectoryBytes) {
        fail("ISO9660 directory data exceeds the traversal limit");
    }

    std::vector<std::uint8_t> result(record.data_length);
    if (!result.empty()) {
        read_exact_at(
            input,
            block_offset(data_block(record), "directory offset"),
            result.data(),
            result.size());
    }
    return result;
}

void reserve_output_path(
    std::unordered_map<std::string, std::string>& paths,
    std::string_view output_path,
    std::string_view iso_path) {
    const auto key = lower_ascii(output_path);
    const auto [iterator, inserted] = paths.emplace(key, iso_path);
    if (!inserted) {
        fail(
            "case-insensitive normalized output collision between " + iterator->second +
            " and " + std::string(iso_path));
    }
}

void reserve_directory_blocks(
    std::map<std::uint64_t, std::uint64_t>& ranges,
    const DirectoryIdentity& identity) {
    const auto block_count = checked_add(
        identity.byte_length,
        kLogicalBlockSize - 1U,
        "directory block count numerator") /
                             kLogicalBlockSize;
    const auto end = checked_add(identity.start_block, block_count, "directory block range");

    const auto next = ranges.lower_bound(identity.start_block);
    if (next != ranges.end() && next->first < end) {
        fail("overlapping or aliased ISO9660 directory extents");
    }
    if (next != ranges.begin()) {
        const auto previous = std::prev(next);
        if (previous->second > identity.start_block) {
            fail("overlapping or aliased ISO9660 directory extents");
        }
    }
    const auto [unused, inserted] = ranges.emplace(identity.start_block, end);
    static_cast<void>(unused);
    if (!inserted) {
        fail("aliased ISO9660 directory extent");
    }
}

Extent public_extent(const Record& record) {
    return Extent{
        record.extent_block,
        record.data_length,
        record.extended_attribute_blocks};
}

bool same_public_file(const File& left, const File& right) noexcept {
    if (left.iso_path != right.iso_path || left.output_path != right.output_path ||
        left.size != right.size || left.extents.size() != right.extents.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.extents.size(); ++index) {
        const auto& a = left.extents[index];
        const auto& b = right.extents[index];
        if (a.logical_block != b.logical_block || a.byte_length != b.byte_length ||
            a.extended_attribute_blocks != b.extended_attribute_blocks) {
            return false;
        }
    }
    return true;
}

void validate_public_extent(const Extent& extent, const Metadata& metadata) {
    const auto start_block = checked_add(
        extent.logical_block,
        extent.extended_attribute_blocks,
        "stream extent data block");
    if (start_block > metadata.volume_blocks ||
        (extent.byte_length != 0U && start_block >= metadata.volume_blocks)) {
        fail("file extent starts outside the declared volume");
    }
    const auto start = block_offset(start_block, "stream extent offset");
    const auto end = checked_add(start, extent.byte_length, "stream extent end");
    if (end > metadata.declared_volume_bytes || end > metadata.image_bytes) {
        fail("file extent exceeds image bounds");
    }
}

}  // namespace

Image Image::open(const std::filesystem::path& source) {
    std::error_code filesystem_error;
    const auto absolute_source = std::filesystem::absolute(source, filesystem_error);
    if (filesystem_error) {
        fail("could not resolve ISO image path");
    }
    if (!std::filesystem::is_regular_file(absolute_source, filesystem_error) || filesystem_error) {
        fail("ISO image is not a readable regular file");
    }
    const auto image_bytes = std::filesystem::file_size(absolute_source, filesystem_error);
    if (filesystem_error) {
        fail("could not determine ISO image size");
    }

    std::ifstream input(absolute_source, std::ios::binary);
    if (!input) {
        fail("could not open ISO image");
    }

    std::optional<Sector> primary;
    std::uint32_t primary_sector = 0;
    std::uint32_t terminator_sector = 0;
    bool found_terminator = false;
    for (std::uint32_t index = 0; index < kMaxDescriptorCount; ++index) {
        const auto sector_number = kFirstDescriptorSector + index;
        auto descriptor = read_sector(input, sector_number, image_bytes);
        if (!std::equal(
                descriptor.begin() + 1,
                descriptor.begin() + 6,
                "CD001")) {
            fail("invalid ISO9660 volume descriptor signature");
        }
        if (descriptor[6] != 1U) {
            fail("unsupported ISO9660 volume descriptor version");
        }

        const auto type = descriptor[0];
        if (type == 1U) {
            if (primary.has_value()) {
                fail("multiple primary ISO9660 volume descriptors");
            }
            primary = descriptor;
            primary_sector = sector_number;
        }
        if (type == 255U) {
            found_terminator = true;
            terminator_sector = sector_number;
            break;
        }
    }

    if (!found_terminator) {
        fail("ISO9660 descriptor terminator was not found within the scan limit");
    }
    if (!primary.has_value()) {
        fail("ISO9660 primary volume descriptor was not found");
    }

    const auto& pvd = *primary;
    const auto volume_blocks = read_both32(pvd.data() + 80, "volume space size");
    const auto volume_set_size = read_both16(pvd.data() + 120, "volume set size");
    const auto volume_sequence = read_both16(pvd.data() + 124, "volume sequence number");
    const auto logical_block_size = read_both16(pvd.data() + 128, "logical block size");
    static_cast<void>(read_both32(pvd.data() + 132, "path table size"));

    if (volume_blocks == 0U) {
        fail("ISO9660 volume has zero blocks");
    }
    if (volume_set_size != 1U || volume_sequence != 1U) {
        fail("multi-volume ISO9660 sets are unsupported");
    }
    if (logical_block_size != kLogicalBlockSize) {
        fail("only 2048-byte ISO9660 logical blocks are supported");
    }

    const auto declared_bytes = block_offset(volume_blocks, "declared volume size");
    if (declared_bytes > image_bytes) {
        fail("declared ISO9660 volume exceeds the physical image");
    }
    if (primary_sector >= volume_blocks) {
        fail("primary volume descriptor lies outside the declared volume");
    }
    if (terminator_sector >= volume_blocks) {
        fail("volume descriptor terminator lies outside the declared volume");
    }

    const auto root_record_length = static_cast<std::size_t>(pvd[156]);
    if (root_record_length < 34U || root_record_length > pvd.size() - 156U) {
        fail("invalid root directory record in primary descriptor");
    }
    auto root = parse_record(
        pvd.data() + 156,
        root_record_length,
        volume_blocks,
        declared_bytes,
        image_bytes);
    if (!is_directory(root) || !is_current_directory(root)) {
        fail("primary descriptor root record is not a root directory");
    }

    Image result;
    result.source_ = absolute_source;
    result.metadata_.system_identifier =
        trim_descriptor_text(pvd.data() + 8, 32U, "system identifier");
    result.metadata_.volume_identifier =
        trim_descriptor_text(pvd.data() + 40, 32U, "volume identifier");
    result.metadata_.volume_blocks = volume_blocks;
    result.metadata_.logical_block_size = logical_block_size;
    result.metadata_.declared_volume_bytes = declared_bytes;
    result.metadata_.image_bytes = image_bytes;
    result.metadata_.primary_descriptor_sector = primary_sector;

    const auto root_identity = directory_identity(root);
    std::deque<DirectoryJob> jobs;
    jobs.push_back(DirectoryJob{root, root_identity, root_identity, {}, {}, 0U});

    std::map<std::uint64_t, std::uint64_t> directory_ranges;
    reserve_directory_blocks(directory_ranges, root_identity);
    std::unordered_map<std::string, std::string> output_paths;
    std::size_t directory_count = 1U;
    std::size_t record_count = 0U;
    std::uint64_t total_directory_bytes = 0U;

    while (!jobs.empty()) {
        auto job = std::move(jobs.front());
        jobs.pop_front();
        auto bytes = read_directory(input, job.record, total_directory_bytes);

        bool saw_current = false;
        bool saw_parent = false;
        std::optional<File> pending_file;
        std::uint8_t pending_flags = 0U;
        std::size_t offset = 0U;
        while (offset < bytes.size()) {
            const auto block_remaining = std::min<std::size_t>(
                kLogicalBlockSize - (offset % kLogicalBlockSize),
                bytes.size() - offset);
            if (bytes[offset] == 0U) {
                if (!std::all_of(
                        bytes.begin() + static_cast<std::vector<std::uint8_t>::difference_type>(offset),
                        bytes.begin() + static_cast<std::vector<std::uint8_t>::difference_type>(
                                            offset + block_remaining),
                        [](std::uint8_t value) { return value == 0U; })) {
                    fail("non-zero data follows directory block padding");
                }
                offset += block_remaining;
                continue;
            }

            const auto record_length = static_cast<std::size_t>(bytes[offset]);
            if (record_length > block_remaining) {
                fail("ISO9660 directory record crosses a logical-block boundary");
            }
            auto record = parse_record(
                bytes.data() + offset,
                record_length,
                volume_blocks,
                declared_bytes,
                image_bytes);
            offset += record_length;

            ++record_count;
            if (record_count > kMaxRecordCount) {
                fail("ISO9660 directory record traversal limit exceeded");
            }

            if (is_current_directory(record) || is_parent_directory(record)) {
                if (pending_file.has_value()) {
                    fail("incomplete multi-extent file sequence");
                }
                if (!is_directory(record)) {
                    fail("special ISO9660 directory record is not a directory");
                }
                const auto identity = directory_identity(record);
                if (is_current_directory(record)) {
                    if (saw_current || !(identity == job.identity)) {
                        fail("invalid or duplicate current-directory record");
                    }
                    saw_current = true;
                } else {
                    if (saw_parent || !(identity == job.parent_identity)) {
                        fail("invalid or duplicate parent-directory record");
                    }
                    saw_parent = true;
                }
                continue;
            }

            const auto identifier = raw_identifier(record);
            const auto output_component = normalize_component(identifier);
            const auto iso_path = join_raw(job.raw_directory, identifier);
            const auto output_path = join_output(job.output_directory, output_component);

            if (pending_file.has_value()) {
                if (is_directory(record) || pending_file->iso_path != iso_path ||
                    (record.flags & 0x7fU) != pending_flags) {
                    fail("incomplete or non-consecutive multi-extent file sequence");
                }
                pending_file->size = checked_add(
                    pending_file->size,
                    record.data_length,
                    "multi-extent file length");
                pending_file->extents.push_back(public_extent(record));
                if (has_more_extents(record)) {
                    if (record.data_length == 0U ||
                        record.data_length % kLogicalBlockSize != 0U) {
                        fail("non-final multi-extent section is not block-aligned");
                    }
                    continue;
                }

                reserve_output_path(output_paths, pending_file->output_path, pending_file->iso_path);
                result.files_.push_back(std::move(*pending_file));
                pending_file.reset();
                continue;
            }

            if (is_directory(record)) {
                if (job.depth >= kMaxDepth) {
                    fail("ISO9660 directory depth limit exceeded");
                }
                if (++directory_count > kMaxDirectoryCount) {
                    fail("ISO9660 directory count limit exceeded");
                }

                reserve_output_path(output_paths, output_path, iso_path);
                const auto identity = directory_identity(record);
                reserve_directory_blocks(directory_ranges, identity);
                jobs.push_back(DirectoryJob{
                    record,
                    identity,
                    job.identity,
                    iso_path.substr(1U),
                    output_path,
                    job.depth + 1U});
                continue;
            }

            File file;
            file.iso_path = iso_path;
            file.output_path = output_path;
            file.size = record.data_length;
            file.extents.push_back(public_extent(record));

            if (has_more_extents(record)) {
                if (record.data_length == 0U ||
                    record.data_length % kLogicalBlockSize != 0U) {
                    fail("non-final multi-extent section is not block-aligned");
                }
                pending_flags = static_cast<std::uint8_t>(record.flags & 0x7fU);
                pending_file = std::move(file);
            } else {
                reserve_output_path(output_paths, file.output_path, file.iso_path);
                result.files_.push_back(std::move(file));
            }
        }

        if (pending_file.has_value()) {
            fail("incomplete multi-extent file at end of directory");
        }
        if (!saw_current || !saw_parent) {
            fail("directory is missing its current or parent record");
        }
    }

    std::sort(
        result.files_.begin(),
        result.files_.end(),
        [](const File& left, const File& right) {
            if (left.iso_path != right.iso_path) {
                return left.iso_path < right.iso_path;
            }
            return left.output_path < right.output_path;
        });
    return result;
}

const File* Image::find_exact(std::string_view iso_path) const noexcept {
    const auto iterator = std::lower_bound(
        files_.begin(),
        files_.end(),
        iso_path,
        [](const File& file, std::string_view path) { return file.iso_path < path; });
    if (iterator == files_.end() || iterator->iso_path != iso_path) {
        return nullptr;
    }
    return &*iterator;
}

std::vector<std::byte> Image::read_small_file(
    const File& file,
    std::uint64_t max_bytes) const {
    if (file.size > max_bytes) {
        fail("ISO9660 file exceeds the requested in-memory read limit");
    }
    if (file.size > std::numeric_limits<std::size_t>::max()) {
        fail("ISO9660 file is too large for an in-memory read");
    }

    std::vector<std::byte> result(static_cast<std::size_t>(file.size));
    std::size_t position = 0U;
    stream_file(file, [&result, &position](std::span<const std::byte> chunk) {
        if (chunk.size() > result.size() - position) {
            fail("streamed ISO9660 data exceeds the declared file size");
        }
        std::memcpy(result.data() + position, chunk.data(), chunk.size());
        position += chunk.size();
    });
    if (position != result.size()) {
        fail("streamed ISO9660 data is shorter than the declared file size");
    }
    return result;
}

void Image::stream_file(const File& file, const ChunkCallback& callback) const {
    if (!callback) {
        fail("ISO9660 stream callback is empty");
    }

    const auto* canonical = find_exact(file.iso_path);
    if (canonical == nullptr || !same_public_file(*canonical, file)) {
        fail("file does not belong to this ISO9660 image");
    }

    std::error_code filesystem_error;
    const auto current_bytes = std::filesystem::file_size(source_, filesystem_error);
    if (filesystem_error || current_bytes != metadata_.image_bytes) {
        fail("ISO image size changed after it was opened");
    }
    std::ifstream input(source_, std::ios::binary);
    if (!input) {
        fail("could not reopen ISO image for streaming");
    }

    std::uint64_t extent_total = 0U;
    for (const auto& extent : file.extents) {
        validate_public_extent(extent, metadata_);
        extent_total = checked_add(extent_total, extent.byte_length, "file extent total");
    }
    if (extent_total != file.size) {
        fail("file extent lengths do not equal the declared file size");
    }

    std::array<std::byte, kStreamChunkBytes> buffer{};
    std::uint64_t streamed = 0U;
    for (const auto& extent : file.extents) {
        const auto start_block = checked_add(
            extent.logical_block,
            extent.extended_attribute_blocks,
            "stream data block");
        auto offset = block_offset(start_block, "stream offset");
        std::uint64_t remaining = extent.byte_length;
        while (remaining != 0U) {
            const auto count = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, buffer.size()));
            read_exact_at(input, offset, buffer.data(), count);
            callback(std::span<const std::byte>(buffer.data(), count));
            offset = checked_add(offset, count, "next stream offset");
            streamed = checked_add(streamed, count, "streamed file length");
            remaining -= count;
        }
    }
    if (streamed != file.size) {
        fail("streamed ISO9660 length does not equal the file size");
    }
}

}  // namespace openrc::iso9660
