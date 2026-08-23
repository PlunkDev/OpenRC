#include "openrc/disc_toc.hpp"

#include "iso9660.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint32_t kGlobalTocSectorCount = 6;
constexpr std::uint32_t kGlobalTocVersion = 1;
constexpr std::uint32_t kGlobalTocByteSize = 0x2960;
constexpr std::size_t kGlobalExtentTableOffset = 0x8;
constexpr std::size_t kLevelDescriptorTableOffset = 0x28c8;
constexpr std::size_t kExtentRecordSize = 8;
constexpr std::size_t kLevelDescriptorSize = 8;
constexpr std::size_t kExpectedEmptyGlobalExtentSlots = 3;
constexpr std::uint32_t kLocalTocSectorCount = 5;
constexpr std::uint32_t kLocalTocByteSize = 0x2434;
constexpr std::size_t kLocalExtentTableOffset = 0x8;
constexpr std::size_t kLocalSizedVagTableOffset = 0x28;
constexpr std::size_t kLocalSizedVagCount = 30;
constexpr std::size_t kLocalReserved0Offset = 0x118;
constexpr std::size_t kLocalReserved0WordCount = 12;
constexpr std::size_t kLocalMusicVagTableOffset = 0x148;
constexpr std::size_t kLocalMusicVagCount = 11;
constexpr std::size_t kLocalReserved1Offset = 0x174;
constexpr std::size_t kLocalReserved1WordCount = 4;
constexpr std::size_t kLocalResourceBlockOffset = 0x184;
constexpr std::size_t kLocalResourceBlockCount = 15;
constexpr std::size_t kLocalResourceBlockSize = 0x250;
constexpr std::size_t kLocalSpeechVagCount = 6;
constexpr std::size_t kLocalWadRunCount = 2;
constexpr std::size_t kLocalWadRunSlotCount = 71;
constexpr std::array<std::size_t, kLocalWadRunCount> kLocalWadRunOffsets{0x18, 0x134};
constexpr std::size_t kPrimaryExtent0IndexBytes = 0x80;
constexpr std::size_t kPrimaryExtent0SubrangeCount = 16;
constexpr std::size_t kPrimaryExtent3HeaderBytes = 20;
constexpr std::size_t kPrimaryExtent3TableCount = 3;
constexpr std::size_t kOpaquePairBytes = 8;
constexpr std::uint64_t kMaximumPrimaryExtent3PairCount = 100'000;
constexpr std::size_t kVagHeaderBytes = 48;
constexpr std::size_t kWadHeaderBytes = 8;

using GlobalTocBytes =
    std::array<std::uint8_t, kGlobalTocSectorCount * kDiscTocSectorSize>;
using LocalTocBytes =
    std::array<std::uint8_t, kLocalTocSectorCount * kDiscTocSectorSize>;

[[noreturn]] void fail(const std::string& message) {
    throw DiscTocError(message);
}

[[nodiscard]] std::uint32_t read_le32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

[[nodiscard]] std::uint32_t read_be32(const std::uint8_t* bytes) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
        (static_cast<std::uint32_t>(bytes[1]) << 16U) |
        (static_cast<std::uint32_t>(bytes[2]) << 8U) |
        static_cast<std::uint32_t>(bytes[3]);
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::string_view description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail("integer overflow while calculating " + std::string(description));
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::string_view description) {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail("integer overflow while calculating " + std::string(description));
    }
    return left * right;
}

[[nodiscard]] std::uint64_t sector_offset(
    const std::uint64_t lba,
    const std::string_view description) {
    if (lba > std::numeric_limits<std::uint64_t>::max() / kDiscTocSectorSize) {
        fail("integer overflow while calculating " + std::string(description));
    }
    return lba * kDiscTocSectorSize;
}

void require_sector_range(
    const std::uint64_t lba,
    const std::uint64_t sectors,
    const std::uint64_t image_sectors,
    const std::string_view description) {
    if (sectors == 0U) {
        fail(std::string(description) + " has zero sectors");
    }
    if (lba >= image_sectors || sectors > image_sectors - lba) {
        fail(std::string(description) + " exceeds the declared or physical image boundary");
    }
}

void read_exact_at(
    std::ifstream& input,
    const std::uint64_t offset,
    const std::span<std::uint8_t> destination,
    const std::uint64_t image_bytes) {
    if (offset > image_bytes || destination.size() > image_bytes - offset) {
        fail("TOC read exceeds the physical image boundary");
    }
    constexpr auto maximum_stream_offset =
        static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max());
    if (offset > maximum_stream_offset ||
        destination.size() >
            static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        fail("TOC read cannot be represented by the host I/O library");
    }

    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        fail("failed to seek in the disc image");
    }
    input.read(
        reinterpret_cast<char*>(destination.data()),
        static_cast<std::streamsize>(destination.size()));
    if (input.gcount() != static_cast<std::streamsize>(destination.size())) {
        fail("unexpected end of disc image while reading TOC data");
    }
}

void require_zero_range(
    std::ifstream& input,
    std::uint64_t offset,
    std::uint64_t byte_count,
    const std::uint64_t image_bytes,
    const std::string_view description) {
    std::array<std::uint8_t, 4096> buffer{};
    while (byte_count != 0U) {
        const auto chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(byte_count, buffer.size()));
        read_exact_at(
            input,
            offset,
            std::span<std::uint8_t>(buffer.data(), chunk),
            image_bytes);
        if (std::any_of(buffer.begin(), buffer.begin() + chunk, [](const auto byte) {
                return byte != 0U;
            })) {
            fail(std::string(description) + " is not zero-filled");
        }
        offset = checked_add(offset, chunk, description);
        byte_count -= chunk;
    }
}

void require_zero_bytes(
    const std::span<const std::uint8_t> bytes,
    const std::string_view description) {
    if (std::any_of(bytes.begin(), bytes.end(), [](const auto byte) { return byte != 0U; })) {
        fail(std::string(description) + " is not zero-filled");
    }
}

[[nodiscard]] std::uint32_t occupied_sectors(
    const std::uint32_t logical_bytes,
    const std::string_view description) {
    if (logical_bytes == 0U) {
        fail(std::string(description) + " has zero logical bytes");
    }
    const auto sectors =
        (static_cast<std::uint64_t>(logical_bytes) + kDiscTocSectorSize - 1U) /
        kDiscTocSectorSize;
    if (sectors > std::numeric_limits<std::uint32_t>::max()) {
        fail(std::string(description) + " sector count cannot be represented");
    }
    return static_cast<std::uint32_t>(sectors);
}

[[nodiscard]] std::uint32_t read_wad_logical_bytes_at(
    std::ifstream& input,
    const std::uint64_t offset,
    const std::uint64_t image_bytes,
    const std::string_view description) {
    std::array<std::uint8_t, kWadHeaderBytes> header{};
    read_exact_at(input, offset, header, image_bytes);
    if (header[0] != 'W' || header[1] != 'A' || header[2] != 'D') {
        fail(std::string(description) + " does not have a WAD header");
    }
    const auto logical_bytes = read_le32(header.data() + 3);
    if (logical_bytes < 16U) {
        fail(std::string(description) + " declares fewer than 16 bytes");
    }
    return logical_bytes;
}

[[nodiscard]] std::uint32_t read_vag_logical_bytes_at(
    std::ifstream& input,
    const std::uint64_t offset,
    const std::uint64_t image_bytes,
    const std::string_view description) {
    std::array<std::uint8_t, kVagHeaderBytes> header{};
    read_exact_at(input, offset, header, image_bytes);
    if (!std::equal(header.begin(), header.begin() + 4, "VAGp")) {
        fail(std::string(description) + " does not have a VAGp header");
    }
    const auto payload_bytes = read_be32(header.data() + 12);
    const auto logical_bytes = checked_add(kVagHeaderBytes, payload_bytes, description);
    if (logical_bytes > std::numeric_limits<std::uint32_t>::max()) {
        fail(std::string(description) + " logical byte size cannot be represented");
    }
    return static_cast<std::uint32_t>(logical_bytes);
}

void require_asset_padding(
    std::ifstream& input,
    const DiscTocAssetRef& asset,
    const std::uint64_t image_bytes,
    const std::string_view description) {
    const auto physical_bytes = checked_multiply(
        asset.occupied_sectors,
        kDiscTocSectorSize,
        description);
    const auto padding_offset = checked_add(
        sector_offset(asset.lba, description),
        asset.logical_bytes,
        description);
    require_zero_range(
        input,
        padding_offset,
        physical_bytes - asset.logical_bytes,
        image_bytes,
        std::string(description) + " sector padding");
}

[[nodiscard]] DiscTocAssetRef probe_wad(
    std::ifstream& input,
    const std::uint32_t lba,
    const std::uint64_t image_bytes,
    const std::uint64_t image_sectors,
    const std::string_view description) {
    require_sector_range(lba, 1, image_sectors, std::string(description) + " header");
    const auto logical_bytes = read_wad_logical_bytes_at(
        input,
        sector_offset(lba, description),
        image_bytes,
        description);
    const auto sectors = occupied_sectors(logical_bytes, description);
    require_sector_range(lba, sectors, image_sectors, description);
    const DiscTocAssetRef asset{
        lba,
        logical_bytes,
        sectors,
        DiscTocSignature::wad};
    require_asset_padding(input, asset, image_bytes, description);
    return asset;
}

[[nodiscard]] DiscTocAssetRef probe_vag(
    std::ifstream& input,
    const std::uint32_t lba,
    const std::uint64_t image_bytes,
    const std::uint64_t image_sectors,
    const std::string_view description) {
    require_sector_range(lba, 1, image_sectors, std::string(description) + " header");
    const auto logical_bytes = read_vag_logical_bytes_at(
        input,
        sector_offset(lba, description),
        image_bytes,
        description);
    const auto sectors = occupied_sectors(logical_bytes, description);
    require_sector_range(lba, sectors, image_sectors, description);
    const DiscTocAssetRef asset{
        lba,
        logical_bytes,
        sectors,
        DiscTocSignature::vagp};
    require_asset_padding(input, asset, image_bytes, description);
    return asset;
}

[[nodiscard]] DiscTocSignature classify_signature_at(
    std::ifstream& input,
    const std::uint64_t offset,
    const std::uint64_t available_bytes,
    const std::uint64_t image_bytes) {
    constexpr std::size_t kProbeBytes = 28;
    std::array<std::uint8_t, kProbeBytes> bytes{};
    const auto count = static_cast<std::size_t>(
        std::min<std::uint64_t>(available_bytes, bytes.size()));
    read_exact_at(
        input,
        offset,
        std::span<std::uint8_t>(bytes.data(), count),
        image_bytes);
    if (count >= 3U && bytes[0] == 'W' && bytes[1] == 'A' && bytes[2] == 'D') {
        return DiscTocSignature::wad;
    }
    if (count >= 4U && std::equal(bytes.begin(), bytes.begin() + 4, "VAGp")) {
        return DiscTocSignature::vagp;
    }
    if (count >= 4U && std::equal(bytes.begin(), bytes.begin() + 4, "2FIP")) {
        return DiscTocSignature::two_fip;
    }
    if ((count >= 4U && std::equal(bytes.begin(), bytes.begin() + 4, "PS2D")) ||
        (count >= kProbeBytes && std::equal(bytes.begin() + 24, bytes.end(), "PS2D"))) {
        return DiscTocSignature::ps2d;
    }
    return DiscTocSignature::other;
}

[[nodiscard]] DiscTocExtent parse_extent(const std::uint8_t* record) noexcept {
    return DiscTocExtent{read_le32(record), read_le32(record + 4)};
}

[[nodiscard]] DiscTocSignature classify_signature(
    std::ifstream& input,
    const DiscTocExtent& extent,
    const std::uint64_t image_bytes) {
    constexpr std::size_t kWrappedPs2dSignatureOffset = 24;
    std::array<std::uint8_t, kWrappedPs2dSignatureOffset + 4> bytes{};
    read_exact_at(
        input,
        sector_offset(extent.lba, "global extent signature offset"),
        bytes,
        image_bytes);

    if (bytes[0] == 'W' && bytes[1] == 'A' && bytes[2] == 'D') {
        return DiscTocSignature::wad;
    }
    if (std::equal(bytes.begin(), bytes.begin() + 4, "VAGp")) {
        return DiscTocSignature::vagp;
    }
    if (std::equal(bytes.begin(), bytes.begin() + 4, "2FIP")) {
        return DiscTocSignature::two_fip;
    }
    if (std::equal(bytes.begin(), bytes.begin() + 4, "PS2D") ||
        std::equal(
            bytes.begin() + kWrappedPs2dSignatureOffset,
            bytes.end(),
            "PS2D")) {
        return DiscTocSignature::ps2d;
    }
    return DiscTocSignature::other;
}

void parse_global_extents(
    const GlobalTocBytes& toc,
    std::ifstream& input,
    const std::uint64_t image_bytes,
    const std::uint64_t image_sectors,
    DiscTocReport& report) {
    std::uint64_t expected_lba =
        checked_add(kDiscTocGlobalLba, kGlobalTocSectorCount, "first global extent LBA");
    report.global_extent_slot_count = kDiscTocGlobalExtentSlotCount;
    report.global_extents.reserve(
        kDiscTocGlobalExtentSlotCount - kExpectedEmptyGlobalExtentSlots);

    for (std::size_t slot = 0; slot < kDiscTocGlobalExtentSlotCount; ++slot) {
        const auto offset = kGlobalExtentTableOffset + slot * kExtentRecordSize;
        const auto extent = parse_extent(toc.data() + offset);
        if (extent.lba == 0U && extent.sectors == 0U) {
            ++report.empty_global_extent_slot_count;
            continue;
        }
        if (extent.lba == 0U || extent.sectors == 0U) {
            fail("global extent slot " + std::to_string(slot) +
                 " is only partially empty");
        }
        if (extent.lba != expected_lba) {
            fail("global extent slot " + std::to_string(slot) +
                 " breaks the contiguous extent chain");
        }
        require_sector_range(
            extent.lba,
            extent.sectors,
            image_sectors,
            "global extent slot " + std::to_string(slot));

        report.global_extents.push_back(DiscTocGlobalExtent{
            slot,
            extent,
            classify_signature(input, extent, image_bytes),
        });
        expected_lba = checked_add(extent.lba, extent.sectors, "global extent end LBA");
    }

    if (report.empty_global_extent_slot_count != kExpectedEmptyGlobalExtentSlots) {
        fail("global TOC does not contain exactly three empty extent slots");
    }
}

[[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>>
parse_level_descriptor_table(const GlobalTocBytes& toc) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> descriptors;
    descriptors.reserve(kDiscTocLevelCount);
    for (std::size_t index = 0; index < kDiscTocLevelCount; ++index) {
        const auto offset = kLevelDescriptorTableOffset + index * kLevelDescriptorSize;
        descriptors.emplace_back(
            read_le32(toc.data() + offset),
            read_le32(toc.data() + offset + 4));
    }
    return descriptors;
}

void parse_levels(
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& descriptors,
    std::ifstream& input,
    const std::uint64_t image_bytes,
    const std::uint64_t image_sectors,
    DiscTocReport& report) {
    report.levels.reserve(descriptors.size());
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const auto toc_lba = static_cast<std::uint64_t>(descriptors[index].first);
        const auto boundary = index + 1U < descriptors.size()
            ? static_cast<std::uint64_t>(descriptors[index + 1U].first)
            : image_sectors;

        if (toc_lba == 0U) {
            fail("level " + std::to_string(index) + " has a zero local TOC LBA");
        }
        if (index + 1U < descriptors.size() && boundary <= toc_lba) {
            fail("level local TOC LBAs are not strictly increasing");
        }

        require_sector_range(
            toc_lba,
            kLocalTocSectorCount,
            image_sectors,
            "level " + std::to_string(index) + " local TOC");
        const auto first_data_lba =
            checked_add(toc_lba, kLocalTocSectorCount, "level data start LBA");
        if (boundary < first_data_lba) {
            fail("level " + std::to_string(index) +
                 " local TOC overlaps the next level TOC");
        }

        LocalTocBytes local_toc{};
        read_exact_at(
            input,
            sector_offset(toc_lba, "local TOC offset"),
            local_toc,
            image_bytes);
        if (read_le32(local_toc.data()) != index) {
            fail("local TOC has an unexpected level id for descriptor " +
                 std::to_string(index));
        }
        if (read_le32(local_toc.data() + 4) != kLocalTocByteSize) {
            fail("local TOC has an unexpected byte size for level " +
                 std::to_string(index));
        }

        DiscTocLevelDescriptor level;
        level.level_id = static_cast<std::uint32_t>(index);
        level.toc_lba = descriptors[index].first;
        level.auxiliary = descriptors[index].second;

        auto expected_lba = first_data_lba;
        for (std::size_t extent_index = 0;
             extent_index < kDiscTocPrimaryExtentCount;
             ++extent_index) {
            const auto offset = kLocalExtentTableOffset + extent_index * kExtentRecordSize;
            const auto extent = parse_extent(local_toc.data() + offset);
            if (extent.lba != expected_lba) {
                fail("primary extent " + std::to_string(extent_index) + " for level " +
                     std::to_string(index) + " breaks the contiguous extent chain");
            }
            require_sector_range(
                extent.lba,
                extent.sectors,
                image_sectors,
                "primary extent " + std::to_string(extent_index) + " for level " +
                    std::to_string(index));

            const auto extent_end =
                checked_add(extent.lba, extent.sectors, "primary extent end LBA");
            if (extent_end > boundary) {
                fail("primary extents for level " + std::to_string(index) +
                     " overlap the next level TOC or image end");
            }
            level.primary_extents[extent_index] = extent;
            expected_lba = extent_end;
        }
        if (index + 1U < descriptors.size() && expected_lba != boundary) {
            fail("primary extents for level " + std::to_string(index) +
                 " do not end at the next level TOC");
        }
        report.levels.push_back(std::move(level));
    }
}

[[nodiscard]] DiscTocWadRun parse_wad_run(
    const std::uint8_t* table,
    std::ifstream& input,
    const std::uint64_t image_bytes,
    const std::uint64_t image_sectors,
    const std::string& description) {
    std::vector<std::uint32_t> lbas;
    lbas.reserve(kLocalWadRunSlotCount);
    bool empty_slot_seen = false;
    for (std::size_t slot = 0; slot < kLocalWadRunSlotCount; ++slot) {
        const auto lba = read_le32(table + slot * sizeof(std::uint32_t));
        if (lba == 0U) {
            empty_slot_seen = true;
            continue;
        }
        if (empty_slot_seen) {
            fail(description + " is not left-packed");
        }
        lbas.push_back(lba);
    }

    DiscTocWadRun result;
    if (lbas.empty()) {
        return result;
    }

    const auto trailing_zero_lba = lbas.back();
    require_sector_range(
        trailing_zero_lba,
        1,
        image_sectors,
        description + " trailing zero sector");
    require_zero_range(
        input,
        sector_offset(trailing_zero_lba, description),
        kDiscTocSectorSize,
        image_bytes,
        description + " trailing sector");

    result.wads.reserve(lbas.size() - 1U);
    for (std::size_t index = 0; index + 1U < lbas.size(); ++index) {
        const auto asset = probe_wad(
            input,
            lbas[index],
            image_bytes,
            image_sectors,
            description + " WAD " + std::to_string(index));
        const auto expected_next =
            checked_add(asset.lba, asset.occupied_sectors, "WAD run next LBA");
        if (expected_next != lbas[index + 1U]) {
            fail(description + " breaks the contiguous WAD chain at slot " +
                 std::to_string(index));
        }
        result.wads.push_back(asset);
    }
    result.trailing_zero_sector_lba = trailing_zero_lba;
    return result;
}

[[nodiscard]] DiscTocLocalTables parse_local_asset_tables(
    const LocalTocBytes& toc,
    const std::uint32_t expected_level_id,
    std::ifstream& input,
    const std::uint64_t image_bytes,
    const std::uint64_t image_sectors,
    std::vector<DiscTocAssetRef>& referenced_vags) {
    const auto level_description = "level " + std::to_string(expected_level_id);
    if (read_le32(toc.data()) != expected_level_id) {
        fail(level_description + " asset TOC has an unexpected level id");
    }
    if (read_le32(toc.data() + 4) != kLocalTocByteSize) {
        fail(level_description + " asset TOC has an unexpected byte size");
    }
    require_zero_bytes(
        std::span<const std::uint8_t>(
            toc.data() + kLocalTocByteSize,
            toc.size() - kLocalTocByteSize),
        level_description + " local TOC physical tail");

    for (std::size_t word = 0; word < kLocalReserved0WordCount; ++word) {
        if (read_le32(toc.data() + kLocalReserved0Offset + word * 4U) != 0U) {
            fail(level_description + " local TOC reserved table at 0x118 is nonzero");
        }
    }
    for (std::size_t word = 0; word < kLocalReserved1WordCount; ++word) {
        if (read_le32(toc.data() + kLocalReserved1Offset + word * 4U) != 0U) {
            fail(level_description + " local TOC reserved table at 0x174 is nonzero");
        }
    }

    DiscTocLocalTables result;
    bool empty_sized_vag_seen = false;
    for (std::size_t index = 0; index < kLocalSizedVagCount; ++index) {
        const auto offset = kLocalSizedVagTableOffset + index * kExtentRecordSize;
        const DiscTocSizedLba record{
            read_le32(toc.data() + offset),
            read_le32(toc.data() + offset + 4),
        };
        result.sized_vags[index] = record;
        const auto lba_empty = record.lba == 0U;
        const auto size_empty = record.byte_size == 0U;
        if (lba_empty != size_empty) {
            fail(level_description + " sized VAG record " + std::to_string(index) +
                 " is only partially empty");
        }
        if (lba_empty) {
            empty_sized_vag_seen = true;
            continue;
        }
        if (empty_sized_vag_seen) {
            fail(level_description + " sized VAG table is not left-packed");
        }
        const auto asset = probe_vag(
            input,
            record.lba,
            image_bytes,
            image_sectors,
            level_description + " sized VAG " + std::to_string(index));
        if (asset.logical_bytes != record.byte_size) {
            fail(level_description + " sized VAG record " + std::to_string(index) +
                 " byte size disagrees with its VAG header");
        }
        referenced_vags.push_back(asset);
    }

    bool empty_music_vag_seen = false;
    for (std::size_t index = 0; index < kLocalMusicVagCount; ++index) {
        const auto lba = read_le32(toc.data() + kLocalMusicVagTableOffset + index * 4U);
        result.music_vag_lbas[index] = lba;
        if (lba == 0U) {
            empty_music_vag_seen = true;
            continue;
        }
        if (empty_music_vag_seen) {
            fail(level_description + " music VAG table is not left-packed");
        }
        referenced_vags.push_back(probe_vag(
            input,
            lba,
            image_bytes,
            image_sectors,
            level_description + " music VAG " + std::to_string(index)));
    }

    for (std::size_t block_index = 0; block_index < kLocalResourceBlockCount;
         ++block_index) {
        const auto block_offset =
            kLocalResourceBlockOffset + block_index * kLocalResourceBlockSize;
        auto& block = result.resource_blocks[block_index];
        const auto block_description =
            level_description + " resource block " + std::to_string(block_index);
        for (std::size_t slot = 0; slot < kLocalSpeechVagCount; ++slot) {
            const auto lba = read_le32(toc.data() + block_offset + slot * 4U);
            block.speech_vag_lbas[slot] = lba;
            if (lba != 0U) {
                referenced_vags.push_back(probe_vag(
                    input,
                    lba,
                    image_bytes,
                    image_sectors,
                    block_description + " speech VAG " + std::to_string(slot)));
            }
        }
        for (std::size_t run_index = 0; run_index < kLocalWadRunCount; ++run_index) {
            block.wad_runs[run_index] = parse_wad_run(
                toc.data() + block_offset + kLocalWadRunOffsets[run_index],
                input,
                image_bytes,
                image_sectors,
                block_description + " WAD run " + std::to_string(run_index));
        }
    }
    return result;
}

[[nodiscard]] DiscTocExtent0Index parse_primary_extent0(
    const DiscTocExtent& extent,
    std::ifstream& input,
    const std::uint64_t image_bytes,
    const std::string& description) {
    const auto extent_bytes =
        checked_multiply(extent.sectors, kDiscTocSectorSize, description);
    if (extent_bytes < kPrimaryExtent0IndexBytes) {
        fail(description + " is smaller than its 0x80-byte index");
    }
    const auto base_offset = sector_offset(extent.lba, description);
    std::array<std::uint8_t, kPrimaryExtent0IndexBytes> index_bytes{};
    read_exact_at(input, base_offset, index_bytes, image_bytes);

    DiscTocExtent0Index result;
    bool empty_record_seen = false;
    std::uint64_t previous_end = kPrimaryExtent0IndexBytes;
    for (std::size_t index = 0; index < kPrimaryExtent0SubrangeCount; ++index) {
        const auto record_offset = index * kExtentRecordSize;
        const auto relative_offset = read_le32(index_bytes.data() + record_offset);
        const auto byte_size = read_le32(index_bytes.data() + record_offset + 4);
        auto& subrange = result.subranges[index];
        subrange.relative_offset = relative_offset;
        subrange.byte_size = byte_size;

        const auto offset_empty = relative_offset == 0U;
        const auto size_empty = byte_size == 0U;
        if (offset_empty != size_empty) {
            fail(description + " subrange " + std::to_string(index) +
                 " is only partially empty");
        }
        if (offset_empty) {
            empty_record_seen = true;
            continue;
        }
        if (empty_record_seen) {
            fail(description + " index is not a prefix of used records");
        }

        const auto expected_offset = result.used_subrange_count == 0U
            ? static_cast<std::uint64_t>(kPrimaryExtent0IndexBytes)
            : checked_add(previous_end, 63U, description) & ~std::uint64_t{63U};
        if (relative_offset != expected_offset) {
            fail(description + " subrange " + std::to_string(index) +
                 " breaks the 64-byte aligned chain");
        }
        const auto subrange_end = checked_add(relative_offset, byte_size, description);
        if (subrange_end > extent_bytes) {
            fail(description + " subrange " + std::to_string(index) +
                 " exceeds the extent boundary");
        }
        if (relative_offset > previous_end) {
            require_zero_range(
                input,
                checked_add(base_offset, previous_end, description),
                relative_offset - previous_end,
                image_bytes,
                description + " alignment gap before subrange " + std::to_string(index));
        }

        const auto absolute_offset = checked_add(base_offset, relative_offset, description);
        subrange.signature =
            classify_signature_at(input, absolute_offset, byte_size, image_bytes);
        if (subrange.signature == DiscTocSignature::wad) {
            if (byte_size < kWadHeaderBytes) {
                fail(description + " subrange " + std::to_string(index) +
                     " has a truncated WAD header");
            }
            if (read_wad_logical_bytes_at(
                    input,
                    absolute_offset,
                    image_bytes,
                    description + " subrange " + std::to_string(index)) != byte_size) {
                fail(description + " subrange " + std::to_string(index) +
                     " byte size disagrees with its WAD header");
            }
        } else if (subrange.signature == DiscTocSignature::vagp) {
            if (byte_size < kVagHeaderBytes) {
                fail(description + " subrange " + std::to_string(index) +
                     " has a truncated VAGp header");
            }
            if (read_vag_logical_bytes_at(
                    input,
                    absolute_offset,
                    image_bytes,
                    description + " subrange " + std::to_string(index)) != byte_size) {
                fail(description + " subrange " + std::to_string(index) +
                     " byte size disagrees with its VAGp header");
            }
        }
        ++result.used_subrange_count;
        previous_end = subrange_end;
    }

    if (result.used_subrange_count == 0U) {
        fail(description + " index does not contain a used subrange");
    }
    require_zero_range(
        input,
        checked_add(base_offset, previous_end, description),
        extent_bytes - previous_end,
        image_bytes,
        description + " tail");
    return result;
}

[[nodiscard]] DiscTocAssetRef parse_primary_wad(
    const DiscTocExtent& extent,
    std::ifstream& input,
    const std::uint64_t image_bytes,
    const std::uint64_t image_sectors,
    const std::string& description) {
    const auto asset =
        probe_wad(input, extent.lba, image_bytes, image_sectors, description);
    if (asset.occupied_sectors != extent.sectors) {
        fail(description + " extent is not the exact sector envelope of its WAD");
    }
    return asset;
}

[[nodiscard]] DiscTocExtent3Tables parse_primary_extent3(
    const DiscTocExtent& extent,
    std::ifstream& input,
    const std::uint64_t image_bytes,
    const std::string& description) {
    const auto base_offset = sector_offset(extent.lba, description);
    std::array<std::uint8_t, kPrimaryExtent3HeaderBytes> header{};
    read_exact_at(input, base_offset, header, image_bytes);

    std::array<std::uint32_t, kPrimaryExtent3TableCount> counts{};
    std::uint64_t total_count = 0;
    for (std::size_t table = 0; table < counts.size(); ++table) {
        counts[table] = read_le32(header.data() + table * 4U);
        total_count = checked_add(total_count, counts[table], description);
    }
    if (total_count > kMaximumPrimaryExtent3PairCount) {
        fail(description + " exceeds the supported opaque-pair count");
    }
    if (read_le32(header.data() + 12) != 0U || read_le32(header.data() + 16) != 0U) {
        fail(description + " reserved header words are nonzero");
    }

    const auto records_bytes = checked_multiply(total_count, kOpaquePairBytes, description);
    const auto logical_bytes =
        checked_add(kPrimaryExtent3HeaderBytes, records_bytes, description);
    const auto required_sectors =
        (checked_add(logical_bytes, kDiscTocSectorSize - 1U, description)) /
        kDiscTocSectorSize;
    if (required_sectors != extent.sectors) {
        fail(description + " counts do not exactly fill the declared sector envelope");
    }

    DiscTocExtent3Tables result;
    std::array<std::uint8_t, 8192> buffer{};
    auto cursor = checked_add(base_offset, kPrimaryExtent3HeaderBytes, description);
    for (std::size_t table = 0; table < counts.size(); ++table) {
        auto& records = result.tables[table];
        if (counts[table] > records.max_size()) {
            fail(description + " table " + std::to_string(table) + " is too large");
        }
        records.reserve(static_cast<std::size_t>(counts[table]));
        std::uint64_t remaining = counts[table];
        while (remaining != 0U) {
            const auto chunk_records = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, buffer.size() / kOpaquePairBytes));
            const auto chunk_bytes = chunk_records * kOpaquePairBytes;
            read_exact_at(
                input,
                cursor,
                std::span<std::uint8_t>(buffer.data(), chunk_bytes),
                image_bytes);
            for (std::size_t index = 0; index < chunk_records; ++index) {
                const auto* record = buffer.data() + index * kOpaquePairBytes;
                records.push_back(DiscTocOpaquePair{
                    read_le32(record),
                    read_le32(record + 4),
                });
            }
            cursor = checked_add(cursor, chunk_bytes, description);
            remaining -= chunk_records;
        }
    }

    const auto physical_bytes =
        checked_multiply(extent.sectors, kDiscTocSectorSize, description);
    require_zero_range(
        input,
        checked_add(base_offset, logical_bytes, description),
        physical_bytes - logical_bytes,
        image_bytes,
        description + " sector padding");
    return result;
}

} // namespace

DiscTocReport inspect_disc_toc(const std::filesystem::path& image_path) {
    iso9660::Metadata metadata;
    try {
        metadata = iso9660::Image::open(image_path).metadata();
    } catch (const iso9660::Error& error) {
        throw DiscTocError("Cannot inspect disc TOC: " + std::string(error.what()));
    }

    if (metadata.image_bytes % kDiscTocSectorSize != 0U) {
        fail("disc image size is not a multiple of 2048 bytes");
    }
    if (metadata.declared_volume_bytes % kDiscTocSectorSize != 0U) {
        fail("declared ISO9660 volume size is not a multiple of 2048 bytes");
    }

    const auto image_sectors = metadata.image_bytes / kDiscTocSectorSize;
    const auto declared_sectors = metadata.declared_volume_bytes / kDiscTocSectorSize;
    const auto usable_sectors = std::min(image_sectors, declared_sectors);
    require_sector_range(
        kDiscTocGlobalLba,
        kGlobalTocSectorCount,
        usable_sectors,
        "global TOC");

    std::ifstream input(image_path, std::ios::binary);
    if (!input) {
        fail("cannot open disc image for TOC inspection");
    }

    GlobalTocBytes global_toc{};
    read_exact_at(
        input,
        sector_offset(kDiscTocGlobalLba, "global TOC offset"),
        global_toc,
        metadata.image_bytes);
    if (read_le32(global_toc.data()) != kGlobalTocVersion) {
        fail("global TOC has an unsupported version");
    }
    if (read_le32(global_toc.data() + 4) != kGlobalTocByteSize) {
        fail("global TOC has an unexpected byte size");
    }

    DiscTocReport report;
    report.image_path = image_path;
    report.image_bytes = metadata.image_bytes;
    report.image_sectors = image_sectors;
    report.declared_volume_bytes = metadata.declared_volume_bytes;
    report.declared_volume_sectors = declared_sectors;
    report.version = kGlobalTocVersion;
    report.byte_size = kGlobalTocByteSize;

    parse_global_extents(
        global_toc,
        input,
        metadata.image_bytes,
        usable_sectors,
        report);
    const auto level_descriptors = parse_level_descriptor_table(global_toc);
    parse_levels(
        level_descriptors,
        input,
        metadata.image_bytes,
        usable_sectors,
        report);
    return report;
}

DiscTocAssetReport inspect_disc_toc_assets(const std::filesystem::path& image_path) {
    DiscTocAssetReport report;
    report.layout = inspect_disc_toc(image_path);

    const auto usable_sectors =
        std::min(report.layout.image_sectors, report.layout.declared_volume_sectors);
    std::ifstream input(image_path, std::ios::binary);
    if (!input) {
        fail("cannot open disc image for TOC asset inspection");
    }

    report.levels.reserve(report.layout.levels.size());
    for (const auto& level : report.layout.levels) {
        LocalTocBytes local_toc{};
        read_exact_at(
            input,
            sector_offset(level.toc_lba, "local asset TOC offset"),
            local_toc,
            report.layout.image_bytes);

        DiscTocLevelAssets assets;
        assets.level_id = level.level_id;
        assets.referenced_vags.reserve(
            kLocalSizedVagCount + kLocalMusicVagCount +
            kLocalResourceBlockCount * kLocalSpeechVagCount);
        assets.local_tables = parse_local_asset_tables(
            local_toc,
            level.level_id,
            input,
            report.layout.image_bytes,
            usable_sectors,
            assets.referenced_vags);

        const auto level_description = "level " + std::to_string(level.level_id);
        assets.primary_extent0 = parse_primary_extent0(
            level.primary_extents[0],
            input,
            report.layout.image_bytes,
            level_description + " primary extent 0");
        for (std::size_t index = 0; index < assets.primary_wads.size(); ++index) {
            assets.primary_wads[index] = parse_primary_wad(
                level.primary_extents[index + 1U],
                input,
                report.layout.image_bytes,
                usable_sectors,
                level_description + " primary extent " + std::to_string(index + 1U));
        }
        assets.primary_extent3 = parse_primary_extent3(
            level.primary_extents[3],
            input,
            report.layout.image_bytes,
            level_description + " primary extent 3");
        report.levels.push_back(std::move(assets));
    }
    return report;
}

std::string_view disc_toc_signature_name(const DiscTocSignature signature) noexcept {
    switch (signature) {
    case DiscTocSignature::wad:
        return "WAD";
    case DiscTocSignature::vagp:
        return "VAGp";
    case DiscTocSignature::two_fip:
        return "2FIP";
    case DiscTocSignature::ps2d:
        return "PS2D";
    case DiscTocSignature::other:
        return "other";
    }
    return "other";
}

} // namespace openrc
