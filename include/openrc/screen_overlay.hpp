#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Prepared display-encoded RGB and integer coverage. Filtering and texture
// modulation have already happened in the compiler's logical pixel raster.
// The fourth byte is a numerator over coverage_denominator, not UNORM alpha.
struct ScreenOverlayImageV1 {
  std::uint32_t width=0, height=0;
  std::vector<std::byte> rgb_coverage;
  bool operator==(const ScreenOverlayImageV1&) const = default;
};
struct ScreenOverlayDrawV1 {
  std::uint32_t image_id=0;
  std::int32_t x=0, y=0;
  bool operator==(const ScreenOverlayDrawV1&) const = default;
};
struct ScreenOverlayFrameV1 {
  std::vector<ScreenOverlayDrawV1> draws;
  bool operator==(const ScreenOverlayFrameV1&) const = default;
};
struct ScreenOverlayV1 {
  std::uint32_t canvas_width=0, canvas_height=0;
  std::uint32_t updates_per_second=0, coverage_denominator=0;
  // UINT32_MAX holds the final frame; otherwise loops this suffix after the
  // initial one-way prefix. This clock is independent of the scenic loop.
  std::uint32_t loop_begin=UINT32_MAX;
  std::vector<ScreenOverlayImageV1> images;
  std::vector<ScreenOverlayFrameV1> frames;
  bool operator==(const ScreenOverlayV1&) const = default;
};
struct ScreenOverlayLimitsV1 {
  std::uint64_t max_bytes=64U*1024U*1024U;
  std::uint32_t max_dimension=4096, max_images=1024;
  std::uint32_t max_frames=100000, max_draws=1000000;
};
class ScreenOverlayError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
void validate_screen_overlay_v1(const ScreenOverlayV1&,ScreenOverlayLimitsV1={});
[[nodiscard]] std::vector<std::byte> encode_screen_overlay_v1(
    const ScreenOverlayV1&,ScreenOverlayLimitsV1={});
[[nodiscard]] ScreenOverlayV1 decode_screen_overlay_v1(
    std::span<const std::byte>,ScreenOverlayLimitsV1={});
[[nodiscard]] std::uint32_t screen_overlay_frame_index_v1(
    const ScreenOverlayV1&,std::uint64_t update);
// Component result is dst + floor((src-dst)*coverage/denominator).
// Denominator/coverage are validated; output alpha is opaque. Draw order is
// authoritative and rectangles are clipped to the canvas before addressing.
void composite_screen_overlay_frame_v1(std::span<std::byte> canvas_rgba,
    const ScreenOverlayV1&,std::uint32_t frame_index,ScreenOverlayLimitsV1={});
} // namespace openrc
