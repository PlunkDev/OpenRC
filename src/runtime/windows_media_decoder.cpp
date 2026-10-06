#include "windows_media_decoder.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace openrc::runtime {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr, const char *operation) {
  if (FAILED(hr)) {
    std::ostringstream out;
    out << operation << " failed (0x" << std::hex << static_cast<std::uint32_t>(hr) << ')';
    throw MediaClipError(out.str());
  }
}
std::int64_t clock100ns(std::int64_t ticks) { return ticks * 1000 / 9; }
std::byte clamp_byte(int value) { return static_cast<std::byte>(std::clamp(value,0,255)); }
} // namespace

struct WindowsMediaDecoderV1::Implementation {
  const MediaClipV1 &clip;
  ComPtr<IMFTransform> decoder;
  std::size_t packet_index = 0;
  bool com_initialized = false, mf_initialized = false, draining = false, finished = false;
  bool input_stopped = false;
  std::uint32_t width = 0, height = 0, stride = 0;
  std::int64_t last_time = -1;

  explicit Implementation(const MediaClipV1 &source) : clip(source) {
    validate_media_clip_v1(clip);
    auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) com_initialized=true;
    else if (hr!=RPC_E_CHANGED_MODE) check(hr,"Initialize media COM");
    try {
      check(MFStartup(MF_VERSION,MFSTARTUP_LITE),"Start Media Foundation");
      mf_initialized=true;
      MFT_REGISTER_TYPE_INFO requested{MFMediaType_Video,MFVideoFormat_MPEG2};
      IMFActivate **activations=nullptr;
      UINT32 activation_count=0;
      check(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                      MFT_ENUM_FLAG_SYNCMFT|MFT_ENUM_FLAG_SORTANDFILTER,
                      &requested,nullptr,&activations,&activation_count),"Find MPEG-2 decoder");
      HRESULT activation_result=MF_E_TOPO_CODEC_NOT_FOUND;
      for (UINT32 i=0;i<activation_count;++i) {
        if (!decoder)
          activation_result=activations[i]->ActivateObject(IID_PPV_ARGS(decoder.GetAddressOf()));
        activations[i]->Release();
      }
      CoTaskMemFree(activations);
      check(activation_result,"Activate MPEG-2 Media Foundation decoder");
      ComPtr<IMFMediaType> input;
      check(MFCreateMediaType(input.GetAddressOf()),"Create video input type");
      check(input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"Set video major type");
      check(input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_MPEG2),"Set MPEG-2 input");
      check(MFSetAttributeSize(input.Get(),MF_MT_FRAME_SIZE,clip.width,clip.height),"Set video dimensions");
      check(MFSetAttributeRatio(input.Get(),MF_MT_FRAME_RATE,clip.frame_rate_numerator,
                                clip.frame_rate_denominator),"Set video cadence");
      check(decoder->SetInputType(0,input.Get(),0),"Set MPEG decoder input");
      set_output_type();
      check(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0),"Begin MPEG stream");
      check(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0),"Start MPEG stream");
    } catch (...) { cleanup(); throw; }
  }
  void cleanup() noexcept {
    decoder.Reset();
    if (mf_initialized) { MFShutdown(); mf_initialized=false; }
    if (com_initialized) { CoUninitialize(); com_initialized=false; }
  }
  ~Implementation() { cleanup(); }

  void set_output_type() {
    for (DWORD index=0;;++index) {
      ComPtr<IMFMediaType> type;
      auto hr=decoder->GetOutputAvailableType(0,index,type.GetAddressOf());
      if (hr==MF_E_NO_MORE_TYPES) throw MediaClipError("MPEG decoder has no NV12 output");
      check(hr,"Enumerate MPEG output types");
      GUID subtype{};
      check(type->GetGUID(MF_MT_SUBTYPE,&subtype),"Read MPEG output subtype");
      if (subtype!=MFVideoFormat_NV12) continue;
      check(decoder->SetOutputType(0,type.Get(),0),"Set NV12 output");
      check(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&width,&height),"Read video output dimensions");
      if (width!=clip.width || height!=clip.height)
        throw MediaClipError("MPEG decoded dimensions disagree with prepared clip");
      UINT32 raw_stride=width;
      if (FAILED(type->GetUINT32(MF_MT_DEFAULT_STRIDE,&raw_stride))) raw_stride=width;
      if (raw_stride<width || raw_stride>16384U) throw MediaClipError("Unsupported MPEG video stride");
      stride=raw_stride;
      return;
    }
  }

  MediaVideoFrameV1 convert(IMFSample *sample) {
    MediaVideoFrameV1 frame;
    frame.width=width; frame.height=height;
    check(sample->GetSampleTime(&frame.presentation_time_100ns),"Read decoded frame timestamp");
    if (FAILED(sample->GetSampleDuration(&frame.duration_100ns)))
      frame.duration_100ns=INT64_C(10000000)*clip.frame_rate_denominator/clip.frame_rate_numerator;
    if (frame.presentation_time_100ns<last_time) throw MediaClipError("MPEG decoder output time regressed");
    last_time=frame.presentation_time_100ns;
    ComPtr<IMFMediaBuffer> buffer;
    check(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()),"Read decoded video buffer");
    BYTE *data=nullptr; DWORD length=0;
    check(buffer->Lock(&data,nullptr,&length),"Lock decoded video buffer");
    try {
      const auto expected=static_cast<std::uint64_t>(stride)*height*3U/2U;
      if (length<expected) throw MediaClipError("Truncated NV12 decoder output");
      frame.rgba.resize(static_cast<std::size_t>(width)*height*4U);
      for (std::uint32_t y=0;y<height;++y) for(std::uint32_t x=0;x<width;++x) {
        const int yy=int(data[y*stride+x])-16;
        auto uv=static_cast<std::size_t>(stride)*height+(y/2U)*stride+(x&~1U);
        const int u=int(data[uv])-128, v=int(data[uv+1])-128;
        auto dst=(static_cast<std::size_t>(y)*width+x)*4U;
        // Limited-range BT.601, the source sequence's BT.470BG matrix.
        frame.rgba[dst]=clamp_byte((298*yy+409*v+128)>>8);
        frame.rgba[dst+1]=clamp_byte((298*yy-100*u-208*v+128)>>8);
        frame.rgba[dst+2]=clamp_byte((298*yy+516*u+128)>>8);
        frame.rgba[dst+3]=std::byte{255};
      }
    } catch (...) { buffer->Unlock(); throw; }
    check(buffer->Unlock(),"Unlock decoded video buffer");
    return frame;
  }

  std::optional<MediaVideoFrameV1> next() {
    if (finished) return std::nullopt;
    // Every iteration consumes input, produces a frame, begins draining or
    // changes the output format. A broken transform cannot spin forever.
    unsigned changes=0;
    for (;;) {
      MFT_OUTPUT_STREAM_INFO info{};
      check(decoder->GetOutputStreamInfo(0,&info),"Read decoder output allocation");
      ComPtr<IMFSample> supplied;
      if (!(info.dwFlags&MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
        check(MFCreateSample(supplied.GetAddressOf()),"Allocate output sample");
        ComPtr<IMFMediaBuffer> buffer;
        check(MFCreateAlignedMemoryBuffer(std::max<DWORD>(info.cbSize,stride*height*3U/2U),
                                           info.cbAlignment ? info.cbAlignment-1U : 0U,
                                           buffer.GetAddressOf()),"Allocate output buffer");
        check(supplied->AddBuffer(buffer.Get()),"Attach output buffer");
      }
      MFT_OUTPUT_DATA_BUFFER output{0,supplied.Get(),0,nullptr};
      DWORD status=0;
      auto hr=decoder->ProcessOutput(0,1,&output,&status);
      ComPtr<IMFSample> produced;
      if (output.pSample && output.pSample!=supplied.Get()) produced.Attach(output.pSample);
      if (output.pEvents) output.pEvents->Release();
      if (hr==MF_E_TRANSFORM_STREAM_CHANGE) {
        if (++changes>4) throw MediaClipError("MPEG decoder repeatedly changed output format");
        set_output_type(); continue;
      }
      if (SUCCEEDED(hr)) {
        if (!output.pSample) throw MediaClipError("MPEG decoder returned no frame");
        return convert(output.pSample);
      }
      if (hr!=MF_E_TRANSFORM_NEED_MORE_INPUT) check(hr,"Decode MPEG frame");
      if (draining) { finished=true; return std::nullopt; }
      if (input_stopped||packet_index==clip.video.size()) {
        check(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM,0),"End MPEG input");
        check(decoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN,0),"Drain final MPEG pictures");
        draining=true; continue;
      }
      const auto &packet=clip.video[packet_index];
      ComPtr<IMFSample> input;
      ComPtr<IMFMediaBuffer> buffer;
      check(MFCreateSample(input.GetAddressOf()),"Allocate input sample");
      check(MFCreateMemoryBuffer(static_cast<DWORD>(packet.bytes.size()),buffer.GetAddressOf()),"Allocate input buffer");
      BYTE *data=nullptr;
      check(buffer->Lock(&data,nullptr,nullptr),"Lock input packet");
      std::memcpy(data,packet.bytes.data(),packet.bytes.size());
      check(buffer->Unlock(),"Unlock input packet");
      check(buffer->SetCurrentLength(static_cast<DWORD>(packet.bytes.size())),"Set input packet size");
      check(input->AddBuffer(buffer.Get()),"Attach input packet");
      if (packet.presentation_time>=0)
        check(input->SetSampleTime(clock100ns(packet.presentation_time)),"Set presentation timestamp");
      if (packet.decode_time>=0)
        check(input->SetUINT64(MFSampleExtension_DecodeTimestamp,clock100ns(packet.decode_time)),"Set decode timestamp");
      check(decoder->ProcessInput(0,input.Get(),0),"Submit MPEG input");
      ++packet_index;
    }
  }
};
WindowsMediaDecoderV1::WindowsMediaDecoderV1(const MediaClipV1 &clip)
    : implementation_(std::make_unique<Implementation>(clip)) {}
WindowsMediaDecoderV1::~WindowsMediaDecoderV1() = default;
std::optional<MediaVideoFrameV1> WindowsMediaDecoderV1::next_frame() { return implementation_->next(); }
bool WindowsMediaDecoderV1::input_available() const noexcept {
  return !implementation_->input_stopped&&implementation_->packet_index<implementation_->clip.video.size();
}
bool WindowsMediaDecoderV1::drained() const noexcept {return implementation_->finished;}
void WindowsMediaDecoderV1::stop_input_and_drain() noexcept {implementation_->input_stopped=true;}
} // namespace openrc::runtime
