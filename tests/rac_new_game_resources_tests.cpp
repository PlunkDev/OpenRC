#include "openrc/rac_new_game_resources.hpp"
#include "openrc/hash.hpp"
#include "openrc/rac_startup.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace {
using namespace openrc;
void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
template<class F>void rejects(F &&f){try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error("Invalid New Game resources were accepted");}
RacFrontendLoadingAssetsV1 fixture() {
  RacFrontendLoadingAssetsV1 a;
  for(unsigned i=0;i<3;++i) {
    auto &t=i==0?a.tile:a.cards[i-1];t.entry.width=i?512:64;t.entry.height=64;t.entry.source_index=i;
    t.indices.resize(t.entry.width*64U);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<t.entry.width;++x)t.indices[y*t.entry.width+x]=static_cast<std::byte>((x*7U+y*11U+i*37U)&255U);
    for(unsigned p=0;p<256;++p)for(unsigned c=0;c<4;++c)t.raw_palette[p*4U+c]=static_cast<std::byte>(c==3?(p*13U)%129U:(p*37U+c*53U+i*7U)&255U);
  }
  return a;
}
ScreenOverlayV1 source_frame(const RacFrontendLoadingAssetsV1 &a,unsigned first,unsigned second,unsigned frame,unsigned duration) {
  ScreenOverlayV1 out;out.canvas_width=512;out.canvas_height=448;out.updates_per_second=50;out.coverage_denominator=128;out.frames.resize(1);
  const auto draws=execute_rac_frontend_loading_draws_v1({first,second,frame,duration,false,true},224);
  for(const auto &d:draws) {
    const auto id=static_cast<std::uint32_t>(out.images.size());out.images.push_back(rasterize_rac_frontend_loading_band_v1(a,d));
    out.frames[0].draws.push_back({id,static_cast<std::int32_t>(d.rectangle_words[0]),static_cast<std::int32_t>(d.rectangle_words[1])});
  }
  return out;
}
void compiler_and_pixels() {
  const auto a=fixture();const auto p=compile_rac_loading_presentation_v1(a,0,1,50);
  check(p.library.frames.size()==600&&p.bands.size()==2&&p.bands[0].y==178&&p.bands[1].y==224,"Source library period or card positions changed");
  const auto encoded=encode_loading_presentation_v1(p);check(decode_loading_presentation_v1(encoded)==p,"Compiler loading resource roundtrip differs");
  unsigned cases=0;
  for(auto duration:{80U,96U,200U,231U,650U})for(auto frame:{0U,1U,15U,31U,32U,64U,65U,79U,80U,95U,96U,184U,199U,230U,599U,600U,649U}) {
    if(frame>=duration)continue;
    const auto native=materialize_loading_presentation_v1(p,frame,duration),source=source_frame(a,0,1,frame,duration);
    std::vector<std::byte> left(512U*448U*4U,std::byte{19}),right=left;
    composite_screen_overlay_frame_v1(left,native,0);composite_screen_overlay_frame_v1(right,source,0);
    check(left==right,"Neutral loading pixels differ from source draw/filter/modulation order");++cases;
  }
  std::cout<<cases<<" full512x448 loading source/native rasters matched\n";
  rejects([&]{(void)compile_rac_loading_presentation_v1(a,2,2,50);});
  auto limits=LoadingPresentationLimitsV1{};limits.overlay.max_bytes=1024;
  rejects([&]{(void)compile_rac_loading_presentation_v1(a,0,1,50,224,limits);});
  const std::array<std::byte,4> bad{};rejects([&]{(void)compile_rac_new_game_resources_v1("missing.iso",bad,0,1,50);});
}
void actual(const char *image,const char *elf_path) {
  std::ifstream in(elf_path,std::ios::binary|std::ios::ate);check(bool(in),"Cannot open actual source ELF");
  const auto size=in.tellg();check(size>0&&size<=32*1024*1024,"Actual source ELF exceeds limits");
  std::vector<std::byte> elf(static_cast<std::size_t>(size));in.seekg(0);in.read(reinterpret_cast<char*>(elf.data()),elf.size());check(bool(in),"Truncated actual source ELF");
  const auto result=compile_rac_new_game_resources_v1(image,elf,0,1,50);check(result.resources.size()==9,"Actual New Game resource count differs");
  for(const auto &r:result.resources) {
    check(prepared_content_sha256_v1(r.payload)==r.payload_sha256,"Actual resource digest differs");
    if(r.type_id=="openrc.loading-presentation") {
      const auto p=decode_loading_presentation_v1(r.payload);check(encode_loading_presentation_v1(p)==r.payload,"Actual loading resource roundtrip differs");
      std::cout<<r.resource_id<<" bytes "<<r.payload.size()<<" period "<<p.library.frames.size()<<" images "<<p.library.images.size();
    } else if(r.type_id=="openrc.frame-color-transfers") {
      const auto p=decode_frame_color_transfer_sequence_v1(r.payload);
      check(encode_frame_color_transfer_sequence_v1(p)==r.payload&&p.lead_updates==1&&p.tail_updates==1,"Actual feedback fade roundtrip/timing differs");
      std::cout<<r.resource_id<<" bytes "<<r.payload.size()<<" transfers "<<p.transfers.size();
    } else {
      const auto p=decode_media_clip_v1(r.payload);check(encode_media_clip_v1(p)==r.payload,"Actual movie resource roundtrip differs");
      std::cout<<r.resource_id<<" bytes "<<r.payload.size()<<" video "<<p.video.size()<<" audio "<<p.audio.size();
    }
    std::cout<<" sha "<<hex_digest(r.payload_sha256)<<'\n';
  }
  std::cout<<"9 actual New Game resources compiled and neutral roundtripped\n";
}
void fade_source_fixture(const char *path) {
  std::ifstream in(path,std::ios::binary|std::ios::ate);check(bool(in),"Cannot open original fade fixture");
  check(in.tellg()>0&&in.tellg()<32768,"Original fade fixture exceeds bound");
  std::vector<std::byte> bytes(static_cast<std::size_t>(in.tellg()));in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
  check(hex_digest(prepared_content_sha256_v1(bytes))=="d9a55963f3c32ff929e9923344a0784f024229a075a193c78aeb70f56c426a62",
      "Original fade fixture hash differs");
  check(bool(in)&&bytes.size()>=12&&std::string(reinterpret_cast<const char*>(bytes.data()),8)=="FADESEQ1","Invalid original fade fixture");
  std::size_t at=8;const auto word=[&]() {check(at<=bytes.size()&&bytes.size()-at>=4,"Truncated original fade fixture");std::uint32_t v=0;
    for(unsigned i=0;i<4;++i)v|=std::to_integer<std::uint32_t>(bytes[at++])<<(8*i);return v;};
  const auto count=word();check(count==5,"Original fade fixture count differs");unsigned samples=0;
  for(unsigned i=0;i<count;++i) {
    const auto duration=word(),lead=word(),tail=word();const auto compiled=compile_rac_frontend_fade_v1(duration,50);
    check(compiled.lead_updates==lead&&compiled.tail_updates==tail&&compiled.transfers.size()==duration,"Fade lead/body/tail timing differs from original calls");
    for(const auto &table:compiled.transfers) {
      check(bytes.size()-at>=256,"Truncated original fade table");
      check(std::equal(table.begin(),table.end(),bytes.begin()+at),"Prepared feedback LUT differs from original alpha/GS byte transfer");at+=256;++samples;
    }
  }
  check(at==bytes.size(),"Trailing original fade fixture bytes");
  std::cout<<count<<" original fade durations / "<<samples*256U<<" source transfer bytes matched\n";
}
void fade_limits() {
  rejects([]{(void)compile_rac_frontend_fade_v1(0,50);});rejects([]{(void)compile_rac_frontend_fade_v1(1025,50);});
  rejects([]{(void)compile_rac_frontend_fade_v1(2,55);});
  auto limits=FrameColorTransferSequenceLimitsV1{};limits.max_bytes=591;
  rejects([&]{(void)compile_rac_frontend_fade_v1(2,50,limits);});
  const auto p=compile_rac_frontend_fade_v1(2,50);std::vector<std::byte> rgba{std::byte{127},std::byte{128},std::byte{255},std::byte{0}};
  apply_image_color_transfer_v1(rgba,p.transfers[0]);check(rgba==std::vector<std::byte>{std::byte{63},std::byte{64},std::byte{127},std::byte{255}},"Feedback byte floor/alpha differs");
  apply_image_color_transfer_v1(rgba,p.transfers[1]);check(rgba==std::vector<std::byte>{std::byte{0},std::byte{0},std::byte{0},std::byte{255}},"Last feedback did not reach black");
}
}
int main(int argc,char **argv){try{compiler_and_pixels();fade_limits();if(argc>=3)actual(argv[1],argv[2]);else check(argc==1,"Expected source ISO and ELF paths");if(argc==4)fade_source_fixture(argv[3]);else check(argc==1||argc==3,"Expected optional original fade fixture");std::cout<<"New Game resource group passed\n";return 0;}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
