#include "windows_media_audio.hpp"
#include "windows_audio_device.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <string>
#include <iostream>

namespace openrc::runtime {
namespace {
void check(MMRESULT result,const char *operation) {
  if(result!=MMSYSERR_NOERROR)
    throw MediaClipError(std::string(operation)+" failed (waveOut "+std::to_string(result)+")");
}
}
struct WindowsMediaAudioV1::Implementation {
  std::span<const std::int16_t> samples;
  std::uint32_t sample_rate=0,channels=0;
  HWAVEOUT device=nullptr;
  WAVEHDR header{};
  bool prepared=false, started=false;
  bool stop_requested=false, reset_complete=false, naturally_completed=false;
  std::uint64_t stopped_samples=0;
  Implementation(std::span<const std::int16_t> source,std::uint32_t rate,std::uint32_t channel_count)
      :samples(source),sample_rate(rate),channels(channel_count) {
    if(samples.empty()) throw MediaClipError("PCM playback requires audio samples");
    WAVEFORMATEX format{};
    format.wFormatTag=WAVE_FORMAT_PCM;
    format.nChannels=static_cast<WORD>(channels);
    format.nSamplesPerSec=sample_rate;
    format.wBitsPerSample=16;
    format.nBlockAlign=static_cast<WORD>(format.nChannels*2U);
    format.nAvgBytesPerSec=format.nSamplesPerSec*format.nBlockAlign;
    const auto output=select_windows_audio_output_v1();
    if(output.explicit_selection)std::clog<<"PCM audio output index="<<output.index<<" name="
      <<windows_audio_output_name_utf8_v1(output.name)<<'\n';
    check(waveOutOpen(&device,output.index,&format,0,0,CALLBACK_NULL),"Open PCM audio");
    header.lpData=reinterpret_cast<LPSTR>(const_cast<std::int16_t*>(samples.data()));
    header.dwBufferLength=static_cast<DWORD>(samples.size()*2U);
    const auto result=waveOutPrepareHeader(device,&header,sizeof(header));
    if(result!=MMSYSERR_NOERROR) { waveOutClose(device);device=nullptr;check(result,"Prepare PCM audio"); }
    prepared=true;
  }
  ~Implementation() {
    if(device) {
      if(!reset_complete) waveOutReset(device);
      if(prepared) waveOutUnprepareHeader(device,&header,sizeof(header));
      waveOutClose(device);
    }
  }
};
WindowsMediaAudioV1::WindowsMediaAudioV1(const MediaClipV1 &clip) {
  validate_media_clip_v1(clip);
  implementation_=std::make_unique<Implementation>(clip.audio,clip.audio_sample_rate,clip.audio_channels);
}
WindowsMediaAudioV1::WindowsMediaAudioV1(const AudioClipV1 &clip) {
  validate_audio_clip_v1(clip);
  implementation_=std::make_unique<Implementation>(clip.samples,clip.sample_rate,clip.channels);
}
WindowsMediaAudioV1::~WindowsMediaAudioV1()=default;
void WindowsMediaAudioV1::start() {
  auto &s=*implementation_;
  if(s.stop_requested||!s.device) throw MediaClipError("PCM playback was retired or stopping");
  if(s.started) throw MediaClipError("PCM playback was already started");
  check(waveOutWrite(s.device,&s.header,sizeof(s.header)),"Start PCM playback");
  s.started=true;
}
std::uint64_t WindowsMediaAudioV1::played_samples() const {
  const auto &s=*implementation_;
  if(s.reset_complete) return s.stopped_samples;
  if(!s.started) return 0;
  MMTIME time{};time.wType=TIME_SAMPLES;
  check(waveOutGetPosition(s.device,&time,sizeof(time)),"Read PCM playback clock");
  std::uint64_t samples=0;
  if(time.wType==TIME_SAMPLES) samples=time.u.sample;
  else if(time.wType==TIME_BYTES) samples=time.u.cb/(2U*s.channels);
  else if(time.wType==TIME_MS) samples=static_cast<std::uint64_t>(time.u.ms)*s.sample_rate/1000U;
  else throw MediaClipError("PCM device returned an unsupported playback clock");
  return std::min(samples,s.samples.size()/s.channels);
}
bool WindowsMediaAudioV1::finished() const {
  const auto &s=*implementation_;
  return s.started && (s.reset_complete ? s.naturally_completed : (s.header.dwFlags&WHDR_DONE)!=0);
}
void WindowsMediaAudioV1::stop_and_retire() {
  auto &s=*implementation_;
  if(!s.device) return;
  s.stop_requested=true;
  if(!s.reset_complete) {
    // waveOutReset zeroes the device clock and marks even interrupted buffers
    // done. Preserve both observations before the real reset.
    const auto samples=played_samples();
    const auto completed=finished();
    check(waveOutReset(s.device),"Stop PCM playback");
    s.stopped_samples=samples;
    s.naturally_completed=completed;
    s.reset_complete=true;
  }
  if(s.prepared) {
    check(waveOutUnprepareHeader(s.device,&s.header,sizeof(s.header)),"Retire PCM buffer");
    s.prepared=false;
    s.header.lpData=nullptr;
    s.header.dwBufferLength=0;
  }
  check(waveOutClose(s.device),"Retire PCM device");
  s.device=nullptr;
}
bool WindowsMediaAudioV1::retired() const noexcept {return implementation_->device==nullptr;}
}
