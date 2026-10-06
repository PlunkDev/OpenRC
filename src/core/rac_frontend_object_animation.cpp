#include "openrc/rac_frontend_object_animation.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace openrc {
namespace {
constexpr std::uint32_t half = 0x3f000000U, one = 0x3f800000U;
[[noreturn]] void fail(const char *message) {
  throw RacFrontendObjectAnimationError(message);
}
void validate(const RacFrontendObjectAnimationBankV1 &bank) {
  if (bank.clips.empty() || bank.clips.size() > 255U)
    fail("Frontend object animation bank exceeds its bounded sequence domain");
  for (const auto &clip : bank.clips) {
    if (clip.frame_references.empty() || clip.frame_references.size() > 255U ||
        clip.frame_references.size() != clip.frame_rate_bits.size() ||
        clip.sound_byte != 0xffU || clip.trigger_byte != 0U)
      fail("Frontend object sequence has unsupported frames or audio");
    const auto rate = clip.frame_rate_bits.front();
    if ((rate != 0U && rate != half) ||
        (clip.rate_override_bits != 0U && clip.rate_override_bits != rate))
      fail("Frontend object sequence has an unsupported phase rate");
    for (std::size_t i = 0; i < clip.frame_references.size(); ++i)
      if ((clip.frame_references[i] & 3U) || clip.frame_rate_bits[i] != rate)
        fail("Frontend object sequence has unaligned frames or varying rates");
  }
}
const RacFrontendObjectAnimationClipV1 &at(
    const RacFrontendObjectAnimationBankV1 &bank, std::uint32_t sequence) {
  if (sequence >= bank.clips.size()) fail("Frontend object sequence is absent");
  return bank.clips[sequence];
}
int phase_units(std::uint32_t bits) {
  if (bits == 0U) return 0;
  if (bits == half) return 1;
  if (bits == one) return 2;
  fail("Frontend object phase lies outside exact half-unit domain");
}
std::uint32_t phase_bits(int units) {
  if (units == 0) return 0;
  if (units == 1) return half;
  if (units == 2) return one;
  fail("Frontend object phase escaped exact half-unit domain");
}
void write(RacFrontendObjectAnimationResultV1 &out, std::uint32_t pc,
           std::uint16_t offset, std::uint8_t bytes, std::uint32_t value) {
  out.writes.push_back({pc, offset, bytes, value});
}
} // namespace

RacFrontendObjectAnimationBankV1 decode_rac_frontend_object_animation_bank_v1(
    std::span<const std::byte> bytes, std::uint64_t max_bytes) {
  if (!max_bytes || bytes.size() > max_bytes || bytes.size() > UINT32_MAX)
    fail("Frontend object class exceeds its input limit");
  const auto model = parse_rac_moby_class_v1(bytes, {max_bytes, false});
  RacFrontendObjectAnimationBankV1 out;
  out.clips.reserve(model.sequence_offsets.size());
  for (auto begin : model.sequence_offsets) {
    if (!begin) fail("Frontend object class contains an absent sequence");
    std::uint64_t end = bytes.size();
    const auto boundary = [&](std::uint64_t candidate) {
      if (candidate > begin) end = std::min(end, candidate);
    };
    for (auto offset : model.sequence_offsets) boundary(offset);
    for (auto offset : std::array<std::uint64_t, 9>{model.packet_table_offset,
          model.collision_offset, model.skeleton_offset,
          model.common_translation_offset, model.joint_metadata_offset,
          model.gif_usage_offset, model.sound_definitions_offset,
          model.shadow_range.offset, std::uint64_t(model.bangles_offset_qwords)*16U})
      boundary(offset);
    const auto sequence = parse_rac_moby_sequence_v1(
        bytes, {begin, end-begin}, {max_bytes, max_bytes, 255U, 255U});
    RacFrontendObjectAnimationClipV1 clip;
    clip.source_offset = begin;
    clip.header_bits = sequence.opaque_prefix_words;
    clip.rate_override_bits = std::bit_cast<std::uint32_t>(sequence.sequence_phase_rate_override);
    clip.sound_byte = sequence.continuous_sound_id;
    clip.trigger_byte = sequence.trigger_count;
    for (const auto &frame : sequence.frames) {
      clip.frame_references.push_back(static_cast<std::uint32_t>(frame.source_offset));
      clip.frame_rate_bits.push_back(std::bit_cast<std::uint32_t>(frame.phase_rate));
    }
    out.clips.push_back(std::move(clip));
  }
  validate(out);
  return out;
}

RacFrontendObjectAnimationResultV1 set_rac_frontend_object_animation_v1(
    const RacFrontendObjectAnimationStateV1 &state,
    const RacFrontendObjectAnimationBankV1 &bank,
    std::uint32_t sequence, std::int32_t frame) {
  validate(bank);
  const auto &clip = at(bank, sequence);
  if (frame < 0) fail("Frontend object setter requires an owned nonnegative frame");
  std::vector<std::vector<RacMobySequenceFrameBindingV1>> frames(bank.clips.size());
  std::vector<RacMobySequenceBindingV1> bindings;
  std::vector<RacMobyFrameFirstWordBindingV1> words;
  for (std::uint32_t i = 0; i < bank.clips.size(); ++i) {
    const auto &c = bank.clips[i];
    for (std::uint32_t j = 0; j < c.frame_references.size(); ++j) {
      frames[i].push_back({static_cast<std::uint8_t>(j), c.frame_references[j]});
      words.push_back({c.frame_references[j], c.frame_rate_bits[j]});
    }
    bindings.push_back({i, static_cast<std::uint8_t>(c.frame_references.size()),
                        c.sound_byte, c.trigger_byte, frames[i]});
  }
  std::sort(words.begin(), words.end(), [](const auto &a, const auto &b) {
    return a.frame_reference < b.frame_reference;
  });
  for (std::size_t i=1; i<words.size(); ++i)
    if (words[i-1].frame_reference==words[i].frame_reference &&
        words[i-1].first_word_bits!=words[i].first_word_bits)
      fail("Frontend object aliased frame has inconsistent first words");
  words.erase(std::unique(words.begin(), words.end(), [](const auto &a, const auto &b) {
    return a.frame_reference==b.frame_reference;
  }), words.end());
  const auto selected = set_rac_moby_sequence_v1(sequence, std::uint64_t(frame),
      state.flags, {bindings, words}, {255U, 65025U, 65025U});
  RacFrontendObjectAnimationResultV1 out{state, {}};
  out.state.indices=selected.indices;
  out.state.references=selected.references;
  out.state.rate_bits=selected.animation_rate_bits;
  out.state.flags=selected.sequence_flags;
  const auto previous=selected.indices.previous_frame;
  const auto initial_next=static_cast<std::uint8_t>(previous+1U);
  write(out,0x213d54,0x52,1,sequence);
  write(out,0x213d68,0x50,1,previous);
  write(out,0x213d6c,0x51,1,initial_next);
  if (initial_next>=clip.frame_references.size())
    write(out,0x213d88,0x51,1,static_cast<std::uint32_t>(clip.frame_references.size()-1U));
  write(out,0x213d90,0x53,1,sequence);
  write(out,0x20d724,0x68,4,selected.references.previous_frame_reference);
  write(out,0x20d730,0x7e,1,selected.references.trigger_byte);
  write(out,0x20d740,0x7c,1,selected.references.sound_byte);
  write(out,0x20d788,0x6c,4,selected.references.current_frame_reference);
  write(out,0x213dcc,0x70,1,selected.sequence_flags);
  write(out,0x213dd0,0x5c,4,selected.animation_rate_bits);
  return out;
}

RacFrontendObjectAnimationResultV1 step_rac_frontend_object_animation_v1(
    const RacFrontendObjectAnimationStateV1 &state,
    const RacFrontendObjectAnimationBankV1 &bank) {
  validate(bank);
  const auto &clip=at(bank,state.indices.current_sequence);
  if (state.indices.previous_sequence!=state.indices.current_sequence ||
      state.indices.previous_frame>=clip.frame_references.size() ||
      state.indices.current_frame>=clip.frame_references.size() ||
      state.references.previous_frame_reference!=clip.frame_references[state.indices.previous_frame] ||
      state.references.current_frame_reference!=clip.frame_references[state.indices.current_frame] ||
      state.references.sound_byte!=0xffU || state.references.trigger_byte || state.sound_handle!=0xffU ||
      state.rate_bits!=clip.frame_rate_bits[state.indices.previous_frame])
    fail("Frontend object animation state has unresolved or inconsistent owners");
  auto units=phase_units(state.phase_bits);
  int direction=0;
  if (state.speed_bits==one) direction=1;
  else if (state.speed_bits==0xbf800000U) direction=-1;
  else if (state.speed_bits!=0U) fail("Frontend object speed is outside exact unit domain");
  RacFrontendObjectAnimationResultV1 out{state,{}};
  auto &s=out.state;
  s.flags=0;
  if (!state.rate_bits || !direction) {
    write(out,0x20e67c,0x70,1,0);
    return out;
  }
  // ADDA(+0,phase) and MADD(speed,.5) are exact for these operands.
  units+=direction;
  if (units>=2) {
    units-=2;
    s.indices.previous_frame=s.indices.current_frame;
    auto next=std::uint32_t(s.indices.current_frame)+1U;
    s.flags=1;
    if (next>=clip.frame_references.size()) { next=0; s.flags|=2; }
    s.indices.current_frame=static_cast<std::uint8_t>(next);
    s.references.previous_frame_reference=state.references.current_frame_reference;
    s.references.current_frame_reference=clip.frame_references[next];
    // DIV(.5), then MUL(.5), preserves the exact residual 0 or .5.
    s.phase_bits=phase_bits(units);
    write(out,0x20e504,0x52,1,s.indices.previous_sequence);
    write(out,0x20e508,0x50,1,s.indices.previous_frame);
    write(out,0x20e50c,0x51,1,s.indices.current_frame);
    write(out,0x20e510,0x68,4,s.references.previous_frame_reference);
    write(out,0x20e514,0x6c,4,s.references.current_frame_reference);
    write(out,0x20e520,0x5c,4,s.rate_bits);
    write(out,0x20e52c,0x54,4,s.phase_bits);
    write(out,0x20e534,0x70,1,s.flags);
  } else if (units<0) {
    s.indices.current_frame=s.indices.previous_frame;
    auto previous=int(s.indices.previous_frame)-1;
    s.flags=1;
    if (previous<0) { previous=static_cast<int>(clip.frame_references.size())-1; s.flags|=2; }
    s.indices.previous_frame=static_cast<std::uint8_t>(previous);
    s.references.current_frame_reference=state.references.previous_frame_reference;
    s.references.previous_frame_reference=clip.frame_references[previous];
    units+=2;
    s.phase_bits=phase_bits(units);
    write(out,0x20e5ac,0x68,4,s.references.previous_frame_reference);
    write(out,0x20e5b0,0x50,1,s.indices.previous_frame);
    write(out,0x20e5b4,0x51,1,s.indices.current_frame);
    write(out,0x20e5bc,0x6c,4,s.references.current_frame_reference);
    write(out,0x20e5c4,0x5c,4,s.rate_bits);
    write(out,0x20e5cc,0x54,4,s.phase_bits);
    write(out,0x20e5d4,0x70,1,s.flags);
  } else {
    s.phase_bits=phase_bits(units);
    write(out,0x20e47c,0x54,4,s.phase_bits);
    write(out,0x20e484,0x70,1,0);
  }
  if (s.flags&2U) {
    s.phase_bits=direction>0 ? 0U : one;
    s.speed_bits=0U;
    write(out,0x23b5b0,0x54,4,s.phase_bits);
    write(out,0x23b5b4,0x58,4,0);
  }
  return out;
}
} // namespace openrc
