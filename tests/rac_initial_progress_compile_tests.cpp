#include "openrc/game_world.hpp"
#include "openrc/rac_initial_progress_compile.hpp"
#include "openrc/session_state_io.hpp"

#include <bit>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr openrc::SessionStateLimitsV1 kLimits{
    2U, 101U, 128U, 16384U, 16384U, 32768U, 128U, 16384U, 4096U};
constexpr openrc::SessionStateIoLimitsV1 kIoLimits{65536U, kLimits};

void expect(const bool okay, const std::string &message) {
  if (!okay)
    throw std::runtime_error(message);
}

void test_source_template_to_neutral_state_and_session() {
  using namespace openrc;
  using namespace openrc::game;
  RacInitialProgressTemplateV1 source;
  source.encoded_source_level = -1;
  for (std::size_t row = 0U; row < source.rows.size(); ++row) {
    for (std::size_t i = 0U; i < 16U; ++i) {
      source.rows[row].selector_bytes[i] =
          static_cast<std::uint8_t>(row * 17U + i);
    }
    for (std::size_t i = 0U; i < 64U; ++i) {
      source.rows[row].primary_bit_words[i] =
          0x80000000U + static_cast<std::uint32_t>(row * 64U + i);
      source.rows[row].registration_slots[i] = {
          static_cast<std::int16_t>(-32000 + static_cast<int>(row * 64U + i)),
          static_cast<std::uint16_t>(0xa000U + row * 64U + i)};
    }
  }
  const auto compiled = compile_rac_initial_progress_v1(source, kLimits);
  const auto encoded =
      encode_session_state_initial_v1(compiled.initial, kIoLimits);
  const auto decoded = decode_session_state_initial_v1(encoded, kIoLimits);
  expect(decoded == compiled.initial,
         "neutral initial-state package roundtrip changed source data");
  GameSessionV1 session(77U, decoded, kLimits);
  const auto *state = session.persistent_state();
  expect(state &&
             state->read_u32(compiled.encoded_source_level, 0U) == 0xffffffffU,
         "template compilation performed the separate reset-wrapper L=0 write");
  for (std::size_t row = 0U; row < source.rows.size(); ++row) {
    const auto &bindings = compiled.rows[row];
    for (std::size_t i = 0U; i < 16U; ++i) {
      expect(state->read_u8(bindings.selector_bytes, i) ==
                 source.rows[row].selector_bytes[i],
             "compiled selector byte/row differs");
    }
    for (std::size_t i = 0U; i < 64U; ++i) {
      const auto slot = source.rows[row].registration_slots[i];
      const auto key_bits = static_cast<std::uint16_t>(slot.key);
      expect(state->read_u32(bindings.primary_bit_words, i) ==
                     source.rows[row].primary_bit_words[i] &&
                 state->read_u16(bindings.registration_keys, i) == key_bits &&
                 state->read_u16(bindings.registration_auxiliary, i) ==
                     slot.auxiliary_bits &&
                 state->read_u32(bindings.registration_words, i) ==
                     (key_bits |
                      (static_cast<std::uint32_t>(slot.auxiliary_bits) << 16U)),
             "compiled state lost source word, signed key, auxiliary bits or "
             "row order");
    }
  }
  const auto original_last_slot = source.rows[19].registration_slots[63];
  session.apply_persistent_state_writes(
      std::array{SessionStateWriteV1{compiled.rows[19].registration_keys, 63U,
                                     SessionStateValueTypeV1::u16, 42U}},
      0U);
  expect(
      state->read_u32(compiled.rows[19].registration_words, 63U) ==
          (42U | (static_cast<std::uint32_t>(original_last_slot.auxiliary_bits)
                  << 16U)),
      "source registration key write did not update the overlapping neutral "
      "word");
  WorldV1 world;
  const auto persistent = session.snapshot().persistent_state;
  world.load_level(session, session.request_level(0U));
  world.load_level(session, session.request_level(1U));
  world.load_level(session, session.request_level(0U));
  expect(session.snapshot().persistent_state == persistent,
         "compiled original progress did not survive leave-and-return level "
         "sequence");
  const GameSessionV1 restored(session.snapshot(), decoded.schema, kLimits);
  expect(restored.snapshot() == session.snapshot(),
         "restored source progress reran the template or forgot a mutation");
  auto smaller = kLimits;
  smaller.max_views = 100U;
  try {
    static_cast<void>(compile_rac_initial_progress_v1(source, smaller));
  } catch (const SessionStateError &) {
    return;
  }
  throw std::runtime_error("source compiler ignored the output view limit");
}

} // namespace

int main() {
  try {
    test_source_template_to_neutral_state_and_session();
    std::cout << "RAC initial-progress compilation tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC initial-progress compilation tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
