#include "openrc/rac_frontend_title.hpp"

#include "openrc/dvp_vu_numeric.hpp"
#include "openrc/ee_cop1_numeric.hpp"
#include "openrc/ps2_palette.hpp"
#include "openrc/rac_frontend_new_game.hpp"

#include <algorithm>
#include <bit>

namespace openrc {
namespace {

void validate(const RacFrontendTitleStateV1 &state) {
  if (state.logo_alpha > 64U || state.prompt_alpha > 128U)
    throw RacFrontendTitleError("Title alpha leaves its reached source domain");
}

void validate_scale(std::uint32_t bits) {
  if (bits != 0x3f800000U && bits != 0x3f555555U)
    throw RacFrontendTitleError("Title timer requires the source unit or PAL scale");
}

std::uint32_t word(std::span<const std::byte> source, std::size_t at) {
  if (at > source.size() || source.size()-at < 4U)
    throw RacFrontendTitleError("Title source word leaves its owned bytes");
  std::uint32_t value = 0U;
  for (unsigned i = 0U; i < 4U; ++i)
    value |= std::to_integer<std::uint32_t>(source[at+i]) << (8U*i);
  return value;
}

// On these sixty source arguments every comparison has finite normal or zero
// operands. Signed-magnitude ordering is therefore exact, without a host
// floating-point operation or an inference about NaNs, ties, or denormals.
bool less_normal(std::uint32_t a, std::uint32_t b) {
  const auto magnitude_a = a & 0x7fffffffU;
  const auto magnitude_b = b & 0x7fffffffU;
  if ((magnitude_a != 0U && magnitude_a < 0x00800000U) || magnitude_a >= 0x7f800000U ||
      (magnitude_b != 0U && magnitude_b < 0x00800000U) || magnitude_b >= 0x7f800000U)
    throw RacFrontendTitleError("Title reflection leaves normal comparison domain");
  if (magnitude_a == 0U && magnitude_b == 0U)
    throw RacFrontendTitleError("Title reflection reaches an unqualified signed-zero tie");
  if ((a ^ b) & 0x80000000U) return (a & 0x80000000U) != 0U;
  return (a & 0x80000000U) ? magnitude_a > magnitude_b : magnitude_a < magnitude_b;
}

RacFrontendTitlePulseV1 pulse(std::uint32_t phase) {
  RacFrontendTitlePulseV1 out;
  out.angle_bits = ee_cop1_add_bits_v1(
      ee_cop1_mul_bits_v1(ee_cop1_cvt_s_w_bits_v1(phase).bits,
                          0x3dd67750U).bits,
      0xc0490fdbU).bits;
  // Original c80..d10 pair sequence. c98's ADDI reads OLD I=+pi while
  // that pair's lower LOI installs -pi for ca0. Its half/pi literals differ
  // by one bit from the COP1 caller's negative-pi literal above.
  auto x = dvp_vu_add_bits_v1(out.angle_bits, 0x3fc90fdaU).bits;
  const auto pi = dvp_vu_add_bits_v1(0U, 0x40490fdaU).bits;
  const auto minus_pi = dvp_vu_add_bits_v1(0U, 0xc0490fdaU).bits;
  const auto upper = dvp_vu_sub_bits_v1(pi, x).bits;
  const auto lower = dvp_vu_sub_bits_v1(minus_pi, x).bits;
  x = less_normal(x, upper) ? x : upper;
  x = less_normal(x, lower) ? lower : x;
  const auto x2 = dvp_vu_mul_bits_v1(x, x).bits;
  const auto x3 = dvp_vu_mul_bits_v1(x, x2).bits;
  const auto x5 = dvp_vu_mul_bits_v1(x3, x2).bits;
  const auto x7 = dvp_vu_mul_bits_v1(x5, x2).bits;
  const auto x9 = dvp_vu_mul_bits_v1(x7, x2).bits;
  const auto initial = dvp_vu_mul_bits_v1(x, 0x3f800000U);
  DvpVuAccumulatorLaneV1 accumulator{initial.bits, initial.overflow};
  const std::array terms{x3, x5, x7};
  const std::array coefficients{0xbe2aaaa4U, 0x3c08873eU, 0xb94fb21dU};
  for (std::size_t i = 0U; i < terms.size(); ++i) {
    const auto result = dvp_vu_madd_bits_v1(accumulator, terms[i], coefficients[i]);
    accumulator = {result.result.bits, result.result.overflow};
  }
  out.cosine_bits = dvp_vu_madd_bits_v1(accumulator, x9, 0x362e9c14U).result.bits;
  out.scaled_bits = ee_cop1_mul_bits_v1(out.cosine_bits, 0x42000000U).bits;
  out.alpha = ee_cop1_cvt_w_s_bits_v1(out.scaled_bits).bits + 96U;
  if (out.alpha < 64U || out.alpha > 128U)
    throw RacFrontendTitleError("Title source pulse leaves its alpha bounds");
  return out;
}

} // namespace

std::array<RacFrontendTitlePulseV1, 60U> compile_rac_frontend_title_pulse_v1() {
  std::array<RacFrontendTitlePulseV1, 60U> result;
  for (std::uint32_t i = 0U; i < result.size(); ++i) result[i] = pulse(i);
  return result;
}

RacFrontendTitleStepV1 step_rac_frontend_title_v1(
    const RacFrontendTitleStateV1 &state, std::uint32_t mode,
    std::uint32_t pressed, std::uint32_t time_scale_bits) {
  validate(state);
  validate_scale(time_scale_bits);
  if (mode != 0U && mode != 3U)
    throw RacFrontendTitleError("Title step requires original mode zero or three");
  RacFrontendTitleStepV1 out{state, {}};
  const auto timer = [&](std::uint32_t frames) {
    return static_cast<std::uint32_t>(evaluate_rac_frontend_timer_v1(frames, time_scale_bits));
  };
  if (mode == 0U) {
    ++out.state.counter;
    if (std::bit_cast<std::int32_t>(timer(30U)) < std::bit_cast<std::int32_t>(out.state.counter))
      out.state.logo_alpha = std::min(64U, out.state.logo_alpha+1U);
    if (std::bit_cast<std::int32_t>(timer(120U)) < std::bit_cast<std::int32_t>(out.state.counter)) {
      const auto elapsed = std::bit_cast<std::int32_t>(out.state.counter-timer(120U));
      // Both admitted thresholds are positive, so the reached subtraction
      // and remainder cannot be negative after the source comparison above.
      out.state.prompt_alpha = pulse(static_cast<std::uint32_t>(elapsed % 60)).alpha;
    }
  } else {
    out.state.counter = timer(60U);
    out.state.logo_alpha = out.state.logo_alpha < 16U ? 0U : out.state.logo_alpha-16U;
    out.state.prompt_alpha = out.state.prompt_alpha < 16U ? 0U : out.state.prompt_alpha-16U;
  }
  out.input_effects = execute_rac_frontend_title_input_v1(mode, pressed);
  return out;
}

std::uint32_t rac_frontend_title_prompt_index_v1(std::uint32_t language) noexcept {
  const auto previous = language-1U;
  return (std::bit_cast<std::int32_t>(previous) < 0 ? 0U : previous)+4U;
}

RacFrontendTitleAssetsV1 compile_rac_frontend_title_assets_v1(
    std::span<const std::byte> source, std::uint32_t language,
    RacFrontendTextureLimitsV1 limits) {
  // The existing decoder establishes the exact directory/payload ownership,
  // bounded allocations, aliases and PSMT8 transfer representation.
  const auto bank = parse_rac_frontend_texture_bank_v1(source, limits);
  RacFrontendTitleAssetsV1 result;
  result.prompt_source_index = rac_frontend_title_prompt_index_v1(language);
  if (result.prompt_source_index >= bank.textures.size())
    throw RacFrontendTitleError("Title language selects an absent original texture");
  const auto &prompt = bank.textures[result.prompt_source_index];
  if (prompt.entry.width != 256U || prompt.entry.height != 128U)
    throw RacFrontendTitleError("Title prompt does not own its original UV extent");
  const auto at = std::uint64_t{word(source, 4U)}+word(source, 0x84U);
  constexpr auto bytes = UINT64_C(256)*128U*4U;
  if ((at & 15U) != 0U || at > source.size() || bytes > source.size()-at ||
      bytes*2U > limits.max_total_output_bytes)
    throw RacFrontendTitleError("Title image planes exceed source or output limits");
  result.logo_source_range = {at, bytes};
  result.prompt_entry = prompt.entry;
  result.logo_rgba_source.assign(source.begin()+static_cast<std::ptrdiff_t>(at),
      source.begin()+static_cast<std::ptrdiff_t>(at+bytes));
  result.prompt_rgba_source.resize(bytes);
  for (std::size_t pixel = 0U; pixel < prompt.indices.size(); ++pixel) {
    const auto logical = std::to_integer<unsigned>(prompt.indices[pixel]);
    // CSM1 logical index exchanges bits3 and4 in the stored RGBA32 CLUT.
    const auto storage = psmt8_clut_storage_index_v1(static_cast<std::uint8_t>(logical));
    std::copy_n(prompt.raw_palette.begin()+storage*4U, 4U,
                result.prompt_rgba_source.begin()+static_cast<std::ptrdiff_t>(pixel*4U));
  }
  return result;
}

std::vector<RacFrontendTitleDrawV1> emit_rac_frontend_title_v1(
    const RacFrontendTitleStateV1 &state, std::uint32_t height,
    std::array<std::uint32_t, 2U> offsets,
    std::uint64_t logo_tex0, std::uint64_t prompt_tex0) {
  validate(state);
  std::vector<RacFrontendTitleDrawV1> result;
  for (unsigned i = 0U; i < 2U; ++i) {
    const auto alpha = i == 0U ? state.logo_alpha : state.prompt_alpha;
    if (alpha == 0U) continue;
    RacFrontendTitleDrawV1 draw;
    draw.logo = i == 0U;
    draw.source_call_pc = draw.logo ? 0x1eb95cU : 0x1eb9d0U;
    draw.arguments.rectangle_words = draw.logo
        ? std::array<std::uint32_t,4U>{236U,16U,256U,128U}
        : std::array<std::uint32_t,4U>{160U,height-80U,192U,96U};
    draw.arguments.uv_rectangle_words = {0U,0U,256U,128U};
    draw.arguments.screen_offset_reads = offsets;
    draw.arguments.rgbaq = 0x00808080U | (alpha << 24U);
    draw.arguments.tex0 = draw.logo ? logo_tex0 : prompt_tex0;
    draw.emission = emit_rac_integer_quad_v1(draw.arguments);
    result.push_back(std::move(draw));
  }
  return result;
}

ScreenOverlayV1 compile_rac_frontend_title_logo_overlay_v1(
    const RacFrontendTitleAssetsV1 &assets, std::uint32_t width,
    std::uint32_t height, std::uint32_t cadence, std::uint32_t time_scale_bits,
    ScreenOverlayLimitsV1 limits) {
  validate_scale(time_scale_bits);
  constexpr auto pixel_bytes = UINT64_C(256)*128U*4U;
  const auto prefix = static_cast<std::uint32_t>(evaluate_rac_frontend_timer_v1(30U,time_scale_bits));
  const auto frames = prefix+64U;
  const auto total_bytes = 96U+64U*(8U+pixel_bytes)+frames*4U+64U*12U;
  if (assets.logo_rgba_source.size() != pixel_bytes || width == 0U || height == 0U ||
      width > limits.max_dimension || height > limits.max_dimension || limits.max_dimension < 256U ||
      cadence == 0U || cadence > 1000U || limits.max_images < 64U || limits.max_frames < frames ||
      limits.max_draws < 64U || limits.max_bytes < total_bytes)
    throw RacFrontendTitleError("Title logo raster or output bounds are invalid");
  ScreenOverlayV1 out;
  out.canvas_width = width;
  out.canvas_height = height;
  out.updates_per_second = cadence;
  out.coverage_denominator = 128U;
  out.images.reserve(64U);
  out.frames.reserve(frames);
  for (std::uint32_t alpha = 1U; alpha <= 64U; ++alpha) {
    ScreenOverlayImageV1 image{256U,128U,assets.logo_rgba_source};
    for (std::size_t at = 3U; at < image.rgb_coverage.size(); at += 4U) {
      const auto original = std::to_integer<std::uint32_t>(image.rgb_coverage[at]);
      image.rgb_coverage[at] = static_cast<std::byte>((original*alpha)>>7U);
    }
    out.images.push_back(std::move(image));
  }
  RacFrontendTitleStateV1 state;
  for (unsigned tick = 0U; tick < frames; ++tick) {
    state = step_rac_frontend_title_v1(state,0U,0U,time_scale_bits).state;
    ScreenOverlayFrameV1 frame;
    if (state.logo_alpha != 0U) frame.draws.push_back({state.logo_alpha-1U,236,16});
    out.frames.push_back(std::move(frame));
  }
  validate_screen_overlay_v1(out, limits);
  return out;
}

ScreenOverlayImageV1 rasterize_rac_frontend_title_prompt_v1(
    const RacFrontendTitleAssetsV1 &assets, std::uint32_t vertex_alpha) {
  if (assets.prompt_rgba_source.size() != 256U*128U*4U ||
      vertex_alpha == 0U || vertex_alpha > 128U)
    throw RacFrontendTitleError("Title prompt raster leaves its original plane or alpha domain");
  for (std::size_t at=3U; at<assets.prompt_rgba_source.size(); at+=4U)
    if (std::to_integer<unsigned>(assets.prompt_rgba_source[at])>128U)
      throw RacFrontendTitleError("Title prompt source alpha exceeds its reached CLUT domain");
  ScreenOverlayImageV1 out{192U,96U,std::vector<std::byte>(192U*96U*4U)};
  const auto texel=[&](std::uint32_t x,std::uint32_t y,unsigned channel) {
    // Source CLAMP=0: wrap each neighbor separately, before filtering.
    return std::to_integer<std::uint32_t>(assets.prompt_rgba_source[
        ((y&127U)*256U+(x&255U))*4U+channel]);
  };
  const auto interpolate=[](std::uint32_t a,std::uint32_t b,std::uint32_t phase) {
    return ((16U-phase)*a+phase*b)>>4U;
  };
  for (std::uint32_t y=0U; y<out.height; ++y) {
    // XY starts at -1/2, UV at zero. After subtracting the texel-center
    // offset: ((n+1/2)*4/3-1/2)*16 = (64*n+8)/3. Both original triangles
    // define this same affine plane. No host FP or reciprocal approximation.
    const auto v=(64U*y+8U)/3U;
    for (std::uint32_t x=0U; x<out.width; ++x) {
      const auto u=(64U*x+8U)/3U;
      for (unsigned channel=0U; channel<4U; ++channel) {
        const auto row0=interpolate(texel(u>>4U,v>>4U,channel),
            texel((u>>4U)+1U,v>>4U,channel),u&15U);
        const auto row1=interpolate(texel(u>>4U,(v>>4U)+1U,channel),
            texel((u>>4U)+1U,(v>>4U)+1U,channel),u&15U);
        const auto filtered=interpolate(row0,row1,v&15U);
        const auto modulated=channel==3U ? (filtered*vertex_alpha)>>7U : filtered;
        out.rgb_coverage[(y*out.width+x)*4U+channel]=static_cast<std::byte>(modulated);
      }
    }
  }
  return out;
}

ScreenOverlayV1 compile_rac_frontend_title_overlay_v1(
    const RacFrontendTitleAssetsV1 &assets,std::uint32_t width,std::uint32_t height,
    std::uint32_t cadence,std::uint32_t time_scale_bits,ScreenOverlayLimitsV1 limits) {
  validate_scale(time_scale_bits);
  const auto prefix=static_cast<std::uint32_t>(evaluate_rac_frontend_timer_v1(120U,time_scale_bits));
  const auto frame_count=prefix+60U;
  const auto draw_bound=std::uint64_t{frame_count}*2U;
  const auto byte_bound=96U+64U*(8U+UINT64_C(256)*128U*4U)+
      128U*(8U+UINT64_C(192)*96U*4U)+frame_count*4U+draw_bound*12U;
  if (width!=512U || (height!=448U && height!=416U) || limits.max_images<192U ||
      limits.max_frames<frame_count || limits.max_draws<draw_bound || limits.max_bytes<byte_bound)
    throw RacFrontendTitleError("Complete title raster or output bounds are invalid");
  auto out=compile_rac_frontend_title_logo_overlay_v1(
      assets,width,height,cadence,time_scale_bits,limits);
  out.images.reserve(192U);
  for (std::uint32_t alpha=1U; alpha<=128U; ++alpha)
    out.images.push_back(rasterize_rac_frontend_title_prompt_v1(assets,alpha));
  out.frames.clear();
  out.frames.reserve(frame_count);
  out.loop_begin=prefix;
  RacFrontendTitleStateV1 state;
  for (std::uint32_t tick=0U; tick<frame_count; ++tick) {
    state=step_rac_frontend_title_v1(state,0U,0U,time_scale_bits).state;
    ScreenOverlayFrameV1 frame;
    if (state.logo_alpha!=0U) frame.draws.push_back({state.logo_alpha-1U,236,16});
    if (state.prompt_alpha!=0U)
      frame.draws.push_back({63U+state.prompt_alpha,160,static_cast<std::int32_t>(height)-80});
    out.frames.push_back(std::move(frame));
  }
  validate_screen_overlay_v1(out,limits);
  return out;
}

} // namespace openrc
