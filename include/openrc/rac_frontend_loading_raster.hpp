#pragma once

#include "openrc/rac_frontend_loading.hpp"
#include "openrc/screen_overlay.hpp"

namespace openrc {

// Bounded compiler raster for the actual512x64 loading bands. The source
// tile is64x64, Q=1, U=0..4 and the raw V endpoints come from the executed
// COP1 draw owner. Coordinates are affine rational values floored to four
// subtexel bits, CLAMP0 repeats each neighbour, then horizontal/vertical
// integer bilinear and MODULATE/128. This shares title's public-reference
// filter contract; physical GS DDA rounding remains unqualified.
// Raw GS alpha is retained until filtering/modulation; neutral output uses
// coverage_denominator128. No source TEX0/PIF reaches the runtime.
[[nodiscard]] ScreenOverlayImageV1 rasterize_rac_frontend_loading_band_v1(
    const RacFrontendLoadingAssetsV1 &assets,
    const RacFrontendLoadingDrawV1 &draw);

} // namespace openrc
