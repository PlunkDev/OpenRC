#include "openrc/rac_frontend_sound_compile.hpp"
#include "openrc/rac_frontend_sound_pcm.hpp"
#include "openrc/rac_frontend_sound_voice.hpp"

#include <functional>
#include <iostream>

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void rejects(const std::function<void()>& call) {
    try { call(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid source sound ownership was accepted");
}

std::vector<std::byte> synthetic_bank() {
    std::vector<std::byte> bytes(0x18U + 0x68U + 0x30U);
    const auto put = [&](std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[at + i] = std::byte((value >> (8U * i)) & 0xffU);
    };
    put(0, 3); put(4, 2); put(8, 0x18); put(12, 0x68); put(16, 0x80); put(20, 0x30);
    put(0x18, 0x6b6c4253); put(0x1c, 1); put(0x20, 4);
    put(0x2c, 0x10000); put(0x30, 0x10001); put(0x34, 0x34); put(0x38, 0x40);
    put(0x3c, 0x5040); put(0x40, 0x30); put(0x44, 0x30);
    put(0x4c, 127); put(0x50, 1); put(0x58, 1); put(0x60, 0x42b65000);
    put(0x68, 0x80ff0000); put(0x6c, 0x9fc0);
    bytes[0x91] = std::byte{1}; bytes[0xa1] = std::byte{7};
    return bytes;
}

openrc::RacFrontendSoundSourceV1 arithmetic_source() {
    openrc::RacFrontendSoundSourceV1 source;
    source.pitch_semitones[10] = 0xe411;
    source.pitch_fine[66] = 0x83de;
    source.stereo_center = {0x2d40, 0x2d40};
    return source;
}

} // namespace

int main() try {
    auto bytes = synthetic_bank();
    const auto owner = openrc::decode_rac_frontend_sound_bank_v1(bytes);
    check(owner.audio.block_count == 1 && owner.audio.frame_count == 3 &&
        owner.bank.descriptor_table_offset == 0x34 && owner.source_allocation_hint == 0x5040,
        "Old SBlk source owner or dynamic-allocation field changed");
    const auto source = arithmetic_source();
    const auto plan = openrc::compile_rac_frontend_sound_voice_plan_v1(source, owner, 0, 1024, true);
    check(plan.phase_numerator == 1880 && plan.phase_denominator == 4096 &&
        plan.output_frames_per_second == 48000, "Original rational pitch was rounded into a sample rate");
    check(plan.group_volume == 819 && plan.channel_volume_registers[0] == 2078 &&
        plan.channel_volume_factors[0] == 4156 && plan.channel_volume_factors[1] == 4156,
        "Original stereo pan/group quadratic volume was replaced by linear gain");
    const auto mono = openrc::compile_rac_frontend_sound_voice_plan_v1(source, owner, 0, 1024, false);
    check(mono.channel_volume_factors[0] > plan.channel_volume_factors[0] &&
        mono.channel_volume_factors[0] == mono.channel_volume_factors[1], "Mono source pan bypass differs");
    const auto muted = openrc::compile_rac_frontend_sound_voice_plan_v1(source, owner, 0, 0, true);
    check(muted.channel_volume_factors[0] == 0 && muted.channel_volume_factors[1] == 0,
        "Muted source group retains an audible voice");
    check(plan.attack_output_ticks == 6 && plan.sustained_envelope == 32767 &&
        plan.envelope_denominator == 32768 && plan.release_output_ticks == 2,
        "Selected envelope was approximated as unity");
    check(plan.source_sample_count_before_end == 56, "Source leading zero frame was trimmed");
    openrc::RacFrontendSoundGainSourceV1 gain_source;
    for(auto& row:gain_source.pan)row={0x2d40,0x2d40};
    const auto rear_gain=openrc::evaluate_rac_frontend_sound_gain_v1(gain_source,{85,85,180,{0,0},819,0});
    check(rear_gain.panned==std::array<std::int16_t,2>{-10377,10377} &&
        rear_gain.channel_factors==std::array<std::int16_t,2>{-2102,2100},
        "Rear stereo phase or signed register rounding changed");
    const auto silent_gain=openrc::evaluate_rac_frontend_sound_gain_v1(gain_source,{0,85,180,{-100,100},819,0});
    check(silent_gain.panned==std::array<std::int16_t,2>{0,0} &&
        silent_gain.channel_factors==std::array<std::int16_t,2>{0,0},"Zero volume retained stale channel polarity");
    const auto full_gain=openrc::evaluate_rac_frontend_sound_gain_v1(gain_source,{127,127,0,{0,0},1024,1});
    check(full_gain.channel_factors==std::array<std::int16_t,2>{32766,32766},
        "Quadratic group gain used the envelope maximum instead of its source divisor");
    const auto interpolation = openrc::make_rac_frontend_sound_interpolation_v1();
    check(interpolation[0] == std::array<std::int16_t,4>{0x12c7,0x59b3,0x1307,-1} &&
        interpolation[255] == std::array<std::int16_t,4>{-1,0x1307,0x59b3,0x12c7},
        "Source interpolation endpoint phases changed");
    const auto silence = openrc::compile_rac_frontend_sound_pcm_v1(owner, plan);
    check(silence.channels == 2 && silence.sample_rate == 48000 && silence.samples.size() == 176,
        "Finite source prefetch/end timing differs");
    for (const auto sample : silence.samples) check(sample == 0, "Silent source grain generated audible samples");
    const auto round_trip = openrc::decode_audio_clip_v1(openrc::encode_audio_clip_v1(silence));
    check(round_trip == silence, "Final sound PCM does not survive its neutral codec");
    auto loop_owner=owner;
    auto& loop_block=loop_owner.audio.blocks[0];
    loop_block.kind=openrc::SBlkAudioBlockKind::looped;
    loop_block.loop_start_frame=1;loop_block.loop_end_frame=2;
    loop_owner.bank.secondary_bytes[17]=std::byte{6};
    loop_owner.bank.secondary_bytes[33]=std::byte{3};
    loop_owner.bank.secondary_bytes[34]=std::byte{0x11};
    const auto loop=openrc::compile_rac_frontend_sound_loop_pcm_v1(loop_owner,0);
    check(loop.repeat_begin==28 && loop.repeat_end==84 && loop.source_loops_before_repeat==0 &&
        loop.source_loops_per_repeat==1 && loop.samples[56]==4096,
        "History-independent source loop was not preserved exactly");
    auto transient_owner=loop_owner;
    transient_owner.bank.secondary_bytes[16]=std::byte{0x10};
    transient_owner.bank.secondary_bytes[46]=std::byte{0x11};
    transient_owner.bank.secondary_bytes[47]=std::byte{0x11};
    const auto transient=openrc::compile_rac_frontend_sound_loop_pcm_v1(transient_owner,0);
    check(transient.repeat_begin==84 && transient.repeat_end==140 &&
        transient.source_loops_before_repeat==1 && transient.source_loops_per_repeat==1 &&
        transient.repeat_entry_history==std::array<std::int32_t,2>{4096,4096} &&
        transient.samples[28]==0 && transient.samples[84]==3840,
        "First predictive loop was collapsed into its different stable recurrence");
    auto invalid_loop=loop_owner;invalid_loop.audio.blocks[0].loop_end_frame=3;
    rejects([&]{(void)openrc::compile_rac_frontend_sound_loop_pcm_v1(invalid_loop,0);});
    rejects([&]{(void)openrc::compile_rac_frontend_sound_loop_pcm_v1(owner,0);});
    rejects([&] { (void)openrc::compile_rac_frontend_sound_voice_plan_v1(source, owner, 5, 1024, true); });
    rejects([&] { (void)openrc::compile_rac_frontend_sound_voice_plan_v1(source, owner, 0, 1025, true); });
    auto wrong_source = source; wrong_source.pitch_fine[66] = 0;
    rejects([&] { (void)openrc::compile_rac_frontend_sound_voice_plan_v1(wrong_source, owner, 0, 1024, true); });
    auto delayed = owner; delayed.audio.items[0].payload[0] = 1;
    rejects([&] { (void)openrc::compile_rac_frontend_sound_voice_plan_v1(source, delayed, 0, 1024, true); });
    auto truncated = bytes; truncated.pop_back();
    rejects([&] { (void)openrc::decode_rac_frontend_sound_bank_v1(truncated); });
    auto overlap = bytes; overlap[16] = std::byte{0x7f};
    rejects([&] { (void)openrc::decode_rac_frontend_sound_bank_v1(overlap); });
    auto tail = bytes; tail.push_back(std::byte{1});
    rejects([&] { (void)openrc::decode_rac_frontend_sound_bank_v1(tail); });
    auto false_start = bytes; false_start[0x82] = std::byte{1};
    rejects([&] { (void)openrc::decode_rac_frontend_sound_bank_v1(false_start); });
    rejects([&] { (void)openrc::make_rac_frontend_sound_source_v1(bytes, bytes, bytes); });
    std::cout << "RAC frontend sound source/ownership/rational pitch/gain tests PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n'; return 1;
}
