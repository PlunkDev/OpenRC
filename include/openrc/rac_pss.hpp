#pragma once

#include "openrc/media_clip.hpp"

namespace openrc {

// Compiler-side MPEG-2 program stream / Sony private audio demultiplexer.
// The caller supplies the proven destination aspect of the original player;
// it is not inferred from coded pixel dimensions or a monitor's aspect.
[[nodiscard]] MediaClipV1 compile_rac_pss_v1(
    std::span<const std::byte> bytes, std::uint32_t display_aspect_numerator,
    std::uint32_t display_aspect_denominator, MediaClipLimitsV1 limits = {});

// Explicit source audio channel for the multi-language movies at 232920.
// 23bcc4 -> 12b008/12ac80 registers key bd ff a1 00 <channel>; 12aebc
// compares all five bytes. Only matching PES payloads enter the continuous
// audio stream. No locale remapping, fallback track, or mixed-channel PCM.
// A present audio stream with no matching channel is outside this compiler's
// qualified domain. The four-argument intro entry remains channel-0-only.
[[nodiscard]] MediaClipV1 compile_rac_pss_v1(
    std::span<const std::byte> bytes, std::uint32_t display_aspect_numerator,
    std::uint32_t display_aspect_denominator, MediaClipLimitsV1 limits,
    std::uint32_t source_audio_channel);

} // namespace openrc
