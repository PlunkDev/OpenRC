#include "openrc/rac_frontend_main_compile.hpp"

#include "openrc/elf.hpp"
#include "openrc/ps2_palette.hpp"

#include <algorithm>
#include <bit>

namespace openrc {
namespace {
[[noreturn]] void fail(const char *message) {throw RacFrontendMainCompileError(message);}
std::uint32_t word(std::span<const std::byte> bytes,std::size_t at) {
  if(at>bytes.size() || bytes.size()-at<4U)fail("Main source word exceeds owned input");
  std::uint32_t out=0U;
  for(unsigned i=0U;i<4U;++i)out|=std::to_integer<std::uint32_t>(bytes[at+i])<<(8U*i);
  return out;
}
std::uint32_t target_extent(std::uint32_t pixels) {
  if(!pixels || pixels>2048U)fail("Main list dimension exceeds bounded source RTT");
  return std::max(128U,std::bit_ceil(pixels));
}
int blend(int source,int destination,int alpha) {
  const int value=(source-destination)*alpha;
  return destination+(value>=0 ? value/128 : -((-value+127)/128));
}
} // namespace

RacFrontendMainVisibilityV1 decode_rac_frontend_main_visibility_v1(
    std::span<const std::byte> elf,std::uint64_t maximum) {
  if(!maximum || elf.size()>maximum)fail("Main visibility ELF exceeds its source bound");
  const auto report=inspect_elf(elf);
  const auto read=[&](std::uint32_t address) {
    for(const auto &segment:report.program_headers)
      if(segment.type==1U && address>=segment.virtual_address &&
         std::uint64_t(address)-segment.virtual_address+4U<=segment.file_size) {
        const auto offset=std::uint64_t(segment.file_offset)+address-segment.virtual_address;
        if(offset>elf.size() || elf.size()-offset<4U)fail("Main visibility source word leaves ELF");
        return word(elf,static_cast<std::size_t>(offset));
      }
    fail("Main visibility source word has no file-backed owner");
  };
  RacFrontendMainVisibilityV1 out;
  for(std::uint32_t i=0U;i<14U;++i)out.enabled_words[i]=read(0x1ce640U+4U*i);
  out.slot_six_enabled_word=read(0x1d6048U);
  return out;
}

RacFrontendMainAssetsV1 compile_rac_frontend_main_assets_v1(
    std::span<const std::byte> elf,std::span<const std::byte> wad,
    std::span<const std::byte> text,std::uint32_t language,
    RacFrontendMainCompileLimitsV1 limits) {
  if(elf.size()>limits.max_elf_bytes || language>=8U)fail("Main source ELF or language exceeds bounds");
  const auto report=inspect_elf(elf);
  const auto region=[&](std::uint32_t address,std::uint32_t count) {
    for(const auto &segment:report.program_headers)
      if(segment.type==1U && address>=segment.virtual_address &&
         std::uint64_t(address)-segment.virtual_address+count<=segment.file_size) {
        const auto offset=std::uint64_t(segment.file_offset)+address-segment.virtual_address;
        if(offset>elf.size() || count>elf.size()-offset)fail("Main source ELF region exceeds file");
        return elf.subspan(static_cast<std::size_t>(offset),count);
      }
    fail("Main source address is not backed by an ELF load segment");
  };
  RacFrontendMainAssetsV1 out;
  out.initial_visibility=decode_rac_frontend_main_visibility_v1(elf,limits.max_elf_bytes);
  out.textures=parse_rac_frontend_texture_bank_v1(wad,limits.textures);
  const auto directory=parse_rac_text_bank_directory_v1(text,limits.text);
  out.text=directory.slots[language];
  out.metrics=parse_rac_font_metric_tables_v1(region(0x1df3d0U,kRacFontMetricTablesBytesV1));
  out.screen.source_reference=0x1d4948U;
  const auto screen=region(out.screen.source_reference,0x84U);
  out.screen.parent_reference=word(screen,0x38U);
  out.screen.active_state=word(screen,0x3cU);
  out.focused_node=word(screen,0x40U);
  std::size_t lists=0U;
  for(std::uint32_t i=0U;i<14U;++i) {
    out.screen.sequences[i]=word(screen,i*4U);
    const auto address=word(screen,0x44U+i*4U);
    out.screen.node_references[i]=address;
    if(!address)continue;
    const auto node=region(address,0x50U);
    out.screen.init_callbacks[i]=word(node,8U);
    out.screen.cleanup_callbacks[i]=word(node,12U);
    if(word(node,0x10U)&4U)continue;
    if(word(node,4U)!=0x21c1b0U || lists==out.nodes.size())
      fail("Main descriptor has an unqualified visible node callback");
    auto &decoded=out.nodes[lists++];
    decoded.source_reference=address;decoded.object_slot=i;
    decoded.owner_flags=word(node,0x10U);decoded.text_flags=word(node,0x30U);
    decoded.selected_row=word(node,0x40U);
    const auto rows=region(word(node,0x34U),24U);
    for(unsigned row=0U;row<2U;++row) {
      const auto key_action=word(rows,row*12U),secondary_age=word(rows,row*12U+8U);
      decoded.rows.push_back({std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(key_action)),
          std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(key_action>>16U)),word(rows,row*12U+4U),
          std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(secondary_age)),
          std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(secondary_age>>16U))});
    }
    if(decoded.rows[0U].text_key==0 || decoded.rows[1U].text_key!=0)
      fail("Main list does not own its original single row and terminator");
  }
  if(lists!=3U)fail("Main descriptor does not own three visible source lists");
  const auto globals=region(0x1602b0U,16U);
  out.clear_rgba=word(globals,0U);out.timer_argument=word(globals,4U);
  out.shadow_x_y={word(globals,8U),word(globals,12U)};
  const auto missing=region(0x199a68U,64U);
  const auto end=std::find(missing.begin(),missing.end(),std::byte{});
  if(end==missing.end())fail("Main source lookup fallback is not terminated");
  out.missing_text.assign(missing.begin(),end+1);
  return out;
}

ScreenOverlayImageV1 compile_rac_frontend_list_rtt_v1(
    const RacFrontendListDrawPlanV1 &draw,const RacFrontendTextureV1 &font,
    std::uint32_t width,std::uint32_t height,std::uint32_t clear,ScreenOverlayLimitsV1 limits) {
  const auto tw=target_extent(width),th=target_extent(height);
  if(!draw.batch_ended || draw.returned_word!=2U || draw.font_source_id!=font.entry.source_index ||
     width>limits.max_dimension || height>limits.max_dimension ||
     std::uint64_t(width)*height*4U+104U>limits.max_bytes ||
     std::uint64_t(tw)*th>131072U || height>=th ||
     font.entry.width!=256U || font.entry.height!=128U || font.indices.size()!=32768U ||
     (clear>>24U)!=128U)
    fail("Main list RTT is outside its completed PSMCT32/crop/clear domain");
  ScreenOverlayImageV1 image{width,height,std::vector<std::byte>(std::size_t(width)*height*4U)};
  for(std::uint32_t y=0U;y<height;++y)for(std::uint32_t x=0U;x<width;++x) {
    const auto at=(std::size_t(y)*width+x)*4U;
    // SDK zeroing covers these rows.2017c8 then covers [0,tw-1)×[0,th-1).
    for(unsigned c=0U;c<3U;++c)image.rgb_coverage[at+c]=
        static_cast<std::byte>(x+1U<tw ? (clear>>(c*8U))&255U : 0U);
    image.rgb_coverage[at+3U]=std::byte{128};
  }
  std::uint64_t draw_count=0U;
  for(const auto &call:draw.glyph_calls) {
    draw_count+=call.plan.draws.size();
    if(draw_count>65536U || draw_count>limits.max_draws)
      fail("Main list glyph raster exceeds its work bound");
  }
  for(const auto &call:draw.glyph_calls)for(const auto &glyph:call.plan.draws) {
    const auto &q=glyph.arguments;
    const auto [x,y,w,h]=q.rectangle_words;
    const auto [u,v,uw,vh]=q.uv_rectangle_words;
    if(w!=uw || h!=vh || (w!=16U && w!=24U) || h!=16U ||
       u>256U || w>256U-u || v>128U || h>128U-v ||
       std::bit_cast<std::int32_t>(x)<-4096 || std::bit_cast<std::int32_t>(x)>4096 ||
       std::bit_cast<std::int32_t>(y)<-4096 || std::bit_cast<std::int32_t>(y)>4096 ||
       q.screen_offset_reads!=std::array<std::uint32_t,2U>{(2048U-th/2U)*16U,(2048U-tw/2U)*16U})
      fail("Main glyph does not have its qualified in-atlas unit sampling");
    const auto emitted=emit_rac_integer_quad_v1(q);
    if(glyph.emission.packet!=emitted.packet || glyph.emission.coordinate_words!=emitted.coordinate_words ||
       glyph.emission.source_cursor_advances!=emitted.source_cursor_advances ||
       std::any_of(emitted.coordinate_words.begin(),emitted.coordinate_words.end(),
                   [](std::uint32_t value){return value>65535U;}))
      fail("Main glyph packet differs from its executed source arguments");
    for(std::uint32_t yy=0U;yy<h;++yy)for(std::uint32_t xx=0U;xx<w;++xx) {
      const auto dx=std::int64_t(std::bit_cast<std::int32_t>(x))+xx;
      const auto dy=std::int64_t(std::bit_cast<std::int32_t>(y))+yy;
      if(dx<0 || dy<0 || dx>=width || dy>=height)continue;
      const auto index=std::to_integer<std::uint8_t>(font.indices[(v+yy)*256U+u+xx]);
      const auto palette=std::size_t(psmt8_clut_storage_index_v1(index))*4U;
      std::array<unsigned,4U> rgba;
      for(unsigned channel=0U;channel<4U;++channel)
        rgba[channel]=std::min(255U,(std::to_integer<unsigned>(font.raw_palette[palette+channel])*
            unsigned((q.rgbaq>>(8U*channel))&255U))>>7U);
      //21c258 TEST=2004b: GEQUAL4, failed fragmentsKEEP; ALPHA=44.
      if(rgba[3U]<4U)continue;
      if(rgba[3U]>128U)fail("Main glyph reaches unqualified superunit coverage");
      const auto at=(std::size_t(dy)*width+static_cast<std::size_t>(dx))*4U;
      for(unsigned channel=0U;channel<3U;++channel)
        image.rgb_coverage[at+channel]=static_cast<std::byte>(blend(
            static_cast<int>(rgba[channel]),std::to_integer<int>(image.rgb_coverage[at+channel]),
            static_cast<int>(rgba[3U])));
    }
  }
  return image;
}

ScreenOverlayV1 compile_rac_frontend_main_lists_v1(
    const RacFrontendMainAssetsV1 &assets,const std::array<RacFrontendProjectedBoundsV1,3U> &bounds,
    const std::array<std::int16_t,3U> &ages,std::uint32_t scale,std::uint32_t cadence,
    RacFrontendMainCompileLimitsV1 limits) {
  if(scale!=0x3f555555U || cadence!=50U)fail("Main raster currently binds the original PAL viewport");
  ScreenOverlayV1 out;out.canvas_width=512U;out.canvas_height=448U;
  out.coverage_denominator=128U;out.updates_per_second=cadence;
  out.frames.emplace_back();
  std::vector<RacFrontendTextureEntryV1> entries;
  for(const auto &texture:assets.textures.textures)entries.push_back(texture.entry);
  for(std::size_t node=0U;node<assets.nodes.size();++node) {
    const auto &source=assets.nodes[node];const auto &b=bounds[node].width_height_x_y;
    if(source.rows.size()!=2U || source.rows[0U].text_key==0 || source.rows[1U].text_key!=0 ||
       source.owner_flags!=0U || source.selected_row!=0U)
      fail("Main node leaves the reached original list domain");
    auto rows=source.rows;rows[0U].color_age=ages[node];
    RacFrontendListDrawInputsV1 input;
    input.width_word=b[0U];input.height_word=b[1U];input.flags=source.text_flags;
    input.selected_row=source.selected_row;input.focused=source.source_reference==assets.focused_node;
    input.rows=rows;input.text_bank=&assets.text;input.font_metrics=&assets.metrics;
    input.missing_text=assets.missing_text;input.shadow_x_y_words=assets.shadow_x_y;
    input.screen_offset_y_x_words={(2048U-target_extent(b[1U])/2U)*16U,
                                  (2048U-target_extent(b[0U])/2U)*16U};
    input.timer_argument_word=assets.timer_argument;input.time_scale_bits=scale;
    input.textures=entries;
    // This is a compiler relocation of the already owned original payload;
    // bind/upload arithmetic is executed, no dynamic game allocation claimed.
    input.texture_payload={0x400000U,static_cast<std::uint32_t>(assets.textures.payload_range.size)};
    input.allocator_begin=0x100000U;
    input.max_total_draws=8192U;
    const auto draw=execute_rac_frontend_list_draw_v1(input);
    // Every reached main label is ordinary source text. Its palette input is
    // observationally irrelevant; reject a new inline-color dependency.
    for(const auto &call:draw.glyph_calls)
      if(std::any_of(call.text.begin(),call.text.end(),[](std::byte byte) {
          const auto value=std::to_integer<unsigned>(byte);return value>=8U && value<16U;
        }))fail("Main source text acquired an unqualified incoming palette dependency");
    if(draw.font_source_id>=assets.textures.textures.size())fail("Main source font binding is absent");
    out.images.push_back(compile_rac_frontend_list_rtt_v1(draw,
        assets.textures.textures[draw.font_source_id],b[0U],b[1U],assets.clear_rgba,limits.overlay));
    out.frames[0U].draws.push_back({static_cast<std::uint32_t>(node),
        std::bit_cast<std::int32_t>(b[2U]+1U),std::bit_cast<std::int32_t>(b[3U]+1U)});
  }
  validate_screen_overlay_v1(out,limits.overlay);
  return out;
}

std::array<RacFrontendMainGeometryFrameV1,13U> compile_rac_frontend_main_entry_geometry_v1(
    std::span<const std::byte> bytes,const RacFrontendMainAssetsV1 &assets) {
  if(!assets.initial_visibility)fail("Main entry has no source-owned initial visibility");
  const auto source=decode_rac_frontend_object_class_v1(bytes,0x400000U,4U,0U,1048576U);
  const auto model=parse_rac_moby_class_v1(bytes,{1048576U,false});
  const auto rig=decode_rac_moby_bind_rig_v1(bytes,model,{1048576U,255U,1.e-8});
  std::array<RacRatchetSequenceV1,14U> sequences;
  for(std::size_t i=0U;i<sequences.size();++i) {
    const auto selected=assets.screen.sequences[i];
    if(selected>=model.sequence_offsets.size())fail("Main source object sequence is absent");
    const auto begin=model.sequence_offsets[selected];
    auto end=std::uint64_t(bytes.size());
    for(const auto offset:model.sequence_offsets)if(offset>begin)end=std::min(end,std::uint64_t(offset));
    sequences[i]=parse_rac_moby_sequence_v1(bytes,{begin,end-begin},{1048576U,1048576U,255U,255U});
  }
  using namespace game;
  constexpr SessionStateLimitsV1 state_limits{8U,16U,64U,4096U,65536U,262144U,65536U,131072U,128U};
  SessionStateInitialV1 initial;
  initial.schema.identity_key="frontend.compiler.main-object-relocations";
  initial.schema.buffers={{"status",15U},{"packed",120U},{"pvar",1920U},{"scalars",8U}};
  initial.schema.views={{"status","status",SessionStateValueTypeV1::u8,0U,15U,1U},
      {"packed","packed",SessionStateValueTypeV1::u32,0U,30U,4U},
      {"pvar","pvar",SessionStateValueTypeV1::u32,0U,480U,4U},
      {"scalars","scalars",SessionStateValueTypeV1::u32,0U,2U,4U}};
  initial.buffers={{"status",std::vector<std::byte>(15U)},
      {"packed",std::vector<std::byte>(120U)},{"pvar",std::vector<std::byte>(1920U)},
      {"scalars",std::vector<std::byte>(8U)}};
  initial.buffers[0U].bytes[0U]=std::byte{255};
  initial.buffers[3U].bytes[4U]=std::byte{14};
  RacMobyAllocateBindingsV1 allocation;
  allocation.state_schema_sha256=hash_session_state_schema_v1(initial.schema,state_limits);
  allocation.cursor={0x500000U,0x500e00U};
  allocation.first_status=PlacementStateElementV1{"status",0U};
  allocation.first_packed_word=PlacementStateElementV1{"packed",0U};
  allocation.first_pvar_word=PlacementStateElementV1{"pvar",0U};
  allocation.current_timer_word=PlacementStateElementV1{"scalars",0U};
  allocation.current_count_word=PlacementStateElementV1{"scalars",1U};
  allocation.all_actor_base_bits=0x4fff00U;allocation.pvar_base_bits=0x600000U;
  SessionStateV1 state(initial,state_limits);
  const auto projection=execute_rac_frontend_menu_projection_v1();
  const std::array position{projection.camera_position_bits[0U],projection.camera_position_bits[1U],
                            projection.camera_position_bits[2U]};
  auto objects=admit_rac_frontend_objects_v1(source,assets.screen.sequences,position,
      allocation,state,0U,{64U,32U});
  RacFrontendObjectScreenStateV1 screen{2U,assets.screen.source_reference,assets.screen.source_reference,0U,0U};
  std::array<RacFrontendMainGeometryFrameV1,13U> out;
  for(std::size_t tick=0U;tick<out.size();++tick) {
    screen=step_rac_frontend_object_screen_v1(screen,std::span(&assets.screen,1U),objects,source).state;
    auto &frame=out[tick];frame.main_active=screen.current_screen==assets.screen.source_reference;
    for(std::size_t i=0U;i<objects.size();++i) {
      advance_rac_frontend_object_animation_v1(objects[i],source);
      frame.corners[i]=sample_rac_frontend_object_corners_v1(objects[i],source,sequences[i],rig,
          {255U,1048576U,255U,255U,1.e-8});
      update_rac_frontend_object_post_v1(objects[i],source);
      frame.models[i]={objects[i].animation,objects[i].post,
          assets.initial_visibility->enabled_words[i]!=0U &&
          (i!=6U || assets.initial_visibility->slot_six_enabled_word!=0U)};
    }
    for(std::size_t i=0U;i<assets.nodes.size();++i) {
      const auto slot=assets.nodes[i].object_slot;
      if(slot>=objects.size())fail("Main list object slot is absent");
      //21a7b4 passes the PVar first and fourth selector-resolved corners.
      frame.list_bounds[i]=execute_rac_frontend_project_bounds_v1(
          frame.corners[slot][0U],frame.corners[slot][3U],projection);
    }
  }
  if(!out.back().main_active ||
     std::any_of(out.begin(),out.end()-1,[](const auto &frame){return frame.main_active;}))
    fail("Main object entry did not execute its original12-update transition");
  return out;
}
} // namespace openrc
