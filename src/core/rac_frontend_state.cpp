#include "openrc/rac_frontend_state.hpp"

#include "openrc/elf.hpp"
#include "openrc/hash.hpp"
#include "openrc/rac_frontend_new_game.hpp"

#include <algorithm>
#include <fstream>

namespace openrc {
namespace {
[[noreturn]] void fail(const char *s){throw RacFrontendInputError(s);}
std::uint32_t word(std::span<const std::byte> bytes,std::size_t at=0) {
  if(at>bytes.size()||bytes.size()-at<4)fail("Truncated frontend state source word");
  std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(bytes[at+i])<<(8*i);
  return value;
}
void store_word(std::vector<std::byte> &bytes,std::uint32_t value) {
  if(bytes.size()!=4)fail("Frontend pointer field has wrong width");
  for(unsigned i=0;i<4;++i)bytes[i]=static_cast<std::byte>(value>>(8*i));
}
void read_file(std::ifstream &file,std::uint64_t at,std::span<std::byte> bytes) {
  file.seekg(static_cast<std::streamoff>(at));file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  if(!file)fail("Truncated frontend state source image extent");
}
struct ResetDescriptor {std::uint32_t address=0,bytes=0,tag=0;bool primary=false;};
} // namespace

RacFrontendStateCompilationV1 compile_rac_frontend_state_v1(
    const std::filesystem::path &image,std::span<const std::byte> elf,
    const RacNewGamePresentationInputsV1 &presentation,const RacNewGameFlowResourcesV1 &resources) {
  if(elf.empty()||elf.size()>32U*1024U*1024U)fail("Frontend state ELF exceeds bound");
  if(!((presentation.video_selector_15ee80==1&&presentation.updates_per_second==50&&presentation.time_scale_bits==0x3f555555U)||
       (presentation.video_selector_15ee80==0&&presentation.updates_per_second==60&&presentation.time_scale_bits==0x3f800000U)))
    fail("Frontend startup display selector and prepared cadence disagree");
  RacFrontendStateCompilationV1 out;out.source_elf_sha256=prepared_content_sha256_v1(elf);
  if(hex_digest(out.source_elf_sha256)!="17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b")
    fail("Frontend state ELF differs from recovered owner contract");
  const auto report=inspect_elf(elf);
  const auto source_bytes=[&](std::uint32_t address,std::uint32_t count,bool file_only=false) {
    if(!count||count>65536U)fail("Frontend source field exceeds its bounded owner");
    const ElfProgramHeader *owner=nullptr;
    for(const auto &segment:report.program_headers)if(segment.type==1U&&address>=segment.virtual_address&&
        std::uint64_t(address)-segment.virtual_address+count<=(file_only?segment.file_size:segment.memory_size)) {
      if(owner)fail("Frontend source field has ambiguous ELF owners");
      owner=&segment;
    }
    if(!owner)fail("Frontend source field has no declared ELF owner");
    std::vector<std::byte> bytes(count,std::byte{0});
    const auto relative=std::uint64_t(address)-owner->virtual_address;
    if(relative<owner->file_size) {
      const auto copied=std::min<std::uint64_t>(count,owner->file_size-relative);
      const auto offset=std::uint64_t(owner->file_offset)+relative;
      if(offset>elf.size()||copied>elf.size()-offset)fail("Frontend field leaves actual ELF file");
      std::copy_n(elf.begin()+static_cast<std::ptrdiff_t>(offset),static_cast<std::size_t>(copied),bytes.begin());
    }
    return bytes;
  };
  std::ifstream disc(image,std::ios::binary);if(!disc)fail("Cannot open frontend state source image");
  // 201f64 reads original disc sector289;201f74..90 compares byte51 to'N'.
  // Resolve that actual startup input here rather than treating an ELF zero
  // or a caller-selected presentation default as the live display choice.
  std::array<std::byte,1> region{};read_file(disc,289ULL*2048U+51U,region);
  const auto startup_selector=region[0]==std::byte{'N'}?0U:1U;
  if(startup_selector!=presentation.video_selector_15ee80)
    fail("Frontend source startup region differs from the prepared display selector");
  std::array<std::byte,0x2960> toc{};read_file(disc,1500ULL*2048U,toc);
  if(hex_digest(prepared_content_sha256_v1(toc))!="10b5950d0c5c4271f40640f5ae1ce7750bfcdc942459814a6f2865be7ba252c4")
    fail("Frontend reset TOC differs from original catalog");
  const auto lba=word(toc,0x10),sectors=word(toc,0x14);
  if(!lba||!sectors||sectors>512)fail("Frontend reset source extent exceeds bound");
  std::vector<std::byte> envelope(std::size_t(sectors)*2048U);read_file(disc,std::uint64_t(lba)*2048U,envelope);
  out.source_template_sha256=prepared_content_sha256_v1(envelope);
  const auto primary=source_bytes(0x1a05c0,768,true),repeated=source_bytes(0x1a08c0,192,true);
  out.reset_template=compile_rac_frontend_reset_template_v1(envelope,primary,repeated,{1048576,10000,20,267,1048576});
  std::vector<ResetDescriptor> descriptors;
  for(const auto &table:std::array<std::pair<std::span<const std::byte>,bool>,2>{{{primary,true},{repeated,false}}})
    for(std::size_t at=0;at+16<table.first.size();at+=16)
      descriptors.push_back({word(table.first,at),word(table.first,at+4),word(table.first,at+8),table.second});
  out.state_limits=rac_frontend_state_limits_v1();
  out.initial.schema.identity_key="openrc.frontend-session/v1";
  const auto add_owner=[&](std::uint32_t source,std::uint32_t count,const std::string &key,std::vector<std::byte> bytes) {
    if(bytes.size()!=count)fail("Frontend state owner has incomplete initial bytes");
    for(const auto &b:out.source_bindings)if(std::uint64_t(source)<std::uint64_t(b.source_begin)+b.byte_count&&
        std::uint64_t(b.source_begin)<std::uint64_t(source)+count)
      fail("Frontend named source owners overlap");
    out.initial.schema.buffers.push_back({key,count});
    out.initial.schema.views.push_back({key+"/bytes",key,SessionStateValueTypeV1::u8,0,count,1});
    out.initial.buffers.push_back({key,std::move(bytes)});
    out.source_bindings.push_back({source,count,key+"/bytes",0});
  };
  const auto alias=[&](const std::string &key,const std::string &buffer,SessionStateValueTypeV1 type,
      std::uint64_t offset,std::uint64_t count,std::uint64_t stride) {
    out.initial.schema.views.push_back({key,buffer,type,offset,count,stride});
  };
  for(const auto &copy:out.reset_template.copies) {
    const ResetDescriptor *descriptor=nullptr;std::uint32_t row=0;
    for(const auto &d:descriptors)if(copy.source_address>=d.address&&copy.bytes.size()==d.bytes&&
        (copy.source_address-d.address)%d.bytes==0&&
        (copy.source_address-d.address)/d.bytes<(d.primary?1U:20U)) {
      if(descriptor)fail("Frontend reset copy has ambiguous descriptor owner");
      descriptor=&d;row=(copy.source_address-d.address)/d.bytes;
    }
    if(!descriptor)fail("Frontend reset copy lost its tag/row owner");
    const auto tag=descriptor->tag;
    auto key=descriptor->primary?"progress/primary/field-"+std::to_string(tag):
        "progress/level/"+std::to_string(row)+"/field-"+std::to_string(tag);
    if(descriptor->primary&&tag==0)key="rac1.progress/encoded-level";
    add_owner(copy.source_address,static_cast<std::uint32_t>(copy.bytes.size()),key,
        source_bytes(copy.source_address,static_cast<std::uint32_t>(copy.bytes.size())));
    if(descriptor->primary&&tag==0)alias("rac1.progress/encoded-source-level",key,SessionStateValueTypeV1::u32,0,1,4);
    if(descriptor->primary&&tag==14) {
      alias("loading/selector-first-unlocked/bytes",key,SessionStateValueTypeV1::u8,8,1,1);
      alias("loading/selector-second-unlocked/bytes",key,SessionStateValueTypeV1::u8,14,1,1);
    }
    if(!descriptor->primary) {
      const auto prefix="rac1.progress/level/"+std::to_string(row)+"/";
      if(tag==0xbbc)alias(prefix+"selectors",key,SessionStateValueTypeV1::u8,0,16,1);
      if(tag==0xbbd)alias(prefix+"primary-words",key,SessionStateValueTypeV1::u32,0,64,4);
      if(tag==0xbbe) {
        alias(prefix+"registration-keys",key,SessionStateValueTypeV1::u16,0,64,4);
        alias(prefix+"registration-auxiliary",key,SessionStateValueTypeV1::u16,2,64,4);
        alias(prefix+"registration-words",key,SessionStateValueTypeV1::u32,0,64,4);
      }
    }
  }
  struct Field {std::uint32_t address,bytes;const char *key;};
  constexpr std::array fields{
      Field{0x1d5118,4,"frontend/no-save/node-flags"},Field{0x1d5134,4,"frontend/no-save/node-phase"},
      Field{0x1d5128,4,"frontend/no-save/node-selection"},Field{0x1d50fc,4,"frontend/no-save/sound-object"},
      Field{0x1d6040,4,"frontend/previous-screen"},Field{0x1d5040,4,"frontend/no-save/parent-screen"},
      Field{0x1d6094,4,"frontend/cancel-guard"},Field{0x1d6098,4,"frontend/pending-save"},
      Field{0x1d60c4,4,"frontend/readiness"},Field{0x15efb0,4,"frontend/card-mode"},
      Field{0x13d46c,4,"frontend/card-status"},Field{0x13d474,4,"frontend/card-result"},
      Field{0x13d398,4,"frontend/card-type"},Field{0x13cc04,4,"input/global-pressed"},
      Field{0x13cbe4,4,"input/pressed"},Field{0x13cbf4,4,"input/repeated"},
      Field{0x15ef34,4,"frontend/saved-selection"},Field{0x15efb4,4,"frontend/flags"},
      Field{0x1d5f78,4,"frontend/requested-screen"},Field{0x13d48c,4,"frontend/save-requested"},
      Field{0x1d5f74,4,"frontend/current-screen"},Field{0x1d5f84,4,"frontend/transition-remaining"},
      Field{0x15f6e4,4,"session/target-level"},Field{0x15f6fc,4,"session/level-change-requested"},
      Field{0x15f690,4,"session/transition-requested"},Field{0x13e15a,2,"session/entry-requested"},
      Field{0x15ef48,2,"session/load-count-a"},Field{0x15ef4a,2,"session/load-count-b"},
      Field{0x15f6e8,4,"frontend/mode"},Field{0x1d5f70,4,"frontend/root-phase"},
      Field{0x15ee80,4,"display/video-selector"},Field{0x16044c,1,"display/saved-video-selector"},
      Field{0x13e156,2,"loading/selector"},Field{0x194210,4,"collision/query-flags"},
      Field{0x13e6ac,4,"audio/group5-volume"},Field{0x13e6b4,4,"audio/reverb-depth"},
      Field{0x13e6b8,1,"audio/reverb-mode"},Field{0x13e6b9,1,"audio/reverb-delay"},
      Field{0x13e6ba,1,"audio/reverb-feedback"},Field{0x13e6bb,1,"audio/reverb-flags"},
      Field{0x15f058,4,"frontend/title/counter"},Field{0x15f050,4,"frontend/title/logo-alpha"},
      Field{0x15f054,4,"frontend/title/prompt-alpha"},
      Field{0x1d6080,4,"frontend/root-entry-counter"},Field{0x1d5f7c,4,"frontend/root-age"},
      Field{0x1d5f80,4,"frontend/root-fade"},Field{0x1d60c0,4,"frontend/root-dialog-result"},
      Field{0x193430,4,"frontend/dialog/auxiliary"},Field{0x193418,4,"frontend/dialog/target-screen"},
      Field{0x193414,4,"frontend/dialog/previous-mode"},Field{0x193400,4,"frontend/dialog/kind"},
      Field{0x19342c,4,"frontend/dialog/draw-delay"},Field{0x193420,4,"frontend/dialog/age"},
      Field{0x193404,4,"frontend/dialog/duration"},Field{0x193424,4,"frontend/dialog/fade"},
      Field{0x13d440,4,"frontend/card-busy"},Field{0x15f6c8,4,"frontend/new-game-context"},
      Field{0x15f6cc,4,"frontend/existing-game-context"},Field{0x1d60b8,4,"frontend/root-cooldown"},
      Field{0x1d4a48,4,"frontend/main/node-flags"},Field{0x1d4980,4,"frontend/main/parent-screen"},
      Field{0x1d49d4,4,"frontend/main/target-screen"},Field{0x1d4a2c,4,"frontend/main/sound-object"},
      Field{0x13d464,4,"frontend/card/sync-pending"},Field{0x13d450,4,"frontend/card/io-command"},
      Field{0x13d454,4,"frontend/card/io-result"},Field{0x13d45c,4,"frontend/card/index"},
      Field{0x13d390,4,"frontend/card/port"},Field{0x13d394,4,"frontend/card/slot"},
      Field{0x13d39c,4,"frontend/card/free-blocks"},Field{0x13d3a0,4,"frontend/card/formatted"},
      Field{0x13d3a4,4,"frontend/card/slot-error"},Field{0x13d3ac,4,"frontend/card/slot-result"},
      Field{0x13d444,4,"frontend/card/slot-change"},Field{0x13d43c,4,"frontend/card/slot-scan"},
      Field{0x13d478,4,"frontend/card/request-argument"},Field{0x13d47c,4,"frontend/card/error"},
      Field{0x15ff4c,4,"frontend/card/sticky-result"},Field{0x161380,4,"frontend/card/mode-age"},
      Field{0x13d420,4,"frontend/card/slot-summary"},Field{0x13d3b0,4,"frontend/card/entry-0"},
      Field{0x13d3cc,4,"frontend/card/entry-1"},Field{0x13d3e8,4,"frontend/card/entry-2"},
      Field{0x13d404,4,"frontend/card/entry-3"}};
  // These source references are compiler-only. Zero is the absent neutral
  // handle. Runtime transitions compare/use the small stable tokens only.
  constexpr std::array<std::pair<std::uint32_t,std::uint32_t>,4> screens{{
      {0x1d4948,1},{0x1d5008,2},{0x1d51b8,3},{0x1d5318,4}}};
  const auto screen_token=[&](std::uint32_t source) {
    if(!source)return 0U;
    for(const auto &[address,token]:screens)if(address==source)return token;
    fail("Frontend initial screen pointer has no neutral handle");
  };
  for(const auto &f:fields) {
    auto bytes=source_bytes(f.address,f.bytes);
    if(f.address==0x1d6040||f.address==0x1d5040||f.address==0x1d5f78||f.address==0x1d5f74||f.address==0x193418||
        f.address==0x1d4980||f.address==0x1d49d4)
      store_word(bytes,screen_token(word(bytes)));
    if((f.address==0x1d50fc||f.address==0x1d4a2c)&&word(bytes)!=0)fail("Frontend initial sound object requires allocator ownership");
    add_owner(f.address,f.bytes,f.key,std::move(bytes));
  }
  // Canonical storage reused by the first level's admission and save owners.
  // These are the actual boot images, not a ready-level initialization. The
  // prepared installation replaces them only after level I/O and cleanup.
  add_owner(0x15fd47U,17U,"rac1.level/selector-cache",source_bytes(0x15fd47U,17U));
  alias("rac1.level/selector-cache/selectors","rac1.level/selector-cache",SessionStateValueTypeV1::u8,1U,16U,1U);
  add_owner(0x1ba860U,256U,"rac1.level/alternate-bits",source_bytes(0x1ba860U,256U));
  alias("rac1.level/alternate-bits/words","rac1.level/alternate-bits",SessionStateValueTypeV1::u32,0U,64U,4U);
  add_owner(0x1bb5c0U,3168U,"rac1.level/saved-state",source_bytes(0x1bb5c0U,3168U));
  alias("rac1.level/saved-state/suppression","rac1.level/saved-state",SessionStateValueTypeV1::u8,0x454U,2047U,1U);
  const auto add_derived=[&](const std::string &key,std::uint32_t value,unsigned n) {
    out.initial.schema.buffers.push_back({key,n});
    out.initial.schema.views.push_back({key+"/bytes",key,SessionStateValueTypeV1::u8,0,n,1});
    std::vector<std::byte> bytes(n);for(unsigned i=0;i<n;++i)bytes[i]=static_cast<std::byte>(value>>(8*i));
    out.initial.buffers.push_back({key,std::move(bytes)});
  };
  const auto current=word(source_bytes(0x1d5f74,4));
  const auto focus=current?word(source_bytes(current+0x40,4))==0x1d50e8: false;
  const auto previous=word(source_bytes(0x1d6040,4));
  add_derived("frontend/no-save/focused",focus?1U:0U,1);
  add_derived("frontend/main/focused",current==0x1d4948&&word(source_bytes(current+0x40,4))==0x1d4a18?1U:0U,1);
  add_derived("frontend/previous-result",previous?word(source_bytes(previous+0x84,4)):0U,4);
  add_derived("frontend/config/dialog-duration",static_cast<std::uint32_t>(evaluate_rac_frontend_timer_v1(30,presentation.time_scale_bits)),4);
  add_derived("frontend/config/dialog-delay",static_cast<std::uint32_t>(evaluate_rac_frontend_timer_v1(10,presentation.time_scale_bits)),4);
  add_derived("frontend/config/title-fade-counter",static_cast<std::uint32_t>(evaluate_rac_frontend_timer_v1(60,presentation.time_scale_bits)),4);
  // Source startup selects the region display and later saves the live byte.
  // Keep the initial ELF owners distinct from this admitted platform input.
  add_derived("frontend/config/startup-video-selector",startup_selector,4);
  out.initial=canonicalize_session_state_initial_v1(out.initial,out.state_limits);
  const RacFrontendNoSaveCompileBindingsV1 input{0x1d50e8,0x1d5008,
      {"frontend/no-save/focused/bytes",0},{"frontend/previous-result/bytes",0},{3,4}};
  out.no_save=compile_rac_frontend_no_save_plan_v1(out.reset_template,input,out.source_bindings,
      out.initial.schema,out.state_limits);
  out.new_game_continuation=*compile_rac_new_game_continuation_v1(presentation,resources,out.source_bindings,
      out.initial.schema,out.state_limits).sequence;
  const auto package=[&](const char *id,const char *type,std::vector<std::byte> bytes) {
    LevelPackageResourceV1 resource;resource.resource_id=id;resource.type_id=type;resource.schema_version=1;
    resource.payload=std::move(bytes);resource.payload_sha256=prepared_content_sha256_v1(resource.payload);
    out.resources.push_back(std::move(resource));
  };
  package("frontend/session-state","openrc.session-state",encode_session_state_initial_v1(out.initial,{4U*1024U*1024U,out.state_limits}));
  package("frontend/no-save-input","openrc.frontend-no-save-input",encode_frontend_no_save_plan_v1(out.no_save));
  package("frontend/new-game-sequence","openrc.frontend-sequence",encode_frontend_sequence_v1(out.new_game_continuation));
  return out;
}
} // namespace openrc
