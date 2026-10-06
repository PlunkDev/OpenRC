#include "openrc/loading_presentation.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <set>

namespace openrc {
namespace {
constexpr std::array<std::byte,8> magic{std::byte{'O'},std::byte{'R'},std::byte{'L'},std::byte{'O'},
    std::byte{'A'},std::byte{'D'},std::byte{'0'},std::byte{'1'}};
[[noreturn]] void fail(const char *s){throw ScreenOverlayError(s);}
void put(std::vector<std::byte>& b,std::uint64_t v,unsigned n=4){for(unsigned i=0;i<n;++i){b.push_back(static_cast<std::byte>(v&255));v>>=8;}}
struct Reader {
  std::span<const std::byte>b;std::size_t at=0;
  std::span<const std::byte>take(std::size_t n){if(n>b.size()-at)fail("Truncated loading presentation");auto r=b.subspan(at,n);at+=n;return r;}
  std::uint64_t get(unsigned n=4){auto s=take(n);std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(std::to_integer<unsigned>(s[i]))<<(8*i);return v;}
  std::uint32_t word(){return static_cast<std::uint32_t>(get());}
};
}
void validate_loading_presentation_v1(const LoadingPresentationV1 &p,LoadingPresentationLimitsV1 limits) {
  validate_screen_overlay_v1(p.library,limits.overlay);
  if(!limits.max_bands||p.bands.empty()||p.bands.size()>limits.max_bands||p.library.loop_begin!=0||
      !p.fade_in_updates||!p.fade_out_updates||p.fade_in_updates>65536||p.fade_out_updates>65536)
    fail("Invalid loading presentation clock or bands");
  std::set<std::uint32_t> backgrounds;
  for(const auto &f:p.library.frames) {
    if(f.draws.empty())fail("Loading presentation has an empty background frame");
    for(const auto &d:f.draws)backgrounds.insert(d.image_id);
  }
  for(const auto &band:p.bands) {
    if(band.label_image>=p.library.images.size()||backgrounds.contains(band.label_image)||
        band.reveal_update>1048576U||band.reveal_ramp_end>1048576U||
        ((band.reveal_ramp_origin||band.reveal_ramp_end)&&
          (band.reveal_ramp_end<=band.reveal_ramp_origin||band.reveal_ramp_end-band.reveal_ramp_origin>65536U||
           band.reveal_update<band.reveal_ramp_origin||band.reveal_update>=band.reveal_ramp_end)))
      fail("Invalid loading presentation label or reveal ramp");
    for(const auto &f:p.library.frames)for(const auto &d:f.draws) {
      const auto x=std::int64_t(d.x)+band.x,y=std::int64_t(d.y)+band.y;
      if(x<INT32_MIN||x>INT32_MAX||y<INT32_MIN||y>INT32_MAX)fail("Loading presentation translated draw overflows");
    }
  }
}
std::vector<std::byte> encode_loading_presentation_v1(const LoadingPresentationV1 &p,LoadingPresentationLimitsV1 limits) {
  validate_loading_presentation_v1(p,limits);const auto overlay=encode_screen_overlay_v1(p.library,limits.overlay);
  const auto total=80U+24U*p.bands.size()+overlay.size();
  if(total>limits.overlay.max_bytes)fail("Loading presentation exceeds byte limit");
  std::vector<std::byte> body;put(body,p.fade_in_updates);put(body,p.fade_out_updates);put(body,overlay.size(),8);
  for(const auto &band:p.bands)for(auto v:{band.label_image,std::bit_cast<std::uint32_t>(band.x),std::bit_cast<std::uint32_t>(band.y),
      band.reveal_update,band.reveal_ramp_origin,band.reveal_ramp_end})put(body,v);
  body.insert(body.end(),overlay.begin(),overlay.end());std::vector<std::byte> out(magic.begin(),magic.end());
  put(out,1);put(out,64);put(out,total,8);put(out,p.bands.size());put(out,0);
  const auto hash=prepared_content_sha256_v1(body);out.insert(out.end(),hash.begin(),hash.end());out.insert(out.end(),body.begin(),body.end());return out;
}
LoadingPresentationV1 decode_loading_presentation_v1(std::span<const std::byte> bytes,LoadingPresentationLimitsV1 limits) {
  if(bytes.size()<200U||bytes.size()>limits.overlay.max_bytes)fail("Loading presentation envelope exceeds bounds");
  Reader in{bytes};const auto sig=in.take(8);
  if(!std::equal(sig.begin(),sig.end(),magic.begin())||in.get()!=1||in.get()!=64||in.get(8)!=bytes.size())fail("Invalid loading presentation header");
  const auto bands=in.word();if(in.get()!=0||!bands||bands>limits.max_bands||std::uint64_t(bands)*24U>bytes.size()-80U)
    fail("Loading presentation band count exceeds bounds");
  const auto stored=in.take(32);const auto hash=prepared_content_sha256_v1(bytes.subspan(64));
  if(!std::equal(stored.begin(),stored.end(),hash.begin()))fail("Loading presentation digest mismatch");
  LoadingPresentationV1 p;p.fade_in_updates=in.word();p.fade_out_updates=in.word();const auto count=in.get(8);
  if(count!=bytes.size()-80U-std::uint64_t(bands)*24U)fail("Loading presentation nested image extent differs");
  for(unsigned i=0;i<bands;++i)p.bands.push_back({in.word(),std::bit_cast<std::int32_t>(in.word()),std::bit_cast<std::int32_t>(in.word()),in.word(),in.word(),in.word()});
  p.library=decode_screen_overlay_v1(in.take(static_cast<std::size_t>(count)),limits.overlay);
  validate_loading_presentation_v1(p,limits);return p;
}
ScreenOverlayV1 materialize_loading_presentation_v1(const LoadingPresentationV1 &p,std::uint32_t frame,std::uint32_t duration,LoadingPresentationLimitsV1 limits) {
  validate_loading_presentation_v1(p,limits);
  if(!duration||duration>1048576U||frame>=duration)fail("Loading materialization is outside its presented clock");
  const auto &library=p.library;const auto denominator=library.coverage_denominator;
  std::uint64_t coverage=std::min<std::uint64_t>(denominator,std::uint64_t(frame)*denominator/p.fade_in_updates);
  if(std::int64_t(frame)>std::int64_t(duration)-p.fade_out_updates)
    coverage=std::uint64_t(duration-frame)*denominator/p.fade_out_updates;
  ScreenOverlayV1 out;out.canvas_width=library.canvas_width;out.canvas_height=library.canvas_height;
  out.updates_per_second=library.updates_per_second;out.coverage_denominator=denominator;out.frames.resize(1);
  const auto &background=library.frames[screen_overlay_frame_index_v1(library,frame)];
  for(const auto &band:p.bands) {
    if(frame<band.reveal_update)continue;
    auto opacity=coverage;
    if(band.reveal_ramp_end&&frame>=band.reveal_ramp_origin&&frame<band.reveal_ramp_end)
      opacity=std::uint64_t(frame-band.reveal_ramp_origin)*denominator/(band.reveal_ramp_end-band.reveal_ramp_origin);
    // Copy only the reached small image(s), never the whole periodic library.
    std::vector<std::pair<std::uint32_t,std::uint32_t>> mapped;
    for(const auto &d:background.draws) {
      auto found=std::find_if(mapped.begin(),mapped.end(),[&](const auto &m){return m.first==d.image_id;});
      std::uint32_t index;
      if(found==mapped.end()) {
        auto image=library.images[d.image_id];for(std::size_t i=3;i<image.rgb_coverage.size();i+=4)
          image.rgb_coverage[i]=static_cast<std::byte>(std::to_integer<unsigned>(image.rgb_coverage[i])*opacity/denominator);
        index=static_cast<std::uint32_t>(out.images.size());out.images.push_back(std::move(image));mapped.push_back({d.image_id,index});
      } else index=found->second;
      out.frames[0].draws.push_back({index,static_cast<std::int32_t>(std::int64_t(d.x)+band.x),static_cast<std::int32_t>(std::int64_t(d.y)+band.y)});
    }
    const auto index=static_cast<std::uint32_t>(out.images.size());out.images.push_back(library.images[band.label_image]);
    out.frames[0].draws.push_back({index,band.x,band.y});
  }
  validate_screen_overlay_v1(out,limits.overlay);return out;
}
} // namespace openrc
