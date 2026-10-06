#include "windows_pcm_stream.hpp"
#include "windows_audio_device.hpp"
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <iostream>

namespace openrc::runtime {
namespace {
void check_wave(MMRESULT result,const char* operation) {
    if(result!=MMSYSERR_NOERROR)
        throw std::runtime_error(std::string(operation)+" failed (waveOut "+std::to_string(result)+")");
}
void check_event(BOOL result,const char* operation) {
    if(!result)throw std::runtime_error(std::string(operation)+" failed (Windows "+std::to_string(GetLastError())+")");
}
struct EventOwner {
    HANDLE value=nullptr;
    ~EventOwner(){if(value)CloseHandle(value);}
};
}
struct WindowsPcmStreamV1::Implementation {
    struct Buffer {
        std::vector<std::int16_t> samples;
        WAVEHDR header{};
        std::uint64_t token=0;
        std::uint32_t frames=0;
        bool prepared=false,queued=false,natural_before_reset=false;
    };
    const DWORD thread=GetCurrentThreadId();
    const WindowsPcmStreamLimitsV1 limits;
    EventOwner event;
    HWAVEOUT device=nullptr;
    std::array<Buffer,32> buffers;
    std::array<WindowsPcmBufferCompletionV1,32> completions;
    std::size_t completion_count=0;
    WindowsPcmStreamStatsV1 totals;
    bool reset_complete=false;
    std::uint32_t clock_type=0,clock_previous=0;
    std::uint64_t clock_wrap=0,clock_frames=0;

    Implementation(std::uint32_t channels,WindowsPcmStreamLimitsV1 selected):limits(selected) {
        if((channels!=1&&channels!=2)||!limits.buffer_count||limits.buffer_count>buffers.size()||
            !limits.max_buffer_frames||limits.max_buffer_frames>sample_rate)
            throw std::invalid_argument("PCM stream requires mono/stereo and 1..32 buffers of 1..48000 frames");
        totals.channels=channels;totals.buffer_count=limits.buffer_count;
        for(std::uint32_t i=0;i<limits.buffer_count;++i)
            buffers[i].samples.resize(std::size_t(limits.max_buffer_frames)*channels);
        totals.owned_bytes=std::uint64_t(limits.buffer_count)*limits.max_buffer_frames*channels*sizeof(std::int16_t);
        event.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!event.value)throw std::runtime_error("Cannot create PCM buffer completion event");
        WAVEFORMATEX format{};
        format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=static_cast<WORD>(channels);
        format.nSamplesPerSec=sample_rate;format.wBitsPerSample=16;
        format.nBlockAlign=static_cast<WORD>(channels*2U);format.nAvgBytesPerSec=sample_rate*format.nBlockAlign;
        const auto output=select_windows_audio_output_v1();
        if(output.explicit_selection)std::clog<<"Streaming PCM output index="<<output.index<<" name="
            <<windows_audio_output_name_utf8_v1(output.name)<<'\n';
        check_wave(waveOutOpen(&device,output.index,&format,reinterpret_cast<DWORD_PTR>(event.value),0,CALLBACK_EVENT),
                   "Open streaming PCM device");
    }
    void require_thread() const {
        if(GetCurrentThreadId()!=thread)throw std::logic_error("PCM stream used outside its owning thread");
    }
    std::uint32_t held() const {
        return static_cast<std::uint32_t>(std::count_if(buffers.begin(),buffers.end(),[](const auto& b){return b.prepared;}));
    }
    std::uint64_t clock() {
        if(reset_complete||!device)return clock_frames;
        MMTIME time{};time.wType=TIME_SAMPLES;
        check_wave(waveOutGetPosition(device,&time,sizeof(time)),"Read streaming PCM clock");
        std::uint32_t current=0;
        if(time.wType==TIME_SAMPLES)current=time.u.sample;
        else if(time.wType==TIME_BYTES)current=time.u.cb;
        else if(time.wType==TIME_MS)current=time.u.ms;
        else throw std::runtime_error("Streaming PCM device returned an unsupported clock type");
        if(clock_type&&clock_type!=time.wType)
            throw std::runtime_error("Streaming PCM device changed its clock units");
        clock_type=time.wType;
        // Buffers are bounded well below a DWORD wrap; poll samples this
        // counter before returning credits needed to submit more audio.
        if(current<clock_previous)clock_wrap+=std::uint64_t{1}<<32U;
        clock_previous=current;
        const auto extended=clock_wrap+current;
        const auto frames=clock_type==TIME_SAMPLES?extended:
            clock_type==TIME_BYTES?extended/(2U*totals.channels):extended*sample_rate/1000U;
        clock_frames=std::min(frames,totals.submitted_frames);
        return clock_frames;
    }
    void return_buffer(Buffer& buffer,bool natural) {
        check_wave(waveOutUnprepareHeader(device,&buffer.header,sizeof(buffer.header)),"Unprepare streaming PCM buffer");
        buffer.prepared=false;
        if(buffer.queued) {
            completions[completion_count++]={buffer.token,buffer.frames,natural};
            if(natural) {
                ++totals.naturally_completed_buffers;totals.naturally_completed_frames+=buffer.frames;
            } else {
                ++totals.cancelled_buffers;totals.cancelled_frames+=buffer.frames;
            }
        }
        buffer.queued=false;buffer.token=0;buffer.frames=0;buffer.header={};
    }
    std::vector<WindowsPcmBufferCompletionV1> poll(bool stop) {
        require_thread();
        if(stop)totals.stopping=true;
        std::vector<WindowsPcmBufferCompletionV1> result;
        result.reserve(limits.buffer_count); // Allocate before changing ownership.
        if(device) {
            if(stop&&!reset_complete) {
                const auto position=clock();
                for(auto& b:buffers)if(b.queued)b.natural_before_reset=(b.header.dwFlags&WHDR_DONE)!=0;
                check_wave(waveOutReset(device),"Stop streaming PCM device");
                clock_frames=position;reset_complete=true;
            } else if(!reset_complete) {
                clock();
                // Reset before inspection, so a concurrent driver completion
                // during/after the scan remains observable to the next wait.
                check_event(ResetEvent(event.value),"Reset streaming PCM completion event");
            }
            for(auto& b:buffers)if(b.prepared&&
                (reset_complete||(b.queued&&(b.header.dwFlags&WHDR_DONE))))
                return_buffer(b,reset_complete?b.natural_before_reset:true);
            if(stop) {
                check_wave(waveOutClose(device),"Close streaming PCM device");
                device=nullptr;
            }
        }
        // Event retirement is independent of device retirement: if closing
        // the event fails after waveOutClose succeeds, the next stop retries
        // this remaining owner without reopening or closing the device twice.
        if(stop&&!device) {
            if(event.value) {
                check_event(CloseHandle(event.value),"Close streaming PCM completion event");
                event.value=nullptr;
            }
            for(auto& b:buffers)std::vector<std::int16_t>{}.swap(b.samples);
            totals.owned_bytes=0;totals.retired=true;
        }
        std::sort(completions.begin(),completions.begin()+completion_count,
            [](const auto& a,const auto& b){return a.token<b.token;});
        for(std::size_t i=0;i<completion_count;++i)result.push_back(completions[i]);
        completion_count=0;
        return result;
    }
};

WindowsPcmStreamV1::WindowsPcmStreamV1(std::uint32_t channels,WindowsPcmStreamLimitsV1 limits)
    :implementation_(std::make_unique<Implementation>(channels,limits)) {}
WindowsPcmStreamV1::~WindowsPcmStreamV1() {
    try{(void)implementation_->poll(true);}catch(...) {
        // A failed reset/unprepare/close cannot justify freeing memory still
        // borrowed by the driver. Keep that bounded native owner alive until
        // process teardown; explicit retirement reports and can retry errors.
        (void)implementation_.release();
    }
}
std::optional<std::uint64_t> WindowsPcmStreamV1::submit(std::span<const std::int16_t> pcm) {
    auto& s=*implementation_;s.require_thread();
    if(s.totals.stopping||!s.device)throw std::logic_error("Streaming PCM output was stopped or retired");
    if(pcm.empty()||pcm.size()%s.totals.channels||pcm.size()/s.totals.channels>s.limits.max_buffer_frames)
        throw std::invalid_argument("Streaming PCM submission is empty, unaligned or exceeds one buffer");
    if(s.held()+s.completion_count>=s.limits.buffer_count)return std::nullopt;
    const auto frames=static_cast<std::uint32_t>(pcm.size()/s.totals.channels);
    if(s.totals.submitted_buffers==std::numeric_limits<std::uint64_t>::max()||
        frames>std::numeric_limits<std::uint64_t>::max()-s.totals.submitted_frames)
        throw std::overflow_error("Streaming PCM submission counter exhausted");
    auto& b=*std::find_if(s.buffers.begin(),s.buffers.begin()+s.limits.buffer_count,[](const auto& v){return !v.prepared;});
    std::copy(pcm.begin(),pcm.end(),b.samples.begin());b.header={};
    b.header.lpData=reinterpret_cast<LPSTR>(b.samples.data());
    b.header.dwBufferLength=static_cast<DWORD>(pcm.size()*sizeof(std::int16_t));
    try {
        check_wave(waveOutPrepareHeader(s.device,&b.header,sizeof(b.header)),"Prepare streaming PCM buffer");
        b.prepared=true;
        check_wave(waveOutWrite(s.device,&b.header,sizeof(b.header)),"Submit streaming PCM buffer");
    } catch(...) {s.totals.stopping=true;throw;}
    b.queued=true;b.frames=frames;b.token=++s.totals.submitted_buffers;
    s.totals.submitted_frames+=frames;
    return b.token;
}
std::vector<WindowsPcmBufferCompletionV1> WindowsPcmStreamV1::poll(){return implementation_->poll(false);}
std::vector<WindowsPcmBufferCompletionV1> WindowsPcmStreamV1::stop_and_retire(){return implementation_->poll(true);}
HANDLE WindowsPcmStreamV1::completion_event() const {
    implementation_->require_thread();return implementation_->event.value;
}
std::uint64_t WindowsPcmStreamV1::played_samples() const {
    implementation_->require_thread();return implementation_->clock();
}
WindowsPcmStreamStatsV1 WindowsPcmStreamV1::stats() const {
    const auto& s=*implementation_;s.require_thread();auto result=s.totals;
    result.held_buffers=s.held();result.pending_completions=static_cast<std::uint32_t>(s.completion_count);
    result.queued_buffers=static_cast<std::uint32_t>(std::count_if(s.buffers.begin(),s.buffers.end(),[](const auto& b){return b.queued;}));
    return result;
}
bool WindowsPcmStreamV1::drained() const {implementation_->require_thread();return !implementation_->held();}
bool WindowsPcmStreamV1::retired() const {implementation_->require_thread();return implementation_->totals.retired;}
}
