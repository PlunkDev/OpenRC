#include "openrc/rac_frontend_input.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace openrc;
using Bytes = std::vector<std::byte>;
void check(bool okay, const char *why) {
  if (!okay) throw std::runtime_error(why);
}
template<class F> void rejects(F &&fn) {
  try { fn(); } catch (const std::runtime_error &) { return; }
  throw std::runtime_error("Invalid frontend input was accepted");
}
void put(Bytes &bytes, std::size_t at, std::uint32_t value) {
  for (unsigned lane = 0U; lane < 4U; ++lane)
    bytes.at(at + lane) = static_cast<std::byte>(value >> (lane * 8U));
}
std::uint32_t word(const RacFrontendInputWriteV1 &effect) {
  std::uint32_t value = 0U;
  for (std::size_t lane = 0U; lane < effect.bytes.size(); ++lane)
    value |= std::to_integer<std::uint32_t>(effect.bytes[lane]) << (8U * lane);
  return value;
}
std::uint32_t last(const RacFrontendInputResultV1 &result, std::uint32_t address) {
  const auto found = std::find_if(result.writes.rbegin(), result.writes.rend(),
      [&](const auto &effect) { return effect.source_address == address; });
  check(found != result.writes.rend(), "Missing expected source write");
  return word(*found);
}
void append(Bytes &bytes, std::uint32_t value) {
  const auto at = bytes.size(); bytes.resize(at + 4U); put(bytes, at, value);
}

// Synthetic tagged payloads only. These schema sizes are the public parser's
// supported layout, and every payload is generated independently of the disc.
struct Tag { std::uint32_t key, size; };
constexpr std::array<Tag, 47U> primary{{
  {0,4},{1,4},{2,4},{3,4},{4,8},{5,0x80},{7,12},{8,32},{9,0x94},
  {10,37},{11,37},{12,12},{13,32},{19,4},{14,20},{20,80},
  {15,0x790},{16,0x4a0},{17,0x120},{18,0x128},
  {21,4},{22,4},{23,4},{24,4},{25,4},{26,4},{27,4},{28,1},{29,1},
  {30,40},{31,4},{32,28},{33,1},{34,4},{35,4},{36,4},{37,12},
  {0x3e8,0x94},{0x3e9,0x94},{0x3ea,0x94},{0x3eb,4},{0x3ec,4},
  {0x3ed,4},{0x3f0,4},{0x3f1,4},{0x3f2,0x96},{0x3f3,4}}};
constexpr std::array<Tag, 11U> repeated{{
  {0xbb9,1},{0xbba,0x800},{0xbbb,4},{0xbbc,16},{0xbbd,256},{0xbbe,256},
  {0xbbf,8},{0xbc0,16},{0xfa0,4},{0xfa1,4},{0xfa2,4}}};
constexpr Ps2SaveBundleLimits limits{1048576U, 1U, 20U, 267U, 1048576U};
std::byte pattern(std::uint32_t tag, std::size_t lane, unsigned row) {
  return static_cast<std::byte>((tag * 13U + lane * 37U + row * 17U) ^ 0xa5U);
}
Bytes record(std::span<const Tag> tags, unsigned row) {
  Bytes bytes(8U);
  for (const auto &tag : tags) {
    append(bytes, tag.key); append(bytes, tag.size);
    for (std::size_t lane = 0U; lane < tag.size; ++lane)
      bytes.push_back(pattern(tag.key, lane, row));
    while (bytes.size() % 4U != 0U) bytes.push_back(std::byte{0U});
  }
  append(bytes, UINT32_MAX); append(bytes, 0U);
  put(bytes, 0U, static_cast<std::uint32_t>(bytes.size() - 8U));
  // Separate narrow checksum recurrence used only by the fixture writer.
  std::uint16_t checksum = 0x8320U;
  for (std::size_t at = 8U; at < bytes.size(); ++at) {
    checksum = static_cast<std::uint16_t>(checksum ^
        (std::to_integer<unsigned>(bytes[at]) << 8U));
    for (unsigned bit = 0U; bit < 8U; ++bit) {
      const bool carry = (checksum & 0x8000U) != 0U;
      checksum = static_cast<std::uint16_t>(checksum << 1U);
      if (carry) checksum ^= 0x1f45U;
    }
  }
  check(checksum != 0U, "Synthetic checksum is zero");
  put(bytes, 4U, checksum); return bytes;
}
Bytes descriptor_bytes(std::span<const Tag> tags, std::uint32_t base) {
  Bytes bytes((tags.size() + 1U) * 16U);
  // Reverse descriptor order: reset must search tags and retain TLV order.
  for (std::size_t index = 0U; index < tags.size(); ++index) {
    const auto ordinal = tags.size() - index - 1U;
    put(bytes, index * 16U, base + static_cast<std::uint32_t>(ordinal) * 0x10000U);
    put(bytes, index * 16U + 4U, tags[ordinal].size);
    put(bytes, index * 16U + 8U, tags[ordinal].key);
  }
  return bytes;
}
struct Fixture {
  Bytes envelope;
  Bytes primary_table = descriptor_bytes(primary, 0x100000U);
  Bytes repeated_table = descriptor_bytes(repeated, 0x800000U);
  Fixture() {
    Bytes icon(kPs2IconSysSize);
    icon[0U] = std::byte{'P'}; icon[1U] = std::byte{'S'};
    icon[2U] = std::byte{'2'}; icon[3U] = std::byte{'D'};
    Bytes model(kPs2MemoryCardIconHeaderSize + kPs2MemoryCardIconVertexSize +
                kPs2MemoryCardIconAnimationSize + kPs2MemoryCardIconTextureSize);
    put(model, 0U, kPs2MemoryCardIconVersion);
    put(model, 4U, kPs2MemoryCardIconShapeCount);
    put(model, 8U, kPs2MemoryCardIconTextureType); put(model, 16U, 1U);
    Bytes state; append(state, 0x1530U); append(state, 0xaa4U);
    const auto initial = record(primary, 0U);
    state.insert(state.end(), initial.begin(), initial.end());
    for (unsigned index = 0U; index < 20U; ++index) {
      const auto row = record(repeated, index);
      state.insert(state.end(), row.begin(), row.end());
    }
    envelope.resize(24U);
    const std::array<const Bytes *, 3U> records{&icon, &model, &state};
    for (std::size_t index = 0U; index < records.size(); ++index) {
      put(envelope, index * 8U, static_cast<std::uint32_t>(envelope.size()));
      put(envelope, index * 8U + 4U, static_cast<std::uint32_t>(records[index]->size()));
      envelope.insert(envelope.end(), records[index]->begin(), records[index]->end());
    }
    envelope.resize((envelope.size() + 2047U) / 2048U * 2048U);
  }
  RacFrontendResetTemplateV1 compile() const {
    return compile_rac_frontend_reset_template_v1(
        envelope, primary_table, repeated_table, limits);
  }
};

void action4() {
  RacFrontendAction4InputsV1 input;
  input.focused = true; input.global_pressed = 0x40U;
  input.target_screen = 0x1d5008U; input.sound_object = 0x135790U;
  input.flags_15efb4 = 0xabcdef04U;
  const auto result = execute_rac_frontend_action4_v1(input);
  check(result.entered_dialog && !result.requested_new_game &&
        result.writes.size() == 13U && result.sounds.size() == 1U,
        "Action 4 did not enter exactly its original dialog");
  check(result.sounds[0U].arguments == std::array<std::uint32_t, 3U>{0U,17U,0x135790U},
        "Action 4 sound ABI changed");
  check(last(result, 0x15efb4U) == 0xabcdef02U &&
        last(result, 0x193418U) == 0x1d5008U &&
        last(result, 0x193414U) == 3U && last(result, 0x15f6e8U) == 4U &&
        last(result, 0x193400U) == 3U && last(result, 0x193420U) == UINT32_MAX &&
        last(result, 0x193404U) == 30U && last(result, 0x193424U) == 30U &&
        last(result, 0x19342cU) == 10U && last(result, 0x13d440U) == 3U,
        "Action 4 dialog stores differ");
  input.focused = false;
  check(execute_rac_frontend_action4_v1(input).writes.empty(), "Inactive node ran");
  input.focused = true; input.flags_15efb4 |= 1U;
  check(!execute_rac_frontend_action4_v1(input).entered_dialog, "Busy confirm ran");
  input.flags_15efb4 &= ~1U;
  for (auto pressed : {0x100U, 0x400U, 0x800U, 0x10U}) {
    input.global_pressed = pressed | 0x40U;
    const auto cancel = execute_rac_frontend_action4_v1(input);
    check(cancel.return_word == -1 && cancel.writes.empty(), "Cancel precedence lost");
  }
  input.parent_screen = 0x1d4948U; input.global_pressed = 0x50U;
  check(last(execute_rac_frontend_action4_v1(input), 0x1d5f78U) == 0x1d4948U,
        "Parent-screen cancel changed");
  input.parent_screen = 0U; input.root_124 = 1U;
  check(execute_rac_frontend_action4_v1(input).entered_dialog, "Modal cancel gate changed");
  input.mode_15f6e8 = 0U;
  rejects([&] { (void)execute_rac_frontend_action4_v1(input); });
}

void title_input() {
  constexpr std::array<std::array<std::uint32_t, 3U>, 10U> gates{{
      {0U,0U,0U}, {0U,0x20U,0U}, {0U,0x40U,1U}, {0U,0x800U,1U},
      {0U,0x840U,1U}, {0U,0xf7bfU,0U}, {1U,0x840U,0U},
      {3U,0x840U,0U}, {4U,UINT32_MAX,0U}, {UINT32_MAX,0x800U,0U}}};
  for (const auto &gate : gates) {
    const auto result = execute_rac_frontend_title_input_v1(gate[0U], gate[1U]);
    check(result.requested_menu == (gate[2U] != 0U) &&
          result.writes.size() == (gate[2U] != 0U ? 5U : 0U),
          "Title mode/button gate changed");
  }
  const auto result = execute_rac_frontend_title_input_v1(0U, 0x40U);
  constexpr std::array<std::array<std::uint32_t, 3U>, 5U> expected{{
      {0x219e6cU,0x1d5f70U,45U}, {0x219e74U,0x1d6080U,0U},
      {0x219e7cU,0x15f6e8U,3U}, {0x219e80U,0x1d5f7cU,0U},
      {0x219e88U,0x1d5f80U,0U}}};
  for (std::size_t index = 0U; index < expected.size(); ++index)
    check(result.writes[index].source_pc == expected[index][0U] &&
          result.writes[index].source_address == expected[index][1U] &&
          word(result.writes[index]) == expected[index][2U],
          "Title initialization request stores changed");
}

void dialog_ready() {
  RacFrontendDialog3ReadyInputsV1 input;
  input.duration = 30U; input.draw_delay = 10U; input.age = UINT32_MAX;
  input.fade = 30U; input.target_screen = 0x1d5008U; input.ui_mode_15efb0 = 1U;
  input.card_b0 = 3U; // Actual store performed by action 4's dialog setup.
  const auto entered = execute_rac_frontend_dialog3_ready_v1(input);
  check(entered.requested_menu && entered.return_word == 1 && entered.writes.size() == 5U &&
        last(entered, 0x193404U) == 29U && last(entered, 0x19342cU) == 9U &&
        last(entered, 0x193420U) == 0U && last(entered, 0x1d5f78U) == 0x1d5008U &&
        last(entered, 0x15f6e8U) == 3U, "Ready dialog incorrectly waited for fade");
  input.card_b0 = 1U;
  const auto busy = execute_rac_frontend_dialog3_ready_v1(input);
  check(!busy.requested_menu && busy.writes.size() == 2U,
        "Busy card advanced the dialog age or changed screens");
  input.card_b0 = 0U; input.ui_mode_15efb0 = 16U; input.age = 29U;
  check(execute_rac_frontend_dialog3_ready_v1(input).writes.size() == 5U,
        "Dialog fade started at age 30 rather than after it");
  input.age = 30U;
  check(last(execute_rac_frontend_dialog3_ready_v1(input), 0x193424U) == 29U,
        "Dialog fade did not decrease after age 30");
  input.age = 0x7fffffffU;
  check(execute_rac_frontend_dialog3_ready_v1(input).writes.size() == 5U,
        "Dialog age wrap lost signed source comparison");
  input.ui_mode_15efb0 = 4U;
  rejects([&] { (void)execute_rac_frontend_dialog3_ready_v1(input); });
}

void reset_template() {
  Fixture fixture;
  const auto result = fixture.compile();
  check(result.copies.size() == 267U, "Reset lost tags or repeated rows");
  std::size_t at = 0U;
  auto verify = [&](std::span<const Tag> tags, std::uint32_t base, unsigned row) {
    for (std::size_t index = 0U; index < tags.size(); ++index) {
      const auto &copy = result.copies.at(at++);
      check(copy.source_pc == 0x20bed0U &&
            copy.source_address == base + index * 0x10000U + row * tags[index].size &&
            copy.bytes.size() == tags[index].size, "Reset descriptor search/stride/order changed");
      for (std::size_t lane = 0U; lane < copy.bytes.size(); ++lane)
        check(copy.bytes[lane] == pattern(tags[index].key, lane, row), "Reset payload changed");
    }
  };
  verify(primary, 0x100000U, 0U);
  for (unsigned row = 0U; row < 20U; ++row) verify(repeated, 0x800000U, row);
  const auto table = fixture.primary_table;
  for (const auto offset : {0U, 4U, 8U, 47U * 16U}) {
    fixture.primary_table = table; put(fixture.primary_table, offset, UINT32_MAX);
    rejects([&] { (void)fixture.compile(); });
  }
  fixture.primary_table = table; fixture.primary_table.pop_back();
  rejects([&] { (void)fixture.compile(); });
  fixture.primary_table = table; fixture.envelope[fixture.envelope.size() - 2048U] ^= std::byte{1U};
  rejects([&] { (void)fixture.compile(); });
}

RacFrontendNoSaveInputsV1 ready() {
  RacFrontendNoSaveInputsV1 input;
  input.focused = true; input.node_address = 0x1d50e8U;
  input.card_type = 2U; input.card_e4 = UINT32_MAX; input.root_154 = 11U;
  input.ui_mode_15efb0 = 1U; input.pressed = 0x20U;
  input.flags_15efb4 = 0xaaaa0006U;
  input.preserved_eef0_eeec_eee8 = {123U, 456U, 789U};
  return input;
}
void no_save() {
  const auto reset = Fixture{}.compile();
  auto input = ready();
  const auto result = execute_rac_frontend_no_save_v1(input, &reset);
  check(result.requested_new_game && result.writes.size() == 280U,
        "No-save action failed to perform the complete reset");
  check(result.writes[3U].source_pc == 0x224be4U &&
        result.writes[4U].source_pc == 0x224bf0U &&
        result.writes[5U] == reset.copies.front(), "Reset ordering changed");
  check(last(result, 0x15efb4U) == 0xaaaa0000U &&
        last(result, 0x13d48cU) == 0U && last(result, 0x15ee84U) == 0U &&
        last(result, 0x15eef0U) == 123U && last(result, 0x15eeecU) == 456U &&
        last(result, 0x15eee8U) == 789U && last(result, 0x15f6e4U) == 0U &&
        last(result, 0x15f6fcU) == 1U && last(result, 0x15f690U) == 1U,
        "Fresh reset/request postconditions changed");
  check(result.writes.back().source_pc == 0x224bccU &&
        result.writes.back().bytes == Bytes{std::byte{1U}, std::byte{0U}},
        "Entry halfword width/order changed");
  rejects([&] { (void)execute_rac_frontend_no_save_v1(input); });
  for (auto member : {&RacFrontendNoSaveInputsV1::card_dc,
                      &RacFrontendNoSaveInputsV1::card_e4,
                      &RacFrontendNoSaveInputsV1::root_154,
                      &RacFrontendNoSaveInputsV1::card_type}) {
    auto blocked = input;
    blocked.*member = member == &RacFrontendNoSaveInputsV1::card_dc ? 3U : 0U;
    check(!execute_rac_frontend_no_save_v1(blocked).requested_new_game,
          "Save-owner readiness gate was bypassed");
  }
  input.global_pressed = 0x940U;
  check(execute_rac_frontend_no_save_v1(input).return_word == 1,
        "Global exit gate precedence changed");
  input.global_pressed = 0x10U;
  check(execute_rac_frontend_no_save_v1(input).return_word == -1,
        "Source back return changed");
  input = ready(); input.node_flags = 1U;
  check(!execute_rac_frontend_no_save_v1(input).requested_new_game,
        "Repeat selector used pressed source");
  input.repeated = 0x20U;
  check(execute_rac_frontend_no_save_v1(input, &reset).requested_new_game,
        "Repeat-selected no-save action failed");
  input = ready(); input.node_flags = 0x2000U;
  rejects([&] { (void)execute_rac_frontend_no_save_v1(input, &reset); });
  input = ready(); input.node_phase = 1U;
  rejects([&] { (void)execute_rac_frontend_no_save_v1(input, &reset); });
  input = ready(); input.pressed = 0x60U;
  rejects([&] { (void)execute_rac_frontend_no_save_v1(input, &reset); });
  input = ready(); input.pressed = 0x5000U; input.saved_selection_15ef34 = 0U;
  const auto navigation = execute_rac_frontend_no_save_v1(input);
  check(last(navigation, 0x15ef34U) == 1U && navigation.sounds.size() == 1U,
        "Simultaneous up/down source ordering changed");
}

// Optional ignored source artifact produced by trace_rac_frontend_input.py.
// It contains the locally supplied source template, never a tracked fixture.
void source_comparison(const char *path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  check(bool(stream), "Cannot open source frontend comparison fixture");
  const auto size = stream.tellg();
  check(size >= 8 && size <= 1048576, "Source frontend fixture exceeds its bound");
  stream.seekg(0);
  Bytes bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char *>(bytes.data()), size);
  check(bool(stream), "Source frontend fixture read failed");
  const std::array<char, 8U> magic{'F','R','O','N','T','I','N','1'};
  for (std::size_t index = 0U; index < magic.size(); ++index)
    check(bytes[index] == static_cast<std::byte>(magic[index]), "Wrong source fixture version");
  std::size_t cursor = 8U;
  auto get = [&]() {
    check(cursor <= bytes.size() && bytes.size() - cursor >= 4U,
          "Truncated source fixture word");
    std::uint32_t value = 0U;
    for (unsigned lane = 0U; lane < 4U; ++lane)
      value |= std::to_integer<std::uint32_t>(bytes[cursor++]) << (8U * lane);
    return value;
  };
  auto blob = [&]() {
    const auto count = get();
    check(cursor <= bytes.size() && count <= bytes.size() - cursor,
          "Truncated source fixture payload");
    Bytes result(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                 bytes.begin() + static_cast<std::ptrdiff_t>(cursor + count));
    cursor += count; return result;
  };
  const auto envelope = blob(), primary_table = blob(), repeated_table = blob();
  auto effects = [&]() {
    const auto count = get();
    check(count <= 1024U, "Source fixture has too many writes");
    std::vector<RacFrontendInputWriteV1> result;
    for (std::uint32_t index = 0U; index < count; ++index) {
      const auto pc = get(), address = get();
      result.push_back({pc, address, blob()});
    }
    return result;
  };
  const auto action_writes = effects(), fresh_writes = effects();
  check(cursor == bytes.size(), "Source fixture has trailing bytes");
  const auto reset = compile_rac_frontend_reset_template_v1(
      envelope, primary_table, repeated_table,
      {1048576U, 10000U, 20U, 267U, 1048576U});
  RacFrontendAction4InputsV1 action;
  action.focused = true; action.global_pressed = 0x40U;
  action.target_screen = 0x1d5008U; action.sound_object = 0x135790U;
  action.flags_15efb4 = 0xabcdef04U;
  check(execute_rac_frontend_action4_v1(action).writes == action_writes,
        "Action 4 native/source write mismatch");
  const auto fresh = execute_rac_frontend_no_save_v1(ready(), &reset);
  check(fresh.writes == fresh_writes, "Fresh reset/request native/source write mismatch");
  std::cout << "rac_frontend_input_source: " << action_writes.size()
            << " action writes, " << fresh_writes.size()
            << " fresh writes, " << reset.copies.size() << " copies matched\n";
}
} // namespace
int main(int argc, char **argv) {
  try {
    title_input(); action4(); dialog_ready(); reset_template(); no_save();
    check(argc <= 2, "Expected at most one optional source fixture path");
    if (argc == 2) source_comparison(argv[1]);
    std::cout << "rac_frontend_input_tests: 5 groups passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rac_frontend_input_tests: " << error.what() << '\n';
    return 1;
  }
}
