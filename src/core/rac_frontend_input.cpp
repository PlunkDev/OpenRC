#include "openrc/rac_frontend_input.hpp"

#include "openrc/rac_frontend_new_game.hpp"
#include "openrc/rac_initial_progress_template.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace openrc {
namespace {
constexpr std::uint32_t kRoot = 0x1d5f70U;

[[noreturn]] void fail(const char *message) {
  throw RacFrontendInputError(message);
}

std::uint32_t read32(std::span<const std::byte> bytes, std::size_t at) {
  std::uint32_t value = 0U;
  for (unsigned lane = 0U; lane < 4U; ++lane)
    value |= std::to_integer<std::uint32_t>(bytes[at + lane]) << (8U * lane);
  return value;
}

void write(RacFrontendInputResultV1 &result, std::uint32_t pc,
           std::uint32_t address, std::uint32_t value, unsigned width = 4U) {
  RacFrontendInputWriteV1 effect{pc, address, {}};
  for (unsigned lane = 0U; lane < width; ++lane)
    effect.bytes.push_back(static_cast<std::byte>(value >> (8U * lane)));
  result.writes.push_back(std::move(effect));
}

std::int32_t signed_word(std::uint32_t word) {
  return std::bit_cast<std::int32_t>(word);
}

struct Descriptor {
  std::uint32_t destination;
  std::uint32_t size;
  std::uint32_t tag;
};

std::vector<Descriptor> descriptors(std::span<const std::byte> bytes,
                                    std::size_t count,
                                    const Ps2SaveTlvRecord &record) {
  if (bytes.size() != (count + 1U) * 16U ||
      read32(bytes, count * 16U) != 0U || record.entries.size() != count)
    fail("Frontend reset descriptor table has an invalid bounded envelope");
  std::vector<Descriptor> result;
  for (std::size_t index = 0U; index < count; ++index) {
    const auto at = index * 16U;
    Descriptor entry{read32(bytes, at), read32(bytes, at + 4U),
                     read32(bytes, at + 8U)};
    const auto found = std::find_if(record.entries.begin(), record.entries.end(),
        [&](const auto &item) { return item.key == entry.tag; });
    if (entry.destination == 0U || found == record.entries.end() ||
        found->payload.size() != entry.size ||
        std::any_of(result.begin(), result.end(), [&](const auto &previous) {
          return previous.tag == entry.tag;
        }))
      fail("Frontend reset descriptor tag or size does not match its payload");
    const auto repetitions = count == 47U ? 1U : 20U;
    if (std::uint64_t{entry.destination} +
            std::uint64_t{entry.size} * repetitions > 0x2000000U)
      fail("Frontend reset descriptor destination exceeds source memory bounds");
    result.push_back(entry);
  }
  return result;
}

void append_record(RacFrontendResetTemplateV1 &result,
                   const Ps2SaveTlvRecord &record,
                   const std::vector<Descriptor> &table, std::uint32_t index) {
  // 20bd70 visits input TLVs in stream order and searches the descriptor tag.
  for (const auto &entry : record.entries) {
    const auto found = std::find_if(table.begin(), table.end(),
        [&](const auto &row) { return row.tag == entry.key; });
    if (found == table.end() || entry.payload.size() != found->size)
      fail("Frontend repeated reset record does not match its descriptors");
    result.copies.push_back({0x20bed0U,
        found->destination + index * found->size, entry.payload});
  }
}
} // namespace

RacFrontendInputResultV1 execute_rac_frontend_title_input_v1(
    std::uint32_t mode_15f6e8, std::uint32_t pressed_13cbe4) {
  RacFrontendInputResultV1 result;
  if (mode_15f6e8 != 0U || (pressed_13cbe4 & 0x840U) == 0U)
    return result;
  write(result, 0x219e6cU, kRoot, 45U);
  write(result, 0x219e74U, kRoot + 0x110U, 0U);
  write(result, 0x219e7cU, 0x15f6e8U, 3U);
  write(result, 0x219e80U, kRoot + 0xcU, 0U);
  write(result, 0x219e88U, kRoot + 0x10U, 0U);
  result.requested_menu = true;
  return result;
}

RacFrontendInputResultV1 execute_rac_frontend_action4_v1(
    const RacFrontendAction4InputsV1 &input) {
  RacFrontendInputResultV1 result;
  if (!input.focused) return result;
  if (input.node_flags != 4U)
    fail("Frontend action 4 requires the recovered title-node flags");
  if ((input.global_pressed & 0xd00U) != 0U) {
    result.return_word = -1;
    return result;
  }
  if ((input.global_pressed & 0x10U) != 0U) {
    if (input.parent_screen != 0U) {
      write(result, 0x21bd18U, kRoot + 8U, input.parent_screen);
      return result;
    }
    if (input.root_124 == 0U) {
      result.return_word = -1;
      return result;
    }
  }
  if ((input.global_pressed & 0x40U) == 0U ||
      (input.flags_15efb4 & 1U) != 0U)
    return result;
  if (input.target_screen == 0U || input.mode_15f6e8 != 3U)
    fail("Frontend action 4 requires its owned target and title mode 3");
  const auto duration = static_cast<std::uint32_t>(
      evaluate_rac_frontend_timer_v1(30U, input.time_scale_bits));
  const auto age = static_cast<std::uint32_t>(
      evaluate_rac_frontend_timer_v1(10U, input.time_scale_bits));
  result.sounds.push_back({0x21bdb8U, {0U, 17U, input.sound_object}});
  write(result, 0x21bdc8U, kRoot + 0x150U, 0U);
  write(result, 0x21bdf8U, 0x15efb4U, (input.flags_15efb4 | 2U) & ~4U);
  // Inline 1fbc80(3, selected row target, 0). Mode 3 skips the two
  // gameplay-only callees at 1fbcc0/1fbcc8. No text lookup on this arm.
  write(result, 0x1fbcbcU, 0x193430U, 0U);
  write(result, 0x1fbcdcU, 0x193418U, input.target_screen);
  write(result, 0x1fbce4U, 0x193414U, input.mode_15f6e8);
  write(result, 0x1fbcecU, 0x15f6e8U, 4U);
  write(result, 0x1fbcf0U, 0x193400U, 3U);
  write(result, 0x1fbcf8U, 0x19342cU, 0U);
  write(result, 0x1fbe08U, 0x193420U, UINT32_MAX);
  write(result, 0x1fbe10U, 0x193404U, duration);
  write(result, 0x1fbe1cU, 0x13d440U, 3U);
  write(result, 0x1fbe28U, 0x193424U, duration);
  write(result, 0x1fbe30U, 0x19342cU, age);
  result.entered_dialog = true;
  return result;
}

RacFrontendInputResultV1 execute_rac_frontend_dialog3_ready_v1(
    const RacFrontendDialog3ReadyInputsV1 &input) {
  RacFrontendInputResultV1 result;
  // 1fded0/1fded4 returns one for retained dialog mode 4 or restored menu 3.
  result.return_word = 1;
  if (input.duration != 0U)
    write(result, 0x1fd418U, 0x193404U, input.duration - 1U);
  if (input.draw_delay != 0U)
    write(result, 0x1fd428U, 0x19342cU, input.draw_delay - 1U);
  if (input.card_b0 == 1U) return result;
  const auto age = input.age + 1U;
  write(result, 0x1fd980U, 0x193420U, age);
  const auto timer = static_cast<std::uint32_t>(
      evaluate_rac_frontend_timer_v1(30U, input.time_scale_bits));
  if (signed_word(timer) < signed_word(age) && input.fade != 0U)
    write(result, 0x1fd9a0U, 0x193424U, input.fade - 1U);
  if ((input.ui_mode_15efb0 != 1U && input.ui_mode_15efb0 != 16U) ||
      input.previous_mode != 3U || input.target_screen == 0U)
    fail("Frontend dialog requires its recovered ready-card title state");
  write(result, 0x1fdbfcU, kRoot + 8U, input.target_screen);
  write(result, 0x1fdc04U, 0x15f6e8U, input.previous_mode);
  result.requested_menu = true;
  return result;
}

RacFrontendResetTemplateV1 compile_rac_frontend_reset_template_v1(
    std::span<const std::byte> ps2d_envelope,
    std::span<const std::byte> primary_descriptors,
    std::span<const std::byte> repeated_descriptors,
    Ps2SaveBundleLimits limits) {
  // Reuse the established complete layout/checksum validation. It also
  // preflights every bounded allocation made by the structural parser.
  (void)parse_rac_initial_progress_template_v1(ps2d_envelope, limits);
  const auto bundle = parse_ps2_save_bundle(ps2d_envelope, limits);
  const auto &state = bundle.save_template;
  const auto primary = descriptors(primary_descriptors, 47U, state.primary_record);
  const auto repeated = descriptors(repeated_descriptors, 11U,
                                    state.repeated_records.front());
  RacFrontendResetTemplateV1 result;
  result.copies.reserve(267U);
  append_record(result, state.primary_record, primary, 0U);
  for (std::uint32_t index = 0U; index < 20U; ++index)
    append_record(result, state.repeated_records[index], repeated, index);
  return result;
}

RacFrontendInputResultV1 execute_rac_frontend_no_save_v1(
    const RacFrontendNoSaveInputsV1 &input,
    const RacFrontendResetTemplateV1 *reset_template) {
  RacFrontendInputResultV1 result;
  if (!input.focused) return result;
  if (input.node_address == 0U || input.node_address > 0x1ffffb0U)
    fail("Frontend input node has no bounded source address");
  if (input.node_phase == 1U ||
      (input.node_phase == 0U && input.previous_result != 0U &&
       (input.previous_screen == 0x1d51b8U ||
        input.previous_screen == 0x1d5318U)) || input.root_128 != 0U)
    fail("Frontend card-save branch is outside the no-save input contract");
  write(result, 0x22481cU, input.node_address + 0x4cU, 2U);
  if ((input.global_pressed & 0xd00U) != 0U && input.root_124 == 0U) {
    result.return_word = 1;
    return result;
  }
  if ((input.global_pressed & 0x10U) != 0U) {
    if (input.parent_screen != 0U) {
      write(result, 0x2249ecU, kRoot + 8U, input.parent_screen);
      return result;
    }
    if (input.root_124 == 0U) {
      result.return_word = -1;
      return result;
    }
  }
  if (input.ui_mode_15efb0 != 1U && input.ui_mode_15efb0 != 16U) {
    write(result, 0x224a2cU, kRoot + 8U, input.parent_screen);
    return result;
  }
  if (signed_word(input.card_dc) >= 3 || signed_word(input.card_e4) >= 0 ||
      signed_word(input.root_154) < 11 || input.card_type != 2U)
    return result;
  const auto pressed = (input.node_flags & 1U) != 0U ? input.repeated : input.pressed;
  auto selection = input.saved_selection_15ef34;
  write(result, 0x224aa4U, input.node_address + 0x40U, selection);
  if ((pressed & 0x1000U) != 0U && selection != 0U) {
    --selection;
    write(result, 0x224ab0U, input.node_address + 0x40U, selection);
  }
  if ((pressed & 0x4000U) != 0U && signed_word(selection) < 4) {
    ++selection;
    write(result, 0x224ad0U, input.node_address + 0x40U, selection);
  }
  write(result, 0x224ae0U, 0x15ef34U, selection);
  if ((pressed & 0x40U) != 0U)
    fail("Frontend slot-confirm/save branch is outside the no-save contract");
  if ((pressed & 0x20U) == 0U) {
    if (selection != input.node_selection)
      result.sounds.push_back({0x224c0cU, {1U, 17U, input.sound_object}});
    return result;
  }
  if ((input.node_flags & 0x2000U) != 0U)
    fail("Frontend preserving-reset branch is outside the fresh-game contract");
  if (reset_template == nullptr || reset_template->copies.size() != 267U)
    fail("Frontend new game requires the complete compiled reset template");
  // Reject forged/truncated reset effects before producing a partial reset.
  for (const auto &copy : reset_template->copies)
    if (copy.source_pc != 0x20bed0U || copy.source_address == 0U ||
        copy.bytes.empty() || copy.bytes.size() > 0x800U ||
        copy.source_address + std::uint64_t{copy.bytes.size()} > 0x2000000U)
      fail("Frontend reset template contains an invalid destination copy");
  write(result, 0x224be4U, 0x13d48cU, 0U);
  write(result, 0x224bf0U, 0x15efb4U, input.flags_15efb4 & ~6U);
  result.writes.insert(result.writes.end(), reset_template->copies.begin(),
                       reset_template->copies.end());
  write(result, 0x209e34U, 0x15eef0U, input.preserved_eef0_eeec_eee8[0U]);
  write(result, 0x209e3cU, 0x15eeecU, input.preserved_eef0_eeec_eee8[1U]);
  write(result, 0x209e44U, 0x15eee8U, input.preserved_eef0_eeec_eee8[2U]);
  write(result, 0x209e5cU, 0x15ee84U, 0U);
  write(result, 0x22f4a8U, 0x15f6e4U, 0U);
  write(result, 0x22f4b0U, 0x15f6fcU, 1U);
  write(result, 0x22f4b8U, 0x15f690U, 1U);
  write(result, 0x224bccU, 0x13e15aU, 1U, 2U);
  result.requested_new_game = true;
  return result;
}

} // namespace openrc
