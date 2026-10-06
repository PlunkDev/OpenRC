#include "openrc/screen_overlay.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <array>
#include <bit>

namespace openrc {
namespace {
constexpr std::array<std::byte,8> magic{std::byte{'O'},std::byte{'R'},std::byte{'S'},std::byte{'C'},
    std::byte{'R'},std::byte{'O'},std::byte{'V'},std::byte{'1'}};
[[noreturn]] void fail(const char* message) { throw ScreenOverlayError(message); }
void put(std::vector<std::byte>& bytes,std::uint64_t value,unsigned count=4) {
  for(unsigned i=0;i<count;++i) {bytes.push_back(static_cast<std::byte>(value&255U));value>>=8;}
}
struct Reader {
  std::span<const std::byte> bytes;std::size_t at=0;
  std::span<const std::byte> take(std::size_t count) {
    if(count>bytes.size()-at) fail("Truncated screen overlay");
    auto result=bytes.subspan(at,count);at+=count;return result;
  }
  std::uint64_t get(unsigned count=4) {
    auto data=take(count);std::uint64_t result=0;
    for(unsigned i=0;i<count;++i) result|=std::uint64_t(std::to_integer<unsigned>(data[i]))<<(i*8);
    return result;
  }
  std::uint32_t word() {return static_cast<std::uint32_t>(get());}
};
void clock(const ScreenOverlayV1& overlay) {
  if(!overlay.updates_per_second||overlay.updates_per_second>1000||overlay.frames.empty()||
      (overlay.loop_begin!=UINT32_MAX&&overlay.loop_begin>=overlay.frames.size()))
    fail("Invalid screen overlay clock");
}
}
void validate_screen_overlay_v1(const ScreenOverlayV1& overlay,ScreenOverlayLimitsV1 limits) {
  clock(overlay);
  if(!overlay.canvas_width||!overlay.canvas_height||overlay.canvas_width>limits.max_dimension||
      overlay.canvas_height>limits.max_dimension||!overlay.coverage_denominator||overlay.coverage_denominator>255||
      overlay.images.size()>limits.max_images||overlay.frames.size()>limits.max_frames)
    fail("Screen overlay geometry or counts exceed limits");
  std::uint64_t bytes=96U,draws=0U;
  for(const auto& image:overlay.images) {
    if(!image.width||!image.height||image.width>limits.max_dimension||image.height>limits.max_dimension||
        image.rgb_coverage.size()!=std::uint64_t(image.width)*image.height*4U)
      fail("Invalid screen overlay image dimensions");
    bytes+=8U+image.rgb_coverage.size();
    if(bytes>limits.max_bytes) fail("Screen overlay exceeds byte limit");
    for(std::size_t i=3;i<image.rgb_coverage.size();i+=4)
      if(std::to_integer<unsigned>(image.rgb_coverage[i])>overlay.coverage_denominator)
        fail("Screen overlay coverage exceeds its denominator");
  }
  for(const auto& frame:overlay.frames) {
    draws+=frame.draws.size();bytes+=4U+12U*frame.draws.size();
    if(draws>limits.max_draws||bytes>limits.max_bytes) fail("Screen overlay draw or byte limit exceeded");
    for(const auto& draw:frame.draws)
      if(draw.image_id>=overlay.images.size()) fail("Screen overlay draw references an absent image");
  }
}
std::vector<std::byte> encode_screen_overlay_v1(const ScreenOverlayV1& overlay,ScreenOverlayLimitsV1 limits) {
  validate_screen_overlay_v1(overlay,limits);std::vector<std::byte> body;
  for(auto value:{overlay.canvas_width,overlay.canvas_height,overlay.updates_per_second,
      overlay.coverage_denominator,overlay.loop_begin,0U,0U,0U}) put(body,value);
  for(const auto& image:overlay.images) {
    put(body,image.width);put(body,image.height);
    body.insert(body.end(),image.rgb_coverage.begin(),image.rgb_coverage.end());
  }
  for(const auto& frame:overlay.frames) {
    put(body,frame.draws.size());
    for(const auto& draw:frame.draws) {
      put(body,draw.image_id);put(body,std::bit_cast<std::uint32_t>(draw.x));put(body,std::bit_cast<std::uint32_t>(draw.y));
    }
  }
  std::vector<std::byte> out(magic.begin(),magic.end());put(out,1);put(out,64);put(out,64U+body.size(),8);
  put(out,overlay.images.size());put(out,overlay.frames.size());
  const auto digest=prepared_content_sha256_v1(body);out.insert(out.end(),digest.begin(),digest.end());
  out.insert(out.end(),body.begin(),body.end());return out;
}
ScreenOverlayV1 decode_screen_overlay_v1(std::span<const std::byte> bytes,ScreenOverlayLimitsV1 limits) {
  if(bytes.size()<96U||bytes.size()>limits.max_bytes) fail("Screen overlay byte envelope exceeds limits");
  Reader in{bytes};const auto signature=in.take(8);
  if(!std::equal(signature.begin(),signature.end(),magic.begin())||in.get()!=1||in.get()!=64||in.get(8)!=bytes.size())
    fail("Invalid screen overlay header");
  const auto image_count=in.word(),frame_count=in.word();
  if(image_count>limits.max_images||!frame_count||frame_count>limits.max_frames||
      std::uint64_t(image_count)*12U+std::uint64_t(frame_count)*4U>bytes.size()-96U)
    fail("Screen overlay counts exceed limits or available records");
  const auto stored=in.take(32);const auto digest=prepared_content_sha256_v1(bytes.subspan(64));
  if(!std::equal(stored.begin(),stored.end(),digest.begin())) fail("Screen overlay digest mismatch");
  ScreenOverlayV1 overlay;
  overlay.canvas_width=in.word();overlay.canvas_height=in.word();overlay.updates_per_second=in.word();
  overlay.coverage_denominator=in.word();overlay.loop_begin=in.word();
  if(in.get()!=0||in.get()!=0||in.get()!=0) fail("Unknown screen overlay flags");
  for(std::uint32_t i=0;i<image_count;++i) {
    ScreenOverlayImageV1 image;image.width=in.word();image.height=in.word();
    if(!image.width||!image.height||image.width>limits.max_dimension||image.height>limits.max_dimension)
      fail("Screen overlay image dimensions exceed limits");
    const auto count=std::uint64_t(image.width)*image.height*4U;
    if(count>bytes.size()-in.at) fail("Screen overlay image exceeds payload");
    const auto pixels=in.take(static_cast<std::size_t>(count));image.rgb_coverage.assign(pixels.begin(),pixels.end());
    overlay.images.push_back(std::move(image));
  }
  std::uint64_t draw_count=0;
  for(std::uint32_t i=0;i<frame_count;++i) {
    ScreenOverlayFrameV1 frame;const auto count=in.word();draw_count+=count;
    if(draw_count>limits.max_draws||std::uint64_t(count)*12U>bytes.size()-in.at)
      fail("Screen overlay draws exceed bounds");
    frame.draws.reserve(count);
    for(std::uint32_t j=0;j<count;++j)
      frame.draws.push_back({in.word(),std::bit_cast<std::int32_t>(in.word()),std::bit_cast<std::int32_t>(in.word())});
    overlay.frames.push_back(std::move(frame));
  }
  if(in.at!=bytes.size()) fail("Trailing screen overlay bytes");
  validate_screen_overlay_v1(overlay,limits);return overlay;
}
std::uint32_t screen_overlay_frame_index_v1(const ScreenOverlayV1& overlay,std::uint64_t update) {
  clock(overlay);
  if(update<overlay.frames.size()) return static_cast<std::uint32_t>(update);
  if(overlay.loop_begin==UINT32_MAX) return static_cast<std::uint32_t>(overlay.frames.size()-1U);
  return overlay.loop_begin+static_cast<std::uint32_t>((update-overlay.loop_begin)%(overlay.frames.size()-overlay.loop_begin));
}
void composite_screen_overlay_frame_v1(std::span<std::byte> canvas,const ScreenOverlayV1& overlay,std::uint32_t frame_index,
    ScreenOverlayLimitsV1 limits) {
  validate_screen_overlay_v1(overlay,limits);
  if(canvas.size()!=std::uint64_t(overlay.canvas_width)*overlay.canvas_height*4U||frame_index>=overlay.frames.size())
    fail("Screen overlay canvas or frame mismatch");
  const auto denominator=static_cast<int>(overlay.coverage_denominator);
  for(const auto& draw:overlay.frames[frame_index].draws) {
    const auto& image=overlay.images[draw.image_id];
    const auto x0=std::max<std::int64_t>(0,draw.x),y0=std::max<std::int64_t>(0,draw.y);
    const auto x1=std::min<std::int64_t>(overlay.canvas_width,std::int64_t(draw.x)+image.width);
    const auto y1=std::min<std::int64_t>(overlay.canvas_height,std::int64_t(draw.y)+image.height);
    for(auto y=y0;y<y1;++y) for(auto x=x0;x<x1;++x) {
      const auto src=static_cast<std::size_t>(((y-draw.y)*image.width+x-draw.x)*4);
      const auto dst=static_cast<std::size_t>((y*overlay.canvas_width+x)*4);
      const auto coverage=std::to_integer<int>(image.rgb_coverage[src+3U]);
      for(std::size_t c=0;c<3;++c) {
        const auto destination=std::to_integer<int>(canvas[dst+c]);
        const auto delta=(std::to_integer<int>(image.rgb_coverage[src+c])-destination)*coverage;
        const auto quotient=delta>=0?delta/denominator:-((-delta+denominator-1)/denominator);
        canvas[dst+c]=static_cast<std::byte>(destination+quotient);
      }
      canvas[dst+3U]=std::byte{255};
    }
  }
}
} // namespace openrc
