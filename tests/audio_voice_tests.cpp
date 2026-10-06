#include "openrc/audio_voice.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace openrc;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F&& call) {
    try { call(); } catch (const AudioVoiceError&) { return; }
    throw std::runtime_error("Invalid voice input was accepted");
}
AudioStreamV1 stream(bool looped = true, unsigned count = 56U) {
    AudioStreamV1 value;
    value.output_sample_rate = 48000; value.phase_denominator = 4;
    value.coefficient_denominator = 1; value.gain_denominator = 16;
    for (auto& row : value.coefficients) row = {1, 0, 0, 0};
    value.input_samples.resize(count);
    for (unsigned i = 0; i < count; ++i) value.input_samples[i] = static_cast<std::int16_t>(32 + i * 16);
    if (looped) { value.repeat_begin = 3; value.repeat_end = count; }
    return value;
}
AudioEnvelopeV1 envelope() {
    AudioEnvelopeV1 value; value.maximum_level = 16; value.counter_period = 1;
    value.stages[0] = {1, 1, UINT32_MAX, 0, 4, 1, 16, false};
    value.stages[1] = {1, 1, UINT32_MAX, 0, 0, 1, 16, true};
    value.stages[2] = {1, 1, UINT32_MAX, 0, 0, 1, 0, false};
    value.stages[3] = {1, 1, UINT32_MAX, 0, -4, 1, 0, true};
    return value;
}
void held_and_release() {
    AudioVoicePlayerV1 voice(stream(), envelope());
    const AudioVoiceControlV1 held{0, {16, -16}};
    std::array<std::int16_t, 8> output{};
    check(voice.render(output, std::span{&held, 1U}).frames_written == 4, "Held voice failed to emit frames");
    check(output == std::array<std::int16_t, 8>{8,-8,16,-16,24,-24,32,-32},
        "Zero pitch failed to advance envelope or preserve signed channel gain");
    check(voice.state().stream.input_cursor == 0, "Zero pitch consumed input");
    voice.release();
    check(voice.state().envelope.level == 16 && voice.state().envelope.counter == 0,
        "Release reset the reached envelope level");
    const AudioVoiceControlV1 moving{4, {16, -16}};
    output.fill(1234);
    const auto result = voice.render(output, std::span{&moving, 1U});
    check(result.frames_written == 4 && result.stop_reason == AudioVoiceStopReasonV1::envelope_stopped,
        "Release failed to stop at the actual envelope end");
    check(output == std::array<std::int16_t,8>{24,-24,24,-24,16,-16,0,0},
        "Release PCM differs or omitted its final zero frame");
    check(voice.state().stream.input_cursor == 4 && voice.state().stream.rendered_frames == 8 &&
        voice.state().envelope.rendered_frames == 8, "Final envelope frame did not advance stream phase");
    const auto retired = voice.state(); output.fill(2345);
    voice.release(); voice.stop();
    check(voice.render(output, std::span{&moving,1U}).frames_written == 0 && voice.state() == retired &&
        std::ranges::all_of(output, [](auto x){return x == 2345;}), "Stopped voice continued or rewrote its tail");
}
void finite_and_refill() {
    const AudioVoiceControlV1 moving{4,{16,16}};
    AudioVoicePlayerV1 simple(stream(false,8), envelope());
    std::array<std::int16_t,20> output; output.fill(99);
    const auto simple_result = simple.render(output, std::span{&moving,1U});
    check(simple_result.frames_written == 5 && simple_result.stop_reason == AudioVoiceStopReasonV1::input_exhausted &&
        output[10] == 99 && simple.state().envelope.rendered_frames == 5, "Finite interpolation EOF advanced another envelope frame");
    for (const auto phase : {2U,16U}) {
        AudioVoicePlayerV1 voice(stream(false), envelope(), AudioReadAheadV1{56,4,12,4});
        const AudioVoiceControlV1 control{phase,{16,16}};
        std::array<std::int16_t,200> pcm; pcm.fill(-123);
        const auto result = voice.render(pcm,std::span{&control,1U});
        const unsigned expected = phase == 2 ? 80 : 13;
        check(result.frames_written == expected && result.stop_reason == AudioVoiceStopReasonV1::input_exhausted &&
            voice.state().envelope.rendered_frames == expected && pcm[expected*2] == -123,
            "Read-ahead EOF lost variable-pitch timing or advanced the envelope after EOF");
    }
    AudioVoicePlayerV1 empty(stream(false,4),envelope(),AudioReadAheadV1{4,4,12,4});
    check(empty.render(output,std::span{&moving,1U}).frames_written == 0 &&
        empty.state().envelope.rendered_frames == 0, "Initial final refill generated a spurious envelope frame");
}
void partition_and_reject() {
    AudioVoicePlayerV1 one(stream(),envelope()), chunks(stream(),envelope());
    std::array<AudioVoiceControlV1,200> controls;
    for (unsigned i=0;i<controls.size();++i)
        controls[i] = {i%11U,{static_cast<int>(i%17U),-static_cast<int>((i*3U)%17U)}};
    std::array<std::int16_t,400> full{}, divided{};
    check(one.render(full,controls).frames_written == 200, "Whole voice render stopped unexpectedly");
    unsigned offset=0;
    for (const auto count : {1U,13U,49U,2U,135U}) {
        check(chunks.render(std::span{divided}.subspan(offset*2,count*2),
            std::span{controls}.subspan(offset,count)).frames_written == count, "Chunked voice stopped unexpectedly");
        offset += count;
    }
    check(full == divided && one.state() == chunks.state(), "Voice chunk boundaries changed PCM or control clocks");
    const auto before=chunks.state(); divided.fill(333);
    auto invalid=controls; invalid.back().channel_gains[1]=17;
    rejects([&]{(void)chunks.render(divided,invalid);});
    check(chunks.state()==before && std::ranges::all_of(divided,[](auto x){return x==333;}),
        "Invalid late control partially changed a voice");
    rejects([&]{(void)chunks.render(std::span{divided}.first(3),std::span{controls}.first(1));});
    AudioVoicePlayerV1 finite(stream(false),envelope(),AudioReadAheadV1{56,4,12,4});
    invalid=controls; invalid.back().phase_increment=17;
    const auto finite_before=finite.state();
    rejects([&]{(void)finite.render(divided,invalid);});
    check(finite.state()==finite_before, "Invalid read-ahead movement was not rejected atomically");
    // A valid refill policy can expose fewer buffered samples than its refill
    // quantum. Reject excessive movement before writing even its first frame.
    AudioVoicePlayerV1 sparse(stream(false,80),envelope(),AudioReadAheadV1{80,16,3,4});
    const std::array<AudioVoiceControlV1,3> sparse_controls{{{44,{16,16}},{64,{16,16}},{0,{16,16}}}};
    const auto sparse_before=sparse.state();
    rejects([&]{(void)sparse.render(std::span{divided}.first(6),sparse_controls);});
    check(sparse.state()==sparse_before && divided[0]==333,
        "Control skipped beyond the available read-ahead window after partial rendering");
    chunks.stop(); const auto stopped=chunks.state();
    check(stopped.stop_reason==AudioVoiceStopReasonV1::forced &&
        chunks.render(divided,controls).frames_written==0 && chunks.state()==stopped,
        "Hard stop emitted more PCM or changed its clock");
    auto over=envelope(); over.maximum_level=17;
    rejects([&]{AudioVoicePlayerV1 bad(stream(),over);});
    rejects([&]{AudioVoicePlayerV1 bad(stream(),envelope(),AudioReadAheadV1{56,4,12,4});});
    rejects([&]{AudioVoicePlayerV1 bad(stream(false),envelope(),AudioReadAheadV1{52,4,12,4});});
    rejects([&]{AudioVoicePlayerV1 bad(stream(false),envelope(),AudioReadAheadV1{56,4,1,4});});
}
}
int main() try {
    held_and_release(); finite_and_refill(); partition_and_reject();
    std::cout << "Audio voice hold/release/final-frame/finite-refill/chunking/atomic-rejection PASS\n";
    return 0;
} catch(const std::exception& error) {std::cerr << "FAIL " << error.what() << '\n';return 1;}
