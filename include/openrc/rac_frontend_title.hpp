#pragma once

#include "openrc/rac_frontend_input.hpp"
#include "openrc/rac_frontend_texture.hpp"
#include "openrc/rac_integer_quad.hpp"
#include "openrc/screen_overlay.hpp"

namespace openrc {

// Compiler-only source state, initialized by 1ebf14/1ebf1c/1ebf24.
struct RacFrontendTitleStateV1 {
  std::uint32_t counter = 0U;
  std::uint32_t logo_alpha = 0U;
  std::uint32_t prompt_alpha = 0U;
  bool operator==(const RacFrontendTitleStateV1 &) const = default;
};

struct RacFrontendTitlePulseV1 {
  std::uint32_t angle_bits = 0U;
  std::uint32_t cosine_bits = 0U;
  std::uint32_t scaled_bits = 0U;
  std::uint32_t alpha = 0U;
  // Ordered integer numerical reference, not a physical-console capture.
  static constexpr bool physical_console_qualified = false;
};

// All sixty reached arguments of 1eb684..1eb6ec, including the original
// VCALLMS c80 polynomial, old-I pairing, and explicit ACC overflow latch.
// No host cos(), reassociation, or MADD-as-separate-MUL+ADD substitution.
[[nodiscard]] std::array<RacFrontendTitlePulseV1, 60U>
compile_rac_frontend_title_pulse_v1();

struct RacFrontendTitleStepV1 {
  RacFrontendTitleStateV1 state;
  RacFrontendInputResultV1 input_effects;
};

// Executes the title-state part of 1eb600..1eb760, then its existing original
// input gate. Modes 0 and 3 only; mode 3 fades both overlays by 16 and leaves
// execution of the separate 21a1a0 menu owner to its existing adapter.
// Admits the source unit and PAL time scales, using the original timer's
// ordered COP1 reference arithmetic; no host rational scaling.
[[nodiscard]] RacFrontendTitleStepV1 step_rac_frontend_title_v1(
    const RacFrontendTitleStateV1 &state, std::uint32_t mode,
    std::uint32_t pressed, std::uint32_t time_scale_bits);

// Original 1eb974..1eb988 signed language selector, including language 0's
// fallback to catalog entry 4. This does not select a host language.
[[nodiscard]] std::uint32_t rac_frontend_title_prompt_index_v1(
    std::uint32_t source_language_word) noexcept;

struct RacFrontendTitleAssetsV1 {
  std::uint32_t prompt_source_index = 0U;
  RacFrontendTextureRangeV1 logo_source_range;
  RacFrontendTextureEntryV1 prompt_entry;
  // Both are original 256x128, linear RGBA byte planes. Alpha is deliberately
  // NOT doubled/clamped: logo texels above128 participate in original
  // bilinear filtering BEFORE vertex-alpha MODULATE. These source planes
  // must be lowered to a neutral compositor contract before publication.
  std::vector<std::byte> logo_rgba_source;
  std::vector<std::byte> prompt_rgba_source;
};

// Logo: 1eb0c8/1eb0d0, shared+header84, direct PSMCT32 256x128 upload.
// Prompt: existing header58/5c/68 catalog, bound by 1eb044..1eb068 and
// 1f4868, logical CLUT reordering with all original alpha bits preserved.
[[nodiscard]] RacFrontendTitleAssetsV1 compile_rac_frontend_title_assets_v1(
    std::span<const std::byte> decoded_frontend,
    std::uint32_t source_language_word, RacFrontendTextureLimitsV1 limits);

// The independently qualified logo layer only: source integer pixel centers
// map exactly to texel centers at 1:1, followed by integer MODULATE. Emits the
// source-scaled timer30 invisible prefix and 64-step alpha ramp; final held.
// Canvas dimensions describe the source render raster, BEFORE its separate
// final display blit. This is not a complete title overlay or a substitute
// for the still separately qualified localized prompt raster/menu transition.
[[nodiscard]] ScreenOverlayV1 compile_rac_frontend_title_logo_overlay_v1(
    const RacFrontendTitleAssetsV1 &assets,
    std::uint32_t source_raster_width, std::uint32_t source_raster_height,
    std::uint32_t updates_per_second, std::uint32_t time_scale_bits,
    ScreenOverlayLimitsV1 limits = {});

// Bounded public-reference raster: the original 256x128 UV rectangle over
// 192x96 pixels, integer pixel centers, affine rational coordinates floored
// to four subtexel bits, horizontal then vertical integer filter, MODULATE.
// This explicitly does not claim qualification of physical GS DDA rounding.
// Original source alpha must be <=128, as in every reached localized CLUT.
[[nodiscard]] ScreenOverlayImageV1 rasterize_rac_frontend_title_prompt_v1(
    const RacFrontendTitleAssetsV1 &assets, std::uint32_t vertex_alpha);

// Complete idle title: original logo ramp and localized sixty-phase prompt.
// Images 0..63 encode logo alpha1..64, images64..191 prompt alpha1..128;
// the extra prompt variants also cover the original mode-three fade values.
// Frames start after the first source update and loop only the prompt suffix.
// Canvas dimensions remain the original render raster, before display blit.
[[nodiscard]] ScreenOverlayV1 compile_rac_frontend_title_overlay_v1(
    const RacFrontendTitleAssetsV1 &assets,
    std::uint32_t source_raster_width, std::uint32_t source_raster_height,
    std::uint32_t updates_per_second, std::uint32_t time_scale_bits,
    ScreenOverlayLimitsV1 limits = {});

struct RacFrontendTitleDrawV1 {
  std::uint32_t source_call_pc = 0U;
  bool logo = false;
  RacIntegerQuadInputsV1 arguments;
  RacIntegerQuadEmissionV1 emission;
};

// Actual 1eb918..1eb9d4 calls and complete existing 1f5800 packets, in draw
// order. Explicit LIVE TEX0/offset/height observations, never invented GS
// allocations or incoming state. The returned source packets stay compiler
// side. 1fb848 then 1f3c10 establish separate sampler/blend/depth state.
[[nodiscard]] std::vector<RacFrontendTitleDrawV1> emit_rac_frontend_title_v1(
    const RacFrontendTitleStateV1 &state, std::uint32_t screen_height_word,
    std::array<std::uint32_t, 2U> screen_offset_y_x_words,
    std::uint64_t logo_tex0, std::uint64_t prompt_tex0);

class RacFrontendTitleError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

} // namespace openrc
