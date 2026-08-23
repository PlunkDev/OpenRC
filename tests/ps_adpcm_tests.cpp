#include "openrc/ps_adpcm.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Payload = std::array<std::byte, 14>;

constexpr openrc::PsAdpcmLimits kGenerousLimits{
    1024U,
    64U,
    64U * openrc::kPsAdpcmSamplesPerFrame,
};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void append_frame(
    std::vector<std::byte>& bytes,
    const std::uint8_t header,
    const std::uint8_t flags,
    const Payload& payload = {}) {
    bytes.push_back(static_cast<std::byte>(header));
    bytes.push_back(static_cast<std::byte>(flags));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

template <typename Callback>
void expect_rejected(Callback&& callback, const std::string& message) {
    bool rejected = false;
    try {
        callback();
    } catch (const openrc::PsAdpcmError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void test_zero_frame_and_metadata() {
    std::vector<std::byte> bytes;
    append_frame(bytes, 0U, 0U);
    const auto decoded =
        openrc::decode_ps_adpcm(bytes, kGenerousLimits);

    expect(decoded.input_bytes == 16U, "input byte count is wrong");
    expect(decoded.frame_count == 1U, "frame count is wrong");
    expect(decoded.sample_count == 28U, "sample count is wrong");
    expect(
        decoded.samples.size() == 28U &&
            std::all_of(
                decoded.samples.begin(),
                decoded.samples.end(),
                [](const std::int16_t value) { return value == 0; }),
        "zero frame did not decode to silence");
    expect(
        decoded.frames ==
            std::vector<openrc::PsAdpcmFrameMetadata>{
                {0U, 0U, 0U, 0U, 0U, 28U}},
        "zero-frame metadata is wrong");
}

void test_nibble_order() {
    Payload payload{};
    payload[0] = std::byte{0x1f};
    std::vector<std::byte> bytes;
    append_frame(bytes, 0x0cU, 0U, payload);
    const auto decoded =
        openrc::decode_ps_adpcm(bytes, kGenerousLimits);

    expect(
        decoded.samples[0] == -1 &&
            decoded.samples[1] == 1 &&
            decoded.samples[2] == 0,
        "low/high nibble order or signed expansion is wrong");
}

void test_predictor_history() {
    Payload establish{};
    establish.back() = std::byte{0x10};
    std::vector<std::byte> bytes;
    append_frame(bytes, 0x00U, 0U, establish);
    append_frame(bytes, 0x1cU, 0U);
    const auto decoded =
        openrc::decode_ps_adpcm(bytes, kGenerousLimits);

    expect(decoded.samples[27] == 4096, "history seed sample is wrong");
    expect(
        decoded.samples[28] == 3840 &&
            decoded.samples[29] == 3600 &&
            decoded.samples[30] == 3375,
        "predictor-1 history arithmetic is wrong");
}

void test_negative_division_truncates_toward_zero() {
    Payload establish{};
    establish.back() = std::byte{0xf0};
    std::vector<std::byte> bytes;
    append_frame(bytes, 0x0cU, 0U, establish);
    append_frame(bytes, 0x1cU, 0U);
    const auto decoded =
        openrc::decode_ps_adpcm(bytes, kGenerousLimits);

    expect(decoded.samples[27] == -1, "negative history seed is wrong");
    expect(
        decoded.samples[28] == 0,
        "negative predictor division did not truncate toward zero");
}

void test_saturation() {
    Payload positive{};
    positive.fill(std::byte{0x77});
    Payload negative{};
    negative.fill(std::byte{0x88});
    std::vector<std::byte> bytes;
    append_frame(bytes, 0x40U, 0U, positive);
    append_frame(bytes, 0x40U, 0U, negative);
    const auto decoded =
        openrc::decode_ps_adpcm(bytes, kGenerousLimits);

    expect(
        decoded.samples[0] == 28672 &&
            decoded.samples[1] == 32767,
        "positive saturation is wrong");
    expect(
        decoded.samples[28] == -1025 &&
            decoded.samples[29] == -32768,
        "negative saturation or saturated history is wrong");
}

void test_flag_seven_is_metadata_only_and_updates_history() {
    Payload establish{};
    establish.back() = std::byte{0x10};
    Payload payload{};
    payload[0] = std::byte{0x01};
    std::vector<std::byte> bytes;
    append_frame(bytes, 0x00U, 0U, establish);
    append_frame(bytes, 0x1cU, 7U, payload);
    append_frame(bytes, 0x1cU, 0U);
    const auto decoded =
        openrc::decode_ps_adpcm(bytes, kGenerousLimits);

    expect(
        decoded.samples[28] == 3841 &&
            decoded.samples[29] == 3600 &&
            decoded.samples[55] == 667 &&
            decoded.samples[56] == 625,
        "flag-7 frame did not decode or update history normally");
    expect(
        decoded.frames[1] ==
            openrc::PsAdpcmFrameMetadata{
                16U, 1U, 12U, 7U, 28U, 28U},
        "flag-7 metadata is wrong");
}

void test_all_low_three_bit_flags_are_accepted() {
    for (const auto flags :
         std::array<std::uint8_t, 8>{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}) {
        std::vector<std::byte> bytes;
        append_frame(bytes, 0U, flags);
        const auto decoded =
            openrc::decode_ps_adpcm(bytes, kGenerousLimits);
        expect(
            decoded.frames.size() == 1U &&
                decoded.frames[0].flags == flags,
            "a valid low-three-bit flag value was rejected");
    }
}

void test_multiframe_metadata_and_owned_output() {
    Payload payload{};
    payload[0] = std::byte{0x01};
    std::vector<std::byte> bytes;
    append_frame(bytes, 0x0cU, 1U, payload);
    append_frame(bytes, 0x20U, 6U);
    const auto decoded =
        openrc::decode_ps_adpcm(bytes, kGenerousLimits);

    expect(
        decoded.frames.size() == 2U &&
            decoded.frames[0] ==
                openrc::PsAdpcmFrameMetadata{
                    0U, 0U, 12U, 1U, 0U, 28U} &&
            decoded.frames[1] ==
                openrc::PsAdpcmFrameMetadata{
                    16U, 2U, 0U, 6U, 28U, 28U},
        "multi-frame metadata offsets are wrong");
    expect(
        decoded.samples.size() == 56U &&
            decoded.sample_count == decoded.samples.size(),
        "multi-frame sample coverage is wrong");

    std::fill(bytes.begin(), bytes.end(), std::byte{0xff});
    expect(
        decoded.samples[0] == 1 &&
            decoded.frames[0].flags == 1U,
        "decoded output borrowed its input");
}

void test_rejections() {
    std::vector<std::byte> bytes;
    expect_rejected(
        [&] { (void)openrc::decode_ps_adpcm(bytes, kGenerousLimits); },
        "empty input was accepted");

    bytes.assign(15U, std::byte{0});
    expect_rejected(
        [&] { (void)openrc::decode_ps_adpcm(bytes, kGenerousLimits); },
        "partial frame was accepted");

    bytes.clear();
    append_frame(bytes, 0x50U, 0U);
    expect_rejected(
        [&] { (void)openrc::decode_ps_adpcm(bytes, kGenerousLimits); },
        "predictor greater than four was accepted");

    bytes.clear();
    append_frame(bytes, 0x0dU, 0U);
    expect_rejected(
        [&] { (void)openrc::decode_ps_adpcm(bytes, kGenerousLimits); },
        "shift greater than twelve was accepted");

    for (const auto flags : std::array<std::uint8_t, 2>{8U, 0xffU}) {
        bytes.clear();
        append_frame(bytes, 0U, flags);
        expect_rejected(
            [&] { (void)openrc::decode_ps_adpcm(bytes, kGenerousLimits); },
            "a flag value with high bits set was accepted");
    }

    bytes.clear();
    append_frame(bytes, 0U, 0U);
    expect_rejected(
        [&] {
            (void)openrc::decode_ps_adpcm(
                bytes,
                openrc::PsAdpcmLimits{15U, 1U, 28U});
        },
        "input-byte cap was ignored");
    expect_rejected(
        [&] {
            (void)openrc::decode_ps_adpcm(
                bytes,
                openrc::PsAdpcmLimits{16U, 0U, 28U});
        },
        "frame-count cap was ignored");
    expect_rejected(
        [&] {
            (void)openrc::decode_ps_adpcm(
                bytes,
                openrc::PsAdpcmLimits{16U, 1U, 27U});
        },
        "sample-count cap was ignored");
}

} // namespace

int main() {
    try {
        test_zero_frame_and_metadata();
        test_nibble_order();
        test_predictor_history();
        test_negative_division_truncates_toward_zero();
        test_saturation();
        test_flag_seven_is_metadata_only_and_updates_history();
        test_all_low_three_bit_flags_are_accepted();
        test_multiframe_metadata_and_owned_output();
        test_rejections();
        std::cout << "OpenRC PS ADPCM tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC PS ADPCM tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
