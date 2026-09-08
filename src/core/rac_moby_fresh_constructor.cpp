#include "openrc/rac_moby_fresh_constructor.hpp"

namespace openrc {

RacMobyFreshConstructorStateV1
construct_rac_moby_fresh_v1(const RacMobyFreshConstructorInputV1 &input) {
  if (input.model && input.model->reference == 0U) {
    throw RacMobyFreshConstructorError(
        "A present resolved Moby model cannot have a null reference");
  }

  // PAL v2.00 constructor24f968..24fb9c first clears the entire fresh slot.
  // Its nonzero-capable fields are represented explicitly by this value.
  RacMobyFreshConstructorStateV1 result;
  result.class_table_index = input.class_table_index;
  result.class_id_low16 = static_cast<std::uint16_t>(input.class_id_bits);
  result.live_index_bits = input.live_index_bits;
  result.live_index_counter_bits = input.live_index_bits << 16U;
  result.callback_reference = input.callback_reference;
  if (input.callback_reference == 0U) {
    result.flags = 2U;
  }
  if (!input.model) {
    result.flags = static_cast<std::uint16_t>(result.flags | 5U);
    return result;
  }

  const auto &model = *input.model;
  result.model_reference = model.reference;
  result.model_byte_0e = model.byte_0e;
  result.flags = static_cast<std::uint16_t>(result.flags | model.flags);
  result.spatial_reference = model.spatial_reference;
  result.scale_bits = model.scale_bits;
  result.animation_speed_bits = 0x3f800000U;
  result.animation_rate_bits = 0x3f800000U;
  if (model.auxiliary_reference != 0U) {
    result.flags = static_cast<std::uint16_t>(result.flags | 0x10U);
    result.auxiliary_reference = model.auxiliary_reference;
  }
  if (model.byte_0f != 0U) {
    result.byte_7f = 0x18U;
    result.flags = static_cast<std::uint16_t>(result.flags | 0x400U);
    // +84/+88/+bd remain zero from the initial clear.
  }
  if (model.byte_06 != 0U) {
    result.byte_73 = 0x18U;
  }
  if (!model.sequence0) {
    return result;
  }

  // Fresh previous/current sequence and frame indices are all zero.
  // Leaf24fbf8 therefore resolves both through logical sequence0 frame0;
  // its previous==ff snapshot branch is unreachable in this constructor.
  const auto &sequence = *model.sequence0;
  result.previous_frame_reference = sequence.frame0_reference;
  result.current_frame_reference = sequence.frame0_reference;
  result.sound_byte = sequence.sound_byte;
  result.trigger_byte = sequence.trigger_byte;
  if (sequence.frame_count >= 2U) {
    // This clears bit2 even when inherited from callback absence/model.
    result.flags = static_cast<std::uint16_t>(result.flags & 0xfffdU);
  }
  if (model.byte_0c == 1U && sequence.frame_count < 2U) {
    result.animation_speed_bits = 0U;
    // Source LB followed by BGEZ, not plain nonzero or host-char signedness.
    if ((sequence.sound_byte & 0x80U) != 0U) {
      result.flags = static_cast<std::uint16_t>(result.flags | 0x40U);
    }
  }
  return result;
}

} // namespace openrc
