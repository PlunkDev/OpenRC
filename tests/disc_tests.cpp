#include "openrc/disc.hpp"

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
    write_le32(target + 2, extent);
    write_be32(target + 6, extent);
    write_le32(target + 10, size);
    write_be32(target + 14, size);
    target[25] = directory ? 0x02U : 0x00U;
    write_le16(target + 28, 1);
    write_be16(target + 30, 1);
    target[32] = static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(), name.end(), target + 33);
    return record_size;
}

std::vector<std::uint8_t> as_name(const std::string& value) {
    return {value.begin(), value.end()};
}

void create_test_iso(const std::filesystem::path& path) {
    constexpr std::uint32_t sector_count = 24;
    constexpr std::uint32_t root_sector = 20;
    constexpr std::uint32_t system_sector = 21;
    constexpr std::uint32_t executable_sector = 22;

    std::vector<std::uint8_t> image(sector_count * kSectorSize, 0);

    auto* pvd = image.data() + 16U * kSectorSize;
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    std::fill(pvd + 8, pvd + 40, static_cast<std::uint8_t>(' '));
    std::fill(pvd + 40, pvd + 72, static_cast<std::uint8_t>(' '));
    std::memcpy(pvd + 40, "OPENRC_TEST_DISC", 16);
    write_le32(pvd + 80, sector_count);
    write_be32(pvd + 84, sector_count);
    write_le16(pvd + 120, 1);
    write_be16(pvd + 122, 1);
    write_le16(pvd + 124, 1);
    write_be16(pvd + 126, 1);
    write_le16(pvd + 128, static_cast<std::uint16_t>(kSectorSize));
    write_be16(pvd + 130, static_cast<std::uint16_t>(kSectorSize));
    write_directory_record(pvd + 156, root_sector, kSectorSize, true, {0});

    auto* terminator = image.data() + 17U * kSectorSize;
    terminator[0] = 255;
    std::memcpy(terminator + 1, "CD001", 5);
    terminator[6] = 1;

    const std::string system_cnf =
        "BOOT2 = cdrom0:\\SCES_509.16;1\r\n"
        "VER = 1.00\r\n"
        "VMODE = PAL\r\n";

    auto* directory = image.data() + root_sector * kSectorSize;
    std::size_t directory_offset = 0;
    directory_offset += write_directory_record(
        directory + directory_offset,
        root_sector,
        kSectorSize,
        true,
        {0});
    directory_offset += write_directory_record(
        directory + directory_offset,
        root_sector,
        kSectorSize,
        true,
        {1});
    directory_offset += write_directory_record(
        directory + directory_offset,
        system_sector,
        static_cast<std::uint32_t>(system_cnf.size()),
        false,
        as_name("SYSTEM.CNF;1"));
    write_directory_record(
        directory + directory_offset,
        executable_sector,
        1234,
        false,
        as_name("SCES_509.16;1"));

    std::copy(
        system_cnf.begin(),
        system_cnf.end(),
        image.begin() + static_cast<std::ptrdiff_t>(system_sector * kSectorSize));

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (!output) {
        throw std::runtime_error("failed to create synthetic ISO");
    }
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_valid_disc(const std::filesystem::path& directory) {
    const auto image_path = directory / "synthetic-rac.iso";
    create_test_iso(image_path);

    const auto report = openrc::inspect_disc(image_path);
    expect(report.volume_id == "OPENRC_TEST_DISC", "volume ID was not parsed");
    expect(report.boot_path == "cdrom0:\\SCES_509.16;1", "boot path was not parsed");
    expect(report.boot_iso_path == "/SCES_509.16;1", "exact boot ISO path was not resolved");
    expect(report.serial == "SCES-50916", "serial was not normalized");
    expect(report.game == openrc::GameId::ratchet_and_clank_2002, "game was not identified");
    expect(report.region == "PAL", "region was not identified");
    expect(!report.supported_build, "serial alone must not mark an unknown executable build as supported");
    expect(report.boot_size == 1234, "boot executable size was not found");
    expect(report.boot_sha256.size() == 64, "boot executable SHA-256 was not calculated");
}

void test_invalid_disc(const std::filesystem::path& directory) {
    const auto image_path = directory / "not-an-iso.bin";
    {
        std::ofstream output(image_path, std::ios::binary | std::ios::trunc);
        const std::vector<char> data(4096, 0);
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
    }

    bool failed_as_expected = false;
    try {
        (void)openrc::inspect_disc(image_path);
    } catch (const openrc::DiscError&) {
        failed_as_expected = true;
    }
    expect(failed_as_expected, "invalid image should be rejected");
}

} // namespace

int main() {
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto test_directory = std::filesystem::temp_directory_path() /
        ("OpenRC-tests-" + std::to_string(unique_suffix));

    try {
        std::filesystem::create_directories(test_directory);
        test_valid_disc(test_directory);
        test_invalid_disc(test_directory);
        std::filesystem::remove_all(test_directory);
        std::cout << "OpenRC disc tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(test_directory, ignored);
        std::cerr << "OpenRC disc tests failed: " << error.what() << '\n';
        return 1;
    }
}
