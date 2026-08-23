#include "../src/core/iso9660.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kSectorSize = openrc::iso9660::kLogicalBlockSize;
constexpr std::uint32_t kSectorCount = 32U;
constexpr std::uint32_t kPvdSector = 17U;
constexpr std::uint32_t kRootSector = 20U;
constexpr std::uint32_t kAssetsSector = 21U;
constexpr std::uint32_t kMultiFirstSector = 22U;
constexpr std::uint32_t kMultiFinalSector = 23U;
constexpr std::uint32_t kZzzSector = 24U;
constexpr std::uint32_t kNestedSector = 25U;
constexpr std::uint32_t kZeroSector = 26U;
constexpr std::uint32_t kCollisionUpperSector = 27U;
constexpr std::uint32_t kCollisionLowerSector = 28U;

enum class FixtureKind {
    valid,
    endian_mismatch,
    output_collision,
};

[[nodiscard]] constexpr std::size_t sector_offset(const std::uint32_t sector) {
    return static_cast<std::size_t>(sector) * kSectorSize;
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

void write_both32(std::uint8_t* target, const std::uint32_t value) {
    write_le32(target, value);
    write_be32(target + 4, value);
}

[[nodiscard]] std::vector<std::uint8_t> identifier(const std::string_view value) {
    return {value.begin(), value.end()};
}

[[nodiscard]] std::size_t write_directory_record(
    std::uint8_t* target,
    const std::uint32_t extent,
    const std::uint32_t data_length,
    const std::uint8_t flags,
    const std::span<const std::uint8_t> name) {
    const std::size_t padding = (name.size() % 2U == 0U) ? 1U : 0U;
    const std::size_t record_size = 33U + name.size() + padding;
    if (record_size > 255U) {
        throw std::runtime_error("synthetic directory record is too large");
    }

    std::fill(target, target + record_size, std::uint8_t{0});
    target[0] = static_cast<std::uint8_t>(record_size);
    write_both32(target + 2, extent);
    write_both32(target + 10, data_length);
    target[25] = flags;
    write_both16(target + 28, 1U);
    target[32] = static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(), name.end(), target + 33);
    return record_size;
}

void append_directory_record(
    std::vector<std::uint8_t>& image,
    const std::uint32_t directory_sector,
    std::size_t& directory_offset,
    const std::uint32_t extent,
    const std::uint32_t data_length,
    const std::uint8_t flags,
    const std::span<const std::uint8_t> name) {
    const auto base = sector_offset(directory_sector);
    if (directory_offset >= kSectorSize) {
        throw std::runtime_error("synthetic directory overflow");
    }
    const auto written = write_directory_record(
        image.data() + base + directory_offset,
        extent,
        data_length,
        flags,
        name);
    if (written > kSectorSize - directory_offset) {
        throw std::runtime_error("synthetic directory record crosses a sector");
    }
    directory_offset += written;
}

void write_descriptor_header(
    std::vector<std::uint8_t>& image,
    const std::uint32_t sector,
    const std::uint8_t type) {
    auto* descriptor = image.data() + sector_offset(sector);
    descriptor[0] = type;
    std::memcpy(descriptor + 1, "CD001", 5U);
    descriptor[6] = 1U;
}

void write_padded_ascii(
    std::uint8_t* destination,
    const std::size_t width,
    const std::string_view value) {
    if (value.size() > width) {
        throw std::runtime_error("synthetic descriptor text is too long");
    }
    std::fill(destination, destination + width, static_cast<std::uint8_t>(' '));
    std::copy(value.begin(), value.end(), destination);
}

[[nodiscard]] std::vector<std::byte> expected_multi_file() {
    std::vector<std::byte> result(kSectorSize);
    for (std::size_t index = 0; index < kSectorSize; ++index) {
        const auto value = static_cast<std::uint8_t>((index * 37U + 11U) & 0xffU);
        result[index] = static_cast<std::byte>(value);
    }
    constexpr std::array<std::uint8_t, 5> tail{0xdeU, 0xadU, 0xbeU, 0xefU, 0x42U};
    for (const auto value : tail) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

void copy_bytes_to_sector(
    std::vector<std::uint8_t>& image,
    const std::uint32_t sector,
    const std::span<const std::byte> bytes) {
    if (bytes.size() > kSectorSize) {
        throw std::runtime_error("synthetic payload exceeds one sector");
    }
    auto* destination = image.data() + sector_offset(sector);
    std::transform(
        bytes.begin(),
        bytes.end(),
        destination,
        [](const std::byte value) { return std::to_integer<std::uint8_t>(value); });
}

void copy_text_to_sector(
    std::vector<std::uint8_t>& image,
    const std::uint32_t sector,
    const std::string_view text) {
    if (text.size() > kSectorSize) {
        throw std::runtime_error("synthetic text exceeds one sector");
    }
    std::copy(text.begin(), text.end(), image.begin() + static_cast<std::ptrdiff_t>(sector_offset(sector)));
}

void write_image(const std::filesystem::path& path, const FixtureKind kind) {
    std::vector<std::uint8_t> image(
        static_cast<std::size_t>(kSectorCount) * kSectorSize,
        std::uint8_t{0});

    // A boot-record descriptor deliberately precedes the primary descriptor.
    write_descriptor_header(image, 16U, 0U);
    write_descriptor_header(image, kPvdSector, 1U);
    write_descriptor_header(image, 18U, 255U);

    auto* pvd = image.data() + sector_offset(kPvdSector);
    write_padded_ascii(pvd + 8, 32U, "OPENRC TEST SYSTEM");
    write_padded_ascii(pvd + 40, 32U, "OPENRC_ISO9660_TEST");
    write_both32(pvd + 80, kSectorCount);
    write_both16(pvd + 120, 1U);
    write_both16(pvd + 124, 1U);
    write_both16(pvd + 128, static_cast<std::uint16_t>(kSectorSize));
    write_both32(pvd + 132, 0U);
    const std::array<std::uint8_t, 1> current_identifier{0U};
    (void)write_directory_record(
        pvd + 156,
        kRootSector,
        static_cast<std::uint32_t>(kSectorSize),
        0x02U,
        current_identifier);

    std::size_t root_offset = 0U;
    const std::array<std::uint8_t, 1> parent_identifier{1U};
    append_directory_record(
        image, kRootSector, root_offset, kRootSector,
        static_cast<std::uint32_t>(kSectorSize), 0x02U, current_identifier);
    append_directory_record(
        image, kRootSector, root_offset, kRootSector,
        static_cast<std::uint32_t>(kSectorSize), 0x02U, parent_identifier);

    // Intentionally unsorted on disc, so Image::files() must establish a stable order.
    const auto zzz_name = identifier("ZZZ.TXT;1");
    append_directory_record(image, kRootSector, root_offset, kZzzSector, 4U, 0x00U, zzz_name);
    const auto assets_name = identifier("ASSETS");
    append_directory_record(
        image, kRootSector, root_offset, kAssetsSector,
        static_cast<std::uint32_t>(kSectorSize), 0x02U, assets_name);
    const auto multi_name = identifier("MULTI.BIN;1");
    append_directory_record(
        image, kRootSector, root_offset, kMultiFirstSector,
        static_cast<std::uint32_t>(kSectorSize), 0x80U, multi_name);
    append_directory_record(
        image, kRootSector, root_offset, kMultiFinalSector, 5U, 0x00U, multi_name);
    const auto zero_name = identifier("ZERO.DAT;1");
    append_directory_record(image, kRootSector, root_offset, kZeroSector, 0U, 0x00U, zero_name);

    if (kind == FixtureKind::output_collision) {
        const auto upper_name = identifier("DUP.TXT;1");
        const auto lower_name = identifier("dup.txt;2");
        append_directory_record(
            image, kRootSector, root_offset, kCollisionUpperSector, 1U, 0x00U, upper_name);
        append_directory_record(
            image, kRootSector, root_offset, kCollisionLowerSector, 1U, 0x00U, lower_name);
    }

    std::size_t assets_offset = 0U;
    append_directory_record(
        image, kAssetsSector, assets_offset, kAssetsSector,
        static_cast<std::uint32_t>(kSectorSize), 0x02U, current_identifier);
    append_directory_record(
        image, kAssetsSector, assets_offset, kRootSector,
        static_cast<std::uint32_t>(kSectorSize), 0x02U, parent_identifier);
    const auto nested_name = identifier("NEST.TXT;1");
    append_directory_record(image, kAssetsSector, assets_offset, kNestedSector, 6U, 0x00U, nested_name);

    const auto multi = expected_multi_file();
    copy_bytes_to_sector(image, kMultiFirstSector, std::span<const std::byte>(multi.data(), kSectorSize));
    copy_bytes_to_sector(
        image,
        kMultiFinalSector,
        std::span<const std::byte>(multi.data() + kSectorSize, multi.size() - kSectorSize));
    copy_text_to_sector(image, kZzzSector, "LAST");
    copy_text_to_sector(image, kNestedSector, "NESTED");
    image[sector_offset(kCollisionUpperSector)] = 0x55U;
    image[sector_offset(kCollisionLowerSector)] = 0xaaU;

    if (kind == FixtureKind::endian_mismatch) {
        write_be32(pvd + 84, kSectorCount - 1U);
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(image.data()),
        static_cast<std::streamsize>(image.size()));
    if (!output) {
        throw std::runtime_error("failed to write synthetic ISO image");
    }
}

void test_valid_image(const std::filesystem::path& directory) {
    const auto path = directory / "valid.iso";
    write_image(path, FixtureKind::valid);

    const auto image = openrc::iso9660::Image::open(path);
    const auto& metadata = image.metadata();
    expect(metadata.system_identifier == "OPENRC TEST SYSTEM", "system identifier mismatch");
    expect(metadata.volume_identifier == "OPENRC_ISO9660_TEST", "volume identifier mismatch");
    expect(metadata.volume_blocks == kSectorCount, "declared block count mismatch");
    expect(metadata.logical_block_size == kSectorSize, "logical block size mismatch");
    expect(metadata.primary_descriptor_sector == kPvdSector, "descriptor scan did not skip boot record");
    expect(
        metadata.declared_volume_bytes == static_cast<std::uint64_t>(kSectorCount) * kSectorSize,
        "declared volume byte count mismatch");
    expect(metadata.image_bytes == metadata.declared_volume_bytes, "physical image size mismatch");

    const std::vector<std::string> expected_iso_paths{
        "/ASSETS/NEST.TXT;1",
        "/MULTI.BIN;1",
        "/ZERO.DAT;1",
        "/ZZZ.TXT;1",
    };
    const std::vector<std::string> expected_output_paths{
        "ASSETS/NEST.TXT",
        "MULTI.BIN",
        "ZERO.DAT",
        "ZZZ.TXT",
    };
    expect(image.files().size() == expected_iso_paths.size(), "unexpected file count");
    for (std::size_t index = 0U; index < image.files().size(); ++index) {
        expect(image.files()[index].iso_path == expected_iso_paths[index], "file list is not sorted by ISO path");
        expect(image.files()[index].output_path == expected_output_paths[index], "normalized output path mismatch");
    }

    const auto* multi_file = image.find_exact("/MULTI.BIN;1");
    expect(multi_file != nullptr, "multi-extent file was not found");
    expect(image.find_exact("/multi.bin;1") == nullptr, "exact lookup unexpectedly ignored case");
    expect(multi_file->size == kSectorSize + 5U, "multi-extent file size mismatch");
    expect(multi_file->extents.size() == 2U, "multi-extent records were not combined");
    expect(multi_file->extents[0].logical_block == kMultiFirstSector, "first extent block mismatch");
    expect(multi_file->extents[0].byte_length == kSectorSize, "first extent length mismatch");
    expect(multi_file->extents[1].logical_block == kMultiFinalSector, "final extent block mismatch");
    expect(multi_file->extents[1].byte_length == 5U, "final extent length mismatch");
    expect(
        multi_file->extents[0].extended_attribute_blocks == 0U &&
            multi_file->extents[1].extended_attribute_blocks == 0U,
        "unexpected extended-attribute blocks");

    const auto expected_multi = expected_multi_file();
    const auto small_read = image.read_small_file(*multi_file, expected_multi.size());
    expect(small_read == expected_multi, "read_small_file did not concatenate extents");

    std::vector<std::byte> streamed;
    image.stream_file(*multi_file, [&streamed](const std::span<const std::byte> chunk) {
        streamed.insert(streamed.end(), chunk.begin(), chunk.end());
    });
    expect(streamed == expected_multi, "stream_file did not concatenate extents");

    const auto* nested_file = image.find_exact("/ASSETS/NEST.TXT;1");
    expect(nested_file != nullptr, "nested file was not found");
    const auto nested = image.read_small_file(*nested_file, 6U);
    const std::vector<std::byte> expected_nested{
        std::byte{'N'}, std::byte{'E'}, std::byte{'S'},
        std::byte{'T'}, std::byte{'E'}, std::byte{'D'}};
    expect(nested == expected_nested, "nested file data mismatch");

    const auto* zero_file = image.find_exact("/ZERO.DAT;1");
    expect(zero_file != nullptr, "zero-byte file was not found");
    expect(zero_file->size == 0U, "zero-byte file acquired a non-zero size");
    expect(zero_file->extents.size() == 1U, "zero-byte file extent metadata missing");
    expect(image.read_small_file(*zero_file, 0U).empty(), "zero-byte read was not empty");
    std::size_t zero_callbacks = 0U;
    image.stream_file(*zero_file, [&zero_callbacks](const std::span<const std::byte>) {
        ++zero_callbacks;
    });
    expect(zero_callbacks == 0U, "zero-byte stream unexpectedly invoked its callback");

    bool limit_rejected = false;
    try {
        (void)image.read_small_file(*multi_file, multi_file->size - 1U);
    } catch (const openrc::iso9660::Error&) {
        limit_rejected = true;
    }
    expect(limit_rejected, "read_small_file ignored its allocation limit");
}

void expect_open_error(
    const std::filesystem::path& path,
    const FixtureKind kind,
    const std::string_view expected_message_fragment) {
    write_image(path, kind);
    try {
        (void)openrc::iso9660::Image::open(path);
    } catch (const openrc::iso9660::Error& error) {
        expect(
            std::string_view(error.what()).find(expected_message_fragment) != std::string_view::npos,
            "parser failed for an unexpected reason");
        return;
    }
    throw std::runtime_error("malformed ISO image was accepted");
}

void test_rejections(const std::filesystem::path& directory) {
    expect_open_error(
        directory / "endian-mismatch.iso",
        FixtureKind::endian_mismatch,
        "little/big-endian mismatch");
    expect_open_error(
        directory / "output-collision.iso",
        FixtureKind::output_collision,
        "case-insensitive normalized output collision");
}

}  // namespace

int main() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("OpenRC-iso9660-tests-" + std::to_string(suffix));

    try {
        std::filesystem::create_directories(directory);
        test_valid_image(directory);
        test_rejections(directory);
        std::filesystem::remove_all(directory);
        std::cout << "OpenRC ISO9660 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::cerr << "OpenRC ISO9660 tests failed: " << error.what() << '\n';
        return 1;
    }
}
