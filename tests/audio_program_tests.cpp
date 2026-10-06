#include "openrc/audio_program.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <functional>
#include <iostream>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void rejects(const std::function<void()>& call) {
    try { call(); } catch (const openrc::AudioProgramError&) { return; }
    throw std::runtime_error("Invalid audio program was accepted");
}
openrc::AudioProgramBankV1 bank() {
    openrc::AudioProgramBankV1 result;
    result.ticks_per_second = 240; result.modulation_tick_divisor = 2;
    result.random = {{1, 7, 23, 49}, 0, 2};
    return result;
}
void owned_wait_and_retirement() {
    using namespace openrc;
    auto input=bank();AudioProgramV1 graph;graph.key=101;graph.voice_count=2;
    graph.nodes={{0,1,AudioProgramVoiceV1{0}},{0,2,AudioProgramWaitOwnedV1{}},
        {0,3,AudioProgramVoiceV1{1}},{0,audio_program_end_v1,AudioProgramReleaseV1{}}};
    input.programs.push_back(graph);
    const auto bytes=encode_audio_program_bank_v1(input);
    check(encode_audio_program_bank_v1(decode_audio_program_bank_v1(bytes))==bytes,"Wait-owned codec changed its graph");
    AudioProgramLimitsV1 limits;limits.max_instances=1;
    AudioProgramSchedulerV1 player(decode_audio_program_bank_v1(bytes),limits);
    const auto token=player.admit(0);const auto initial=player.instance_state(token);
    check(initial.owned_count==1 && initial.node==1 && initial.delay_ticks==1 && player.take_events().size()==1,
        "Same-tick voice admission did not block wait-owned");
    rejects([&]{player.complete_owned(token,0);});rejects([&]{player.complete_owned(token,2);});
    rejects([&]{player.retire(token);});rejects([&]{(void)player.admit(0);});
    check(player.instance_state(token)==initial,"Invalid logical completion/retirement changed owner state");
    player.advance_tick();check(player.instance_state(token)==initial,"Owned wait did not retry exactly one tick");
    player.complete_owned(token);check(player.instance_state(token).owned_count==0 && player.take_events().empty(),
        "Logical completion invented a graph tick or event");
    player.advance_tick();const auto events=player.take_events();
    check(events.size()==2 && events[0].tick==2 && events[0].voice_index==1 &&
        events[1].kind==AudioProgramEventKindV1::release_owned && player.instance_state(token).owned_count==0 &&
        player.instance_state(token).node==audio_program_end_v1,"Wait did not continue/release in its first unowned tick");
    rejects([&]{player.complete_owned(token);});player.retire(token);
    rejects([&]{(void)player.instance_state(token);});rejects([&]{player.complete_owned(token);});
    const auto next=player.admit(0);check(next>token,"Retirement reused an old completion token");
    rejects([&]{player.complete_owned(token);});

    input.programs[0].nodes={{0,1,AudioProgramVoiceV1{0}},{0,2,AudioProgramReleaseV1{}},
        {0,3,AudioProgramWaitOwnedV1{}},{0,audio_program_end_v1,AudioProgramVoiceV1{1}}};
    AudioProgramSchedulerV1 detached(input);const auto id=detached.admit(0);
    check(detached.take_events().size()==3 && detached.instance_state(id).owned_count==1 &&
        detached.instance_state(id).node==audio_program_end_v1,"Released tails incorrectly blocked same-tick wait");

    input.programs[0].nodes={{1,audio_program_end_v1,AudioProgramVoiceV1{0}}};
    AudioProgramSchedulerV1 order(input);const auto a=order.admit(0),b=order.admit(0),c=order.admit(0);
    order.stop_instance(b);order.retire(b);const auto d=order.admit(0);
    order.advance_tick();const auto ordered=order.take_events();
    check(ordered.size()==3 && ordered[0].instance==a && ordered[1].instance==c && ordered[2].instance==d,
        "Retirement reordered live admissions or recycled an instance token");
}
void stop_entries_and_rollback() {
    using namespace openrc;
    auto input=bank();AudioProgramV1 graph;graph.key=102;graph.voice_count=2;
    AudioProgramOscillatorV1 oscillator;oscillator.scalar=5;oscillator.phase_denominator=1;
    oscillator.phase_increment=1;oscillator.values={-3,7,-11};
    graph.nodes={{0,1,oscillator},{0,2,AudioProgramVoiceV1{0}},{0,3,AudioProgramStopSectionV1{}},
        {0,4,AudioProgramReleaseV1{}},{0,audio_program_end_v1,AudioProgramVoiceV1{1}}};
    input.programs.push_back(graph);
    const auto bytes=encode_audio_program_bank_v1(input);
    check(encode_audio_program_bank_v1(decode_audio_program_bank_v1(bytes))==bytes,"Stop-section codec changed its graph");
    AudioProgramSchedulerV1 player(decode_audio_program_bank_v1(bytes));const auto id=player.admit(0);
    check(player.take_events().size()==1 && player.instance_state(id).node==audio_program_end_v1 &&
        player.instance_state(id).owned_count==1,"Normal flow executed the explicit stop tail");
    player.stop_instance(id);const auto tail=player.take_events();
    check(tail.size()==2 && tail[0].kind==AudioProgramEventKindV1::release_owned && tail[1].voice_index==1 &&
        player.instance_state(id).owned_count==1 && player.instance_state(id).stop_requested,
        "Explicit stop did not execute its prepared release/new-voice tail in order");
    const auto once=player.instance_state(id);player.stop_instance(id);
    check(player.instance_state(id)==once && player.take_events().empty(),"Repeated stop restarted its tail");
    player.advance_tick();player.advance_tick();
    check(player.instance_state(id).scalars[5]==7,"Stop prematurely disabled modulation of retained tail voices");
    player.complete_owned(id);player.advance_tick();player.advance_tick();
    check(player.instance_state(id).scalars[5]==7,"Completed graph modulated after losing its final logical voice");
    player.retire(id);

    graph.nodes={{0,audio_program_end_v1,AudioProgramVoiceV1{0}}};input.programs[0]=graph;
    AudioProgramSchedulerV1 explicit_only(input);const auto plain=explicit_only.admit(0);(void)explicit_only.take_events();
    explicit_only.stop_instance(plain);
    check(explicit_only.instance_state(plain).owned_count==1 && explicit_only.take_events().empty(),
        "Unprepared stop invented a release/completion event");
    rejects([&]{explicit_only.retire(plain);});explicit_only.complete_owned(plain);explicit_only.retire(plain);

    graph.nodes={{0,1,AudioProgramVoiceV1{0}},{0,2,AudioProgramStopSectionV1{}},
        {2,audio_program_end_v1,AudioProgramReleaseV1{}}};input.programs[0]=graph;
    AudioProgramSchedulerV1 delayed(input);const auto delay_id=delayed.admit(0);(void)delayed.take_events();
    delayed.stop_instance(delay_id);delayed.advance_tick();delayed.stop_instance(delay_id);
    check(delayed.take_events().empty() && delayed.instance_state(delay_id).delay_ticks==1,
        "Repeated stop reset explicit neutral stop-entry delay");
    delayed.advance_tick();check(delayed.take_events().size()==1 && delayed.instance_state(delay_id).owned_count==0,
        "Prepared stop-entry delay did not release at its reached tick");

    graph.nodes={{0,1,AudioProgramVoiceV1{0}},{0,2,AudioProgramStopSectionV1{}},
        {0,3,AudioProgramRandomSetV1{2,16,1,1,0,1,1}},
        {0,4,AudioProgramReleaseV1{}},{0,audio_program_end_v1,AudioProgramVoiceV1{1}}};input.programs[0]=graph;
    AudioProgramLimitsV1 limits;limits.max_events=2;AudioProgramSchedulerV1 bounded(input,limits);
    const auto bounded_id=bounded.admit(0),draws=bounded.random_draws();
    const auto before=bounded.instance_state(bounded_id);const auto random=bounded.random_state();
    rejects([&]{bounded.stop_instance(bounded_id);});
    check(bounded.instance_state(bounded_id)==before && bounded.random_draws()==draws &&
        bounded.random_state().words==random.words && bounded.random_state().index==random.index &&
        bounded.take_events().size()==1,"Failed stop did not roll back count/latch/random/events");
    bounded.stop_instance(bounded_id);check(bounded.take_events().size()==2 && bounded.random_draws()==draws+1,
        "A failed stop poisoned the subsequent valid transaction");
    input.programs[0].nodes.push_back({0,audio_program_end_v1,AudioProgramStopSectionV1{}});
    rejects([&]{validate_audio_program_bank_v1(input);});
}
void idle_clock_admission() {
    using namespace openrc;
    auto input=bank();AudioProgramV1 graph;graph.key=103;graph.voice_count=1;
    AudioProgramOscillatorV1 oscillator;oscillator.scalar=5;oscillator.phase_denominator=1;
    oscillator.phase_increment=1;oscillator.values={-3,7,-11};
    graph.nodes={{0,1,oscillator},{0,audio_program_end_v1,AudioProgramVoiceV1{0}}};
    input.programs.push_back(graph);
    for(const auto tick:{4097U,4098U}) {
        AudioProgramSchedulerV1 player(input);const auto random=player.random_state();
        player.advance_idle_to(tick);player.advance_idle_to(tick);
        check(player.tick()==tick && player.random_draws()==0 && player.random_state().words==random.words &&
            player.random_state().index==random.index,"Idle clock advance changed random ownership");
        rejects([&]{player.advance_idle_to(tick-1U);});
        const auto id=player.admit(0);const auto event=player.take_events();
        check(event.size()==1 && event[0].tick==tick && event[0].scalars[5]==-3,
            "Idle advance reset admission tick or initial modulation phase");
        rejects([&]{player.advance_idle_to(tick+100U);});
        player.advance_tick();
        check(player.instance_state(id).scalars[5]==(tick%2U?7:-3),"Idle clock lost retained modulation parity");
        if(tick%2U==0) {player.advance_tick();check(player.instance_state(id).scalars[5]==7,"Even admission delayed wrong LFO edge");}
        player.complete_owned(id);player.retire(id);
        player.advance_idle_to(UINT64_C(1)<<40U);
        check(player.tick()==(UINT64_C(1)<<40U) && player.random_draws()==0,"Long empty clock advance changed RNG");
        player.stop();rejects([&]{player.advance_idle_to(UINT64_C(1)<<40U);});
    }
    input.programs[0].nodes={{0,1,AudioProgramVoiceV1{0}},{0,audio_program_end_v1,AudioProgramReleaseV1{}}};
    AudioProgramSchedulerV1 pending(input);const auto id=pending.admit(0);pending.retire(id);
    rejects([&]{pending.advance_idle_to(5);});
    check(pending.tick()==0 && pending.take_events().size()==2,"Idle advance discarded pending retired-owner events");
    pending.advance_idle_to(5);check(pending.tick()==5,"Drained empty owner did not admit idle advancement");
}
} // namespace
int main() try {
    using namespace openrc;
    owned_wait_and_retirement();stop_entries_and_rollback();idle_clock_admission();
    auto input = bank();
    AudioProgramV1 program;
    program.key = 17; program.voice_count = 2;
    program.nodes = {
        {0, 1, AudioProgramRandomSetV1{0, 6, 1, 1, 10, 1, 1}},
        {0, 2, AudioProgramVoiceV1{0}},
        {3, 3, AudioProgramRandomWaitV1{5}},
        {2, 4, AudioProgramVoiceV1{1}},
        {0, audio_program_end_v1, AudioProgramReleaseV1{}}
    };
    input.programs.push_back(program);
    const auto encoded = encode_audio_program_bank_v1(input);
    const auto decoded = decode_audio_program_bank_v1(encoded);
    check(encode_audio_program_bank_v1(decoded) == encoded, "Audio program codec is not canonical");
    AudioProgramSchedulerV1 player(decoded);
    const auto id = player.admit(0);
    const auto first = player.take_events();
    check(first.size() == 1 && first[0].tick == 0 && first[0].scalars[0] == 14 && player.random_draws() == 1,
        "Admission did not execute the explicit shared random owner");
    for (unsigned i = 0; i < 4; ++i) player.advance_tick();
    check(player.take_events().empty(), "Node pre-delay or random post-delay was skipped");
    player.advance_tick();
    const auto done = player.take_events();
    check(done.size() == 2 && done[0].tick == 5 && done[0].voice_index == 1 &&
        done[1].kind == AudioProgramEventKindV1::release_owned &&
        player.instance_state(id).node == audio_program_end_v1 && player.random_draws() == 2,
        "Zero random wait did not compose with next node pre-delay");

    auto oscillating = bank();
    AudioProgramOscillatorV1 oscillator;
    oscillator.scalar = 5; oscillator.phase_denominator = 8; oscillator.phase_increment = 11;
    oscillator.values = {-3, 7, -11};
    program.nodes = {{0, 1, oscillator}, {0, audio_program_end_v1, AudioProgramVoiceV1{0}}};
    oscillating.programs.push_back(program);
    AudioProgramSchedulerV1 waves(oscillating);
    const auto wave = waves.admit(0);
    check(waves.take_events().at(0).scalars[5] == -3, "Oscillator did not sample before initial advance");
    waves.advance_tick();
    check(waves.instance_state(wave).scalars[5] == -3, "Oscillator ran between divided ticks");
    waves.advance_tick();
    check(waves.instance_state(wave).scalars[5] == 7, "Oscillator discarded fractional phase");
    waves.advance_tick(); waves.advance_tick();
    check(waves.instance_state(wave).scalars[5] == -11, "Oscillator phase did not cross a table entry");
    waves.stop(); rejects([&] { waves.advance_tick(); }); rejects([&] { (void)waves.admit(0); });

    auto shared = bank(); shared.random = {{0, 0}, 0, 1};
    program.nodes = {
        {0, audio_program_end_v1, AudioProgramRandomBranchV1{{1, 2}, true, 0, true}},
        {0, audio_program_end_v1, AudioProgramVoiceV1{0}},
        {0, audio_program_end_v1, AudioProgramVoiceV1{1}}
    };
    shared.programs.push_back(program);
    AudioProgramSchedulerV1 choices(decode_audio_program_bank_v1(encode_audio_program_bank_v1(shared)));
    (void)choices.admit(0); (void)choices.admit(0);
    const auto selected = choices.take_events();
    check(selected.size() == 2 && selected[0].voice_index == 1 && selected[1].voice_index == 0,
        "Shared previous-choice owner was incorrectly duplicated per instance");

    auto bounded = bank();
    program.nodes = {{0, 0, AudioProgramVoiceV1{0}}}; bounded.programs.push_back(program);
    AudioProgramLimitsV1 limits; limits.max_events = 3;
    AudioProgramSchedulerV1 full(bounded, limits);
    rejects([&] { (void)full.admit(0); });
    check(full.take_events().empty() && full.random_draws() == 0 && full.tick() == 0,
        "Rejected admission left a partial event transaction");
    bounded.programs[0].nodes = {{1, 1, AudioProgramVoiceV1{0}}, {0, 1, AudioProgramVoiceV1{0}}};
    AudioProgramSchedulerV1 overflow(bounded, limits);
    const auto pending = overflow.admit(0);
    rejects([&] { overflow.advance_tick(); });
    check(overflow.tick() == 0 && overflow.instance_state(pending).delay_ticks == 1 && overflow.take_events().empty(),
        "Rejected tick did not roll back the pending graph and events");

    auto invalid = input; invalid.programs[0].nodes[0].next = 999;
    rejects([&] { validate_audio_program_bank_v1(invalid); });
    invalid = input; invalid.random.forward_tap = 0;
    rejects([&] { validate_audio_program_bank_v1(invalid); });
    invalid = input; invalid.programs[0].nodes[0].action = AudioProgramRandomSetV1{0, 0};
    rejects([&] { validate_audio_program_bank_v1(invalid); });
    invalid = input; invalid.programs[0].nodes[0].action = AudioProgramRandomBranchV1{{1}, true, 0};
    rejects([&] { validate_audio_program_bank_v1(invalid); });
    for (std::size_t length = 0; length < encoded.size(); ++length)
        rejects([&] { (void)decode_audio_program_bank_v1(std::span(encoded).first(length)); });
    auto corrupt = encoded; corrupt.back() ^= std::byte{1};
    rejects([&] { (void)decode_audio_program_bank_v1(corrupt); });
    const auto rewrite_word = [](std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) bytes[at + i] = std::byte((value >> (8U * i)) & 255U);
        const auto digest = prepared_content_sha256_v1(std::span(bytes).subspan(64));
        std::copy(digest.begin(), digest.end(), bytes.begin() + 32);
    };
    corrupt = encoded; rewrite_word(corrupt, 72, UINT32_MAX);
    rejects([&] { (void)decode_audio_program_bank_v1(corrupt); });
    corrupt = encoded; rewrite_word(corrupt, 84, UINT32_MAX);
    rejects([&] { (void)decode_audio_program_bank_v1(corrupt); });
    corrupt = encoded; rewrite_word(corrupt, 152, 99);
    rejects([&] { (void)decode_audio_program_bank_v1(corrupt); });
    for(const auto kind:{10U,11U}) {
        corrupt=encoded;rewrite_word(corrupt,152,kind);
        rejects([&]{(void)decode_audio_program_bank_v1(corrupt);});
    }
    corrupt = encoded; rewrite_word(corrupt, 164, UINT32_MAX);
    rejects([&] { (void)decode_audio_program_bank_v1(corrupt); });
    std::cout << "Audio program clocks/shared random/phase/logical ownership/wait/stop tails/retirement/atomic bounds PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n'; return 1;
}
