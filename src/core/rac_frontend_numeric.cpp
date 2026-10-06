#include "openrc/rac_frontend_numeric.hpp"

#include "openrc/dvp_vu_numeric.hpp"
#include "openrc/ee_cop1_numeric.hpp"

#include <bit>

namespace openrc {

RacMobyPostVectorV1 rac_frontend_transform4_v1(
    const RacMobyPostVectorV1 &vector,
    const std::array<RacMobyPostVectorV1,4U> &columns) {
  RacMobyPostVectorV1 out{};
  for (std::size_t lane=0;lane<4;++lane) {
    const auto product=dvp_vu_mul_bits_v1(columns[0][lane],vector[0]);
    DvpVuAccumulatorLaneV1 acc{product.bits,product.overflow};
    for (std::size_t component=1;component<4;++component) {
      const auto next=dvp_vu_madd_bits_v1(acc,columns[component][lane],vector[component]);
      acc={next.result.bits,next.result.overflow};
    }
    out[lane]=acc.bits;
  }
  return out;
}

RacMobyPostVectorV1 rac_frontend_transform3_v1(
    const RacMobyPostVectorV1 &vector, const RacMobyPostMatrixV1 &columns) {
  return rac_frontend_transform4_v1(vector,
      {columns[0],columns[1],columns[2],{0U,0U,0U,0x3f800000U}});
}

RacMobyPostActorV1 execute_rac_moby_zero_rotation_post_v1(
    const RacMobyPostActorV1 &actor, const RacMobyPostBindingsV1 &bindings,
    const RacMobyPostLimitsV1 limits) {
  RacMobyPostV1 post(actor,bindings,limits);
  // The source has at most rotation, scale and derived-vector stages here.
  for (unsigned stage=0;stage<4;++stage) {
    const auto &next=post.continuation();
    if (std::holds_alternative<RacMobyPostReturnedV1>(next)) return post.staged_actor();
    if (const auto *rotation=std::get_if<RacMobyPostRotationRequestV1>(&next)) {
      for (std::size_t lane=0;lane<3;++lane)
        if (rotation->rotation_bits[lane]!=0U)
          throw RacMobyPostError("Zero-rotation source adapter reached nonzero rotation");
      post.resume(RacMobyPostRotationResultV1{{
          RacMobyPostVectorV1{0x3f800000U,0U,0U,0U},
          RacMobyPostVectorV1{0U,0x3f800000U,0U,0U},
          RacMobyPostVectorV1{0U,0U,0x3f800000U,0U}}});
    } else if (std::holds_alternative<RacMobyPostScaleRequestV1>(next)) {
      post.evaluate_scale_reference();
    } else if (const auto *derived=std::get_if<RacMobyPostDerivedRequestV1>(&next)) {
      std::array<RacMobyPostVectorV1,4U> columns{
          derived->columns[0],derived->columns[1],derived->columns[2],
          {derived->scaled_position_bits[0],derived->scaled_position_bits[1],derived->scaled_position_bits[2],0U}};
      auto vector=derived->scaled_header_bits;vector[3]=0x3f800000U;
      const auto transformed=rac_frontend_transform4_v1(vector,columns);
      post.resume(RacMobyPostDerivedResultV1{{transformed[0],transformed[1],transformed[2]}});
    } else {
      throw RacMobyPostError("Zero-rotation post reached an unintegrated blend/spatial owner");
    }
  }
  throw RacMobyPostError("Zero-rotation post exceeded its source continuation count");
}

namespace {
using Vector = RacMobyPostVectorV1;
using Matrix = std::array<Vector,4U>;
constexpr std::uint32_t one = 0x3f800000U;
constexpr Matrix identity{{Vector{one,0,0,0},Vector{0,one,0,0},
                            Vector{0,0,one,0},Vector{0,0,0,one}}};
std::uint32_t add(std::uint32_t a,std::uint32_t b) {
  return dvp_vu_add_bits_v1(a,b).bits;
}
std::uint32_t sub(std::uint32_t a,std::uint32_t b) {
  return dvp_vu_sub_bits_v1(a,b).bits;
}
std::uint32_t mul(std::uint32_t a,std::uint32_t b) {
  return dvp_vu_mul_bits_v1(a,b).bits;
}
// 125380 consumes pi/2-|angle| in vf6.x. Its polynomial computes cosine;
// its positive square root and a3 then produce the signed sine. This is the
// source polynomial, including every truncation, not a host sin/cos library.
std::array<std::uint32_t,2> rotation_pair(std::uint32_t angle) {
  const bool negative=(angle>>31U)!=0U && ((angle>>23U)&255U)!=0U;
  const auto u=negative ? ee_cop1_add_bits_v1(0x3fc90fdbU,angle).bits
                        : ee_cop1_sub_bits_v1(0x3fc90fdbU,angle).bits;
  const auto square=mul(u,u);
  Vector terms{0x362e9c14U,0xb94fb21fU,0x3c08873eU,0xbe2aaaa4U};
  for(auto &term:terms) term=mul(mul(term,u),square);
  for(std::size_t lane=0;lane<3;++lane) terms[lane]=mul(terms[lane],square);
  auto cosine=add(add(0U,u),terms[3]);
  for(std::size_t lane=0;lane<2;++lane) terms[lane]=mul(terms[lane],square);
  cosine=add(cosine,terms[2]);
  terms[0]=mul(terms[0],square);
  cosine=add(cosine,terms[1]);
  cosine=add(0U,add(cosine,terms[0]));
  const auto magnitude=add(0U,dvp_vu_sqrt_bits_v1(sub(one,mul(cosine,cosine))));
  return {negative?sub(0U,magnitude):add(0U,magnitude),cosine};
}
}

RacFrontendCameraV1 execute_rac_frontend_camera_v1(
    const std::array<std::uint32_t,3U> &position_bits,
    const std::array<std::uint32_t,3U> &rotation_bits) {
  // The frontend owner has a fixed PAL projection and these helpers consume
  // finite Euler angles. Reject values outside that source numerical domain.
  for(const auto raw:position_bits)
    if (((raw>>23U)&255U)==255U)
      throw RacMobyPostError("Frontend camera position exceeds neutral finite range");
  for(const auto raw:rotation_bits)
    if ((raw&0x7fffffffU)>0x40490fdbU)
      throw RacMobyPostError("Frontend camera Euler angle exceeds source rotation domain");
  RacFrontendCameraV1 out;
  out.rotation_rows=identity;
  for(std::size_t axis=0;axis<3;++axis) {
    const auto [sine,cosine]=rotation_pair(rotation_bits[axis]);
    const auto minus_sine=sub(0U,sine);
    Matrix rotation=identity;
    if(axis==0) { // 1254a0
      rotation[1]={0U,add(0U,cosine),add(0U,sine),0U};
      rotation[2]={0U,minus_sine,add(0U,cosine),0U};
    } else if(axis==1) { // 125548
      rotation[0]={add(0U,cosine),0U,minus_sine,0U};
      rotation[2]={add(0U,sine),0U,add(0U,cosine),0U};
    } else { // 1253f8
      rotation[0]={add(0U,cosine),add(0U,sine),0U,0U};
      rotation[1]={minus_sine,add(0U,cosine),0U,0U};
    }
    for(auto &column:out.rotation_rows)
      column=rac_frontend_transform4_v1(column,rotation);
  }
  out.view_columns=identity;
  for(std::size_t lane=0;lane<3;++lane) {
    // 1eb338 stores {-rowZ,-rowX,rowY}; 1f2608 negates/transposes
    // this to screen basis {rowX,-rowY,-rowZ}. Keep the two negations.
    const auto front=out.rotation_rows[2][lane]^0x80000000U;
    const auto left=out.rotation_rows[0][lane]^0x80000000U;
    out.view_columns[lane]={left^0x80000000U,out.rotation_rows[1][lane]^0x80000000U,front,0U};
    out.camera.position[lane]=std::bit_cast<float>(position_bits[lane]);
    out.camera.right[lane]=std::bit_cast<float>(out.view_columns[lane][0]);
    out.camera.up[lane]=std::bit_cast<float>(out.view_columns[lane][1]^0x80000000U);
    out.camera.forward[lane]=std::bit_cast<float>(front);
  }
  // 1f3140 builds the original scale from half-viewport and near distance.
  // Adapt that actual scale to the neutral frustum instead of assuming that
  // inverse(divide()) recovers the original parameter bit-for-bit.
  const auto tan_vertical=ee_cop1_mul_bits_v1(0x3f2147aeU,0x3f418937U).bits;
  const auto horizontal=ee_cop1_div_bits_v1(0x43800000U,
      ee_cop1_mul_bits_v1(0x3f2147aeU,0x42000000U).bits).bits;
  const auto vertical=ee_cop1_div_bits_v1(0x43600000U,
      ee_cop1_mul_bits_v1(tan_vertical,0x42000000U).bits).bits;
  out.camera.tangent_half_horizontal=std::bit_cast<float>(ee_cop1_div_bits_v1(
      0x43800000U,ee_cop1_mul_bits_v1(0x42000000U,horizontal).bits).bits);
  out.camera.tangent_half_vertical=std::bit_cast<float>(ee_cop1_div_bits_v1(
      0x43600000U,ee_cop1_mul_bits_v1(0x42000000U,vertical).bits).bits);
  // Source 1f28f4 scales camera/world coordinates by 1024 before projection.
  out.camera.near_plane=std::bit_cast<float>(ee_cop1_div_bits_v1(0x42000000U,0x44800000U).bits);
  out.camera.far_plane=std::bit_cast<float>(ee_cop1_div_bits_v1(0x49360000U,0x44800000U).bits);
  validate_scene_camera_v1(out.camera);
  return out;
}

RacFrontendProjectedBoundsV1 execute_rac_frontend_project_bounds_v1(
    const RacMobyPostVectorV1 &first_corner,
    const RacMobyPostVectorV1 &second_corner,
    const RacFrontendProjectionStateV1 &state) {
  std::array<Vector,2U> points{first_corner,second_corner};
  for(auto &point:points) {
    for(std::size_t lane=0;lane<3;++lane)
      point[lane]=mul(sub(point[lane],state.camera_position_bits[lane]),0x44800000U);
    point[3]=one;
    point=rac_frontend_transform4_v1(point,state.projection_view_columns);
  }
  // 238e7c uses the same reciprocal for first XY; second X and Y have
  // separate DIV.S operations. Retain source operand order for every MUL.
  const auto reciprocal=ee_cop1_div_bits_v1(one,points[0][3]).bits;
  points[0][1]=ee_cop1_mul_bits_v1(points[0][1],reciprocal).bits;
  points[0][0]=ee_cop1_mul_bits_v1(points[0][0],reciprocal).bits;
  points[1][0]=ee_cop1_mul_bits_v1(points[1][0],ee_cop1_div_bits_v1(one,points[1][3]).bits).bits;
  points[1][1]=ee_cop1_mul_bits_v1(points[1][1],ee_cop1_div_bits_v1(one,points[1][3]).bits).bits;
  for(auto &point:points) for(std::size_t axis=0;axis<2;++axis)
    point[axis]=ee_cop1_mul_bits_v1(point[axis],state.screen_scale_bits[axis]).bits;
  RacFrontendProjectedBoundsV1 out;
  for(std::size_t axis=0;axis<2;++axis) {
    const auto origin=ee_cop1_cvt_s_w_bits_v1(state.origin_words[axis]).bits;
    const auto position=ee_cop1_add_bits_v1(
        ee_cop1_mul_bits_v1(points[0][axis],0x3e800000U).bits,origin).bits;
    out.width_height_x_y[2+axis]=ee_cop1_cvt_w_s_bits_v1(position).bits;
    const auto extent=ee_cop1_mul_bits_v1(
        ee_cop1_sub_bits_v1(points[1][axis],points[0][axis]).bits,0x3e800000U).bits;
    out.width_height_x_y[axis]=ee_cop1_cvt_w_s_bits_v1(extent).bits;
  }
  return out;
}

RacFrontendProjectionStateV1 execute_rac_frontend_menu_projection_v1() {
  const auto ee_mul=[](std::uint32_t a,std::uint32_t b) {return ee_cop1_mul_bits_v1(a,b).bits;};
  const auto ee_div=[](std::uint32_t a,std::uint32_t b) {return ee_cop1_div_bits_v1(a,b).bits;};
  const auto near=0x42000000U,far=0x49360000U,depth_scale=0xcafffbe0U;
  const auto vertical=ee_mul(0x3f2147aeU,0x3f418937U);
  const auto denominator=ee_mul(near,ee_cop1_sub_bits_v1(far,near).bits);
  // The depth-column MUL and subsequent DIV must not be cancelled: the
  // source's intermediate ordered rounding remains part of the raw matrix.
  const auto depth=ee_div(ee_mul(ee_div(ee_cop1_add_bits_v1(far,near).bits,
      denominator),depth_scale),depth_scale);
  const auto translation=ee_div(ee_mul(ee_div(ee_mul(ee_mul(near,0xc0000000U),far),
      denominator),depth_scale),depth_scale);
  Matrix projection{{
      Vector{ee_div(ee_div(0x43800000U,ee_mul(0x3f2147aeU,near)),0x44800000U),0U,0U,0U},
      Vector{0U,ee_div(ee_div(0x43600000U,ee_mul(vertical,near)),0x44600000U),0U,0U},
      Vector{0U,0U,depth,ee_div(one,near)},Vector{0U,0U,translation,0U}}};
  // 1f286c/1f287c scale the first two source columns by +1c0=4.
  for(std::size_t column=0;column<2;++column)
    for(auto &lane:projection[column])lane=mul(lane,0x40800000U);
  // 219c08 initializes basis {forward X,left Y,up Z}; COP1 NEG in
  // 1f2608 produces these exact signed-zero columns before 1fa540.
  const Matrix view{{Vector{0x80000000U,0x80000000U,one,0U},
      Vector{0xbf800000U,0x80000000U,0U,0U},
      Vector{0x80000000U,0xbf800000U,0U,0U},Vector{0U,0U,0U,one}}};
  RacFrontendProjectionStateV1 out;
  out.camera_position_bits={0x43800000U,0x43800000U,0x42800000U,0U};
  out.screen_scale_bits={0x44800000U,0x44600000U};out.origin_words={256U,224U};
  for(std::size_t column=0;column<4;++column)
    out.projection_view_columns[column]=rac_frontend_transform4_v1(view[column],projection);
  return out;
}
} // namespace openrc
