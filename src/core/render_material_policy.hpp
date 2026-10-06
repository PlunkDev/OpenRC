#pragma once

#include "openrc/render_scene.hpp"
#include <bit>
#include <span>

namespace openrc::detail {
inline bool has_render_material_extension_v1(const RenderSceneMaterialV1 &m) {
  return m.color_math!=RenderSceneColorMathV1::linear ||
      m.blend_mode!=RenderSceneBlendModeV1::opaque ||
      m.interpolation!=RenderSceneInterpolationV1::perspective ||
      m.depth_test!=RenderSceneDepthTestV1::less_equal || !m.depth_write ||
      m.texture_modulation_denominator!=255U || m.blend_denominator!=255U ||
      m.alpha_failure!=RenderSceneAlphaFailureV1::discard;
}
inline void validate_render_material_extension_v1(const RenderSceneMaterialV1 &m,
    std::span<const RenderSceneTextureV1> textures) {
  const auto reject=[] {throw RenderSceneError("Material has an unsupported encoded-color/depth policy");};
  if((m.color_math!=RenderSceneColorMathV1::linear && m.color_math!=RenderSceneColorMathV1::encoded_integer) ||
     (m.blend_mode!=RenderSceneBlendModeV1::opaque && m.blend_mode!=RenderSceneBlendModeV1::source_over) ||
     (m.interpolation!=RenderSceneInterpolationV1::perspective && m.interpolation!=RenderSceneInterpolationV1::affine) ||
     (m.depth_test!=RenderSceneDepthTestV1::less_equal && m.depth_test!=RenderSceneDepthTestV1::always) ||
     (m.alpha_failure!=RenderSceneAlphaFailureV1::discard && m.alpha_failure!=RenderSceneAlphaFailureV1::rgb_only) ||
     !m.texture_modulation_denominator || !m.blend_denominator)reject();
  if(m.alpha_failure==RenderSceneAlphaFailureV1::rgb_only &&
     (m.color_math!=RenderSceneColorMathV1::encoded_integer ||
      m.alpha_mode!=RenderSceneAlphaModeV1::mask || !m.alpha_cutoff_rgba8))reject();
  if(m.color_math==RenderSceneColorMathV1::linear) {
    if(m.blend_mode!=RenderSceneBlendModeV1::opaque ||
       m.interpolation!=RenderSceneInterpolationV1::perspective ||
       m.texture_modulation_denominator!=255U || m.blend_denominator!=255U)reject();
  } else {
    if(m.base_color_rgba8!=UINT32_C(0xffffffff) || !m.use_vertex_color ||
       m.mipmap_filter!=RenderSceneMipmapFilterV1::none || m.min_filter!=m.mag_filter)reject();
    if(m.base_color_texture_id && (*m.base_color_texture_id>=textures.size() ||
       textures[*m.base_color_texture_id].color_space!=RenderSceneTextureColorSpaceV1::linear))reject();
  }
  if((!m.base_color_texture_id && m.texture_modulation_denominator!=255U) ||
     (m.blend_mode==RenderSceneBlendModeV1::opaque && m.blend_denominator!=255U))reject();
}
inline void validate_encoded_material_uv_v1(const RenderSceneMaterialV1 &m,
    std::span<const RenderSceneTextureV1> textures,float u,float v) {
  if(m.color_math!=RenderSceneColorMathV1::encoded_integer || !m.base_color_texture_id)return;
  if(*m.base_color_texture_id>=textures.size() || textures[*m.base_color_texture_id].mips.empty())
    throw RenderSceneError("Encoded material UV has no texture owner");
  const auto &mip=textures[*m.base_color_texture_id].mips[0U];
  const auto bounded=[](float value,std::uint32_t dimension) {
    const auto bits=std::bit_cast<std::uint32_t>(value)&0x7fffffffU;
    const auto exponent=bits>>23U;
    if(exponent==255U || !dimension)return false;
    if(exponent<=118U)return true;
    if(exponent>174U)return false;
    // Exact binary32 significand times integer dimension, compared against
    //2^24 without overflow or rounded host multiplication at the boundary.
    const auto product=std::uint64_t((bits&0x7fffffU)|0x800000U)*dimension;
    return product<=(UINT64_C(1)<<(174U-exponent));
  };
  if(!bounded(u,mip.width) || !bounded(v,mip.height))
    throw RenderSceneError("Encoded material UV exceeds the signed shader texel envelope");
}
} // namespace openrc::detail
