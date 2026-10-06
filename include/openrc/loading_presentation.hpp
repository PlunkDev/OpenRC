#pragma once

#include "openrc/screen_overlay.hpp"

namespace openrc {
struct LoadingPresentationBandV1 {
  std::uint32_t label_image=0;
  std::int32_t x=0,y=0;
  std::uint32_t reveal_update=0;
  // Optional local reveal ramp overrides the common end fade while active.
  // Equal zero endpoints disable it; otherwise origin <= update < end.
  std::uint32_t reveal_ramp_origin=0,reveal_ramp_end=0;
  bool operator==(const LoadingPresentationBandV1&)const=default;
};
struct LoadingPresentationV1 {
  // Periodic, compiler-rasterized background draws, plus undrawn label images.
  // Pixels already contain filtering/modulation, with full-opacity coverage.
  ScreenOverlayV1 library;
  std::uint32_t fade_in_updates=0,fade_out_updates=0;
  std::vector<LoadingPresentationBandV1> bands;
  bool operator==(const LoadingPresentationV1&)const=default;
};
struct LoadingPresentationLimitsV1 {
  ScreenOverlayLimitsV1 overlay;
  std::uint32_t max_bands=16;
};
void validate_loading_presentation_v1(const LoadingPresentationV1&,LoadingPresentationLimitsV1={});
[[nodiscard]] std::vector<std::byte> encode_loading_presentation_v1(const LoadingPresentationV1&,LoadingPresentationLimitsV1={});
[[nodiscard]] LoadingPresentationV1 decode_loading_presentation_v1(std::span<const std::byte>,LoadingPresentationLimitsV1={});
// Output is an ordinary small neutral overlay for this actual frame/duration.
// The host clears/presents its real target and performs the corresponding I/O
// tail; materialization alone does not acknowledge a frame or a completed load.
[[nodiscard]] ScreenOverlayV1 materialize_loading_presentation_v1(
    const LoadingPresentationV1&,std::uint32_t frame,std::uint32_t duration,
    LoadingPresentationLimitsV1={});
} // namespace openrc
