#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Neutral opaque display image and its preceding feedback color transfers.
// Each table maps one previous display-encoded RGB channel to its next byte.
// Runtime never receives a blend register, source bitmap or compressed WAD.
struct ImagePresentationV1 {
  std::uint32_t width=0, height=0;
  std::uint32_t display_aspect_numerator=0, display_aspect_denominator=0;
  std::uint32_t updates_per_second=0;
  std::uint32_t transfer_lead_updates=0, transfer_tail_updates=0;
  std::vector<std::array<std::byte,256>> color_transfers;
  // Credit work elapsed after this image's initialization began. The clock
  // is quantized, wraps, and is converted to update credit before waiting.
  // Keeping this generic clock avoids replacing loading with a fixed sleep.
  std::uint32_t initialization_clock_hz=0;
  std::uint32_t initialization_clock_modulus=0;
  std::uint32_t initialization_credit_divisor=0;
  std::uint32_t minimum_initialization_updates=0;
  std::vector<std::byte> rgba;
  bool operator==(const ImagePresentationV1&) const = default;
};
struct ImagePresentationLimitsV1 {
  std::uint64_t max_bytes=16U*1024U*1024U;
  std::uint32_t max_width=1920, max_height=1088, max_transfers=1024;
};
class ImagePresentationError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
void validate_image_presentation_v1(const ImagePresentationV1& image,
    ImagePresentationLimitsV1 limits={});
[[nodiscard]] std::vector<std::byte> encode_image_presentation_v1(
    const ImagePresentationV1& image, ImagePresentationLimitsV1 limits={});
[[nodiscard]] ImagePresentationV1 decode_image_presentation_v1(
    std::span<const std::byte> bytes, ImagePresentationLimitsV1 limits={});
[[nodiscard]] std::uint32_t image_initialization_remaining_updates_v1(
    const ImagePresentationV1& image, std::uint64_t elapsed_nanoseconds);
void apply_image_color_transfer_v1(std::span<std::byte> rgba,
    const std::array<std::byte,256>& transfer);

// A feedback operation on the caller's previous completed framebuffer.
// Each table consumes the result of the preceding table, preserving byte
// quantization between presentations. There is no fabricated input image.
struct FrameColorTransferSequenceV1 {
  std::uint32_t updates_per_second=0,lead_updates=0,tail_updates=0;
  std::vector<std::array<std::byte,256>> transfers;
  bool operator==(const FrameColorTransferSequenceV1&)const=default;
};
struct FrameColorTransferSequenceLimitsV1 {
  std::uint64_t max_bytes=80U+1024U*256U;
  std::uint32_t max_transfers=1024U;
};
void validate_frame_color_transfer_sequence_v1(const FrameColorTransferSequenceV1&,
    FrameColorTransferSequenceLimitsV1={});
[[nodiscard]] std::vector<std::byte> encode_frame_color_transfer_sequence_v1(
    const FrameColorTransferSequenceV1&,FrameColorTransferSequenceLimitsV1={});
[[nodiscard]] FrameColorTransferSequenceV1 decode_frame_color_transfer_sequence_v1(
    std::span<const std::byte>,FrameColorTransferSequenceLimitsV1={});
} // namespace openrc
