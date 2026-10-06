#include "openrc/rac_frontend_decoration.hpp"

#include "openrc/ee_cop1_numeric.hpp"

#include <bit>

namespace openrc {
namespace {
using U32=std::uint32_t;
std::int32_t signed_word(U32 value) {return std::bit_cast<std::int32_t>(value);}
U32 random_bounded(RacFrontendDecorationResultV1 &out,U32 modulus) {
  // 1160d8 returns seed&7fffffff,2140b0 shifts16/masks7fff before DIV.
  out.state.random_seed_word=out.state.random_seed_word*0x41c64e6dU+0x3039U;
  ++out.random_calls;
  return ((out.state.random_seed_word>>16U)&0x7fffU)%modulus;
}
}
RacFrontendDecorationResultV1 execute_rac_frontend_decoration_v1(
    const RacFrontendDecorationInputsV1 &input) {
  RacFrontendDecorationResultV1 out;out.state=input.state;
  if(!input.object_present||!input.pvar_present)return out;
  const auto [x,y,width,height]=input.rectangle_words;
  if(signed_word(x)>=512||signed_word(x+width)<0||
      signed_word(y)>=(input.pal?449:417)||signed_word(y+height)<0) {
    out.culled=true;return out;
  }
  using Asset=RacFrontendDecorationAssetV1;
  constexpr std::uint64_t source_alpha_over=UINT64_C(0x8000000044);
  out.draws.push_back({Asset::sprite_bank_e99e_variant7,0x225170U,
      {x,y,width,height},true,{},UINT64_C(0x807f7f7f),source_alpha_over});
  if(out.state.flash_word) {
    out.state.age_word+=2U;
    const auto u=random_bounded(out,200U),v=random_bounded(out,200U);
    const auto difference=signed_word(out.state.age_word-128U);
    // Original1f9b70 uses signed trapping NEG. This is unreachable for the
    // actual0..256 cycle; reject the sole malformed overflow input.
    if(difference==INT32_MIN)throw std::runtime_error("Frontend flash reached source signed NEG overflow");
    const auto absolute=static_cast<U32>(difference<0?-difference:difference);
    const auto intensity=(128U-absolute)*2U;
    const auto selected=signed_word(intensity)<129?intensity:128U;
    // DSLL32 retains all signed32 input bits; do not clamp a malformed age
    // into a different source packet. Normal source ages give FIX0..128.
    const auto alpha=(std::uint64_t(selected)<<32U)|0x68U;
    out.draws.push_back({Asset::catalog26_flash,0x225218U,{x,y,width,height},
        false,{u,v,width,height},UINT64_C(0x00808080),alpha});
    if(signed_word(out.state.age_word)>=256)out.state.flash_word=0U;
  } else if(random_bounded(out,2000U)==0U) {
    out.state.age_word=0U;out.state.flash_word=1U;
  }
  // Original signed arithmetic shift after wrapping3*height.
  const auto uv_height=static_cast<U32>(signed_word(height*3U)>>1);
  out.draws.push_back({Asset::catalog28_overlay,0x2252b8U,{x,y,width,height},
      false,{0U,0U,width,uv_height},UINT64_C(0x50606060),source_alpha_over});
  const auto adjustment=[](U32 extent)->U32 {
    if(signed_word(extent)>=151)return 0U;
    return signed_word(extent)>=76?UINT32_MAX:UINT32_MAX-1U;
  };
  out.draws.push_back({Asset::catalog25_border,0x22531cU,
      {x+1U,y+1U,width+adjustment(width),height+adjustment(height)},
      false,{1U,1U,62U,62U},UINT64_C(0x80808080),source_alpha_over});
  return out;
}

SceneCameraV1 execute_rac_frontend_menu_camera_v1() {
  const auto mul=[](U32 a,U32 b){return ee_cop1_mul_bits_v1(a,b).bits;};
  const auto div=[](U32 a,U32 b){return ee_cop1_div_bits_v1(a,b).bits;};
  SceneCameraV1 out;
  const auto negative_zero=std::bit_cast<float>(0x80000000U);
  out.position={256.0F,256.0F,64.0F};out.right={negative_zero,-1.0F,negative_zero};
  out.up={0.0F,0.0F,1.0F};out.forward={1.0F,0.0F,0.0F};
  const auto tan_vertical=mul(0x3f2147aeU,0x3f418937U);
  const auto horizontal=div(0x43800000U,mul(0x3f2147aeU,0x42000000U));
  const auto vertical=div(0x43600000U,mul(tan_vertical,0x42000000U));
  out.tangent_half_horizontal=std::bit_cast<float>(div(0x43800000U,mul(0x42000000U,horizontal)));
  out.tangent_half_vertical=std::bit_cast<float>(div(0x43600000U,mul(0x42000000U,vertical)));
  out.near_plane=std::bit_cast<float>(div(0x42000000U,0x44800000U));
  out.far_plane=std::bit_cast<float>(div(0x49360000U,0x44800000U));
  validate_scene_camera_v1(out);
  return out;
}
} // namespace openrc
