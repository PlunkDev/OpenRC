#include "openrc/rac_frontend_menu_resources.hpp"
#include "openrc/rac_startup.hpp"
#include "openrc/hash.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {
using namespace openrc;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
template<class Fn>void rejects(Fn &&f){try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error("Invalid main resource input was accepted");}
std::vector<std::byte> read(const char *path) {
  std::ifstream stream(path,std::ios::binary|std::ios::ate);
  check(bool(stream)&&stream.tellg()>0&&stream.tellg()<32*1024*1024,"Invalid main ELF extent");
  std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg()));stream.seekg(0);
  stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size());check(bool(stream),"Truncated main ELF");return bytes;
}
void source_dialog_fixture(const char *path,const std::array<ScreenOverlayV1,9> &layers) {
  const auto bytes=read(path);
  check(bytes.size()==2037772&&hex_digest(prepared_content_sha256_v1(bytes))==
      "4ee888aab382b923294af4a49bfc81033c430ca9c6deb6ad44ed4f53eadae45f","Dialog original source fixture differs");
  check(std::string_view(reinterpret_cast<const char*>(bytes.data()),8)=="DIALAYR1","Dialog fixture magic differs");
  std::size_t at=8,draws=0;
  const auto word=[&]() {
    check(at<=bytes.size()&&bytes.size()-at>=4,"Truncated dialog source word");std::uint32_t value=0;
    for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(bytes[at++])<<(8*i);return value;
  };
  check(word()==layers.size(),"Dialog source layer count differs");
  for(const auto &layer:layers) {
    const auto count=word();check(count==layer.frames[0].draws.size(),"Dialog draw count differs from original instruction output");draws+=count;
    for(const auto &draw:layer.frames[0].draws) {
      const auto x=word(),y=word(),w=word(),h=word(),size=word();const auto &image=layer.images.at(draw.image_id);
      check(draw.x==static_cast<std::int32_t>(x)&&draw.y==static_cast<std::int32_t>(y)&&image.width==w&&image.height==h,
          "Prepared dialog rectangle differs from original source packet");
      check(size==image.rgb_coverage.size()&&at<=bytes.size()&&size<=bytes.size()-at,"Dialog source raster extent differs");
      check(std::equal(image.rgb_coverage.begin(),image.rgb_coverage.end(),bytes.begin()+at),
          "Prepared dialog pixels differ from the original packet logical reference raster");at+=size;
    }
  }
  check(at==bytes.size()&&draws==459,"Dialog original source fixture coverage differs");
  std::cout<<"459 original source dialog draws matched: 54860 layout,23432 glyph,41710 quad,324 panel instructions\n";
}
void actual(const char *image,const char *elf_path,const char *fixture) {
  const auto elf=read(elf_path);const auto wad=read_rac_startup_wad_v1(image,0x14e8,16U*1024U*1024U,32U*1024U*1024U);
  const auto compiled=compile_rac_frontend_menu_resources_v1(image,elf,wad.decoded_bytes);
  check(compiled.resources.size()==13&&compiled.entry_updates==12&&compiled.active_timeline_sample==12,"Original menu package shape differs");
  const auto &r=compiled.resources;
  for(const auto &resource:r)check(resource.payload_sha256==prepared_content_sha256_v1(resource.payload),"Menu payload digest differs");
  const auto actors=decode_actor_library_v1(r[0].payload,rac_frontend_menu_actor_io_limits_v1());
  const auto animation=decode_actor_animation_bank_v1(r[1].payload,rac_frontend_menu_animation_io_limits_v1());
  const auto timeline=decode_scene_timeline_v1(r[2].payload);
  const auto lists=decode_screen_overlay_v1(r[3].payload);
  check(actors.models.size()==1&&animation.clips.size()==14&&timeline.actors.size()==14&&timeline.samples.size()==13,
      "Menu resource helper lost actual model instances or source entry samples");
  check(lists.frames.size()==20&&lists.loop_begin==UINT32_MAX&&lists.coverage_denominator==128&&
      lists.canvas_width==512&&lists.canvas_height==448&&lists.updates_per_second==50,"Menu list source clock or viewport differs");
  for(unsigned i=0;i<12;++i)check(lists.frames[i].draws.empty(),"Main lists became visible before original entry gate");
  for(unsigned i=12;i<20;++i)check(lists.frames[i].draws.size()==3,"Original main list owner lost one of its three labels");
  for(const auto &sample:timeline.samples)for(unsigned i=0;i<14;++i)check(sample.actors[i].enabled==(i!=6),"Original main actor visibility differs");
  check(lists.images.at(lists.frames[12].draws[0].image_id)!=lists.images.at(lists.frames[19].draws[0].image_id),
      "Prepared main focused-row color clock became a constant image");
  check(encode_screen_overlay_v1(lists)==r[3].payload&&encode_scene_timeline_v1(timeline)==r[2].payload,"Main resource codecs changed canonical bytes");
  constexpr std::array<const char*,9> dialog_ids{"frontend/dialog/backdrop",
    "frontend/dialog/body/empty","frontend/dialog/body/absent","frontend/dialog/body/without-game","frontend/dialog/body/unavailable",
    "frontend/dialog/prompts/new","frontend/dialog/prompts/existing","frontend/dialog/prompts/without-game","frontend/dialog/prompts/unavailable"};
  std::array<ScreenOverlayV1,9> dialog;
  std::size_t rasters=0,draws=0;
  for(unsigned i=0;i<dialog.size();++i) {
    check(r[i+4].resource_id==dialog_ids[i]&&r[i+4].type_id=="openrc.screen-overlay","Dialog resource semantic binding differs");
    auto &layer=dialog[i];layer=decode_screen_overlay_v1(r[i+4].payload,rac_frontend_menu_overlay_limits_v1());
    check(layer.frames.size()==(i?26U:1U)&&layer.canvas_width==512&&layer.canvas_height==448&&layer.coverage_denominator==128,
        "Dialog source viewport or remaining-counter clock differs");
    rasters+=layer.images.size();for(const auto &frame:layer.frames)draws+=frame.draws.size();
    if(i)for(const auto &draw:layer.frames.back().draws) {
      const auto &pixels=layer.images.at(draw.image_id).rgb_coverage;
      for(std::size_t at=3;at<pixels.size();at+=4)check(pixels[at]==std::byte{},"Dialog initial remaining25 sample is not transparent");
    }
    check(encode_screen_overlay_v1(layer,rac_frontend_menu_overlay_limits_v1())==r[i+4].payload,"Dialog resource codec changed canonical bytes");
  }
  check(dialog[0].frames[0].draws.size()==1&&dialog[0].images[0].rgb_coverage[3]==std::byte{48},"Original black backdrop differs");
  for(unsigned i=1;i<=4;++i) {
    check(dialog[i].frames[0].draws.size()>=7,"Dialog panel lost its original seven rectangle calls");
    const auto &draw=dialog[i].frames[0].draws[0];const auto &image=dialog[i].images.at(draw.image_id);
    check(draw.x==96&&image.width==320&&image.rgb_coverage[0]==std::byte{4}&&image.rgb_coverage[3]==std::byte{80},
        "Dialog panel size, original RGB or final coverage differs");
    check(dialog[i].frames[0].draws.size()==dialog[i].frames[25].draws.size(),"Prepared opacity deleted authoritative source draws");
  }
  check(dialog[1].frames[0].draws.size()==7&&dialog[2].frames[0].draws.size()>100,
      "Original empty and absent-card bodies lost distinct text content");
  check(dialog[5]!=dialog[6],"Original new/existing controller glyph prompts were substituted");
  std::vector<std::byte> canvas(512U*448U*4U,std::byte{200});
  const auto overlay_limits=rac_frontend_menu_overlay_limits_v1();
  composite_screen_overlay_frame_v1(canvas,dialog[0],0,overlay_limits);
  check(canvas[0]==std::byte{125},"Original backdrop coverage did not composite with denominator128");
  const auto faded=canvas;
  composite_screen_overlay_frame_v1(canvas,dialog[2],25,overlay_limits);composite_screen_overlay_frame_v1(canvas,dialog[5],25,overlay_limits);
  check(canvas==faded,"Independent transparent body/prompt samples altered the framebuffer");
  composite_screen_overlay_frame_v1(canvas,dialog[2],0,overlay_limits);
  check(canvas!=faded,"Original visible body/panel emitted no coverage");
  const auto body_only=canvas;composite_screen_overlay_frame_v1(canvas,dialog[5],25,overlay_limits);
  check(canvas==body_only,"Prompt opacity followed the independent body countdown");
  composite_screen_overlay_frame_v1(canvas,dialog[5],0,overlay_limits);
  check(canvas!=body_only,"Original visible prompts emitted no coverage");
  if(fixture)source_dialog_fixture(fixture,dialog);
  auto corrupt=elf;corrupt.back()^=std::byte{1};rejects([&]{(void)compile_rac_frontend_menu_resources_v1(image,corrupt,wad.decoded_bytes);});
  std::cout<<"Actual ISO main resources: 14 instances,13 entry samples,"<<lists.frames.size()<<" list frames,"<<lists.images.size()<<" original list rasters passed\n";
  std::cout<<"Original absent-card dialog: 9 layers,"<<rasters<<" rasters,"<<draws<<" ordered draws,independent 26-sample fades passed\n";
}
}
int main(int argc,char **argv) {
  try {
    check(argc==1||argc==3||argc==4,"Expected optional original ISO,ELF,and dialog source fixture");
    rejects([]{(void)compile_rac_frontend_menu_resources_v1({}, {}, {});});
    if(argc>=3)actual(argv[1],argv[2],argc==4?argv[3]:nullptr);
    std::cout<<"Frontend menu resource tests passed\n";return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
