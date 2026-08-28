#include "openrc/ps_adpcm.hpp"
#include "openrc/sblk_audio.hpp"
#include "openrc/sblk_wav.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kItemCount = 4;
constexpr std::size_t kSecondarySize = 0xb0;
constexpr openrc::SBlkAudioLimits kAudioLimits{
    kItemCount,
    2U,
    kSecondarySize,
};
constexpr openrc::SBlkWavLimitsV1 kWavLimits{
    kSecondarySize,
    kSecondarySize / openrc::kPsAdpcmFrameSize,
    (kSecondarySize / openrc::kPsAdpcmFrameSize) *
        openrc::kPsAdpcmSamplesPerFrame,
    4096U,
};

void write_le32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

void write_item_word(
    openrc::SBlkBundleV3& bundle,
    const std::size_t item_index,
    const std::size_t word_index,
    const std::uint32_t value) {
    write_le32(
        bundle.item_bytes,
        item_index * openrc::kSBlkV1ItemSize + word_index * 4U,
        value);
}

[[nodiscard]] openrc::SBlkBundleV3 make_valid_bundle() {
    openrc::SBlkBundleV3 bundle;
    bundle.opaque_a = 2U << 16U;
    bundle.opaque_b = (2U << 16U) | static_cast<std::uint32_t>(kItemCount);
    bundle.descriptors = {
        openrc::SBlkDescriptor{
            0x89abcdefU,
            2U,
            2U,
            0U,
            0U,
        },
        openrc::SBlkDescriptor{
            0x01234567U,
            0x00010002U,
            2U,
            0x00010000U,
            2U * openrc::kSBlkV1ItemSize,
        },
    };
    bundle.item_bytes.resize(
        kItemCount * openrc::kSBlkV1ItemSize,
        std::byte{0});

    write_item_word(bundle, 0U, 0U, 1U);
    write_item_word(bundle, 0U, 2U, 0x807f335aU);
    write_item_word(bundle, 0U, 8U, 0x1110U);
    write_item_word(bundle, 0U, 9U, 0x40U);
    write_item_word(bundle, 1U, 0U, 0x99U);
    write_item_word(bundle, 1U, 2U, 0xaabbccddU);
    write_item_word(bundle, 2U, 0U, 1U);
    write_item_word(bundle, 2U, 2U, 0xfe81335aU);
    write_item_word(bundle, 3U, 0U, 1U);
    write_item_word(bundle, 3U, 6U, 0x50U);
    write_item_word(bundle, 3U, 8U, 0x2220U);
    write_item_word(bundle, 3U, 9U, 0x60U);

    bundle.secondary_bytes.resize(kSecondarySize, std::byte{0});
    // One-shot: lead-in Z, flag 0, full-zero flag 0, flag 1, flag 7.
    bundle.secondary_bytes[0x10] = std::byte{0x0c};
    bundle.secondary_bytes[0x12] = std::byte{0x11};
    bundle.secondary_bytes[0x30] = std::byte{0x1c};
    bundle.secondary_bytes[0x31] = std::byte{0x01};
    bundle.secondary_bytes[0x32] = std::byte{0x22};
    bundle.secondary_bytes[0x40] = std::byte{0x2c};
    bundle.secondary_bytes[0x41] = std::byte{0x07};
    bundle.secondary_bytes[0x42] = std::byte{0x33};

    // Loop: lead-in Z, flag 2, flag 6, flag 2, flag 3, padding Z.
    bundle.secondary_bytes[0x60] = std::byte{0x0c};
    bundle.secondary_bytes[0x61] = std::byte{0x02};
    bundle.secondary_bytes[0x62] = std::byte{0x44};
    bundle.secondary_bytes[0x70] = std::byte{0x1c};
    bundle.secondary_bytes[0x71] = std::byte{0x06};
    bundle.secondary_bytes[0x72] = std::byte{0x55};
    bundle.secondary_bytes[0x80] = std::byte{0x2c};
    bundle.secondary_bytes[0x81] = std::byte{0x02};
    bundle.secondary_bytes[0x82] = std::byte{0x66};
    bundle.secondary_bytes[0x90] = std::byte{0x3c};
    bundle.secondary_bytes[0x91] = std::byte{0x03};
    bundle.secondary_bytes[0x92] = std::byte{0x77};
    return bundle;
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void expect_sblk_wav_error(Function&& function, const std::string& message) {
    bool rejected = false;
    try {
        function();
    } catch (const openrc::SBlkWavError&) {
        rejected = true;
    }
    expect(rejected, message);
}

[[nodiscard]] std::uint16_t read_le16(
    const std::vector<std::byte>& bytes,
    const std::size_t offset) {
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
        static_cast<std::uint16_t>(
            std::to_integer<std::uint8_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::vector<std::byte>& bytes,
    const std::size_t offset) {
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 3U])) << 24U);
}

void expect_canonical_wav_header(
    const std::vector<std::byte>& wav,
    const std::uint32_t sample_rate,
    const std::uint32_t sample_count) {
    const auto data_bytes = sample_count * sizeof(std::int16_t);
    expect(wav.size() == 44U + data_bytes, "WAV size is wrong");
    expect(
        std::equal(
            wav.begin(),
            wav.begin() + 4,
            std::as_bytes(std::span("RIFF", 4U)).begin()),
        "WAV RIFF signature is wrong");
    expect(read_le32(wav, 0x04U) == 36U + data_bytes, "WAV RIFF size is wrong");
    expect(
        std::equal(
            wav.begin() + 8,
            wav.begin() + 12,
            std::as_bytes(std::span("WAVE", 4U)).begin()),
        "WAV WAVE signature is wrong");
    expect(read_le16(wav, 0x14U) == 1U, "WAV encoding is not PCM");
    expect(read_le16(wav, 0x16U) == 1U, "WAV channel count is not mono");
    expect(read_le32(wav, 0x18U) == sample_rate, "WAV sample rate is wrong");
    expect(
        read_le32(wav, 0x1cU) == sample_rate * sizeof(std::int16_t),
        "WAV byte rate is wrong");
    expect(read_le16(wav, 0x20U) == 2U, "WAV block alignment is wrong");
    expect(read_le16(wav, 0x22U) == 16U, "WAV bit depth is wrong");
    expect(read_le32(wav, 0x28U) == data_bytes, "WAV data size is wrong");
}

void expect_wav_pcm_matches(
    const std::vector<std::byte>& wav,
    const std::vector<std::int16_t>& samples) {
    expect(wav.size() == 44U + samples.size() * 2U, "WAV PCM size is wrong");
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto actual = read_le16(wav, 44U + index * 2U);
        const auto expected = static_cast<std::uint16_t>(samples[index]);
        expect(actual == expected, "WAV PCM sample differs from the bounded decode");
    }
}

void test_caller_supplied_one_shot_export() {
    const auto bundle = make_valid_bundle();
    const auto audio = openrc::analyze_sblk_audio_v1(bundle, kAudioLimits);
    const auto result = openrc::encode_sblk_physical_block_pcm16_mono_wav_v1(
        bundle,
        audio,
        0U,
        openrc::SBlkWavSampleRateV1{
            openrc::SBlkWavSampleRatePolicyV1::caller_supplied_hz,
            22'050U,
        },
        kWavLimits);

    expect(result.physical_block_index == 0U, "physical block index is wrong");
    expect(
        result.block_kind == openrc::SBlkAudioBlockKind::one_shot,
        "one-shot kind was not preserved");
    expect(
        result.sample_rate_policy ==
            openrc::SBlkWavSampleRatePolicyV1::caller_supplied_hz &&
            result.sample_rate_hz == 22'050U,
        "caller-supplied sample-rate policy was not preserved");
    expect(
        result.content_frame_count == 3U && result.content_sample_count == 84U,
        "one-shot content counts are wrong");
    expect(
        !result.loop_sample_begin.has_value() &&
            !result.loop_sample_end.has_value(),
        "one-shot export unexpectedly reports a loop");
    expect_canonical_wav_header(result.wav_bytes, 22'050U, 84U);

    const auto expected = openrc::decode_ps_adpcm(
        std::span<const std::byte>(bundle.secondary_bytes).subspan(0x10U, 0x30U),
        openrc::PsAdpcmLimits{0x30U, 3U, 84U});
    expect_wav_pcm_matches(result.wav_bytes, expected.samples);
}

void test_named_spu_diagnostic_loop_export() {
    const auto bundle = make_valid_bundle();
    const auto audio = openrc::analyze_sblk_audio_v1(bundle, kAudioLimits);
    const auto result = openrc::encode_sblk_physical_block_pcm16_mono_wav_v1(
        bundle,
        audio,
        1U,
        openrc::SBlkWavSampleRateV1{
            openrc::SBlkWavSampleRatePolicyV1::spu_native_48000_diagnostic,
            0U,
        },
        kWavLimits);

    expect(result.physical_block_index == 1U, "loop block index is wrong");
    expect(
        result.block_kind == openrc::SBlkAudioBlockKind::looped,
        "looped kind was not preserved");
    expect(
        result.sample_rate_policy ==
            openrc::SBlkWavSampleRatePolicyV1::spu_native_48000_diagnostic &&
            result.sample_rate_hz ==
                openrc::kSBlkSpuNativeDiagnosticSampleRateHz,
        "named SPU diagnostic policy did not resolve to 48 kHz");
    expect(
        result.content_frame_count == 4U && result.content_sample_count == 112U,
        "loop content counts are wrong");
    expect(
        result.loop_sample_begin == 28U && result.loop_sample_end == 112U,
        "loop sample bounds are wrong");
    expect_canonical_wav_header(result.wav_bytes, 48'000U, 112U);

    const auto expected = openrc::decode_ps_adpcm(
        std::span<const std::byte>(bundle.secondary_bytes).subspan(0x60U, 0x40U),
        openrc::PsAdpcmLimits{0x40U, 4U, 112U});
    expect_wav_pcm_matches(result.wav_bytes, expected.samples);
}

void test_rate_policy_and_selector_rejections() {
    const auto bundle = make_valid_bundle();
    const auto audio = openrc::analyze_sblk_audio_v1(bundle, kAudioLimits);
    const auto encode = [&](const std::uint64_t block_index,
                            const openrc::SBlkWavSampleRateV1 rate) {
        return openrc::encode_sblk_physical_block_pcm16_mono_wav_v1(
            bundle, audio, block_index, rate, kWavLimits);
    };

    expect_sblk_wav_error(
        [&] {
            (void)encode(
                0U,
                {openrc::SBlkWavSampleRatePolicyV1::caller_supplied_hz, 0U});
        },
        "a zero caller-supplied sample rate was accepted");
    expect_sblk_wav_error(
        [&] {
            (void)encode(
                0U,
                {openrc::SBlkWavSampleRatePolicyV1::spu_native_48000_diagnostic,
                 44'100U});
        },
        "a caller rate attached to the named 48 kHz policy was accepted");
    expect_sblk_wav_error(
        [&] {
            (void)encode(
                0U,
                {static_cast<openrc::SBlkWavSampleRatePolicyV1>(0xffU), 1U});
        },
        "an unsupported sample-rate policy was accepted");
    expect_sblk_wav_error(
        [&] {
            (void)encode(
                0U,
                {openrc::SBlkWavSampleRatePolicyV1::caller_supplied_hz,
                 std::numeric_limits<std::uint32_t>::max()});
        },
        "an overflowing PCM byte rate was accepted");
    expect_sblk_wav_error(
        [&] {
            (void)encode(
                2U,
                {openrc::SBlkWavSampleRatePolicyV1::caller_supplied_hz,
                 22'050U});
        },
        "an out-of-range physical block index was accepted");
    expect_sblk_wav_error(
        [&] {
            (void)encode(
                std::numeric_limits<std::uint64_t>::max(),
                {openrc::SBlkWavSampleRatePolicyV1::caller_supplied_hz,
                 22'050U});
        },
        "an overflowing physical block index was accepted");
}

void test_export_limits() {
    const auto bundle = make_valid_bundle();
    const auto audio = openrc::analyze_sblk_audio_v1(bundle, kAudioLimits);
    constexpr openrc::SBlkWavSampleRateV1 kRate{
        openrc::SBlkWavSampleRatePolicyV1::caller_supplied_hz,
        22'050U,
    };

    auto limits = kWavLimits;
    limits.max_content_adpcm_bytes = 0x2fU;
    expect_sblk_wav_error(
        [&] {
            (void)openrc::encode_sblk_physical_block_pcm16_mono_wav_v1(
                bundle, audio, 0U, kRate, limits);
        },
        "the SBlk content-byte limit was ignored");

    limits = kWavLimits;
    limits.max_content_frames = 2U;
    expect_sblk_wav_error(
        [&] {
            (void)openrc::encode_sblk_physical_block_pcm16_mono_wav_v1(
                bundle, audio, 0U, kRate, limits);
        },
        "the SBlk content-frame limit was ignored");

    limits = kWavLimits;
    limits.max_content_samples = 83U;
    expect_sblk_wav_error(
        [&] {
            (void)openrc::encode_sblk_physical_block_pcm16_mono_wav_v1(
                bundle, audio, 0U, kRate, limits);
        },
        "the SBlk content-sample limit was ignored");

    limits = kWavLimits;
    limits.max_output_bytes = 211U;
    expect_sblk_wav_error(
        [&] {
            (void)openrc::encode_sblk_physical_block_pcm16_mono_wav_v1(
                bundle, audio, 0U, kRate, limits);
        },
        "the SBlk WAV output limit was ignored");
}

void test_in_memory_contract_rejections() {
    const auto bundle = make_valid_bundle();
    const auto audio = openrc::analyze_sblk_audio_v1(bundle, kAudioLimits);
    constexpr openrc::SBlkWavSampleRateV1 kRate{
        openrc::SBlkWavSampleRatePolicyV1::caller_supplied_hz,
        22'050U,
    };
    const auto expect_report_rejected = [&](openrc::SBlkAudioReportV1 report) {
        expect_sblk_wav_error(
            [&] {
                (void)openrc::encode_sblk_physical_block_pcm16_mono_wav_v1(
                    bundle, report, 0U, kRate, kWavLimits);
            },
            "an inconsistent in-memory SBlk report was accepted");
    };

    auto inconsistent = audio;
    ++inconsistent.block_count;
    expect_report_rejected(std::move(inconsistent));

    inconsistent = audio;
    --inconsistent.secondary_byte_count;
    expect_report_rejected(std::move(inconsistent));

    inconsistent = audio;
    inconsistent.blocks[0].offset = 1U;
    expect_report_rejected(std::move(inconsistent));

    inconsistent = audio;
    inconsistent.blocks[0].offset = static_cast<std::uint32_t>(kSecondarySize);
    expect_report_rejected(std::move(inconsistent));

    inconsistent = audio;
    inconsistent.blocks[0].content_end_frame =
        inconsistent.blocks[0].content_begin_frame;
    expect_report_rejected(std::move(inconsistent));

    inconsistent = audio;
    inconsistent.blocks[0].loop_start_frame = 1U;
    expect_report_rejected(std::move(inconsistent));

    inconsistent = audio;
    inconsistent.blocks[0].kind = openrc::SBlkAudioBlockKind::looped;
    expect_report_rejected(std::move(inconsistent));
}

} // namespace

int main() {
    try {
        test_caller_supplied_one_shot_export();
        test_named_spu_diagnostic_loop_export();
        test_rate_policy_and_selector_rejections();
        test_export_limits();
        test_in_memory_contract_rejections();
        std::cout << "OpenRC SBlk WAV tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC SBlk WAV tests failed: " << error.what() << '\n';
        return 1;
    }
}
