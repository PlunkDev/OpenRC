#include "openrc/rac_new_game_resources.hpp"
#include "openrc/rac_startup.hpp"
#include "openrc/rac_frontend_new_game.hpp"
#include "openrc/hash.hpp"

#include <algorithm>
#include <fstream>
#include <map>

namespace openrc {
namespace {
[[noreturn]] void fail(const char *s){throw RacFrontendInputError(s);}
ScreenOverlayImageV1 periodic_crop(const ScreenOverlayImageV1 &full) {
  if(full.width!=512U||full.height!=64U||full.rgb_coverage.size()!=512U*64U*4U)fail("Loading background raster dimensions differ");
  ScreenOverlayImageV1 crop;crop.width=128;crop.height=64;crop.rgb_coverage.resize(128U*64U*4U);
  for(unsigned y=0;y<64;++y)for(unsigned x=0;x<512;++x)for(unsigned c=0;c<4;++c) {
    const auto value=full.rgb_coverage[(y*512U+x)*4U+c];
    if(value!=full.rgb_coverage[(y*512U+x%128U)*4U+c])fail("Loading tile raster does not have its exact source repeat period");
    if(x<128U)crop.rgb_coverage[(y*128U+x)*4U+c]=value;
  }
  return crop;
}
std::uint32_t word(std::span<const std::byte> b,std::size_t at) {
  if(at>b.size()||b.size()-at<4)fail("Truncated New Game source record");
  std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::to_integer<std::uint32_t>(b[at+i])<<(8*i);return v;
}
void read(std::ifstream &file,std::uint64_t at,std::span<std::byte> bytes) {
  file.seekg(static_cast<std::streamoff>(at));file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  if(!file)fail("New Game source range is truncated");
}
}
LoadingPresentationV1 compile_rac_loading_presentation_v1(const RacFrontendLoadingAssetsV1 &assets,
    std::uint32_t first,std::uint32_t second,std::uint32_t cadence,std::uint32_t origin,LoadingPresentationLimitsV1 limits) {
  if((cadence!=50U&&cadence!=60U)||origin!=224U||first>16U||second>16U)
    fail("Loading compiler leaves its qualified display/card domain");
  if(assets.cards[0].entry.source_index!=first+1U||assets.cards[1].entry.source_index!=second+1U)
    fail("Loading compiler assets are not the selected original card pair");
  // Preflight the largest reached library:600 independent128x64 rasters,
  // two labels,600 frame draw lists and outer metadata.
  constexpr std::uint64_t maximum_bytes=600U*(128U*64U*4U+8U+4U+4U*12U)+2U*(512U*64U*4U+8U)+512U;
  if(maximum_bytes>limits.overlay.max_bytes||limits.overlay.max_images<602U||
      limits.overlay.max_frames<600U||limits.overlay.max_draws<2400U||limits.overlay.max_dimension<512U||limits.max_bands<2U)
    fail("Loading compiler output limits cannot admit its source period");
  LoadingPresentationV1 p;p.fade_in_updates=32;p.fade_out_updates=16;
  auto &library=p.library;library.canvas_width=512;library.canvas_height=448;library.updates_per_second=cadence;
  library.coverage_denominator=128;library.loop_begin=0;
  std::map<PreparedContentDigestV1,std::uint32_t> hashes;
  for(unsigned frame=0;frame<600U;++frame) {
    const RacFrontendLoadingCardStateV1 state{first,second,frame,1048576U,false,true};
    auto draw=execute_rac_frontend_loading_draws_v1(state,origin).front();
    if(!draw.uses_stq||draw.texture_slot!=0)fail("Loading draw owner did not produce its first background");
    draw.rgba=0x80808080U;auto image=periodic_crop(rasterize_rac_frontend_loading_band_v1(assets,draw));
    const auto digest=prepared_content_sha256_v1(image.rgb_coverage);const auto found=hashes.find(digest);std::uint32_t id;
    if(found==hashes.end()) {id=static_cast<std::uint32_t>(library.images.size());hashes.emplace(digest,id);library.images.push_back(std::move(image));}
    else {id=found->second;if(library.images[id]!=image)fail("Loading raster digest collision");}
    ScreenOverlayFrameV1 f;for(unsigned repeat=0;repeat<4;++repeat)f.draws.push_back({id,static_cast<std::int32_t>(128U*repeat),0});
    library.frames.push_back(std::move(f));
  }
  for(unsigned band=0;band<(first==second?1U:2U);++band) {
    const RacFrontendLoadingCardStateV1 state{first,second,band?65U:0U,1048576U,false,true};
    const auto draws=execute_rac_frontend_loading_draws_v1(state,origin);
    const auto draw=std::find_if(draws.begin(),draws.end(),[&](const auto &d){return d.texture_slot==band+1U;});
    if(draw==draws.end())fail("Loading draw owner omitted its selected label");
    auto label=rasterize_rac_frontend_loading_band_v1(assets,*draw);const auto id=static_cast<std::uint32_t>(library.images.size());
    library.images.push_back(std::move(label));
    p.bands.push_back({id,static_cast<std::int32_t>(draw->rectangle_words[0]),static_cast<std::int32_t>(draw->rectangle_words[1]),
        band?65U:0U,band?64U:0U,band?96U:0U});
  }
  validate_loading_presentation_v1(p,limits);return p;
}
RacNewGamePreparedResourcesV1 compile_rac_new_game_resources_v1(const std::filesystem::path &image,
    std::span<const std::byte> elf,std::uint32_t language,std::uint32_t selector,std::uint32_t cadence,RacNewGameResourceLimitsV1 limits) {
  if(!limits.max_elf_bytes||elf.empty()||elf.size()>limits.max_elf_bytes||
      (language!=0U&&language!=2U&&language!=3U&&language!=4U&&language!=5U)||
      (cadence!=50U&&cadence!=60U)||!limits.max_total_source_movie_bytes||!limits.max_total_payload_bytes)
    fail("Invalid bounded New Game source preparation inputs");
  RacNewGamePreparedResourcesV1 out;out.source_elf_sha256=prepared_content_sha256_v1(elf);
  if(hex_digest(out.source_elf_sha256)!="17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b")
    fail("New Game source ELF differs from the executed source contract");
  std::ifstream source(image,std::ios::binary);if(!source)fail("Cannot open New Game source image");
  std::array<std::byte,0x2960> toc{};read(source,std::uint64_t(1500)*2048U,toc);
  out.source_toc_sha256=prepared_content_sha256_v1(toc);
  if(hex_digest(out.source_toc_sha256)!="10b5950d0c5c4271f40640f5ae1ce7750bfcdc942459814a6f2865be7ba252c4")
    fail("New Game TOC differs from the original selected catalog");
  const auto card_slot=language?language-1U:0U;
  auto wad=read_rac_startup_wad_v1(image,0x1388U+8U*card_slot,limits.max_wad_source_bytes,limits.max_wad_decoded_bytes);
  out.source_loading_wad_sha256=prepared_content_sha256_v1(wad.source_bytes);
  std::uint64_t source_total=0,payload_total=0;
  auto admit=[&](LevelPackageResourceV1 r,bool movie,unsigned ordinal) {
    if(r.payload.size()>limits.max_total_payload_bytes-payload_total)fail("New Game neutral payloads exceed aggregate limit");
    payload_total+=r.payload.size();r.schema_version=1;r.payload_sha256=prepared_content_sha256_v1(r.payload);
    r.provenance.push_back({LevelPackageProvenanceKindV1::generated,"compiler/rac-new-game-resources-v1",0,0,{}});
    const FrontendSequenceResourceV1 binding{r.resource_id,r.type_id,r.payload_sha256};
    if(movie)out.sequence_bindings.movies[ordinal]=binding;else out.sequence_bindings.loading_cards[ordinal]=binding;
    out.resources.push_back(std::move(r));
  };
  const RacFrontendTextureLimitsV1 texture_limits{limits.max_wad_decoded_bytes,3,512,64,32768,69632,354304};
  // Validate all three movie extents and the total before allocating any film.
  std::array<RacStartupMovieV1,3> movies;std::array<std::uint64_t,3> ends{};const auto image_bytes=std::filesystem::file_size(image);
  for(unsigned i=0;i<3;++i) {
    auto &m=movies[i];m.source_toc_offset=(selector?0x1998U:0x1938U)+8U*i;
    m.source_call_pc=std::array<std::uint32_t,3>{0x23359c,0x2335c4,0x2335f8}[i];
    m.extent={word(toc,m.source_toc_offset),word(toc,m.source_toc_offset+4U)};m.source_byte_offset=std::uint64_t(m.extent.lba)*2048U;
    const auto occupied=(std::uint64_t(m.extent.byte_size)+2047U)/2048U*2048U;
    if(m.extent.lba<1506U||m.extent.byte_size<4U||m.extent.byte_size>limits.media.max_bytes||
        m.extent.byte_size>limits.max_total_source_movie_bytes-source_total||m.source_byte_offset>image_bytes||occupied>image_bytes-m.source_byte_offset)
      fail("New Game source movies exceed their admitted extents or aggregate limit");
    source_total+=m.extent.byte_size;ends[i]=m.source_byte_offset+occupied;
    for(unsigned j=0;j<i;++j)if(m.source_byte_offset<ends[j]&&movies[j].source_byte_offset<ends[i])fail("New Game source movie extents overlap");
  }
  for(unsigned i=0;i<3;++i) {
    const auto first=i==0U?0U:i==1U?2U:3U,second=i==0U?1U:i==1U?2U:4U;
    const auto assets=parse_rac_frontend_loading_assets_v1(wad.decoded_bytes,first,second,texture_limits);
    auto loading_limits=limits.loading;
    loading_limits.overlay.max_bytes=std::min(loading_limits.overlay.max_bytes,limits.max_total_payload_bytes-payload_total);
    const auto presentation=compile_rac_loading_presentation_v1(assets,first,second,cadence,224,loading_limits);
    LevelPackageResourceV1 card;card.resource_id="new-game/loading-"+std::to_string(i);card.type_id="openrc.loading-presentation";
    card.payload=encode_loading_presentation_v1(presentation,loading_limits);
    card.provenance.push_back({LevelPackageProvenanceKindV1::iso_range,"disc/loading-cards",wad.source_byte_offset,wad.source_bytes.size(),out.source_loading_wad_sha256});
    admit(std::move(card),false,i);
    auto media_limits=limits.media;media_limits.max_bytes=std::min(media_limits.max_bytes,limits.max_total_payload_bytes-payload_total);
    auto bytes=read_rac_startup_movie_v1(image,movies[i],media_limits.max_bytes);out.source_movie_sha256[i]=prepared_content_sha256_v1(bytes);
    auto media=compile_rac_pss_v1(bytes,4,3,media_limits,language);
    LevelPackageResourceV1 film;film.resource_id="new-game/movie-"+std::to_string(i);film.type_id="openrc.media-clip";
    film.payload=encode_media_clip_v1(media,media_limits);
    film.provenance.push_back({LevelPackageProvenanceKindV1::iso_range,"disc/new-game-movies",movies[i].source_byte_offset,bytes.size(),out.source_movie_sha256[i]});
    admit(std::move(film),true,i);
  }
  const auto initial_fade=static_cast<std::uint32_t>(evaluate_rac_frontend_timer_v1(6U,cadence==50U?0x3f555555U:0x3f800000U));
  const std::array durations{initial_fade,2U,4U};
  for(unsigned i=0;i<durations.size();++i) {
    LevelPackageResourceV1 fade;fade.resource_id="new-game/fade-"+std::to_string(durations[i]);
    fade.type_id="openrc.frame-color-transfers";fade.schema_version=1;
    fade.payload=encode_frame_color_transfer_sequence_v1(compile_rac_frontend_fade_v1(durations[i],cadence));
    if(fade.payload.size()>limits.max_total_payload_bytes-payload_total)fail("New Game feedback payloads exceed aggregate limit");
    payload_total+=fade.payload.size();fade.payload_sha256=prepared_content_sha256_v1(fade.payload);
    fade.provenance.push_back({LevelPackageProvenanceKindV1::generated,"compiler/rac-new-game-resources-v1",0,0,{}});
    out.sequence_bindings.fades[i]={fade.resource_id,fade.type_id,fade.payload_sha256};out.resources.push_back(std::move(fade));
  }
  return out;
}
} // namespace openrc
