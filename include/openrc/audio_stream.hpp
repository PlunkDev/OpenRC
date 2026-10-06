#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Prepared mono input and an explicit finite polyphase filter. The optional
// repeat is a suffix of input_samples; the preceding samples play once. Input
// decoding, predictor recurrence and control/envelope schedules belong to the
// compiler. This resource contains no compressed audio or platform registers.
struct AudioStreamV1 {
  std::uint32_t output_sample_rate=0,phase_denominator=0;
  std::uint32_t coefficient_denominator=0,gain_denominator=0;
  std::array<std::array<std::int16_t,4>,256> coefficients{};
  std::vector<std::int16_t> input_samples;
  // Both zero means finite input. Otherwise begin < end == sample count.
  std::uint64_t repeat_begin=0,repeat_end=0;
  bool operator==(const AudioStreamV1&)const=default;
};
struct AudioStreamLimitsV1 {
  std::uint64_t max_bytes=64U*1024U*1024U,max_input_samples=32U*1024U*1024U;
  std::uint32_t max_phase_increment=1U<<24U,max_render_frames=1048576U;
};
class AudioStreamError final:public std::runtime_error {
public:using std::runtime_error::runtime_error;
};
void validate_audio_stream_v1(const AudioStreamV1&,AudioStreamLimitsV1={});
[[nodiscard]] std::vector<std::byte> encode_audio_stream_v1(const AudioStreamV1&,AudioStreamLimitsV1={});
[[nodiscard]] AudioStreamV1 decode_audio_stream_v1(std::span<const std::byte>,AudioStreamLimitsV1={});

struct AudioStreamControlV1 {
  // Zero holds the current input position, including for finite input. It does
  // not pause the output clock or suppress new envelope/channel controls.
  std::uint32_t phase_increment=0,envelope=0;
  // Signed gains in [-gain_denominator,+gain_denominator] preserve channel
  // polarity. The envelope remains nonnegative.
  std::array<std::int32_t,2> channel_gains{};
  bool operator==(const AudioStreamControlV1&)const=default;
};
struct AudioStreamPlaybackStateV1 {
  std::uint64_t input_cursor=0,rendered_frames=0;
  std::uint32_t phase=0;
  bool exhausted=false;
  bool operator==(const AudioStreamPlaybackStateV1&)const=default;
};
struct AudioStreamRenderResultV1 {
  std::uint32_t frames_written=0;
  bool exhausted=false;
};

// Owns one admitted resource; rendering allocates no memory. Each frame reads
// cursor+[0,1,2,3], wrapping each tap through the prepared repeat if needed.
// It selects row floor(phase*256/phase_denominator), floors EACH signed tap
// product/divisor, sums those terms, floors the envelope multiply, then floors
// each channel gain and clamps to PCM16. Only then does it advance phase/input.
// These are explicit neutral arithmetic semantics, not a hardware emulation.
class AudioStreamPlayerV1 final {
public:
  explicit AudioStreamPlayerV1(AudioStreamV1,AudioStreamLimitsV1={});
  [[nodiscard]] const AudioStreamPlaybackStateV1& state()const noexcept;
  [[nodiscard]] std::uint32_t output_sample_rate()const noexcept;
  void reset()noexcept;
  // Output is interleaved stereo. Supply one constant control or one control
  // per requested frame. All controls/bounds are checked before any mutation.
  // Finite input ends when four lookahead samples are no longer available;
  // unwritten output remains untouched. Source-specific early-stop behavior
  // must be supplied by a qualified caller, never inferred from input EOF.
  [[nodiscard]] AudioStreamRenderResultV1 render(
      std::span<std::int16_t> output,std::span<const AudioStreamControlV1> controls);
private:
  AudioStreamV1 resource_;
  AudioStreamLimitsV1 limits_;
  AudioStreamPlaybackStateV1 state_;
};
} // namespace openrc
