#include "openrc/map_art.hpp"
#include "openrc/scene_block_directory.hpp"
#include "openrc/two_fip.hpp"
#include "openrc/wad_bundle.hpp"
#include "openrc/wad_payload_probes.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kMaximumPayloadBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumSceneRecords = 4096U;
constexpr std::uint32_t kMapArtRegion0Size = 0x310U;
constexpr std::uint32_t kMapArtImageSize = 0x4420U;
constexpr std::array<std::uint32_t, openrc::kBoundaryTableBoundaryCount>
    kMapArtBoundaries{
        0x20U,
        0x330U,
        0x340U,
        0x350U,
        0x360U,
        0x4780U,
        0x8ba0U,
        0xcfc0U,
    };

using SceneBoundaries = std::array<
    std::uint32_t,
    openrc::kSceneBlockSectionBoundaryCount>;

constexpr SceneBoundaries kFirstSceneBoundaries{
    0x00U, 0x20U, 0x50U, 0x70U, 0x80U,
    0xa0U, 0xb0U, 0xc0U, 0xe0U};
constexpr SceneBoundaries kSecondSceneBoundaries{
    0x00U, 0x10U, 0x40U, 0x50U, 0x70U,
    0xa0U, 0xc0U, 0xe0U, 0x100U};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_le16(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_le32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

[[nodiscard]] constexpr std::uint32_t pack_halfwords(
    const std::uint32_t low,
    const std::uint32_t high) {
    return (low & 0xffffU) | ((high & 0xffffU) << 16U);
}

[[nodiscard]] std::vector<std::byte> make_two_fip(
    const std::uint32_t width = 1U,
    const std::uint32_t height = 1U) {
    const auto pixels = static_cast<std::size_t>(width) * height;
    std::vector<std::byte> bytes(
        openrc::kTwoFipPixelDataOffset + pixels,
        std::byte{0});
    for (std::size_t index = 0U; index < openrc::kTwoFipMagic.size(); ++index) {
        bytes[index] = static_cast<std::byte>(openrc::kTwoFipMagic[index]);
    }
    write_le32(bytes, 0x08U, width);
    write_le32(bytes, 0x0cU, height);
    write_le32(bytes, 0x10U, openrc::kTwoFipPsmT8Format);
    write_le32(bytes, 0x1cU, 1U);
    return bytes;
}

[[nodiscard]] std::vector<std::byte> make_map_art() {
    std::vector<std::byte> bytes(kMapArtBoundaries.back(), std::byte{0});
    for (std::size_t index = 0U; index < kMapArtBoundaries.size(); ++index) {
        write_le32(bytes, index * sizeof(std::uint32_t), kMapArtBoundaries[index]);
    }

    const auto region0 = static_cast<std::size_t>(kMapArtBoundaries[0]);
    write_le32(bytes, region0, openrc::kMapArtRegion0RecordCount);
    write_le32(bytes, region0 + 4U, openrc::kMapArtRegion0SecondHeaderWord);
    for (std::size_t index = 0U;
         index < openrc::kMapArtRegion0RecordCount;
         ++index) {
        write_le16(
            bytes,
            region0 + 8U + index * sizeof(std::uint16_t),
            static_cast<std::uint16_t>(
                openrc::kMapArtRegion0RelativeRecordBase + index + 1U));
    }
    expect(kMapArtRegion0Size == kMapArtBoundaries[1] - kMapArtBoundaries[0],
           "MapArt test fixture region size changed");

    const auto image = make_two_fip(
        openrc::kMapArtImageWidth,
        openrc::kMapArtImageHeight);
    expect(image.size() == kMapArtImageSize,
           "MapArt test fixture image size changed");
    for (std::size_t image_index = 0U;
         image_index < openrc::kMapArtImageCount;
         ++image_index) {
        const auto destination = kMapArtBoundaries[
            openrc::kMapArtFirstImageRegion + image_index];
        std::copy(image.begin(), image.end(), bytes.begin() + destination);
    }
    return bytes;
}

[[nodiscard]] std::size_t scene_entry_word_offset(
    const std::size_t entry,
    const std::size_t word) {
    return openrc::kSceneBlockDirectoryV1Stride +
        entry * openrc::kSceneBlockDirectoryV1Stride + word * 4U;
}

void write_scene_entry(
    std::vector<std::byte>& bytes,
    const std::size_t entry,
    const std::array<float, 4>& values,
    const std::uint32_t block_offset,
    const SceneBoundaries& boundaries,
    const std::uint32_t marker) {
    for (std::size_t component = 0U; component < values.size(); ++component) {
        write_le32(
            bytes,
            scene_entry_word_offset(entry, component),
            std::bit_cast<std::uint32_t>(values[component]));
    }
    write_le32(bytes, scene_entry_word_offset(entry, 4U), block_offset);
    write_le32(
        bytes,
        scene_entry_word_offset(entry, 5U),
        pack_halfwords(boundaries[0], boundaries[1]));
    write_le32(
        bytes,
        scene_entry_word_offset(entry, 6U),
        pack_halfwords(boundaries[3], boundaries[4]));
    write_le32(
        bytes,
        scene_entry_word_offset(entry, 7U),
        pack_halfwords(boundaries[2], boundaries[5]));
    write_le32(bytes, scene_entry_word_offset(entry, 8U), marker);
    write_le32(bytes, scene_entry_word_offset(entry, 9U), marker ^ 0xa5a5a5a5U);
    write_le32(bytes, scene_entry_word_offset(entry, 10U), marker ^ 0x3c3c3c3cU);
    write_le32(
        bytes,
        scene_entry_word_offset(entry, 11U),
        pack_halfwords(
            (boundaries[8] - boundaries[7]) /
                openrc::kSceneBlockSectionAlignment,
            boundaries[7]));
    write_le32(
        bytes,
        scene_entry_word_offset(entry, 12U),
        pack_halfwords(boundaries[6], boundaries[7]));
    write_le32(
        bytes,
        scene_entry_word_offset(entry, 13U),
        openrc::kSceneBlockSectionMarker);
    write_le32(bytes, scene_entry_word_offset(entry, 14U), boundaries.back());
    write_le32(bytes, scene_entry_word_offset(entry, 15U), marker ^ 0x5a5a5a5aU);
}

[[nodiscard]] std::vector<std::byte> make_scene_block_directory() {
    constexpr std::uint32_t kDeclaredCount = 2U;
    constexpr std::uint32_t kFirstBlockOffset = 0x80U;
    constexpr std::uint32_t kSecondBlockOffset = 0x1a0U;
    constexpr std::uint32_t kInputBytes = 0x2e0U;
    std::vector<std::byte> bytes(kInputBytes, std::byte{0});
    write_le32(bytes, 0x00U, openrc::kSceneBlockDirectoryV1Stride);
    write_le32(bytes, 0x04U, kDeclaredCount);
    write_le32(bytes, 0x08U, std::bit_cast<std::uint32_t>(2.5F));
    write_scene_entry(
        bytes,
        0U,
        {1.0F, -2.0F, 0.0F, 4.0F},
        kFirstBlockOffset,
        kFirstSceneBoundaries,
        0x11223344U);
    write_scene_entry(
        bytes,
        1U,
        {-4.0F, 8.0F, 12.0F, 0.5F},
        kSecondBlockOffset,
        kSecondSceneBoundaries,
        0x55667788U);
    return bytes;
}

[[nodiscard]] std::vector<std::byte> make_wad_bundle() {
    constexpr std::uint32_t kInitialSize = 0x10U;
    std::vector<std::byte> bytes(
        openrc::kWadBundleV1HeaderSize + kInitialSize,
        std::byte{0});
    write_le32(bytes, 0U, openrc::kWadBundleV1HeaderSize);
    write_le32(bytes, 4U, kInitialSize);
    const auto initial = static_cast<std::size_t>(openrc::kWadBundleV1HeaderSize);
    bytes[initial] = std::byte{'W'};
    bytes[initial + 1U] = std::byte{'A'};
    bytes[initial + 2U] = std::byte{'D'};
    write_le32(bytes, initial + 3U, kInitialSize);
    return bytes;
}

void expect_only_probe_matches(
    const std::vector<openrc::WadPayloadProbeV1>& probes,
    const std::size_t expected_index,
    const std::span<const std::byte> payload,
    const std::string& format_name) {
    for (std::size_t index = 0U; index < probes.size(); ++index) {
        const auto expected = index == expected_index
            ? openrc::WadPayloadProbeDecisionV1::match
            : openrc::WadPayloadProbeDecisionV1::no_match;
        expect(
            probes[index].inspect(payload) == expected,
            format_name + " production-probe classification is wrong");
    }
}

void test_factory_and_strict_format_matches() {
    const auto probes = openrc::make_known_wad_payload_probes_v1(
        {kMaximumPayloadBytes, kMaximumSceneRecords});
    expect(probes.size() == 4U, "known probe count is wrong");
    expect(
        probes[0].format_name == "TwoFipV1" &&
            probes[1].format_name == "MapArtV1" &&
            probes[2].format_name == "SceneBlockDirectoryV1" &&
            probes[3].format_name == "WadBundleV1",
        "known probe registration order or names changed");

    expect_only_probe_matches(probes, 0U, make_two_fip(), "TwoFipV1");
    expect_only_probe_matches(probes, 1U, make_map_art(), "MapArtV1");
    expect_only_probe_matches(
        probes,
        2U,
        make_scene_block_directory(),
        "SceneBlockDirectoryV1");
    expect_only_probe_matches(probes, 3U, make_wad_bundle(), "WadBundleV1");
}

void test_every_probe_rejects_a_near_miss() {
    const auto probes = openrc::make_known_wad_payload_probes_v1(
        {kMaximumPayloadBytes, kMaximumSceneRecords});

    auto two_fip = make_two_fip();
    two_fip.back() = std::byte{1};
    two_fip.push_back(std::byte{1});
    expect(
        probes[0].inspect(two_fip) ==
            openrc::WadPayloadProbeDecisionV1::no_match,
        "TwoFipV1 probe accepted non-zero trailing bytes");

    auto map_art = make_map_art();
    write_le32(map_art, 0U, 0x10U);
    expect(
        probes[1].inspect(map_art) ==
            openrc::WadPayloadProbeDecisionV1::no_match,
        "MapArtV1 probe accepted a bad boundary table");

    auto scene = make_scene_block_directory();
    write_le32(scene, 0x04U, 1U);
    expect(
        probes[2].inspect(scene) ==
            openrc::WadPayloadProbeDecisionV1::no_match,
        "SceneBlockDirectoryV1 probe accepted a one-record directory");

    auto bundle = make_wad_bundle();
    write_le32(bundle, 0U, openrc::kWadBundleV1HeaderSize - 8U);
    expect(
        probes[3].inspect(bundle) ==
            openrc::WadPayloadProbeDecisionV1::no_match,
        "WadBundleV1 probe accepted a bad header size");
}

void test_factory_limits() {
    for (const auto limits :
         std::array<openrc::WadPayloadKnownFormatProbeLimitsV1, 2>{
             openrc::WadPayloadKnownFormatProbeLimitsV1{
                 0U,
                 kMaximumSceneRecords},
             openrc::WadPayloadKnownFormatProbeLimitsV1{
                 kMaximumPayloadBytes,
                 0U}}) {
        bool rejected = false;
        try {
            (void)openrc::make_known_wad_payload_probes_v1(limits);
        } catch (const openrc::WadPayloadKnownFormatProbeError&) {
            rejected = true;
        }
        expect(rejected, "a zero known-probe limit was accepted");
    }

    const auto byte_limited = openrc::make_known_wad_payload_probes_v1({8U, 4U});
    bool rejected = false;
    try {
        (void)byte_limited.front().inspect(make_two_fip());
    } catch (const openrc::WadPayloadKnownFormatProbeError&) {
        rejected = true;
    }
    expect(rejected, "known-probe input envelope was ignored");

    const auto record_limited = openrc::make_known_wad_payload_probes_v1(
        {kMaximumPayloadBytes, 1U});
    expect(
        record_limited[2].inspect(make_scene_block_directory()) ==
            openrc::WadPayloadProbeDecisionV1::no_match,
        "the explicit scene-record cap was ignored");
}

} // namespace

int main() {
    try {
        test_factory_and_strict_format_matches();
        test_every_probe_rejects_a_near_miss();
        test_factory_limits();
        std::cout << "OpenRC known WadV1 payload probe tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "OpenRC known WadV1 payload probe tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
