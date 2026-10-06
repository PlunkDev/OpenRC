#include "openrc/state_installation.hpp"
#include "openrc/game_world.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>

namespace {
using namespace openrc;
using namespace openrc::game;
using Type = SessionStateValueTypeV1;
constexpr SessionStateLimitsV1 kStateLimits{
    4U, 8U, 64U, 4096U, 1024U, 4096U, 1024U, 4096U, 32U};

void check(const bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

template <class Error = StateInstallationError, class Function>
void rejects(Function &&function) {
  try { function(); }
  catch (const Error &) { return; }
  throw std::runtime_error("Invalid state installation was accepted");
}

SessionStateInitialV1 initial() {
  SessionStateInitialV1 value;
  value.schema.identity_key = "installation/session";
  value.schema.buffers = {{"resident", 12U}, {"persistent", 4U}};
  value.schema.views = {
      {"level/words", "resident", Type::u32, 0U, 3U, 4U},
      {"level/bytes", "resident", Type::u8, 0U, 12U, 1U},
      {"level/halves", "resident", Type::u16, 0U, 6U, 2U},
      {"level/sliding", "resident", Type::u16, 5U, 3U, 1U},
      {"progress", "persistent", Type::u32, 0U, 1U, 4U}};
  value.buffers = {
      {"resident", std::vector<std::byte>(12U, std::byte{0xaaU})},
      {"persistent", {std::byte{0x10U}, std::byte{0x20U},
                       std::byte{0x30U}, std::byte{0x40U}}}};
  return value;
}

StateInstallationV1 installation(const SessionStateV1 &state) {
  StateInstallationV1 value;
  value.level_id = 7U;
  value.state_schema_sha256 = state.schema_sha256();
  value.writes = {
      {"level/words", 0U, Type::u32, 0x11223344U},
      {"level/bytes", 1U, Type::u8, 0U},
      {"level/halves", 1U, Type::u16, 0xbeefU},
      {"level/bytes", 1U, Type::u8, 0x7aU},
      {"level/words", 1U, Type::u32, 0xdecafbadU},
      {"level/sliding", 0U, Type::u16, 0x1234U},
      {"level/bytes", 8U, Type::u8, 0U}};
  return value;
}

bool same_write(const SessionStateWriteV1 &a, const SessionStateWriteV1 &b) {
  return a.view_key == b.view_key && a.element_index == b.element_index &&
         a.value_type == b.value_type && a.value_bits == b.value_bits;
}

void codec_and_ordered_installation() {
  SessionStateV1 state(initial(), kStateLimits);
  const auto plan = installation(state);
  const auto encoded = encode_state_installation_v1(plan);
  const auto decoded = decode_state_installation_v1(encoded);
  check(decoded.schema_version == 1U && decoded.level_id == 7U &&
            decoded.state_schema_sha256 == plan.state_schema_sha256 &&
            decoded.writes.size() == plan.writes.size() &&
            std::equal(decoded.writes.begin(), decoded.writes.end(),
                       plan.writes.begin(), same_write),
        "Codec lost zero values, duplicates, types or the ordered writes");
  check(encode_state_installation_v1(decoded) == encoded,
        "State installation binary is not canonical");
  // A value changed after the installation was prepared must survive it.
  const std::array progress{
      SessionStateWriteV1{"progress", 0U, Type::u32, 0x87654321U}};
  state.apply_batch(progress, state.revision());
  const auto revision = state.revision();
  execute_state_installation_v1(decoded, 7U, state, revision);
  const std::array expected{
      std::byte{0x44U}, std::byte{0x7aU}, std::byte{0xefU}, std::byte{0xbeU},
      std::byte{0xadU}, std::byte{0x34U}, std::byte{0x12U}, std::byte{0xdeU},
      std::byte{0x00U}, std::byte{0xaaU}, std::byte{0xaaU}, std::byte{0xaaU}};
  check(std::ranges::equal(state.buffer_bytes("resident"), expected) &&
            state.read_u32("level/words", 0U) == 0xbeef7a44U &&
            state.read_u16("level/halves", 0U) == 0x7a44U &&
            state.read_u32("level/words", 1U) == 0xde1234adU &&
            state.read_u32("progress", 0U) == 0x87654321U &&
            state.revision() == revision + 1U,
        "Installation did not copy nonzero storage in order or erased persistent state");
  const auto once = state.snapshot();
  rejects([&] { execute_state_installation_v1(decoded, 7U, state, revision); });
  check(state.snapshot() == once, "Stale installation changed a completed session");

  SessionStateV1 reversed(initial(), kStateLimits);
  auto reverse = plan;
  std::ranges::reverse(reverse.writes);
  execute_state_installation_v1(reverse, 7U, reversed, 0U);
  check(reversed.read_u32("level/words", 0U) == 0x11223344U &&
            reversed.read_u32("level/words", 1U) == 0xdecafbadU,
        "Installation reordered aliased writes by destination key");
}

void atomic_rejection() {
  SessionStateV1 state(initial(), kStateLimits);
  const auto plan = installation(state);
  const auto before = state.snapshot();
  const auto unchanged = [&] {
    check(state.snapshot() == before, "Rejected installation partially changed state");
  };
  rejects([&] { execute_state_installation_v1(plan, 8U, state, 0U); });
  unchanged();
  rejects([&] { execute_state_installation_v1(plan, 7U, state, 1U); });
  unchanged();
  auto bad = plan;
  bad.state_schema_sha256[0] ^= std::byte{1U};
  rejects([&] { execute_state_installation_v1(bad, 7U, state, 0U); });
  unchanged();
  const std::array invalid_last{
      SessionStateWriteV1{"missing", 0U, Type::u8, 1U},
      SessionStateWriteV1{"level/bytes", 12U, Type::u8, 1U},
      SessionStateWriteV1{"level/bytes", UINT64_MAX, Type::u8, 1U},
      SessionStateWriteV1{"level/bytes", 0U, Type::u16, 1U},
      SessionStateWriteV1{"level/bytes", 0U, Type::u8, 256U},
      SessionStateWriteV1{"level/halves", 0U, Type::u16, 65536U},
      SessionStateWriteV1{"level/words", 0U, static_cast<Type>(3U), 1U}};
  for (const auto &last : invalid_last) {
    bad = plan;
    // Both buffers would change if a prefix were committed before validation.
    bad.writes.push_back({"progress", 0U, Type::u32, 0U});
    bad.writes.push_back(last);
    rejects([&] { execute_state_installation_v1(bad, 7U, state, 0U); });
    unchanged();
  }
  auto state_limits = kStateLimits;
  state_limits.max_batch_writes = plan.writes.size() - 1U;
  SessionStateV1 bounded(initial(), state_limits);
  const auto bounded_before = bounded.snapshot();
  rejects([&] { execute_state_installation_v1(plan, 7U, bounded, 0U); });
  check(bounded.snapshot() == bounded_before,
        "Session batch limit failure changed canonical bytes");
  auto exhausted = state.snapshot();
  exhausted.revision = UINT64_MAX;
  SessionStateV1 no_revision(state.schema(), exhausted, kStateLimits);
  rejects([&] { execute_state_installation_v1(plan, 7U, no_revision, UINT64_MAX); });
  check(no_revision.snapshot() == exhausted, "Revision exhaustion changed state");
}

void store(std::vector<std::byte> &bytes, std::size_t offset,
           std::uint64_t value, const unsigned count = 4U) {
  for (unsigned i = 0; i < count; ++i) {
    bytes.at(offset++) = static_cast<std::byte>(value & 255U);
    value >>= 8U;
  }
}
void rehash(std::vector<std::byte> &bytes) {
  const auto digest = prepared_content_sha256_v1(
      std::span<const std::byte>(bytes).subspan(56U));
  std::copy(digest.begin(), digest.end(), bytes.begin() + 24U);
}

void malformed_encoding_and_limits() {
  SessionStateV1 state(initial(), kStateLimits);
  auto plan = installation(state);
  plan.writes.resize(1U);
  const auto encoded = encode_state_installation_v1(plan);
  // Header56 + body prefix48 + record prefix24 + exact key bytes.
  check(encoded.size() == 128U + plan.writes[0].view_key.size(),
        "Installation encoding acquired unexplained padding");
  for (std::size_t n = 0; n < encoded.size(); ++n)
    rejects([&] { static_cast<void>(decode_state_installation_v1(
        std::span<const std::byte>(encoded).first(n))); });
  auto bytes = encoded;
  bytes.back() ^= std::byte{1U};
  rejects([&] { static_cast<void>(decode_state_installation_v1(bytes)); });
  const auto bad_word = [&](const std::size_t at, const std::uint64_t value,
                            const unsigned width = 4U) {
    auto modified = encoded;
    store(modified, at, value, width);
    rehash(modified); // Reach structural validation, not only hash rejection.
    rejects([&] { static_cast<void>(decode_state_installation_v1(modified)); });
  };
  bad_word(0U, 0U);       // magic
  bad_word(8U, 2U);       // schema version
  bad_word(12U, 1U);      // header reserved
  bad_word(16U, UINT64_MAX, 8U); // body length
  bad_word(60U, 1U);      // body reserved
  bad_word(96U, 0U, 8U);  // empty write count
  bad_word(96U, UINT64_MAX, 8U); // count cannot allocate from a short input
  bad_word(104U, 0U);     // empty key
  bad_word(104U, UINT32_MAX); // key length
  bad_word(108U, 3U);     // unknown type
  bad_word(124U, 1U);     // record reserved
  bytes = encoded;
  store(bytes, 108U, static_cast<unsigned>(Type::u8));
  store(bytes, 120U, 256U);
  rehash(bytes);
  rejects([&] { static_cast<void>(decode_state_installation_v1(bytes)); });
  bytes = encoded;
  std::fill(bytes.begin() + 64U, bytes.begin() + 96U, std::byte{});
  rehash(bytes);
  rejects([&] { static_cast<void>(decode_state_installation_v1(bytes)); });
  for (const auto invalid : {std::byte{0U}, std::byte{32U}, std::byte{127U}, std::byte{255U}}) {
    bytes = encoded; bytes[128U] = invalid; rehash(bytes);
    rejects([&] { static_cast<void>(decode_state_installation_v1(bytes)); });
  }
  bytes = encoded; bytes.push_back(std::byte{});
  store(bytes, 16U, bytes.size() - 56U, 8U); rehash(bytes);
  rejects([&] { static_cast<void>(decode_state_installation_v1(bytes)); });

  StateInstallationLimitsV1 exact{encoded.size(), 1U,
      static_cast<std::uint32_t>(plan.writes[0].view_key.size())};
  check(encode_state_installation_v1(plan, exact) == encoded &&
            decode_state_installation_v1(encoded, exact).writes.size() == 1U,
        "Exact installation bounds were rejected");
  auto limits = exact; --limits.max_bytes;
  rejects([&] { static_cast<void>(encode_state_installation_v1(plan, limits)); });
  rejects([&] { static_cast<void>(decode_state_installation_v1(encoded, limits)); });
  limits = exact; --limits.max_key_bytes;
  rejects([&] { static_cast<void>(encode_state_installation_v1(plan, limits)); });
  rejects([&] { static_cast<void>(decode_state_installation_v1(encoded, limits)); });
  const auto full = installation(state);
  const auto full_bytes = encode_state_installation_v1(full);
  limits = {}; limits.max_writes = full.writes.size() - 1U;
  rejects([&] { static_cast<void>(encode_state_installation_v1(full, limits)); });
  rejects([&] { static_cast<void>(decode_state_installation_v1(full_bytes, limits)); });
  for (unsigned i = 0; i < 3U; ++i) {
    limits = {};
    if (i == 0U) limits.max_bytes = 0U;
    if (i == 1U) limits.max_writes = 0U;
    if (i == 2U) limits.max_key_bytes = 0U;
    rejects([&] { validate_state_installation_v1(plan, limits); });
    rejects([&] { static_cast<void>(decode_state_installation_v1(encoded, limits)); });
  }
  auto bad = plan; bad.writes.clear();
  rejects([&] { execute_state_installation_v1(bad, 7U, state, state.revision()); });
  rejects([&] { static_cast<void>(encode_state_installation_v1(bad)); });
  bad = plan; bad.state_schema_sha256 = {};
  rejects([&] { validate_state_installation_v1(bad); });
  bad = plan; bad.schema_version = 2U;
  rejects([&] { validate_state_installation_v1(bad); });
}

void existing_game_session_owner() {
  GameSessionV1 session(0x12345678U, initial(), kStateLimits);
  const auto *owner = session.persistent_state();
  const auto plan = installation(*owner);
  static_cast<void>(session.request_level(7U, 3U, LevelRequestReasonV1::new_game));
  const auto metadata = session.snapshot();
  session.apply_state_installation(plan, 7U, owner->revision());
  check(session.persistent_state() == owner && owner->revision() == 1U &&
            owner->read_u32("level/words", 0U) == 0xbeef7a44U,
        "Game session replaced the persistent owner during installation");
  auto expected = metadata;
  expected.persistent_state = owner->snapshot();
  check(session.snapshot() == expected,
        "Installation changed level ownership, requests or simulation metadata");
  const auto before = session.snapshot();
  rejects<GameWorldError>([&] { session.apply_state_installation(plan, 8U, owner->revision()); });
  rejects<GameWorldError>([&] { session.apply_state_installation(plan, 7U, 0U); });
  check(session.snapshot() == before, "Game session wrapper lost atomic rejection");
  GameSessionV1 absent(1U);
  const auto absent_before = absent.snapshot();
  rejects<GameWorldError>([&] { absent.apply_state_installation(plan, 7U, 0U); });
  check(absent.snapshot() == absent_before && !absent.persistent_state(),
        "Installation manufactured a missing persistent session");
}

} // namespace

int main() {
  try {
    codec_and_ordered_installation();
    atomic_rejection();
    malformed_encoding_and_limits();
    existing_game_session_owner();
    std::cout << "State installation codec, ordered aliases and atomic live-session admission passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "State installation test failed: " << error.what() << '\n';
    return 1;
  }
}
