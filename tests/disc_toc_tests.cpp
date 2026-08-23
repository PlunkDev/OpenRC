#include "openrc/disc_toc.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::uint32_t kSectorSize = openrc::kDiscTocSectorSize;
constexpr std::uint32_t kImageSectors = 2200;
constexpr std::uint32_t kRootLba = 20;
constexpr std::uint32_t kFirstLevelTocLba = 2000;
constexpr std::uint32_t kLevelStride = 9;
constexpr std::uint64_t kGlobalTocOffset =
    static_cast<std::uint64_t>(openrc::kDiscTocGlobalLba) * kSectorSize;
constexpr std::size_t kGlobalExtentOffset = 0x8;
constexpr std::size_t kLevelDescriptorOffset = 0x28c8;
constexpr std::size_t kLocalExtentOffset = 0x8;
constexpr std::size_t kLocalSizedVagOffset = 0x28;
constexpr std::size_t kLocalMusicVagOffset = 0x148;
constexpr std::size_t kLocalResourceBlockOffset = 0x184;
constexpr std::size_t kLocalWadRunOffset = kLocalResourceBlockOffset + 0x18;
constexpr std::uint32_t kSizedVagLba = 1983;
constexpr std::uint32_t kMusicVagLba = 1984;
constexpr std::uint32_t kSpeechVagLba = 1985;
constexpr std::uint32_t kWadRunLba = 1986;
constexpr std::uint32_t kWadRunTerminatorLba = 1987;

[[nodiscard]] constexpr std::uint64_t sector_offset(const std::uint32_t lba) {
    return static_cast<std::uint64_t>(lba) * kSectorSize;
}

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void write_le16(std::uint8_t* target, const std::uint16_t value) {
    target[0] = static_cast<std::uint8_t>(value & 0xffU);
    target[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void write_be16(std::uint8_t* target, const std::uint16_t value) {
    target[0] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    target[1] = static_cast<std::uint8_t>(value & 0xffU);
}

void write_both16(std::uint8_t* target, const std::uint16_t value) {
    write_le16(target, value);
    write_be16(target + 2, value);
}

void write_le32(std::uint8_t* target, const std::uint32_t value) {
    target[0] = static_cast<std::uint8_t>(value & 0xffU);
    target[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    target[2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    target[3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

void write_be32(std::uint8_t* target, const std::uint32_t value) {
    target[0] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
    target[1] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    target[2] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    target[3] = static_cast<std::uint8_t>(value & 0xffU);
}

void write_wad_header(
    std::uint8_t* target,
    const std::uint32_t logical_bytes,
    const std::uint8_t opaque_byte = 0) {
    std::copy_n("WAD", 3, target);
    write_le32(target + 3, logical_bytes);
    target[7] = opaque_byte;
}

void write_vag_header(std::uint8_t* target, const std::uint32_t payload_bytes) {
    std::copy_n("VAGp", 4, target);
    write_be32(target + 12, payload_bytes);
}

void write_both32(std::uint8_t* target, const std::uint32_t value) {
    write_le32(target, value);
    write_be32(target + 4, value);
}

void write_at(
    std::fstream& image,
    const std::uint64_t offset,
    const std::uint8_t* bytes,
    const std::size_t size) {
    image.clear();
    image.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    image.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(size));
    if (!image) {
        throw std::runtime_error("failed to write synthetic TOC image");
    }
}

template <std::size_t Size>
void write_at(
    std::fstream& image,
    const std::uint64_t offset,
    const std::array<std::uint8_t, Size>& bytes) {
    write_at(image, offset, bytes.data(), bytes.size());
}

[[nodiscard]] std::array<std::uint8_t, 34> root_record(const std::uint8_t identifier) {
    std::array<std::uint8_t, 34> record{};
    record[0] = static_cast<std::uint8_t>(record.size());
    write_both32(record.data() + 2, kRootLba);
    write_both32(record.data() + 10, kSectorSize);
    record[25] = 0x02U;
    write_both16(record.data() + 28, 1);
    record[32] = 1;
    record[33] = identifier;
    return record;
}

void write_iso9660_metadata(std::fstream& image) {
    std::array<std::uint8_t, kSectorSize> pvd{};
    pvd[0] = 1;
    std::copy_n("CD001", 5, pvd.begin() + 1);
    pvd[6] = 1;
    std::fill_n(pvd.begin() + 8, 32, static_cast<std::uint8_t>(' '));
    std::fill_n(pvd.begin() + 40, 32, static_cast<std::uint8_t>(' '));
    write_both32(pvd.data() + 80, kImageSectors);
    write_both16(pvd.data() + 120, 1);
    write_both16(pvd.data() + 124, 1);
    write_both16(pvd.data() + 128, static_cast<std::uint16_t>(kSectorSize));
    write_both32(pvd.data() + 132, 0);
    const auto current = root_record(0);
    std::copy(current.begin(), current.end(), pvd.begin() + 156);
    write_at(image, sector_offset(16), pvd);

    std::array<std::uint8_t, kSectorSize> terminator{};
    terminator[0] = 255;
    std::copy_n("CD001", 5, terminator.begin() + 1);
    terminator[6] = 1;
    write_at(image, sector_offset(17), terminator);

    std::array<std::uint8_t, kSectorSize> root{};
    const auto parent = root_record(1);
    std::copy(current.begin(), current.end(), root.begin());
    std::copy(parent.begin(), parent.end(), root.begin() + 34);
    write_at(image, sector_offset(kRootLba), root);
}

void write_fixture(const std::filesystem::path& path) {
    {
        std::ofstream create(path, std::ios::binary | std::ios::trunc);
        if (!create) {
            throw std::runtime_error("failed to create sparse TOC fixture");
        }
    }
    std::filesystem::resize_file(
        path,
        static_cast<std::uint64_t>(kImageSectors) * kSectorSize);

    std::fstream image(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!image) {
        throw std::runtime_error("failed to open sparse TOC fixture");
    }
    write_iso9660_metadata(image);

    std::array<std::uint8_t, 6U * kSectorSize> global_toc{};
    write_le32(global_toc.data(), 1);
    write_le32(global_toc.data() + 4, 0x2960);

    constexpr std::array<std::size_t, 3> empty_slots{100, 300, 478};
    std::uint32_t next_global_lba = openrc::kDiscTocGlobalLba + 6;
    for (std::size_t slot = 0; slot < openrc::kDiscTocGlobalExtentSlotCount; ++slot) {
        if (std::find(empty_slots.begin(), empty_slots.end(), slot) != empty_slots.end()) {
            continue;
        }
        const auto offset = kGlobalExtentOffset + slot * 8U;
        write_le32(global_toc.data() + offset, next_global_lba);
        write_le32(global_toc.data() + offset + 4, 1);
        ++next_global_lba;
    }

    for (std::size_t level = 0; level < openrc::kDiscTocLevelCount; ++level) {
        const auto toc_lba =
            kFirstLevelTocLba + static_cast<std::uint32_t>(level) * kLevelStride;
        const auto descriptor_offset = kLevelDescriptorOffset + level * 8U;
        write_le32(global_toc.data() + descriptor_offset, toc_lba);
        write_le32(
            global_toc.data() + descriptor_offset + 4,
            0x1000U + static_cast<std::uint32_t>(level));
    }
    write_at(image, kGlobalTocOffset, global_toc);

    constexpr std::array<std::array<std::uint8_t, 4>, 3> signatures{{
        {{'W', 'A', 'D', 0}},
        {{'V', 'A', 'G', 'p'}},
        {{'2', 'F', 'I', 'P'}},
    }};
    for (std::size_t index = 0; index < signatures.size(); ++index) {
        write_at(
            image,
            sector_offset(openrc::kDiscTocGlobalLba + 6U +
                          static_cast<std::uint32_t>(index)),
            signatures[index]);
    }
    std::array<std::uint8_t, 28> wrapped_ps2d{};
    std::copy_n("PS2D", 4, wrapped_ps2d.begin() + 24);
    write_at(
        image,
        sector_offset(openrc::kDiscTocGlobalLba + 9U),
        wrapped_ps2d);

    std::array<std::uint8_t, 48> vag{};
    write_vag_header(vag.data(), 0);
    write_at(image, sector_offset(kSizedVagLba), vag);
    write_at(image, sector_offset(kMusicVagLba), vag);
    write_at(image, sector_offset(kSpeechVagLba), vag);

    std::array<std::uint8_t, 16> shared_wad{};
    write_wad_header(shared_wad.data(), static_cast<std::uint32_t>(shared_wad.size()), 1);
    write_at(image, sector_offset(kWadRunLba), shared_wad);

    for (std::size_t level = 0; level < openrc::kDiscTocLevelCount; ++level) {
        const auto toc_lba =
            kFirstLevelTocLba + static_cast<std::uint32_t>(level) * kLevelStride;
        std::array<std::uint8_t, 5U * kSectorSize> local_toc{};
        write_le32(local_toc.data(), static_cast<std::uint32_t>(level));
        write_le32(local_toc.data() + 4, 0x2434);
        for (std::size_t extent = 0;
             extent < openrc::kDiscTocPrimaryExtentCount;
             ++extent) {
            const auto offset = kLocalExtentOffset + extent * 8U;
            write_le32(
                local_toc.data() + offset,
                toc_lba + 5U + static_cast<std::uint32_t>(extent));
            write_le32(local_toc.data() + offset + 4, 1);
        }
        write_le32(local_toc.data() + kLocalSizedVagOffset, kSizedVagLba);
        write_le32(local_toc.data() + kLocalSizedVagOffset + 4, 48);
        write_le32(local_toc.data() + kLocalMusicVagOffset, kMusicVagLba);
        write_le32(local_toc.data() + kLocalResourceBlockOffset, kSpeechVagLba);
        write_le32(local_toc.data() + kLocalWadRunOffset, kWadRunLba);
        write_le32(local_toc.data() + kLocalWadRunOffset + 4, kWadRunTerminatorLba);
        write_at(image, sector_offset(toc_lba), local_toc);

        std::array<std::uint8_t, kSectorSize> extent0{};
        write_le32(extent0.data(), 0x80);
        write_le32(extent0.data() + 4, 1);
        write_le32(extent0.data() + 8, 0xc0);
        write_le32(extent0.data() + 12, 16);
        extent0[0x80] = 0x42;
        write_wad_header(extent0.data() + 0xc0, 16, 2);
        write_at(image, sector_offset(toc_lba + 5U), extent0);

        std::array<std::uint8_t, kSectorSize> primary_wad{};
        write_wad_header(primary_wad.data(), 16, 3);
        write_at(image, sector_offset(toc_lba + 6U), primary_wad);
        primary_wad[7] = 4;
        write_at(image, sector_offset(toc_lba + 7U), primary_wad);

        std::array<std::uint8_t, kSectorSize> extent3{};
        write_le32(extent3.data(), 1);
        write_le32(extent3.data() + 4, 1);
        write_le32(extent3.data() + 8, 1);
        write_le32(extent3.data() + 20, 0x10U + static_cast<std::uint32_t>(level));
        write_le32(extent3.data() + 24, 0x11);
        write_le32(extent3.data() + 28, 0x20);
        write_le32(extent3.data() + 32, 0x21);
        write_le32(extent3.data() + 36, 0x30);
        write_le32(extent3.data() + 40, 0x31);
        write_at(image, sector_offset(toc_lba + 8U), extent3);
    }
}

void mutate_le32(
    const std::filesystem::path& path,
    const std::uint64_t offset,
    const std::uint32_t value) {
    std::array<std::uint8_t, 4> bytes{};
    write_le32(bytes.data(), value);
    std::fstream image(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!image) {
        throw std::runtime_error("failed to reopen synthetic TOC image");
    }
    write_at(image, offset, bytes);
}

void mutate_be32(
    const std::filesystem::path& path,
    const std::uint64_t offset,
    const std::uint32_t value) {
    std::array<std::uint8_t, 4> bytes{};
    write_be32(bytes.data(), value);
    std::fstream image(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!image) {
        throw std::runtime_error("failed to reopen synthetic TOC image");
    }
    write_at(image, offset, bytes);
}

void mutate_byte(
    const std::filesystem::path& path,
    const std::uint64_t offset,
    const std::uint8_t value) {
    const std::array<std::uint8_t, 1> byte{value};
    std::fstream image(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!image) {
        throw std::runtime_error("failed to reopen synthetic TOC image");
    }
    write_at(image, offset, byte);
}

void expect_rejected(
    const std::filesystem::path& directory,
    const std::string& name,
    const std::function<void(const std::filesystem::path&)>& mutate,
    const std::string_view message) {
    const auto path = directory / name;
    write_fixture(path);
    mutate(path);
    bool rejected = false;
    try {
        (void)openrc::inspect_disc_toc(path);
    } catch (const openrc::DiscTocError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void expect_asset_rejected(
    const std::filesystem::path& directory,
    const std::string& name,
    const std::function<void(const std::filesystem::path&)>& mutate,
    const std::string_view message) {
    const auto path = directory / name;
    write_fixture(path);
    mutate(path);
    bool rejected = false;
    try {
        (void)openrc::inspect_disc_toc_assets(path);
    } catch (const openrc::DiscTocError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void test_valid_sparse_image(const std::filesystem::path& directory) {
    const auto path = directory / "valid-sparse.iso";
    write_fixture(path);
    const auto report = openrc::inspect_disc_toc(path);

    expect(report.image_path == path, "image path was not preserved");
    expect(report.image_sectors == kImageSectors, "physical sector count is wrong");
    expect(report.declared_volume_sectors == kImageSectors, "declared sector count is wrong");
    expect(report.version == 1, "global TOC version is wrong");
    expect(report.byte_size == 0x2960, "global TOC byte size is wrong");
    expect(
        report.global_extent_slot_count == openrc::kDiscTocGlobalExtentSlotCount,
        "global extent slot count is wrong");
    expect(report.empty_global_extent_slot_count == 3, "empty slot count is wrong");
    expect(report.global_extents.size() == 476, "non-empty global extent count is wrong");
    expect(report.levels.size() == openrc::kDiscTocLevelCount, "level count is wrong");

    std::array<std::size_t, 5> signature_counts{};
    for (const auto& extent : report.global_extents) {
        ++signature_counts[static_cast<std::size_t>(extent.signature)];
    }
    expect(signature_counts[0] == 1, "WAD signature tally is wrong");
    expect(signature_counts[1] == 1, "VAGp signature tally is wrong");
    expect(signature_counts[2] == 1, "2FIP signature tally is wrong");
    expect(signature_counts[3] == 1, "PS2D signature tally is wrong");
    expect(signature_counts[4] == 472, "other signature tally is wrong");
    expect(
        openrc::disc_toc_signature_name(openrc::DiscTocSignature::two_fip) == "2FIP",
        "signature display name is wrong");

    const auto& first_level = report.levels.front();
    expect(first_level.level_id == 0, "level id is wrong");
    expect(first_level.toc_lba == kFirstLevelTocLba, "level TOC LBA is wrong");
    expect(first_level.auxiliary == 0x1000, "level auxiliary value is wrong");
    for (std::size_t index = 0; index < first_level.primary_extents.size(); ++index) {
        expect(
            first_level.primary_extents[index].lba ==
                kFirstLevelTocLba + 5U + static_cast<std::uint32_t>(index),
            "primary extent LBA is wrong");
        expect(first_level.primary_extents[index].sectors == 1, "primary extent size is wrong");
    }
}

void test_valid_asset_report(const std::filesystem::path& directory) {
    const auto path = directory / "valid-assets.iso";
    write_fixture(path);
    const auto report = openrc::inspect_disc_toc_assets(path);

    expect(report.layout.image_path == path, "asset report did not preserve its layout");
    expect(report.levels.size() == openrc::kDiscTocLevelCount, "asset level count is wrong");
    const auto& level = report.levels.front();
    expect(level.level_id == 0, "asset level id is wrong");
    expect(level.local_tables.sized_vags[0].lba == kSizedVagLba, "sized VAG LBA is wrong");
    expect(level.local_tables.sized_vags[0].byte_size == 48, "sized VAG size is wrong");
    expect(
        level.local_tables.music_vag_lbas[0] == kMusicVagLba,
        "music VAG LBA is wrong");
    expect(
        level.local_tables.resource_blocks[0].speech_vag_lbas[0] == kSpeechVagLba,
        "speech VAG LBA is wrong");
    expect(level.referenced_vags.size() == 3, "validated VAG reference count is wrong");
    for (const auto& vag : level.referenced_vags) {
        expect(vag.logical_bytes == 48, "validated VAG logical size is wrong");
        expect(vag.occupied_sectors == 1, "validated VAG sector count is wrong");
        expect(vag.signature == openrc::DiscTocSignature::vagp, "VAG signature is wrong");
    }

    const auto& run = level.local_tables.resource_blocks[0].wad_runs[0];
    expect(run.wads.size() == 1, "WAD run payload count is wrong");
    expect(run.wads[0].lba == kWadRunLba, "WAD run LBA is wrong");
    expect(run.wads[0].logical_bytes == 16, "WAD run logical size is wrong");
    expect(
        run.trailing_zero_sector_lba == kWadRunTerminatorLba,
        "WAD run terminal sector is wrong");
    expect(
        !level.local_tables.resource_blocks[0].wad_runs[1]
             .trailing_zero_sector_lba.has_value(),
        "empty WAD run gained a terminal sector");

    expect(level.primary_extent0.used_subrange_count == 2, "extent0 used count is wrong");
    expect(
        level.primary_extent0.subranges[0].signature == openrc::DiscTocSignature::other,
        "opaque extent0 subrange was misclassified");
    expect(
        level.primary_extent0.subranges[1].signature == openrc::DiscTocSignature::wad,
        "extent0 WAD subrange was not classified");
    for (const auto& wad : level.primary_wads) {
        expect(wad.logical_bytes == 16, "primary WAD logical size is wrong");
        expect(wad.occupied_sectors == 1, "primary WAD sector count is wrong");
        expect(wad.signature == openrc::DiscTocSignature::wad, "primary WAD signature is wrong");
    }
    expect(level.primary_extent3.tables[0].size() == 1, "extent3 table 0 count is wrong");
    expect(level.primary_extent3.tables[1].size() == 1, "extent3 table 1 count is wrong");
    expect(level.primary_extent3.tables[2].size() == 1, "extent3 table 2 count is wrong");
    expect(level.primary_extent3.tables[0][0].first == 0x10, "extent3 opaque pair changed");
    expect(level.primary_extent3.tables[2][0].second == 0x31, "extent3 opaque pair changed");
}

void test_global_header_and_bounds(const std::filesystem::path& directory) {
    expect_rejected(
        directory,
        "bad-version.iso",
        [](const auto& path) { mutate_le32(path, kGlobalTocOffset, 2); },
        "unsupported global TOC version should be rejected");
    expect_rejected(
        directory,
        "bad-global-size.iso",
        [](const auto& path) { mutate_le32(path, kGlobalTocOffset + 4, 0x295c); },
        "incorrect global TOC byte size should be rejected");
    expect_rejected(
        directory,
        "global-extent-oob.iso",
        [](const auto& path) {
            mutate_le32(path, kGlobalTocOffset + kGlobalExtentOffset + 4, 1000);
        },
        "out-of-bounds global extent should be rejected");
    expect_rejected(
        directory,
        "unaligned-image-size.iso",
        [](const auto& path) {
            std::filesystem::resize_file(
                path,
                static_cast<std::uint64_t>(kImageSectors) * kSectorSize + 1U);
        },
        "non-sector-aligned image should be rejected");
}

void test_continuity(const std::filesystem::path& directory) {
    expect_rejected(
        directory,
        "broken-global-chain.iso",
        [](const auto& path) {
            mutate_le32(
                path,
                kGlobalTocOffset + kGlobalExtentOffset + 8,
                openrc::kDiscTocGlobalLba + 8);
        },
        "broken global extent continuity should be rejected");
    expect_rejected(
        directory,
        "broken-local-chain.iso",
        [](const auto& path) {
            mutate_le32(
                path,
                sector_offset(kFirstLevelTocLba) + kLocalExtentOffset + 8,
                kFirstLevelTocLba + 7);
        },
        "broken primary extent continuity should be rejected");
    expect_rejected(
        directory,
        "level-overlap.iso",
        [](const auto& path) {
            mutate_le32(
                path,
                sector_offset(kFirstLevelTocLba) + kLocalExtentOffset + 3U * 8U + 4U,
                2);
        },
        "primary extents overlapping the next level TOC should be rejected");
}

void test_local_headers(const std::filesystem::path& directory) {
    expect_rejected(
        directory,
        "bad-level-id.iso",
        [](const auto& path) { mutate_le32(path, sector_offset(kFirstLevelTocLba), 1); },
        "incorrect local TOC level id should be rejected");
    expect_rejected(
        directory,
        "bad-local-size.iso",
        [](const auto& path) {
            mutate_le32(path, sector_offset(kFirstLevelTocLba) + 4, 0x2430);
        },
        "incorrect local TOC byte size should be rejected");
    expect_rejected(
        directory,
        "non-increasing-levels.iso",
        [](const auto& path) {
            mutate_le32(
                path,
                kGlobalTocOffset + kLevelDescriptorOffset + 8,
                kFirstLevelTocLba);
        },
        "non-increasing level TOC LBAs should be rejected");
}

void test_asset_local_tables(const std::filesystem::path& directory) {
    const auto local_offset = sector_offset(kFirstLevelTocLba);
    expect_asset_rejected(
        directory,
        "asset-partial-pair.iso",
        [local_offset](const auto& path) {
            mutate_le32(path, local_offset + kLocalSizedVagOffset + 4, 0);
        },
        "partially empty sized VAG pair should be rejected");
    expect_asset_rejected(
        directory,
        "asset-non-left-packed.iso",
        [local_offset](const auto& path) {
            mutate_le32(path, local_offset + kLocalMusicVagOffset + 8, kMusicVagLba);
        },
        "non-left-packed VAG table should be rejected");
    expect_asset_rejected(
        directory,
        "asset-local-reserved.iso",
        [local_offset](const auto& path) { mutate_le32(path, local_offset + 0x118, 1); },
        "nonzero local reserved word should be rejected");
    expect_asset_rejected(
        directory,
        "asset-local-tail.iso",
        [local_offset](const auto& path) { mutate_byte(path, local_offset + 0x2434, 1); },
        "nonzero local TOC physical tail should be rejected");
    expect_asset_rejected(
        directory,
        "asset-bad-vag-magic.iso",
        [](const auto& path) { mutate_byte(path, sector_offset(kSizedVagLba), 'X'); },
        "bad VAG magic should be rejected");
    expect_asset_rejected(
        directory,
        "asset-bad-vag-size.iso",
        [](const auto& path) { mutate_be32(path, sector_offset(kSizedVagLba) + 12, 1); },
        "sized VAG header disagreement should be rejected");
    expect_asset_rejected(
        directory,
        "asset-vag-padding.iso",
        [](const auto& path) { mutate_byte(path, sector_offset(kMusicVagLba) + 48, 1); },
        "nonzero VAG sector padding should be rejected");
    expect_asset_rejected(
        directory,
        "asset-bad-wad-magic.iso",
        [](const auto& path) { mutate_byte(path, sector_offset(kWadRunLba), 'X'); },
        "bad WAD magic should be rejected");
    expect_asset_rejected(
        directory,
        "asset-bad-wad-size.iso",
        [](const auto& path) { mutate_le32(path, sector_offset(kWadRunLba) + 3, 15); },
        "undersized WAD should be rejected");
    expect_asset_rejected(
        directory,
        "asset-wad-padding.iso",
        [](const auto& path) { mutate_byte(path, sector_offset(kWadRunLba) + 16, 1); },
        "nonzero WAD-run sector padding should be rejected");
    expect_asset_rejected(
        directory,
        "asset-missing-zero-terminal.iso",
        [local_offset](const auto& path) {
            mutate_le32(path, local_offset + kLocalWadRunOffset + 4, 0);
        },
        "WAD run without its zero terminal sector should be rejected");
    expect_asset_rejected(
        directory,
        "asset-nonzero-after-zero.iso",
        [local_offset](const auto& path) {
            mutate_le32(path, local_offset + kLocalWadRunOffset + 12, kWadRunTerminatorLba);
        },
        "nonzero WAD slot after an empty slot should be rejected");
}

void test_asset_primary_extents(const std::filesystem::path& directory) {
    const auto extent0_offset = sector_offset(kFirstLevelTocLba + 5U);
    expect_asset_rejected(
        directory,
        "asset-extent0-overlap.iso",
        [extent0_offset](const auto& path) { mutate_le32(path, extent0_offset + 8, 0x80); },
        "overlapping extent0 subranges should be rejected");
    expect_asset_rejected(
        directory,
        "asset-extent0-alignment.iso",
        [extent0_offset](const auto& path) { mutate_le32(path, extent0_offset + 8, 0xc1); },
        "misaligned extent0 subrange should be rejected");
    expect_asset_rejected(
        directory,
        "asset-extent0-gap.iso",
        [extent0_offset](const auto& path) { mutate_byte(path, extent0_offset + 0x81, 1); },
        "nonzero extent0 alignment gap should be rejected");
    expect_asset_rejected(
        directory,
        "asset-extent0-tail.iso",
        [extent0_offset](const auto& path) { mutate_byte(path, extent0_offset + 0xd0, 1); },
        "nonzero extent0 tail should be rejected");
    expect_asset_rejected(
        directory,
        "asset-extent0-used-after-empty.iso",
        [extent0_offset](const auto& path) {
            mutate_le32(path, extent0_offset + 3U * 8U, 0x100);
            mutate_le32(path, extent0_offset + 3U * 8U + 4U, 1);
        },
        "extent0 used record after an empty record should be rejected");

    expect_asset_rejected(
        directory,
        "asset-primary-extra-sector.iso",
        [](const auto& path) {
            const auto last_toc = kFirstLevelTocLba +
                static_cast<std::uint32_t>(openrc::kDiscTocLevelCount - 1U) * kLevelStride;
            const auto local_offset = sector_offset(last_toc);
            mutate_le32(path, local_offset + kLocalExtentOffset + 8U + 4U, 2);
            mutate_le32(path, local_offset + kLocalExtentOffset + 16U, last_toc + 8U);
            mutate_le32(path, local_offset + kLocalExtentOffset + 24U, last_toc + 9U);
        },
        "primary WAD with an extra sector should be rejected");
    expect_asset_rejected(
        directory,
        "asset-primary-padding.iso",
        [](const auto& path) {
            mutate_byte(path, sector_offset(kFirstLevelTocLba + 6U) + 16, 1);
        },
        "nonzero primary WAD padding should be rejected");

    const auto extent3_offset = sector_offset(kFirstLevelTocLba + 8U);
    expect_asset_rejected(
        directory,
        "asset-extent3-overflow.iso",
        [extent3_offset](const auto& path) {
            mutate_le32(path, extent3_offset, std::numeric_limits<std::uint32_t>::max());
        },
        "overflowing extent3 count should be rejected");
    expect_asset_rejected(
        directory,
        "asset-extent3-reserved.iso",
        [extent3_offset](const auto& path) { mutate_le32(path, extent3_offset + 12, 1); },
        "nonzero extent3 reserved word should be rejected");
    expect_asset_rejected(
        directory,
        "asset-extent3-padding.iso",
        [extent3_offset](const auto& path) { mutate_byte(path, extent3_offset + 44, 1); },
        "nonzero extent3 sector padding should be rejected");
}

} // namespace

int main() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("OpenRC-disc-toc-tests-" + std::to_string(suffix));

    try {
        std::filesystem::create_directories(directory);
        test_valid_sparse_image(directory);
        test_valid_asset_report(directory);
        test_global_header_and_bounds(directory);
        test_continuity(directory);
        test_local_headers(directory);
        test_asset_local_tables(directory);
        test_asset_primary_extents(directory);
        std::filesystem::remove_all(directory);
        std::cout << "OpenRC disc TOC tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::cerr << "OpenRC disc TOC tests failed: " << error.what() << '\n';
        return 1;
    }
}
