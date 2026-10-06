#include "openrc/frontend_input.hpp"

#include <algorithm>
#include <bit>

namespace openrc {
namespace {
using F = FrontendNoSaveFieldV1;
constexpr std::array<std::byte, 8> magic{std::byte{'O'}, std::byte{'R'},
    std::byte{'F'}, std::byte{'I'}, std::byte{'N'}, std::byte{'P'},
    std::byte{'0'}, std::byte{'1'}};
[[noreturn]] void fail(const char *s) { throw FrontendInputError(s); }
unsigned width(F f) { return f == F::focused ? 1U : f == F::entry_requested ? 2U : 4U; }
void key(const std::string &s, FrontendInputLimitsV1 limits) {
  if (s.empty() || s.size() > limits.max_key_bytes ||
      std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 33 || c > 126; }))
    fail("Invalid frontend input state key");
}
void validate_shape(const FrontendNoSavePlanV1 &p, FrontendInputLimitsV1 l) {
  if (!l.max_bytes || !l.max_key_bytes || !l.max_reset_writes ||
      p.reset_writes.empty() || p.reset_writes.size() > l.max_reset_writes ||
      !p.save_result_screen_tokens[0] || !p.save_result_screen_tokens[1] ||
      p.save_result_screen_tokens[0] == p.save_result_screen_tokens[1])
    fail("Invalid frontend input plan bounds or screen tokens");
  std::uint64_t size = 108;
  for (const auto &f : p.fields) { key(f.view_key, l); size += 12U + f.view_key.size(); }
  for (const auto &w : p.reset_writes) {
    key(w.view_key, l);
    if (w.value_type != SessionStateValueTypeV1::u8 || w.value_bits > 255U)
      fail("Frontend reset requires canonical byte writes");
    size += 16U + w.view_key.size();
  }
  if (size > l.max_bytes) fail("Frontend input plan exceeds byte limit");
}
void put(std::vector<std::byte> &out, std::uint64_t value, unsigned n = 4) {
  for (unsigned i = 0; i < n; ++i) { out.push_back(static_cast<std::byte>(value & 255U)); value >>= 8; }
}
void put_key(std::vector<std::byte> &out, const std::string &s) {
  put(out, s.size()); for (unsigned char c : s) out.push_back(static_cast<std::byte>(c));
}
struct Reader {
  std::span<const std::byte> bytes; std::size_t at = 0;
  std::span<const std::byte> take(std::size_t n) {
    if (n > bytes.size() - at) fail("Truncated frontend input plan");
    const auto r = bytes.subspan(at, n); at += n; return r;
  }
  std::uint64_t get(unsigned n = 4) {
    const auto b = take(n); std::uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i) v |= std::uint64_t(std::to_integer<unsigned>(b[i])) << (8U * i);
    return v;
  }
  std::string string(FrontendInputLimitsV1 l) {
    const auto n = get(); if (n > l.max_key_bytes) fail("Frontend input key exceeds limit");
    const auto b = take(static_cast<std::size_t>(n));
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
  }
};
} // namespace

void validate_frontend_no_save_plan_v1(const FrontendNoSavePlanV1 &p,
    const game::SessionStateV1 &state, FrontendInputLimitsV1 l) {
  validate_shape(p, l);
  if (p.state_schema_sha256 != state.schema_sha256()) fail("Frontend input state schema mismatch");
  auto check = [&](const std::string &key, std::uint64_t at, unsigned n) {
    const auto &views = state.schema().views;
    const auto v = std::lower_bound(views.begin(), views.end(), key,
        [](const auto &f,const std::string &k) { return f.key < k; });
    if (v == views.end() || v->key != key || v->value_type != SessionStateValueTypeV1::u8 || v->byte_stride != 1U ||
        at > v->element_count || n > v->element_count - at)
      fail("Frontend input field is outside its canonical byte view");
  };
  for (std::size_t i = 0; i < p.fields.size(); ++i)
    check(p.fields[i].view_key, p.fields[i].first_element, width(static_cast<F>(i)));
  for (const auto &w : p.reset_writes) check(w.view_key, w.element_index, 1);
}

std::vector<std::byte> encode_frontend_no_save_plan_v1(const FrontendNoSavePlanV1 &p,
    FrontendInputLimitsV1 l) {
  validate_shape(p, l); std::vector<std::byte> body;
  body.insert(body.end(), p.state_schema_sha256.begin(), p.state_schema_sha256.end());
  for (auto token : p.save_result_screen_tokens) put(body, token);
  put(body, p.reset_writes.size());
  for (const auto &f : p.fields) { put_key(body, f.view_key); put(body, f.first_element, 8); }
  for (const auto &w : p.reset_writes) { put_key(body, w.view_key); put(body, w.element_index, 8); put(body, w.value_bits); }
  std::vector<std::byte> out(magic.begin(), magic.end());
  put(out, 1); put(out, 64); put(out, 64U + body.size(), 8); put(out, p.fields.size()); put(out, 0);
  const auto digest = prepared_content_sha256_v1(body);
  out.insert(out.end(), digest.begin(), digest.end()); out.insert(out.end(), body.begin(), body.end());
  return out;
}

FrontendNoSavePlanV1 decode_frontend_no_save_plan_v1(std::span<const std::byte> bytes,
    FrontendInputLimitsV1 l) {
  if (bytes.size() < 108U + 12U * kFrontendNoSaveFieldCountV1 || bytes.size() > l.max_bytes)
    fail("Frontend input envelope exceeds bounds");
  Reader r{bytes}; const auto signature = r.take(8);
  if (!std::equal(signature.begin(), signature.end(), magic.begin()) || r.get() != 1 ||
      r.get() != 64 || r.get(8) != bytes.size() || r.get() != kFrontendNoSaveFieldCountV1 || r.get() != 0)
    fail("Invalid frontend input header");
  const auto stored = r.take(32); const auto digest = prepared_content_sha256_v1(bytes.subspan(64));
  if (!std::equal(stored.begin(), stored.end(), digest.begin())) fail("Frontend input digest mismatch");
  FrontendNoSavePlanV1 p; const auto schema = r.take(32);
  std::copy(schema.begin(), schema.end(), p.state_schema_sha256.begin());
  for (auto &token : p.save_result_screen_tokens) token = static_cast<std::uint32_t>(r.get());
  const auto count = r.get();
  if (!count || count > l.max_reset_writes || count * 16U > bytes.size() - r.at)
    fail("Frontend reset count exceeds limits or available bytes");
  for (auto &f : p.fields) { f.view_key = r.string(l); f.first_element = r.get(8); }
  if (count * 16U > bytes.size() - r.at) fail("Truncated frontend reset writes");
  for (std::uint64_t i = 0; i < count; ++i) {
    game::SessionStateWriteV1 w; w.view_key = r.string(l); w.element_index = r.get(8);
    w.value_bits = static_cast<std::uint32_t>(r.get()); p.reset_writes.push_back(std::move(w));
  }
  if (r.at != bytes.size()) fail("Trailing frontend input bytes");
  validate_shape(p, l); return p;
}

FrontendNoSaveEvaluationV1 evaluate_frontend_no_save_v1(const FrontendNoSavePlanV1 &p,
    const game::SessionStateV1 &state, std::uint64_t expected_revision, FrontendInputLimitsV1 l) {
  validate_frontend_no_save_plan_v1(p, state, l);
  if (state.revision() != expected_revision) fail("Stale frontend input state revision");
  FrontendNoSaveResultV1 out; out.committed_revision = expected_revision;
  const auto empty = [&]() { return FrontendNoSaveEvaluationV1{out,expected_revision,{}}; };
  const auto read = [&](F f) {
    const auto &ref = p.fields[static_cast<std::size_t>(f)]; std::uint32_t v = 0;
    for (unsigned i = 0; i < width(f); ++i) v |= std::uint32_t(state.read_u8(ref.view_key, ref.first_element + i)) << (8U * i);
    return v;
  };
  if (read(F::focused) > 1) fail("Frontend focus must be an actual boolean state value");
  if (!read(F::focused)) return empty();
  const auto phase = read(F::node_phase), previous = read(F::previous_screen);
  if (phase == 1U || (phase == 0U && read(F::previous_result) != 0U &&
      (previous == p.save_result_screen_tokens[0] || previous == p.save_result_screen_tokens[1])) || read(F::pending_save)) {
    out.unsupported = FrontendNoSaveUnsupportedV1::card_save_or_completion; return empty();
  }
  std::vector<game::SessionStateWriteV1> writes;
  const auto write = [&](F f, std::uint32_t v) {
    const auto &ref = p.fields[static_cast<std::size_t>(f)];
    for (unsigned i = 0; i < width(f); ++i)
      writes.push_back({ref.view_key, ref.first_element + i, SessionStateValueTypeV1::u8, (v >> (8U * i)) & 255U});
  };
  const auto commit = [&]() { return FrontendNoSaveEvaluationV1{out,expected_revision,std::move(writes)}; };
  const auto signed_word = [](std::uint32_t v) { return std::bit_cast<std::int32_t>(v); };
  write(F::node_phase, 2);
  const auto global = read(F::global_pressed), guard = read(F::cancel_guard), parent = read(F::parent_screen);
  if ((global & 0xd00U) && !guard) { out.return_word = 1; return commit(); }
  if (global & 0x10U) {
    if (parent) { write(F::current_screen, parent); return commit(); }
    if (!guard) { out.return_word = -1; return commit(); }
  }
  if (read(F::card_mode) != 1U && read(F::card_mode) != 16U) { write(F::current_screen, parent); return commit(); }
  if (signed_word(read(F::card_status)) >= 3 || signed_word(read(F::card_result)) >= 0 ||
      signed_word(read(F::readiness)) < 11 || read(F::card_type) != 2U) return commit();
  const auto flags = read(F::node_flags);
  const auto pressed = read((flags & 1U) ? F::repeated : F::pressed);
  auto selection = read(F::saved_selection); const auto previous_selection = read(F::node_selection);
  write(F::node_selection, selection);
  if ((pressed & 0x1000U) && selection) { --selection; write(F::node_selection, selection); }
  if ((pressed & 0x4000U) && signed_word(selection) < 4) { ++selection; write(F::node_selection, selection); }
  write(F::saved_selection, selection);
  if (pressed & 0x40U) { out.unsupported = FrontendNoSaveUnsupportedV1::slot_confirm; return empty(); }
  if (!(pressed & 0x20U)) {
    if (selection != previous_selection) { out.selection_sound_requested = true; out.sound_object_token = read(F::sound_object); }
    return commit();
  }
  if (flags & 0x2000U) { out.unsupported = FrontendNoSaveUnsupportedV1::preserving_reset; return empty(); }
  const std::array preserved{read(F::preserved_a), read(F::preserved_b), read(F::preserved_c)};
  write(F::save_requested, 0); write(F::flags, read(F::flags) & ~6U);
  writes.insert(writes.end(), p.reset_writes.begin(), p.reset_writes.end());
  write(F::preserved_a, preserved[0]); write(F::preserved_b, preserved[1]); write(F::preserved_c, preserved[2]);
  write(F::current_level, 0); write(F::target_level, 0); write(F::level_change_requested, 1);
  write(F::transition_requested, 1); write(F::entry_requested, 1);
  out.requested_new_game = true; return commit();
}
FrontendNoSaveResultV1 execute_frontend_no_save_v1(const FrontendNoSavePlanV1 &p,
    game::SessionStateV1 &state,std::uint64_t expected_revision,FrontendInputLimitsV1 l) {
  auto evaluation=evaluate_frontend_no_save_v1(p,state,expected_revision,l);
  state.apply_batch(evaluation.writes,evaluation.expected_revision);
  evaluation.result.committed_revision=state.revision();return evaluation.result;
}
} // namespace openrc
