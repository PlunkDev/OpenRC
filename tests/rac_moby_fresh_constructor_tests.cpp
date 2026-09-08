#include "openrc/rac_moby_fresh_constructor.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using openrc::construct_rac_moby_fresh_v1;
using openrc::RacMobyFreshConstructorInputV1;
using openrc::RacMobyFreshConstructorStateV1;
using openrc::RacMobyFreshModelV1;
using openrc::RacMobyFreshSequenceV1;

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

RacMobyFreshConstructorInputV1 bound_input() {
  RacMobyFreshConstructorInputV1 input;
  input.class_id_bits = 0x12340345U;
  input.class_table_index = 0xfdU;
  input.live_index_bits = 0xabcd1234U;
  input.callback_reference = 0x11223344U;
  input.model = RacMobyFreshModelV1{};
  input.model->reference = 0x55667788U;
  input.model->scale_bits = 0x80000000U;
  return input;
}

void test_null_model_and_fixed_state() {
  RacMobyFreshConstructorInputV1 input;
  input.class_id_bits = 0xfedcba98U;
  input.class_table_index = 0xffU;
  input.live_index_bits = 0xffffffffU;
  auto expected = RacMobyFreshConstructorStateV1{};
  expected.class_id_low16 = 0xba98U;
  expected.class_table_index = 0xffU;
  expected.live_index_bits = 0xffffffffU;
  expected.live_index_counter_bits = 0xffff0000U;
  expected.flags = 7U;
  expect(
      construct_rac_moby_fresh_v1(input) == expected,
      "Null model must retain all fresh sentinels, truncation and zero fields");
  input.callback_reference = 0x80000001U;
  expected.callback_reference = input.callback_reference;
  expected.flags = 5U;
  expect(construct_rac_moby_fresh_v1(input) == expected,
         "Null model and null callback are distinct source branches");
  expect(expected.packed_bounds_bits == 0x80807f7fU &&
             expected.packed_bits == 0x0040404000000000ULL &&
             expected.occlusion_bits == 0x7f80U,
         "Fixed source endian layout must remain exact");
}

void test_bound_model_without_sequence() {
  auto input = bound_input();
  input.model->flags = 0x8021U;
  input.model->byte_0e = 0x81U;
  input.model->spatial_reference = 0x80000000U;
  input.model->auxiliary_reference = 0x80000001U;
  input.model->byte_06 = 255U;
  input.model->byte_0f = 128U;
  input.model->byte_0c = 1U;
  const auto original = input;
  auto expected = RacMobyFreshConstructorStateV1{};
  expected.class_id_low16 = 0x0345U;
  expected.class_table_index = 0xfdU;
  expected.live_index_bits = 0xabcd1234U;
  expected.live_index_counter_bits = 0x12340000U;
  expected.callback_reference = input.callback_reference;
  expected.model_reference = input.model->reference;
  expected.scale_bits = 0x80000000U;
  expected.animation_speed_bits = 0x3f800000U;
  expected.animation_rate_bits = 0x3f800000U;
  expected.flags = 0x8431U;
  expected.model_byte_0e = 0x81U;
  expected.spatial_reference = 0x80000000U;
  expected.auxiliary_reference = 0x80000001U;
  expected.byte_73 = 0x18U;
  expected.byte_7f = 0x18U;
  expect(construct_rac_moby_fresh_v1(input) == expected,
         "Bound model scalar copies and byte-presence branches must be exact");
  expect(input == original,
         "Constructor must not mutate shared model/input state");
}

void test_every_sequence_byte_and_signed_sound_gate() {
  auto input = bound_input();
  input.callback_reference = 0U;
  input.model->flags = 0x8102U;
  input.model->sequence0 = RacMobyFreshSequenceV1{};
  for (std::uint32_t count = 0U; count < 256U; ++count) {
    for (std::uint32_t sound = 0U; sound < 256U; ++sound) {
      for (const auto model_kind :
           std::array<std::uint8_t, 4U>{0U, 1U, 2U, 255U}) {
        auto &sequence = *input.model->sequence0;
        input.model->byte_0c = model_kind;
        sequence.frame_count = static_cast<std::uint8_t>(count);
        sequence.sound_byte = static_cast<std::uint8_t>(sound);
        sequence.trigger_byte = static_cast<std::uint8_t>(255U - sound);
        sequence.frame0_reference = count == 0U ? 0U : 0xabcdef01U;
        const auto result = construct_rac_moby_fresh_v1(input);
        const bool stopped = model_kind == 1U && count < 2U;
        const auto flags =
            static_cast<std::uint16_t>((count < 2U ? 0x8102U : 0x8100U) |
                                       (stopped && sound >= 128U ? 0x40U : 0U));
        expect(
            result.flags == flags &&
                result.animation_speed_bits == (stopped ? 0U : 0x3f800000U) &&
                result.animation_rate_bits == 0x3f800000U &&
                result.sound_byte == sound &&
                result.trigger_byte == 255U - sound &&
                result.previous_frame_reference == sequence.frame0_reference &&
                result.current_frame_reference == sequence.frame0_reference,
            "Sequence count/type/signed-sound decisions must preserve exact "
            "source gates");
      }
    }
  }
}

void test_shared_flags_are_current_and_not_actor_flags() {
  auto input = bound_input();
  input.callback_reference = 0U;
  input.model->flags = 0x4006U;
  input.model->sequence0 = RacMobyFreshSequenceV1{0x12345678U, 2U, 255U, 7U};
  const auto first = construct_rac_moby_fresh_v1(input);
  expect(first.flags == 0x4004U, "Framecount must clear inherited bit2");
  // Later accepted placement writes shared model flags, not this helper.
  input.model->flags = static_cast<std::uint16_t>(input.model->flags | 0x20U);
  const auto second = construct_rac_moby_fresh_v1(input);
  expect(second.flags == 0x4024U && input.model->flags == 0x4026U &&
             first.flags == 0x4004U,
         "Later constructors must read cumulative model flags without "
         "retroactive actor writes");
  input.model->flags = static_cast<std::uint16_t>(input.model->flags | 0x8000U);
  expect(construct_rac_moby_fresh_v1(input).flags == 0xc024U,
         "Model flags are not restricted to currently observed retail mode "
         "values");
}

void test_raw_scale_and_invalid_presence() {
  auto input = bound_input();
  for (const auto bits : std::array<std::uint32_t, 8U>{
           0U, 0x80000000U, 1U, 0x807fffffU, 0x3f800000U, 0x7f800000U,
           0x7fc01234U, 0xffffffffU}) {
    input.model->scale_bits = bits;
    expect(
        construct_rac_moby_fresh_v1(input).scale_bits == bits,
        "Constructor copies all scale bits without host floating conversion");
  }
  input.model->reference = 0U;
  bool failed = false;
  try {
    static_cast<void>(construct_rac_moby_fresh_v1(input));
  } catch (const openrc::RacMobyFreshConstructorError &) {
    failed = true;
  }
  expect(failed,
         "A present model cannot masquerade as unresolved/null source binding");
}

} // namespace

int main() {
  try {
    test_null_model_and_fixed_state();
    test_bound_model_without_sequence();
    test_every_sequence_byte_and_signed_sound_gate();
    test_shared_flags_are_current_and_not_actor_flags();
    test_raw_scale_and_invalid_presence();
    std::cout << "RAC fresh constructor tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
