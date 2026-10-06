#include "windows_audio_bank.hpp"
#include "windows_media_audio.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace openrc::runtime {
struct WindowsAudioBankV1::Implementation {
    struct Voice {
        std::uint64_t token=0;
        std::uint32_t owner=0;
        std::size_t clip_index=0;
        WindowsAudioVoiceStateV1 state=WindowsAudioVoiceStateV1::queued;
        unsigned reserved_events=0;
        std::unique_ptr<WindowsMediaAudioV1> audio;
    };
    // Voices are destroyed before their borrowed immutable clip storage.
    std::vector<WindowsAudioBankClipV1> clips;
    std::array<Voice,voice_limit> slots;
    std::array<WindowsAudioVoiceEventV1,event_limit> events;
    std::size_t event_count=0,reserved_events=0;
    WindowsAudioBankStatsV1 totals;
    bool stopping_all=false;

    explicit Implementation(std::vector<WindowsAudioBankClipV1> values,std::uint64_t limit)
        :clips(std::move(values)) {
        if(clips.empty())throw std::invalid_argument("PCM bank requires finite clips");
        std::sort(clips.begin(),clips.end(),[](const auto& a,const auto& b){return a.key<b.key;});
        for(std::size_t i=0;i<clips.size();++i) {
            validate_audio_clip_v1(clips[i].clip);
            if(i&&clips[i-1].key==clips[i].key)
                throw std::invalid_argument("PCM bank contains duplicate clip keys");
            const auto bytes=std::uint64_t(clips[i].clip.samples.size())*sizeof(std::int16_t);
            if(bytes>limit-totals.pcm_bytes)
                throw std::length_error("PCM bank exceeds its aggregate storage limit");
            totals.pcm_bytes+=bytes;
        }
        totals.loaded=true;totals.clips=clips.size();
    }
    void emit(Voice& voice,WindowsAudioVoiceEventKindV1 kind,std::uint64_t samples=0) noexcept {
        // Every admitted voice reserves both start and terminal entries. Queue
        // backpressure guarantees space even when shutdown must report errors.
        events[event_count++]={voice.token,clips[voice.clip_index].key,voice.owner,kind,samples};
        --voice.reserved_events;--reserved_events;
    }
    void discard_unused_reservation(Voice& voice) noexcept {
        reserved_events-=voice.reserved_events;voice.reserved_events=0;
    }
    void retire(Voice& voice) {
        voice.state=WindowsAudioVoiceStateV1::retiring;
        voice.audio->stop_and_retire();
        if(!voice.audio->retired())throw std::logic_error("PCM device did not acknowledge retirement");
        const auto natural=voice.audio->finished();
        emit(voice,natural?WindowsAudioVoiceEventKindV1::natural_completion:
                          WindowsAudioVoiceEventKindV1::stopped,voice.audio->played_samples());
        if(natural)++totals.naturally_completed;else ++totals.stopped;
        discard_unused_reservation(voice);
        voice=Voice{};
    }
    std::array<Voice*,voice_limit> ordered_voices() {
        std::array<Voice*,voice_limit> result{};
        std::size_t count=0;
        for(auto& voice:slots)if(voice.token)result[count++]=&voice;
        std::sort(result.begin(),result.begin()+count,[](const auto* a,const auto* b){return a->token<b->token;});
        return result;
    }
    void stop_all_and_wait() {
        stopping_all=true;
        std::exception_ptr first_error;
        for(auto* voice:ordered_voices()) {
            if(!voice)break;
            if(!voice->audio) {
                emit(*voice,WindowsAudioVoiceEventKindV1::cancelled);
                ++totals.cancelled;discard_unused_reservation(*voice);*voice=Voice{};
                continue;
            }
            try{retire(*voice);}catch(...){if(!first_error)first_error=std::current_exception();}
        }
        if(first_error)std::rethrow_exception(first_error);
        stopping_all=false;
    }
};

WindowsAudioBankV1::WindowsAudioBankV1(std::vector<WindowsAudioBankClipV1> clips,std::uint64_t limit)
    :implementation_(std::make_unique<Implementation>(std::move(clips),limit)) {}
WindowsAudioBankV1::~WindowsAudioBankV1() {
    try{implementation_->stop_all_and_wait();}catch(...){}
    // Remaining audio destructors retry their own best-effort device cleanup
    // before Implementation destroys the PCM storage. Explicit stop reports
    // failures to callers; the destructor cannot claim successful retirement.
}
std::uint64_t WindowsAudioBankV1::queue(std::uint32_t key,std::uint32_t owner) {
    auto& s=*implementation_;
    if(!s.totals.loaded||s.stopping_all)
        throw std::logic_error("PCM bank is unloaded or still stopping");
    const auto clip=std::lower_bound(s.clips.begin(),s.clips.end(),key,
        [](const auto& value,std::uint32_t selected){return value.key<selected;});
    if(clip==s.clips.end()||clip->key!=key)throw std::out_of_range("PCM bank clip key is absent");
    const auto slot=std::find_if(s.slots.begin(),s.slots.end(),[](const auto& voice){return !voice.token;});
    if(slot==s.slots.end())throw std::length_error("PCM bank has no available voice slot");
    if(s.event_count+s.reserved_events+2U>event_limit)
        throw std::length_error("PCM bank event acknowledgments must be consumed");
    if(s.totals.accepted==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("PCM bank voice token exhausted");
    slot->clip_index=static_cast<std::size_t>(clip-s.clips.begin());
    slot->token=++s.totals.accepted;slot->owner=owner;slot->reserved_events=2;
    s.reserved_events+=2;
    return slot->token;
}
void WindowsAudioBankV1::pump() {
    auto& s=*implementation_;
    if(!s.totals.loaded)throw std::logic_error("Cannot pump an unloaded PCM bank");
    for(auto* voice:s.ordered_voices()) {
        if(!voice)break;
        if(voice->state==WindowsAudioVoiceStateV1::retiring||
            (voice->state==WindowsAudioVoiceStateV1::active&&voice->audio->finished()))s.retire(*voice);
    }
    if(s.stopping_all)return;
    for(auto* voice:s.ordered_voices()) {
        if(!voice)break;
        if(voice->state==WindowsAudioVoiceStateV1::active)continue;
        if(!voice->audio) {
            voice->audio=std::make_unique<WindowsMediaAudioV1>(s.clips[voice->clip_index].clip);
            voice->state=WindowsAudioVoiceStateV1::prepared;
        }
        voice->audio->start();
        voice->state=WindowsAudioVoiceStateV1::active;
        s.emit(*voice,WindowsAudioVoiceEventKindV1::started);
        ++s.totals.started;
    }
}
void WindowsAudioBankV1::stop_all_and_wait() {implementation_->stop_all_and_wait();}
void WindowsAudioBankV1::unload() {
    auto& s=*implementation_;
    if(std::ranges::any_of(s.slots,[](const auto& voice){return voice.token!=0;}))
        throw std::logic_error("Cannot unload PCM storage with pending or opened voices");
    std::vector<WindowsAudioBankClipV1>{}.swap(s.clips);
    s.totals.loaded=false;s.totals.clips=0;s.totals.pcm_bytes=0;
    s.stopping_all=false;
}
WindowsAudioBankStatsV1 WindowsAudioBankV1::stats() const noexcept {
    const auto& s=*implementation_;
    auto result=s.totals;result.pending_events=s.event_count;result.stopping=s.stopping_all;
    for(const auto& voice:s.slots)if(voice.token) {
        switch(voice.state) {
        case WindowsAudioVoiceStateV1::queued:++result.queued;break;
        case WindowsAudioVoiceStateV1::prepared:++result.prepared;break;
        case WindowsAudioVoiceStateV1::active:++result.active;break;
        case WindowsAudioVoiceStateV1::retiring:++result.retiring;break;
        }
    }
    return result;
}
std::vector<WindowsAudioVoiceV1> WindowsAudioBankV1::voices() const {
    const auto& s=*implementation_;
    std::vector<WindowsAudioVoiceV1> result;
    for(const auto& voice:s.slots)if(voice.token)
        result.push_back({voice.token,s.clips[voice.clip_index].key,voice.owner,voice.state,
                          voice.audio?voice.audio->played_samples():0});
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.token<b.token;});
    return result;
}
WindowsAudioVoiceV1 WindowsAudioBankV1::voice(std::uint64_t token) const {
    const auto& s=*implementation_;
    if(token)for(const auto& voice:s.slots)if(voice.token==token)
        return {voice.token,s.clips[voice.clip_index].key,voice.owner,voice.state,
                voice.audio?voice.audio->played_samples():0};
    throw std::out_of_range("PCM voice token is missing or stale");
}
std::vector<WindowsAudioVoiceEventV1> WindowsAudioBankV1::take_events() {
    auto& s=*implementation_;
    std::vector<WindowsAudioVoiceEventV1> result(s.events.begin(),s.events.begin()+s.event_count);
    s.event_count=0;
    return result;
}
}
