#include "openrc/rac_frontend_geometry.hpp"
#include "openrc/dvp_vu_numeric.hpp"

namespace openrc {
RacFrontendObjectGeometryV1 complete_rac_frontend_geometry_v1(
    const std::array<RacMobyPostVectorV1,4> &corners) {
  RacFrontendObjectGeometryV1 out;out.corners=corners;
  for(std::size_t edge=0;edge<2;++edge) {
    std::array<std::uint32_t,3> squared{};
    for(std::size_t lane=0;lane<3;++lane) {
      const auto difference=dvp_vu_sub_bits_v1(corners[edge+1][lane],corners[0][lane]).bits;
      squared[lane]=dvp_vu_mul_bits_v1(difference,difference).bits;
    }
    const auto xy=dvp_vu_add_bits_v1(squared[0],squared[1]);
    const auto sum=dvp_vu_madd_bits_v1({xy.bits,xy.overflow},0x3f800000U,squared[2]);
    out.edge_length_bits[edge]=dvp_vu_add_bits_v1(0U,dvp_vu_sqrt_bits_v1(sum.result.bits)).bits;
  }
  return out;
}
RacFrontendObjectGeometryV1 sample_rac_frontend_object_geometry_v1(
    const RacFrontendObjectV1 &object,const RacFrontendObjectClassV1 &source_class,
    const RacRatchetSequenceV1 &sequence,const RacMobyBindRigV1 &bind_rig,
    const RacRatchetPoseLimitsV1 pose_limits) {
  return complete_rac_frontend_geometry_v1(sample_rac_frontend_object_corners_v1(
      object,source_class,sequence,bind_rig,pose_limits));
}
} // namespace openrc
