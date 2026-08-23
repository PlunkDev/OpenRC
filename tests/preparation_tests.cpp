#include "openrc/hash.hpp"
#include "openrc/preparation.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kSectorSize = 2048;

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

std::size_t write_directory_record(
    std::uint8_t* target,
    const std::uint32_t extent,
    const std::uint32_t size,
    const bool directory,
    const std::vector<std::uint8_t>& name) {
    const auto padding = name.size() % 2U == 0U ? 1U : 0U;
    const auto record_size = 33U + name.size() + padding;
    std::fill(target, target + record_size, 0);
    target[0] = static_cast<std::uint8_t>(record_size);
    write_both32(target + 2, extent);
    write_both32(target + 10, size);
    target[25] = directory ? 0x02U : 0x00U;
    write_both16(target + 28, 1);
    target[32] = static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(), name.end(), target + 33);
    return record_size;
}

std::vector<std::uint8_t> as_name(const std::string& value) {
    return {value.begin(), value.end()};
}

struct SyntheticDisc {
    std::string system_cnf;
    std::vector<std::uint8_t> boot;
    std::vector<std::uint8_t> data;
};

SyntheticDisc create_test_iso(const std::filesystem::path& path) {
    constexpr std::uint32_t sector_count = 32;
    constexpr std::uint32_t root_sector = 20;
    constexpr std::uint32_t system_sector = 21;
    constexpr std::uint32_t boot_sector = 22;
    constexpr std::uint32_t data_directory_sector = 23;
    constexpr std::uint32_t data_sector = 24;

    SyntheticDisc contents;
    contents.system_cnf =
        "BOOT2 = cdrom0:\\SCES_509.16;1\r\n"
        "VER = 1.00\r\n"
        "VMODE = PAL\r\n";
    contents.boot.resize(173);
    for (std::size_t index = 0; index < contents.boot.size(); ++index) {
        contents.boot[index] = static_cast<std::uint8_t>((index * 13U + 7U) & 0xffU);
    }
    contents.data = {0x10U, 0x20U, 0x30U, 0x40U, 0x50U};

    std::vector<std::uint8_t> image(sector_count * kSectorSize, 0);
    auto* pvd = image.data() + 16U * kSectorSize;
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    std::fill(pvd + 8, pvd + 40, static_cast<std::uint8_t>(' '));
    std::memcpy(pvd + 8, "OPENRC", 6);
    std::fill(pvd + 40, pvd + 72, static_cast<std::uint8_t>(' '));
    std::memcpy(pvd + 40, "OPENRC_PREPARATION_TEST", 23);
    write_both32(pvd + 80, sector_count);
    write_both16(pvd + 120, 1);
    write_both16(pvd + 124, 1);
    write_both16(pvd + 128, static_cast<std::uint16_t>(kSectorSize));
    write_both32(pvd + 132, 0);
    write_directory_record(pvd + 156, root_sector, kSectorSize, true, {0});

    auto* terminator = image.data() + 17U * kSectorSize;
    terminator[0] = 255;
    std::memcpy(terminator + 1, "CD001", 5);
    terminator[6] = 1;

    auto* root = image.data() + root_sector * kSectorSize;
    std::size_t root_offset = 0;
    root_offset += write_directory_record(root + root_offset, root_sector, kSectorSize, true, {0});
    root_offset += write_directory_record(root + root_offset, root_sector, kSectorSize, true, {1});
    root_offset += write_directory_record(
        root + root_offset,
        system_sector,
        static_cast<std::uint32_t>(contents.system_cnf.size()),
        false,
        as_name("SYSTEM.CNF;1"));
    root_offset += write_directory_record(
        root + root_offset,
        boot_sector,
        static_cast<std::uint32_t>(contents.boot.size()),
        false,
        as_name("SCES_509.16;1"));
    write_directory_record(
        root + root_offset,
        data_directory_sector,
        kSectorSize,
        true,
        as_name("DATA"));

    auto* data_directory = image.data() + data_directory_sector * kSectorSize;
    std::size_t data_directory_offset = 0;
    data_directory_offset += write_directory_record(
        data_directory + data_directory_offset,
        data_directory_sector,
        kSectorSize,
        true,
        {0});
    data_directory_offset += write_directory_record(
        data_directory + data_directory_offset,
        root_sector,
        kSectorSize,
        true,
        {1});
    write_directory_record(
        data_directory + data_directory_offset,
        data_sector,
        static_cast<std::uint32_t>(contents.data.size()),
        false,
        as_name("BLOB.BIN;1"));

    std::copy(
        contents.system_cnf.begin(),
        contents.system_cnf.end(),
        image.begin() + static_cast<std::ptrdiff_t>(system_sector * kSectorSize));
    std::copy(
        contents.boot.begin(),
        contents.boot.end(),
        image.begin() + static_cast<std::ptrdiff_t>(boot_sector * kSectorSize));
    std::copy(
        contents.data.begin(),
        contents.data.end(),
        image.begin() + static_cast<std::ptrdiff_t>(data_sector * kSectorSize));

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (!output) {
        throw std::runtime_error("failed to create synthetic preparation ISO");
    }
    return contents;
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("failed to open extracted test file");
    }
    const auto size = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    if (input.gcount() != static_cast<std::streamsize>(result.size())) {
        throw std::runtime_error("failed to read extracted test file");
    }
    return result;
}

std::string read_text(const std::filesystem::path& path) {
    const auto bytes = read_bytes(path);
    return {bytes.begin(), bytes.end()};
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_inventory_and_preparation(const std::filesystem::path& directory) {
    const auto image_path = directory / "synthetic.iso";
    const auto contents = create_test_iso(image_path);
    const auto games_directory = directory / "games";

    const auto inventory = openrc::inventory_disc(image_path);
    expect(inventory.files.size() == 3, "inventory did not include every file");
    expect(inventory.disc.boot_iso_path == "/SCES_509.16;1", "exact BOOT path is wrong");
    expect(inventory.files[0].iso_path == "/DATA/BLOB.BIN;1", "inventory order is not deterministic");
    expect(inventory.files[0].output_path == "DATA/BLOB.BIN", "output path was not mapped");
    expect(inventory.files[0].extents.size() == 1, "file extent was not mapped");
    expect(inventory.files[0].extents[0].logical_block == 24, "file data LBA is wrong");

    std::vector<openrc::PreparationPhase> phases;
    const auto result = openrc::prepare_game_files(
        image_path,
        games_directory,
        [&phases](const openrc::PreparationProgress& progress) {
            phases.push_back(progress.phase);
            expect(progress.bytes_processed <= progress.total_bytes || progress.total_bytes == 0,
                "progress byte count exceeded its total");
            return true;
        });

    expect(!result.already_prepared, "first preparation was reported as reused");
    expect(result.image_sha256 == openrc::sha256_file(image_path), "full image hash is wrong");
    expect(result.destination == games_directory / "SCES-50916" / result.image_sha256,
        "destination layout is wrong");
    expect(read_bytes(result.boot_executable_path) == contents.boot, "BOOT bytes changed");
    expect(read_bytes(result.destination / "files" / "DATA" / "BLOB.BIN") == contents.data,
        "nested extracted bytes changed");
    expect(read_text(result.destination / "files" / "SYSTEM.CNF") == contents.system_cnf,
        "SYSTEM.CNF bytes changed");

    const auto manifest = read_text(result.manifest_path);
    expect(manifest.find('\r') == std::string::npos, "manifest is not LF-only");
    expect(manifest.ends_with("}\n"), "manifest has no deterministic final LF");
    expect(manifest.find("\"iso_path\": \"/DATA/BLOB.BIN;1\"") != std::string::npos,
        "manifest omitted the raw ISO path");
    expect(manifest.find("\"logical_block\": 24") != std::string::npos,
        "manifest omitted file extents");
    expect(manifest.find("\"source\": {") != std::string::npos,
        "manifest source is not a deterministic object");
    expect(manifest.find("synthetic.iso") == std::string::npos,
        "manifest leaked the source ISO path");
    expect(std::find(phases.begin(), phases.end(), openrc::PreparationPhase::hashing_image) != phases.end(),
        "hashing progress was not published");
    expect(std::find(phases.begin(), phases.end(), openrc::PreparationPhase::extracting_files) != phases.end(),
        "extraction progress was not published");

    const auto reused = openrc::prepare_game_files(image_path, games_directory);
    expect(reused.already_prepared, "valid existing preparation was not reused");

    bool reuse_cancelled = false;
    try {
        (void)openrc::prepare_game_files(
            image_path,
            games_directory,
            [](const openrc::PreparationProgress& progress) {
                return progress.phase != openrc::PreparationPhase::verifying_files ||
                    progress.bytes_processed == 0;
            });
    } catch (const openrc::PreparationCancelled&) {
        reuse_cancelled = true;
    }
    expect(reuse_cancelled, "existing-file verification could not be cancelled");

    const auto copied_image_directory = directory / "copied-image";
    std::filesystem::create_directories(copied_image_directory);
    const auto copied_image_path = copied_image_directory / "same-disc.iso";
    std::filesystem::copy_file(image_path, copied_image_path);
    const auto copied_result = openrc::prepare_game_files(
        copied_image_path,
        directory / "copied-games");
    expect(copied_result.image_sha256 == result.image_sha256,
        "byte-identical ISO copy produced a different image hash");
    expect(read_text(copied_result.manifest_path) == manifest,
        "byte-identical ISO copy produced different manifest bytes");

    const auto data_path = result.destination / "files" / "DATA" / "BLOB.BIN";
    auto tampered_data = contents.data;
    tampered_data.front() ^= 0xffU;
    {
        std::ofstream output(data_path, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(tampered_data.data()),
            static_cast<std::streamsize>(tampered_data.size()));
        expect(static_cast<bool>(output), "failed to tamper extracted data file");
    }
    bool tampered_file_rejected = false;
    try {
        (void)openrc::prepare_game_files(image_path, games_directory);
    } catch (const std::runtime_error&) {
        tampered_file_rejected = true;
    }
    expect(tampered_file_rejected,
        "same-size tampering of an extracted file was not rejected");
    {
        std::ofstream output(data_path, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(contents.data.data()),
            static_cast<std::streamsize>(contents.data.size()));
        expect(static_cast<bool>(output), "failed to restore extracted data file");
    }

    {
        std::ofstream output(result.manifest_path, std::ios::binary | std::ios::trunc);
        output << "{\n  \"image_sha256\": \"wrong\"\n}\n";
    }
    bool rejected = false;
    try {
        (void)openrc::prepare_game_files(image_path, games_directory);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, "conflicting existing destination was not rejected");
}

void test_cancelled_staging_is_removed(const std::filesystem::path& directory) {
    const auto image_path = directory / "cancelled.iso";
    create_test_iso(image_path);
    const auto games_directory = directory / "cancelled-games";

    bool cancelled = false;
    try {
        (void)openrc::prepare_game_files(
            image_path,
            games_directory,
            [](const openrc::PreparationProgress& progress) {
                return progress.phase != openrc::PreparationPhase::extracting_files ||
                    progress.bytes_processed == 0;
            });
    } catch (const openrc::PreparationCancelled&) {
        cancelled = true;
    }
    expect(cancelled, "progress cancellation did not throw PreparationCancelled");

    const auto serial_directory = games_directory / "SCES-50916";
    if (std::filesystem::exists(serial_directory)) {
        expect(
            std::filesystem::directory_iterator(serial_directory) ==
                std::filesystem::directory_iterator{},
            "cancelled preparation left a staging or final directory");
    }
}

} // namespace

int main() {
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto test_directory = std::filesystem::temp_directory_path() /
        ("OpenRC-preparation-tests-" + std::to_string(unique_suffix));

    try {
        std::filesystem::create_directories(test_directory);
        test_inventory_and_preparation(test_directory);
        test_cancelled_staging_is_removed(test_directory);
        std::filesystem::remove_all(test_directory);
        std::cout << "OpenRC preparation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(test_directory, ignored);
        std::cerr << "OpenRC preparation tests failed: " << error.what() << '\n';
        return 1;
    }
}
