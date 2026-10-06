#pragma once

#include "openrc/rac_moby_class.hpp"
#include "openrc/rac_moby_sequence_set.hpp"
#include "openrc/rac_ratchet_sequence.hpp"

#include <array>
#include <span>
#include <vector>

namespace openrc {

// Compiler-owned ordinary Moby animation data. References are class-relative
// source offsets; they never become runtime pointers. No observed result is
// accepted in place of an executed animation operation.
struct RacFrontendObjectAnimationClipV1 {
  std::uint32_t source_offset = 0;
  std::array<std::uint32_t, 4> header_bits{};
  std::uint32_t rate_override_bits = 0;
  std::vector<std::uint32_t> frame_references;
  std::vector<std::uint32_t> frame_rate_bits;
  std::uint8_t sound_byte = 0xff;
  std::uint8_t trigger_byte = 0;
};

struct RacFrontendObjectAnimationBankV1 {
  std::vector<RacFrontendObjectAnimationClipV1> clips;
};

// Uses the existing ordinary-Moby class and regular-sequence parsers. The
// bounded frontend domain has silent sequences of 1..255 frames, with all
// frame rates equal to either zero or one half and a matching/zero override.
[[nodiscard]] RacFrontendObjectAnimationBankV1
decode_rac_frontend_object_animation_bank_v1(
    std::span<const std::byte> class_bytes, std::uint64_t max_input_bytes);

struct RacFrontendObjectAnimationStateV1 {
  RacMobyFrameIndicesV1 indices;
  RacMobyFrameReferencesV1 references;
  std::uint32_t phase_bits = 0;
  std::uint32_t speed_bits = 0;
  std::uint32_t rate_bits = 0;
  std::uint8_t flags = 0;
  std::uint8_t sound_handle = 0xff;
  bool operator==(const RacFrontendObjectAnimationStateV1 &) const = default;
};

struct RacFrontendObjectAnimationWriteV1 {
  std::uint32_t source_pc = 0;
  std::uint16_t object_offset = 0;
  std::uint8_t byte_count = 0;
  std::uint32_t value = 0;
  bool operator==(const RacFrontendObjectAnimationWriteV1 &) const = default;
};

struct RacFrontendObjectAnimationResultV1 {
  RacFrontendObjectAnimationStateV1 state;
  std::vector<RacFrontendObjectAnimationWriteV1> writes;
};

class RacFrontendObjectAnimationError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Original213d28 ->20d6d0, sharing the existing complete Moby setter. Keeps
// phase and speed exactly as the source does. Snapshot sequenceff is outside
// this frontend bank and is rejected, never assigned Veldin's snapshot base.
[[nodiscard]] RacFrontendObjectAnimationResultV1
set_rac_frontend_object_animation_v1(
    const RacFrontendObjectAnimationStateV1 &state,
    const RacFrontendObjectAnimationBankV1 &bank,
    std::uint32_t sequence, std::int32_t frame);

// Executes20e3d0 and the wrap/stop part23b578..23b5b4. Phase is restricted to
// {0,.5,1}, speed to {0,+1,-1}, previous/current sequence equal, and silent
// homogeneous rates {0,.5}. Every reached COP1 operation is EXACT on this
// closed domain, including ADDA/MADD, DIV by .5 and boundary crossings. This
// does not claim general ACC/FCSR execution or execute23b5d0's pose geometry.
[[nodiscard]] RacFrontendObjectAnimationResultV1
step_rac_frontend_object_animation_v1(
    const RacFrontendObjectAnimationStateV1 &state,
    const RacFrontendObjectAnimationBankV1 &bank);

} // namespace openrc
