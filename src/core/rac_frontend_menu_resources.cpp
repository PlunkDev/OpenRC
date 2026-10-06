#include "openrc/rac_frontend_menu_resources.hpp"
#include "openrc/hash.hpp"
#include "openrc/elf.hpp"
#include "openrc/ee_cop1_numeric.hpp"
#include "openrc/ps2_palette.hpp"

#include <algorithm>
#include <bit>
#include <fstream>

namespace openrc {
namespace {
[[noreturn]] void fail(const char *message){throw RacFrontendMenuActorCompileError(message);}
constexpr ActorLibraryLimitsV1 actor_limits{1U,1U,128U,256U,255U,255U,16U,1U,16U,17U,
    4U,65536U,65536U,196608U,4096U,4096U,16000000U,64000000U};
constexpr ActorAnimationLimitsV1 animation_limits{14U,255U,3570U,255U,910350U,128U,
    4096U,120U,1.e6F,1.e-8};
constexpr RacFrontendMenuActorCompileLimitsV1 compile_limits{
    {1048576U,false},{{1048576U,255U,1.e-8},
      {{1048576U,65536U,65536U,65536U,65536U,65536U,65536U},255U,65536U,65536U},65536U},
    actor_limits,{{1048576U,1048576U,255U,255U},{255U,1048576U,65535U,65535U,1.e-8},animation_limits}};
std::uint32_t word(std::span<const std::byte> bytes,std::size_t at) {
  if(at>bytes.size()||bytes.size()-at<4)fail("Truncated frontend menu source word");
  std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(bytes[at+i])<<(8*i);return value;
}
void read(std::ifstream &file,std::uint64_t at,std::span<std::byte> bytes) {
  file.seekg(static_cast<std::streamoff>(at));file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  if(!file)fail("Truncated frontend menu source image extent");
}
void append_frame(ScreenOverlayV1 &out,const ScreenOverlayV1 &source) {
  if(source.frames.size()!=1||source.frames[0].draws.size()!=3)fail("Main source list compiler omitted its three draws");
  ScreenOverlayFrameV1 frame;
  for(const auto &draw:source.frames[0].draws) {
    const auto &image=source.images.at(draw.image_id);
    const auto found=std::find(out.images.begin(),out.images.end(),image);
    const auto id=static_cast<std::uint32_t>(found-out.images.begin());
    if(found==out.images.end())out.images.push_back(image);
    frame.draws.push_back({id,draw.x,draw.y});
  }
  out.frames.push_back(std::move(frame));
}
ScreenOverlayV1 overlay() {
  ScreenOverlayV1 out;out.canvas_width=512;out.canvas_height=448;
  out.updates_per_second=50;out.coverage_denominator=128;return out;
}
void append_draw(ScreenOverlayV1 &out,ScreenOverlayFrameV1 &frame,
    ScreenOverlayImageV1 image,std::int32_t x,std::int32_t y) {
  const auto found=std::find(out.images.begin(),out.images.end(),image);
  const auto id=static_cast<std::uint32_t>(found-out.images.begin());
  if(found==out.images.end())out.images.push_back(std::move(image));
  frame.draws.push_back({id,x,y});
}
void rectangle(ScreenOverlayV1 &out,ScreenOverlayFrameV1 &frame,
    int top,int bottom,int left,int right,std::uint32_t rgba) {
  if(top<0||bottom>448||left<0||right>512||top>=bottom||left>=right||(rgba>>24)>128)
    fail("Dialog panel rectangle is outside its recovered PAL domain");
  ScreenOverlayImageV1 image{static_cast<std::uint32_t>(right-left),static_cast<std::uint32_t>(bottom-top),{}};
  image.rgb_coverage.resize(std::size_t(image.width)*image.height*4U);
  for(std::size_t i=0;i<image.rgb_coverage.size();++i)image.rgb_coverage[i]=static_cast<std::byte>(rgba>>(8*(i%4)));
  append_draw(out,frame,std::move(image),left,top);
}
void panel(ScreenOverlayV1 &out,ScreenOverlayFrameV1 &frame,int top,int bottom,std::uint32_t coverage) {
  const auto rgba=0x00040404U|(coverage<<24);
  // Original1f62c8 emits these seven1f5650 calls in this exact order.
  rectangle(out,frame,top,bottom,96,416,rgba);
  rectangle(out,frame,top+1,bottom-1,94,96,rgba);
  rectangle(out,frame,top+2,bottom-2,93,94,rgba);
  rectangle(out,frame,top+4,bottom-4,92,93,rgba);
  rectangle(out,frame,top+1,bottom-1,416,418,rgba);
  rectangle(out,frame,top+2,bottom-2,418,419,rgba);
  rectangle(out,frame,top+4,bottom-4,419,420,rgba);
}
void glyphs(ScreenOverlayV1 &out,ScreenOverlayFrameV1 &frame,
    const RacIntegerGlyphPlanV1 &plan,const RacFrontendTextureV1 &font,const RacTextClipCallV1 &clip) {
  if(font.entry.source_index!=1||font.entry.width!=256||font.entry.height!=128||font.indices.size()!=32768)
    fail("Dialog lost its original font1 atlas");
  for(const auto &glyph:plan.draws) {
    const auto &q=glyph.arguments;
    const auto [rx,ry,w,h]=q.rectangle_words;const auto [u,v,uw,vh]=q.uv_rectangle_words;
    const auto x=std::bit_cast<std::int32_t>(rx),y=std::bit_cast<std::int32_t>(ry);
    if(w!=uw||h!=vh||(w!=16&&w!=24)||h!=16||u>256||w>256-u||v>128||h>128-v||
        x<-4096||x>4096||y<-4096||y>4096||
        q.screen_offset_reads!=std::array<std::uint32_t,2>{(2048U-224U)*16U,(2048U-256U)*16U})
      fail("Dialog glyph is outside the executed unit-sampling domain");
    const auto emitted=emit_rac_integer_quad_v1(q);
    if(emitted.packet!=glyph.emission.packet||emitted.coordinate_words!=glyph.emission.coordinate_words||
        emitted.source_cursor_advances!=glyph.emission.source_cursor_advances||
        std::any_of(emitted.coordinate_words.begin(),emitted.coordinate_words.end(),[](auto value){return value>65535U;}))
      fail("Dialog glyph packet differs from its executed source arguments");
    const auto left=std::max(x,clip.left),right=std::min(x+static_cast<int>(w),clip.right_inclusive+1);
    const auto top=std::max(y,clip.top),bottom=std::min(y+static_cast<int>(h),clip.bottom_inclusive+1);
    if(left>=right||top>=bottom)continue;
    ScreenOverlayImageV1 image{static_cast<std::uint32_t>(right-left),static_cast<std::uint32_t>(bottom-top),{}};
    image.rgb_coverage.reserve(std::size_t(image.width)*image.height*4U);
    for(int yy=top;yy<bottom;++yy)for(int xx=left;xx<right;++xx) {
      const auto index=std::to_integer<std::uint8_t>(font.indices[(v+yy-y)*256U+u+xx-x]);
      const auto palette=std::size_t(psmt8_clut_storage_index_v1(index))*4U;
      for(unsigned channel=0;channel<4;++channel) {
        const auto component=std::min(255U,(std::to_integer<unsigned>(font.raw_palette[palette+channel])*
            unsigned((q.rgbaq>>(8U*channel))&255U))>>7U);
        if(channel==3&&component>128)fail("Dialog glyph reaches unsupported superunit coverage");
        image.rgb_coverage.push_back(static_cast<std::byte>(component));
      }
    }
    //1f55c0 disabled alpha test before the panel/glyph passes. Preserve each
    // sprite independently: flattening on transparency changes GS rounding.
    append_draw(out,frame,std::move(image),left,top);
  }
}
std::vector<std::pair<std::string,ScreenOverlayV1>> dialog_layers(
    std::span<const std::byte> elf,const RacFrontendMainAssetsV1 &assets) {
  const auto report=inspect_elf(elf);
  const auto source_word=[&](std::uint32_t address) {
    for(const auto &segment:report.program_headers)
      if(segment.type==1&&address>=segment.virtual_address&&
          std::uint64_t(address)-segment.virtual_address+4U<=segment.file_size)
        return word(elf,static_cast<std::size_t>(std::uint64_t(segment.file_offset)+address-segment.virtual_address));
    fail("Dialog constant has no original file-backed owner");
  };
  const auto left_color=source_word(0x15f5f0),right_color=source_word(0x15f5f4);
  if(left_color!=0x00ffa888||right_color!=0x80ffa888)
    fail("Original dialog colors differ");
  std::vector<RacFrontendTextureEntryV1> entries;
  for(const auto &texture:assets.textures.textures)entries.push_back(texture.entry);
  const std::array<std::uint32_t,1> requests{1};
  const auto bindings=plan_rac_frontend_gs_bindings_v1(entries,requests,
      {0x400000,static_cast<std::uint32_t>(assets.textures.payload_range.size)},0x100000,16);
  const auto tex0=bindings.bindings.at(0).tex0;
  const auto &font=assets.textures.textures.at(1);const auto &metrics=assets.metrics.tables[0];
  const auto text=[&](std::uint32_t key) {
    std::vector<std::byte> bytes;
    if(key) {
      const auto found=std::find_if(assets.text.entries.begin(),assets.text.entries.end(),[&](const auto &entry){return entry.key==key;});
      if(found==assets.text.entries.end()||found->text_bytes.size()>4096)fail("Original dialog text key is absent or too large");
      bytes=found->text_bytes;
      // No reached absent-card string depends on incoming nonzero palette
      // entries. Reject a new dependency instead of inventing live palette.
      if(std::any_of(bytes.begin(),bytes.end(),[](std::byte value){const auto b=std::to_integer<unsigned>(value);return b==0||(b>=8&&b<16);}))
        fail("Dialog text acquired an unqualified incoming palette dependency");
    }
    bytes.push_back(std::byte{});return bytes;
  };
  const auto color=[&](unsigned remaining,bool prompt) {
    // The source divider consumes remaining directly. Timed-color with this
    // exact integer complement reaches identical operands and mixer1fa8a8.
    const auto plan=plan_rac_frontend_timed_color_v1(25-remaining,
        prompt?0x0020ffff:left_color,prompt?0x8020ffff:right_color,30,0x3f555555);
    if(plan.duration!=25||!plan.returned_low64||!plan.factor_bits)
      fail("Dialog PAL fade failed original numeric evaluation");
    return plan;
  };
  const auto draw_text=[&](ScreenOverlayV1 &out,ScreenOverlayFrameV1 &frame,std::span<const std::byte> bytes,
      int x,int y,std::int64_t count,std::uint64_t rgba,const RacTextClipCallV1 &clip) {
    RacIntegerGlyphInputsV1 input;input.x_word=static_cast<std::uint32_t>(x);input.y_word=static_cast<std::uint32_t>(y);
    input.byte_limit=count;input.rgbaq=rgba;input.tex0=tex0;
    input.screen_offset_words={(2048U-224U)*16U,(2048U-256U)*16U};
    glyphs(out,frame,execute_rac_integer_glyph_v1(bytes,metrics,input,{4096,8192}),font,clip);
  };
  std::vector<std::pair<std::string,ScreenOverlayV1>> out;
  auto backdrop=overlay();backdrop.frames.emplace_back();rectangle(backdrop,backdrop.frames.back(),0,448,0,512,48U<<24);
  out.emplace_back("frontend/dialog/backdrop",std::move(backdrop));
  std::array<RacTextLayoutBoxV1,4> boxes;
  constexpr std::array<std::uint32_t,4> body_keys{0,20395,20396,20391};
  constexpr std::array<const char*,4> body_ids{"empty","absent","without-game","unavailable"};
  for(unsigned variant=0;variant<4;++variant) {
    const auto bytes=text(body_keys[variant]);RacTextLayoutRequestV1 request;
    request.box={0,448,96,416,256,104,0,0,16,5,0,0};request.inline_colors_enabled=true;
    request.screen_width=512;request.screen_height=448;request.tex0=tex0;
    const auto measure=plan_rac_text_layout_v1(bytes,metrics,request,65536);
    const int height=measure.box.height+40,top=(448-height)/2,bottom=top+height;
    if(height<40||height>448)fail("Dialog text height leaves original viewport");
    request.box=measure.box;request.box.top=static_cast<std::int16_t>(top);request.box.bottom=static_cast<std::int16_t>(bottom);
    request.box.anchor_y=static_cast<std::int16_t>(top+4);request.box.flags^=4;
    boxes[variant]=request.box;
    auto body=overlay();
    for(unsigned remaining=0;remaining<=25;++remaining) {
      body.frames.emplace_back();auto &frame=body.frames.back();const auto timed=color(remaining,false);
      const auto coverage=ee_cop1_cvt_w_s_bits_v1(ee_cop1_mul_bits_v1(*timed.factor_bits,0x42a00000).bits).bits;
      if(coverage>80)fail("Dialog panel coverage exceeds original bound");
      panel(body,frame,top,bottom,coverage);request.rgbaq=*timed.returned_low64;
      const auto layout=plan_rac_text_layout_v1(bytes,metrics,request,65536);
      for(const auto &line:layout.lines)if(line.draw) {
        const auto &draw=*line.draw;
        if(draw.floating||line.first_byte<0||line.last_byte_inclusive<line.first_byte-1)fail("Dialog body leaves original integer glyph owner");
        draw_text(body,frame,std::span(bytes).subspan(line.first_byte),draw.x_base,draw.y_base,
            line.last_byte_inclusive-line.first_byte+1,draw.initial_rgbaq,layout.entry_clip);
      }
    }
    validate_screen_overlay_v1(body,rac_frontend_menu_overlay_limits_v1());
    out.emplace_back(std::string("frontend/dialog/body/")+body_ids[variant],std::move(body));
  }
  constexpr std::array<const char*,4> prompt_ids{"new","existing","without-game","unavailable"};
  constexpr std::array<std::array<std::uint32_t,3>,4> prompt_keys{{{21075,21072,0},{21076,21072,0},{0,0,20392},{0,0,20392}}};
  constexpr std::array<int,3> centers{202,309,256};
  for(unsigned variant=0;variant<4;++variant) {
    auto prompts=overlay();const auto &box=boxes[variant<2?1:variant];
    for(unsigned remaining=0;remaining<=25;++remaining) {
      prompts.frames.emplace_back();auto &frame=prompts.frames.back();const auto timed=color(remaining,true);
      for(unsigned slot=0;slot<3;++slot)if(prompt_keys[variant][slot]) {
        const auto bytes=text(prompt_keys[variant][slot]);const auto width=measure_rac_text_width_v1(bytes,-1,metrics,4096);
        if(width<0||width>512)fail("Dialog prompt width leaves original centered domain");
        draw_text(prompts,frame,bytes,centers[slot]-width/2,box.bottom-20,-1,*timed.returned_low64,{0,511,0,447});
      }
    }
    validate_screen_overlay_v1(prompts,rac_frontend_menu_overlay_limits_v1());
    out.emplace_back(std::string("frontend/dialog/prompts/")+prompt_ids[variant],std::move(prompts));
  }
  return out;
}
}
ActorLibraryIoLimitsV1 rac_frontend_menu_actor_io_limits_v1(){return {64U*1024U*1024U,actor_limits};}
ActorAnimationIoLimitsV1 rac_frontend_menu_animation_io_limits_v1(){return {64U*1024U*1024U,animation_limits};}
ScreenOverlayLimitsV1 rac_frontend_menu_overlay_limits_v1(){ScreenOverlayLimitsV1 out;out.max_images=4096;return out;}

RacFrontendMenuResourcesV1 compile_rac_frontend_menu_resources_v1(
    const std::filesystem::path &image,std::span<const std::byte> elf,std::span<const std::byte> wad,
    RacFrontendMenuResourceLimitsV1 limits) {
  if(elf.empty()||elf.size()>limits.main.max_elf_bytes||wad.empty()||wad.size()>limits.max_source_wad_bytes||
      limits.max_source_wad_bytes>64U*1024U*1024U||!limits.max_total_payload_bytes)
    fail("Frontend menu source or aggregate payload limit is invalid");
  RacFrontendMenuResourcesV1 out;
  out.source_elf_sha256=prepared_content_sha256_v1(elf);out.source_wad_sha256=prepared_content_sha256_v1(wad);
  if(hex_digest(out.source_elf_sha256)!="17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b"||
      hex_digest(out.source_wad_sha256)!="159e6341c3180831940350097bbbbf2115b9d35e4a030a856d563206b462c2d2")
    fail("Frontend menu sources differ from the recovered owner contract");
  std::ifstream disc(image,std::ios::binary);if(!disc)fail("Cannot open frontend menu source image");
  std::array<std::byte,0x2960> toc{};read(disc,1500ULL*2048U,toc);
  if(hex_digest(prepared_content_sha256_v1(toc))!="10b5950d0c5c4271f40640f5ae1ce7750bfcdc942459814a6f2865be7ba252c4")
    fail("Frontend menu TOC differs from original catalog");
  const auto text_at=std::uint64_t(word(toc,0x1528))*2048U,text_size=std::uint64_t(word(toc,0x152c))*2048U;
  if(!text_size||text_size>2U*1024U*1024U)fail("Frontend menu text exceeds bound");
  std::vector<std::byte> text(static_cast<std::size_t>(text_size));read(disc,text_at,text);
  out.source_text_sha256=prepared_content_sha256_v1(text);
  if(hex_digest(out.source_text_sha256)!="f650990d2b794c87302006960769a23da858d15099e1a6742402fdcc0a54c785")
    fail("Frontend menu text differs from original bank");
  const auto assets=compile_rac_frontend_main_assets_v1(elf,wad,text,0,limits.main);
  if(assets.screen.active_state!=46||assets.focused_node!=0x1d4a18||assets.screen.parent_reference||
      assets.nodes[0].rows[0].text_key!=20266||assets.nodes[1].rows[0].text_key!=20264||
      assets.nodes[2].rows[0].text_key!=20199)fail("Main screen binding differs from the recovered descriptor");
  const auto count=word(wad,0x18),directory=word(wad,0x1c),shared=word(wad,4);
  if(!count||count>256||std::uint64_t(directory)+std::uint64_t(count)*32U>wad.size())fail("Invalid main class directory");
  std::uint64_t class_at=0,class_end=wad.size();std::array<std::uint8_t,16> slots;slots.fill(255);unsigned used=0;
  for(unsigned i=0;i<count;++i) {
    const auto row=std::size_t(directory)+i*32U;
    if(word(wad,row+4)!=1138)continue;
    if(class_at)fail("Duplicate main class1138");
    class_at=std::uint64_t(shared)+word(wad,row);
    for(unsigned j=0;j<16;++j){slots[j]=std::to_integer<std::uint8_t>(wad[row+16+j]);used+=slots[j]!=255;}
  }
  if(!class_at||class_at>=wad.size())fail("Original main class1138 is absent");
  for(unsigned i=0;i<count;++i) {
    const auto at=std::uint64_t(shared)+word(wad,std::size_t(directory)+i*32U);
    if(at>class_at)class_end=std::min(class_end,at);
  }
  if(class_end<=class_at||class_end>wad.size()||class_end-class_at>1048576)fail("Main class has invalid owned extent");
  const auto source_class=wad.subspan(static_cast<std::size_t>(class_at),static_cast<std::size_t>(class_end-class_at));
  if(hex_digest(prepared_content_sha256_v1(source_class))!="4f26a2e0938721205ea897d6c450e6dd8956ac5348cfe5a23e35c1047632e5f9")
    fail("Main class extent differs from original owner");
  std::uint64_t gs_size=0;const auto uploads=word(wad,8),gs_directory=word(wad,12);
  if(uploads>4096||std::uint64_t(gs_directory)+std::uint64_t(uploads)*16U>wad.size())fail("Invalid main GS upload directory");
  for(unsigned i=0;i<uploads;++i) {
    const auto row=std::size_t(gs_directory)+i*16U;
    const auto type=word(wad,row),dimensions=word(wad,row+4);
    if(word(wad,row+12)!=gs_size)fail("Main GS source rows lost contiguous upload ownership");
    if(type==0)gs_size+=1024;else if(type==2)gs_size+=512;
    else {if(type!=19)fail("Unknown main GS source format");gs_size+=std::max(UINT64_C(256),std::uint64_t(dimensions&65535U)*(dimensions>>16U));}
    if(gs_size>64U*1024U*1024U)fail("Main GS upload extent exceeds bound");
  }
  const auto gs_at=word(wad,0),texture_table=word(wad,0x3c),texture_count=word(wad,0x38);
  if(gs_at>wad.size()||gs_size>wad.size()-gs_at||texture_table>wad.size()||texture_count>255||
      std::uint64_t(texture_count)*16U>wad.size()-texture_table)fail("Main texture source extent is invalid");
  const auto textures=decode_rac_level_moby_texture_bank_v1(wad.subspan(texture_table,texture_count*16U),wad,
      wad.subspan(gs_at,static_cast<std::size_t>(gs_size)),std::uint64_t(shared)+word(wad,0x60),
      {64U*1024U*1024U,64U*1024U*1024U,64U*1024U*1024U,255U,4096U,4096U,16000000U,16000000U,64000000U});
  const auto compiled=compile_rac_frontend_menu_actor_v1(source_class,textures,slots,static_cast<std::uint8_t>(used),
      assets.screen.sequences,50,compile_limits);
  const auto entry=compile_rac_frontend_main_entry_geometry_v1(source_class,assets);
  const auto timeline=compile_rac_frontend_menu_actor_timeline_v1(compiled,entry,
      {rac_frontend_menu_actor_io_limits_v1(),rac_frontend_menu_animation_io_limits_v1(),{}});
  ScreenOverlayV1 lists;lists.canvas_width=512;lists.canvas_height=448;lists.updates_per_second=50;
  lists.coverage_denominator=128;lists.frames.resize(12);
  const auto duration=static_cast<std::uint32_t>(evaluate_rac_frontend_timer_v1(assets.timer_argument,0x3f555555));
  if(duration!=8||assets.nodes[0].rows[0].color_age||assets.nodes[1].rows[0].color_age||assets.nodes[2].rows[0].color_age)
    fail("Main row color clock differs from its source initial values");
  for(unsigned age=1;age<=duration;++age)append_frame(lists,compile_rac_frontend_main_lists_v1(
      assets,entry.back().list_bounds,{static_cast<std::int16_t>(age),0,0},0x3f555555,50,limits.main));
  validate_screen_overlay_v1(lists,limits.main.overlay);
  std::uint64_t total=0;
  const auto add=[&](const char *id,const char *type,std::vector<std::byte> bytes) {
    if(bytes.size()>limits.max_total_payload_bytes-total)fail("Main neutral resources exceed aggregate payload bound");
    total+=bytes.size();LevelPackageResourceV1 resource;resource.resource_id=id;resource.type_id=type;resource.schema_version=1;
    resource.payload=std::move(bytes);resource.payload_sha256=prepared_content_sha256_v1(resource.payload);
    resource.provenance.push_back({LevelPackageProvenanceKindV1::generated,"compiler/rac-frontend-menu-resources-v1",0,0,{}});
    out.resources.push_back(std::move(resource));
  };
  add("frontend/menu/actors","openrc.actor-library",encode_actor_library_v1(compiled.actor_library,rac_frontend_menu_actor_io_limits_v1()));
  add("frontend/menu/animation","openrc.actor-animation-bank",encode_actor_animation_bank_v1(compiled.actor_animation,rac_frontend_menu_animation_io_limits_v1()));
  add("frontend/menu/timeline","openrc.scene-timeline",encode_scene_timeline_v1(timeline));
  add("frontend/menu/lists","openrc.screen-overlay",encode_screen_overlay_v1(lists,limits.main.overlay));
  for(auto &[id,layer]:dialog_layers(elf,assets))add(id.c_str(),"openrc.screen-overlay",
      encode_screen_overlay_v1(layer,rac_frontend_menu_overlay_limits_v1()));
  return out;
}
} // namespace openrc
