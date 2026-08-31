#include "openrc/localized_subtitle_bank.hpp"
#include "openrc/map_art.hpp"
#include "openrc/rac_gameplay_bank.hpp"
#include "openrc/rac_moby_class.hpp"
#include "openrc/scene_animation_bank.hpp"
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
constexpr std::uint64_t kMaximumSceneAnimationActors = 256U;
constexpr std::uint64_t kMaximumSceneAnimationFrames = 65'536U;
constexpr std::uint64_t kMaximumSubtitleEntries = 4096U;
constexpr std::uint32_t kRacGameplayMobyInstancesOffset = 0x1d0U;
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

[[nodiscard]] std::vector<std::byte> make_scene_animation_bank(
    const std::size_t subtitle_entries) {
    constexpr std::uint32_t kActorOffset = 0x40U;
    constexpr std::uint32_t kSequenceOffset = 0x50U;
    constexpr std::uint32_t kFrameOffset = 0x70U;
    constexpr std::uint32_t kRootOffset = 0x90U;
    constexpr std::uint32_t kSubtitleOffset = 0xa0U;
    const auto directory_bytes = static_cast<std::uint32_t>(
        (subtitle_entries + 1U) *
        openrc::kLocalizedSubtitleBankEntryBytesV1);
    const auto text_count = static_cast<std::uint32_t>(
        subtitle_entries * openrc::kLocalizedSubtitleLanguageCountV1);
    const auto tail_bytes =
        (directory_bytes + text_count * 4U + 0x0fU) & ~0x0fU;
    std::vector<std::byte> bytes(
        kSubtitleOffset + tail_bytes,
        std::byte{0});
    write_le32(bytes, 0x00U, 7U);
    write_le32(bytes, 0x04U, kSubtitleOffset);
    write_le32(bytes, 0x08U, openrc::kSceneAnimationBankTagV1);
    write_le32(bytes, 0x0cU, 1U);
    write_le32(bytes, 0x10U, 0x20U);
    write_le32(bytes, 0x14U, kActorOffset);

    write_le32(bytes, kActorOffset, 0U);
    write_le32(bytes, kActorOffset + 0x04U, 1U);
    write_le32(bytes, kActorOffset + 0x08U, 0U);
    write_le32(bytes, kActorOffset + 0x0cU, kRootOffset);
    bytes[kSequenceOffset + 0x10U] = std::byte{1};
    bytes[kSequenceOffset + 0x12U] = std::byte{0xff};
    bytes[kSequenceOffset + 0x13U] = std::byte{0xff};
    write_le32(bytes, kSequenceOffset + 0x1cU, 0x20U);
    write_le16(bytes, kFrameOffset + 0x06U, 1U);

    for (std::size_t entry = 0U; entry < subtitle_entries; ++entry) {
        const auto entry_offset = static_cast<std::size_t>(
            kSubtitleOffset + entry *
                openrc::kLocalizedSubtitleBankEntryBytesV1);
        write_le16(bytes, entry_offset, static_cast<std::uint16_t>(entry));
        write_le16(
            bytes,
            entry_offset + 0x02U,
            static_cast<std::uint16_t>(entry + 1U));
        for (std::size_t language = 0U;
             language < openrc::kLocalizedSubtitleLanguageCountV1;
             ++language) {
            const auto text_index =
                entry * openrc::kLocalizedSubtitleLanguageCountV1 + language;
            const auto relative = static_cast<std::uint16_t>(
                directory_bytes + text_index * 4U);
            write_le16(
                bytes,
                entry_offset + 0x04U + language * sizeof(std::uint16_t),
                relative);
            bytes[kSubtitleOffset + relative] = static_cast<std::byte>(
                static_cast<unsigned char>('A' + text_index));
        }
    }
    write_le32(
        bytes,
        kSubtitleOffset + subtitle_entries *
            openrc::kLocalizedSubtitleBankEntryBytesV1,
        0xffffffffU);
    return bytes;
}

[[nodiscard]] std::vector<std::byte> make_localized_subtitle_bank() {
    return make_scene_animation_bank(1U);
}

[[nodiscard]] std::vector<std::byte>
make_two_entry_localized_subtitle_bank() {
    return make_scene_animation_bank(2U);
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

[[nodiscard]] std::vector<std::byte> make_rac_gameplay_bank() {
    constexpr std::uint32_t kMobyClassesOffset = 0x1c0U;
    constexpr std::array<std::uint32_t, openrc::kRacGameplayBlockCountV1>
        kPhysicalPointerSlots{
            0x88U, 0x00U, 0x10U, 0x14U, 0x18U, 0x1cU,
            0x20U, 0x24U, 0x28U, 0x2cU, 0x04U, 0x80U,
            0x08U, 0x0cU, 0x40U, 0x44U, 0x54U, 0x58U,
            0x50U, 0x5cU, 0x48U, 0x4cU, 0x30U, 0x34U,
            0x38U, 0x3cU, 0x70U, 0x60U, 0x64U, 0x68U,
            0x6cU, 0x84U, 0x7cU, 0x78U, 0x74U, 0x8cU};
    std::vector<std::byte> bytes(0x3a0U, std::byte{0});
    auto block_offset = openrc::kRacGameplayFirstBlockOffsetV1;
    for (std::size_t index = 0U;
         index < kPhysicalPointerSlots.size();
         ++index) {
        write_le32(bytes, kPhysicalPointerSlots[index], block_offset);
        if (index == 1U) {
            block_offset += 0x50U;
        } else if (index == 15U) {
            block_offset += 0x90U;
        } else {
            block_offset += 0x10U;
        }
    }
    write_le32(bytes, kMobyClassesOffset, 2U);
    write_le32(bytes, kMobyClassesOffset + 4U, 0x123U);
    write_le32(bytes, kMobyClassesOffset + 8U, 0x456U);
    write_le32(bytes, kRacGameplayMobyInstancesOffset, 1U);
    write_le32(bytes, kRacGameplayMobyInstancesOffset + 4U, 3U);
    write_le32(
        bytes,
        kRacGameplayMobyInstancesOffset + 0x10U,
        openrc::kRacGameplayMobyRecordBytesV1);
    write_le32(bytes, kRacGameplayMobyInstancesOffset + 0x28U, 0x123U);
    write_le32(bytes, kRacGameplayMobyInstancesOffset + 0x2cU, 0x3f800000U);
    return bytes;
}

[[nodiscard]] std::vector<std::byte> make_rac_moby_class() {
    constexpr std::uint32_t kPacketTableOffset = 0x80U;
    constexpr std::uint32_t kVifOffset = 0x90U;
    constexpr std::uint32_t kVertexOffset = 0xb0U;
    std::vector<std::byte> bytes(0xd0U, std::byte{0});
    write_le32(bytes, 0x00U, kPacketTableOffset);
    bytes[0x04U] = std::byte{1};
    bytes[0x07U] = std::byte{1};
    bytes[0x0bU] = std::byte{0xff};
    bytes[0x0cU] = std::byte{1};
    write_le32(bytes, 0x1cU, 0x70U);
    write_le32(bytes, 0x24U, 0x3f800000U);
    write_le32(bytes, 0x3cU, 0x40000000U);
    write_le32(bytes, 0x48U, 0x50U);
    write_le32(bytes, kPacketTableOffset, kVifOffset);
    write_le16(bytes, kPacketTableOffset + 4U, 2U);
    write_le32(bytes, kPacketTableOffset + 8U, kVertexOffset);
    bytes[kPacketTableOffset + 0x0cU] = std::byte{2};
    bytes[kPacketTableOffset + 0x0dU] = std::byte{2};
    bytes[kPacketTableOffset + 0x0eU] = std::byte{1};
    bytes[kPacketTableOffset + 0x0fU] = std::byte{4};
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
        {kMaximumPayloadBytes,
         kMaximumSceneRecords,
         kMaximumSceneAnimationActors,
         kMaximumSceneAnimationFrames,
         kMaximumSubtitleEntries});
    expect(probes.size() == 7U, "known probe count is wrong");
    expect(
        probes[0].format_name == "TwoFipV1" &&
            probes[1].format_name == "MapArtV1" &&
            probes[2].format_name == "SceneBlockDirectoryV1" &&
            probes[3].format_name == "WadBundleV1" &&
            probes[4].format_name == "SceneAnimationBankV1" &&
            probes[5].format_name == "RacGameplayBankV1" &&
            probes[6].format_name == "RacMobyClassV1",
        "known probe registration order or names changed");

    expect_only_probe_matches(probes, 0U, make_two_fip(), "TwoFipV1");
    expect_only_probe_matches(probes, 1U, make_map_art(), "MapArtV1");
    expect_only_probe_matches(
        probes,
        2U,
        make_scene_block_directory(),
        "SceneBlockDirectoryV1");
    expect_only_probe_matches(probes, 3U, make_wad_bundle(), "WadBundleV1");
    expect_only_probe_matches(
        probes,
        4U,
        make_localized_subtitle_bank(),
        "SceneAnimationBankV1");
    expect_only_probe_matches(
        probes,
        5U,
        make_rac_gameplay_bank(),
        "RacGameplayBankV1");
    expect_only_probe_matches(
        probes,
        6U,
        make_rac_moby_class(),
        "RacMobyClassV1");
}

void test_every_probe_rejects_a_near_miss() {
    const auto probes = openrc::make_known_wad_payload_probes_v1(
        {kMaximumPayloadBytes,
         kMaximumSceneRecords,
         kMaximumSceneAnimationActors,
         kMaximumSceneAnimationFrames,
         kMaximumSubtitleEntries});

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

    auto subtitles = make_localized_subtitle_bank();
    write_le16(subtitles, 0xaeU, 1U);
    expect(
        probes[4].inspect(subtitles) ==
            openrc::WadPayloadProbeDecisionV1::no_match,
        "SceneAnimationBankV1 probe accepted a bad subtitle tail");

    auto gameplay = make_rac_gameplay_bank();
    write_le32(
        gameplay,
        kRacGameplayMobyInstancesOffset + 0x10U,
        0x70U);
    expect(
        probes[5].inspect(gameplay) ==
            openrc::WadPayloadProbeDecisionV1::no_match,
        "RacGameplayBankV1 probe accepted a bad moby record");

    auto incomplete_gameplay = make_rac_gameplay_bank();
    write_le32(incomplete_gameplay, 0x88U, 0U);
    expect(
        probes[5].inspect(incomplete_gameplay) ==
            openrc::WadPayloadProbeDecisionV1::no_match,
        "RacGameplayBankV1 probe accepted an incomplete directory");

    auto moby_class = make_rac_moby_class();
    moby_class[0x07U] = std::byte{2};
    expect(
        probes[6].inspect(moby_class) ==
            openrc::WadPayloadProbeDecisionV1::no_match,
        "RacMobyClassV1 probe accepted a bad metal-begin index");
}

void test_factory_limits() {
    for (const auto limits :
         std::array<openrc::WadPayloadKnownFormatProbeLimitsV1, 5>{
             openrc::WadPayloadKnownFormatProbeLimitsV1{
                 0U,
                 kMaximumSceneRecords,
                 kMaximumSceneAnimationActors,
                 kMaximumSceneAnimationFrames,
                 kMaximumSubtitleEntries},
             openrc::WadPayloadKnownFormatProbeLimitsV1{
                 kMaximumPayloadBytes,
                 0U,
                 kMaximumSceneAnimationActors,
                 kMaximumSceneAnimationFrames,
                 kMaximumSubtitleEntries},
             openrc::WadPayloadKnownFormatProbeLimitsV1{
                 kMaximumPayloadBytes,
                 kMaximumSceneRecords,
                 0U,
                 kMaximumSceneAnimationFrames,
                 kMaximumSubtitleEntries},
             openrc::WadPayloadKnownFormatProbeLimitsV1{
                 kMaximumPayloadBytes,
                 kMaximumSceneRecords,
                 kMaximumSceneAnimationActors,
                 0U,
                 kMaximumSubtitleEntries},
             openrc::WadPayloadKnownFormatProbeLimitsV1{
                 kMaximumPayloadBytes,
                 kMaximumSceneRecords,
                 kMaximumSceneAnimationActors,
                 kMaximumSceneAnimationFrames,
                 0U}}) {
        bool rejected = false;
        try {
            (void)openrc::make_known_wad_payload_probes_v1(limits);
        } catch (const openrc::WadPayloadKnownFormatProbeError&) {
            rejected = true;
        }
        expect(rejected, "a zero known-probe limit was accepted");
    }

    const auto byte_limited = openrc::make_known_wad_payload_probes_v1(
        {8U, 4U, 4U, 4U, 4U});
    bool rejected = false;
    try {
        (void)byte_limited.front().inspect(make_two_fip());
    } catch (const openrc::WadPayloadKnownFormatProbeError&) {
        rejected = true;
    }
    expect(rejected, "known-probe input envelope was ignored");

    const auto record_limited = openrc::make_known_wad_payload_probes_v1(
        {kMaximumPayloadBytes,
         1U,
         kMaximumSceneAnimationActors,
         kMaximumSceneAnimationFrames,
         kMaximumSubtitleEntries});
    expect(
        record_limited[2].inspect(make_scene_block_directory()) ==
            openrc::WadPayloadProbeDecisionV1::no_match,
        "the explicit scene-record cap was ignored");

    const auto subtitle_limited =
        openrc::make_known_wad_payload_probes_v1(
            {kMaximumPayloadBytes,
             kMaximumSceneRecords,
             kMaximumSceneAnimationActors,
             kMaximumSceneAnimationFrames,
             1U});
    const auto two_entry_subtitles =
        make_two_entry_localized_subtitle_bank();
    expect(
        subtitle_limited[4].inspect(two_entry_subtitles) ==
            openrc::WadPayloadProbeDecisionV1::no_match,
        "the explicit localized-subtitle entry cap was ignored");
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
