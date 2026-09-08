#include "openrc/rac_frontend_new_game.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <optional>

namespace openrc {
namespace {
using U32 = std::uint32_t;
using U64 = std::uint64_t;
using Bytes = std::vector<std::byte>;
using Numeric = RacFrontendNewGameNumericV1;
using NK = RacFrontendNewGameNumericKindV1;
using Control = RacFrontendNewGameControlV1;
using CK = Control::Kind;
std::int32_t s32(U32 word) { return std::bit_cast<std::int32_t>(word); }
U64 sx(U32 word) {
  return static_cast<U64>(static_cast<std::int64_t>(s32(word)));
}
std::int16_t s16(U32 word) {
  return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(word));
}
U32 asr4(U32 word) {
  return (word >> 4U) | ((word & 0x80000000U) ? 0xf0000000U : 0U);
}
U32 half_zero(U32 word) {
  const auto value = word + (word >> 31U);
  return (value >> 1U) | (value & 0x80000000U);
}
void put(std::span<std::byte> bytes, std::size_t at, U64 value,
         std::size_t count) {
  for (std::size_t i = 0U; i < count; ++i)
    bytes[at + i] = static_cast<std::byte>(value >> (8U * i));
}
U64 get(std::span<const std::byte> bytes) {
  U64 result = 0U;
  for (std::size_t i = 0U; i < bytes.size(); ++i)
    result |= std::to_integer<U64>(bytes[i]) << (8U * i);
  return result;
}
void preamble(std::span<std::byte> bytes, U32 reg, U64 value) {
  put(bytes, 0U, 0x10000002U, 4U);
  put(bytes, 12U, 0x50000002U, 4U);
  put(bytes, 16U, 0x1000000000008001ULL, 8U);
  put(bytes, 24U, 0xeU, 8U);
  put(bytes, 32U, value, 8U);
  put(bytes, 40U, reg, 8U);
}

class Owner {
public:
  explicit Owner(const RacFrontendNewGameInputsV1 &input) : in(input) {
    out.final_node_bytes = in.node_bytes;
    if (in.node_address == 0U || (in.node_address & 3U) != 0U ||
        static_cast<U64>(in.node_address) + 80U > 0x100000000ULL ||
        in.source.size() > in.limits.max_source_regions)
      fail("Unowned/wrapping callback node or excessive source regions");
    U64 total = 0U;
    for (std::size_t i = 0U; i < in.source.size(); ++i) {
      const auto &a = in.source[i];
      const auto end = static_cast<U64>(a.address) + a.bytes.size();
      if (!a.address || a.bytes.empty() || end > 0x100000000ULL ||
          a.bytes.size() > in.limits.max_source_bytes - total)
        fail("Invalid or excessive callback source ownership");
      total += a.bytes.size();
      if (a.address < static_cast<U64>(in.node_address) + 80U &&
          in.node_address < end)
        fail("Immutable callback source aliases mutable node");
      for (std::size_t j = 0U; j < i; ++j) {
        const auto &b = in.source[j];
        if (a.address < static_cast<U64>(b.address) + b.bytes.size() &&
            b.address < end)
          fail("Callback source owners overlap");
      }
    }
  }

  RacFrontendNewGamePlanV1 run() {
    U32 flags = node(0x30U);
    out.font_source_id = (flags & 8U) ? 3U : 1U;
    if (flags & 16U)
      out.font_source_id = 2U;
    preamble(std::span(out.preamble_packets).first(48U), 0x42U, 0x44U);
    preamble(std::span(out.preamble_packets).subspan(48U), 0x47U, 0x2004bU);
    control(CK::preamble, 0x21b30cU);

    U32 selection = 0U, alternative = 0U;
    flags = node(0x30U);
    // Original21b330 is an unconditional branch delay-slot LW, even when
    // flag20 is clear. Its source owner is required, not optimized away.
    const auto current_selection_word = read(0x15ee84U);
    if (flags & 0x20U) {
      selection = current_selection_word - 1U;
      if (selection >= 18U)
        selection = 0xffffffffU;
    } else if (flags & 0x40U) {
      selection = read(0x1a0414U) - 1U;
    } else if (flags & 4U) {
      const auto time = timer(0x21b36cU);
      if (s32(node(0x44U)) < s32(time))
        write(0x21b38cU, 0x44U, timer(0x21b384U));
      write(0x21b394U, 0x48U, 0U);
      write(0x21b39cU, 0x4cU, 0U);
    } else if (flags & 0x80U) {
      selection = read(active() + 0x40U);
      if (flags & 0x8000U)
        alternative = read(0x15ee80U) != 0U;
    } else if (flags & 0x100U) {
      const auto selected = active();
      selection = read(selected + 0x3cU);
      const auto row = read(selected + 0x48U) + selection * 10U;
      const auto first = s16(read(row + 4U, 2U));
      const auto second = static_cast<U32>(s16(read(row + 6U, 2U)));
      if (read((first == 0 ? 0x13d5c8U : 0x13d490U) + second, 1U) == 0U)
        selection = 0xffffffffU;
    } else if (flags & 0x1000U) {
      const auto selected = active();
      selection = read(selected + 0x40U);
      const auto row = read(selected + 0x34U) + selection * 12U;
      const auto key = s16(read(row, 2U));
      write(0x21b460U, 0x34U, 0xffffU);
      // Original1ff4f8(key,1,node+34): first match among exactly150 rows.
      for (U32 i = 0U; i < 150U; ++i) {
        if (s16(read(0x199812U + i * 4U, 2U)) == key) {
          write(0x1ff534U, 0x34U, read(0x199810U + i * 4U, 2U), 2U);
          break;
        }
      }
    } else {
      const auto selected = active();
      const auto row = read(selected + 0x48U) + read(selected + 0x3cU) * 10U;
      selection = static_cast<U32>(s16(read(row + 6U, 2U)));
      alternative = read(0x13e620U + selection, 1U) != 0U;
    }

    if (node(0x44U) == 0xffffffffU) {
      write(0x21b4bcU, 0x44U, timer(0x21b4b4U));
      write(0x21b4c0U, 0x48U, selection);
      write(0x21b4c4U, 0x4cU, alternative);
    }
    if (selection != node(0x48U)) {
      if (s32(timer(0x21b4d4U)) < s32(node(0x44U)))
        write(0x21b4f4U, 0x44U, timer(0x21b4ecU));
      U32 value = node(0x44U);
      for (unsigned i = 0U; i < 3U; ++i)
        value = s32(value) < 1 ? 0U : value - 1U;
      write(0x21b524U, 0x44U, value);
      if (value != 0U) {
        selection = node(0x48U);
        alternative = node(0x4cU);
      } else {
        write(0x21b53cU, 0x48U, selection);
        write(0x21b544U, 0x4cU, alternative);
        write(0x21b548U, 0x30U, node(0x30U) & ~0x400U);
        write(0x21b550U, 0x3cU, 0U);
      }
    } else {
      write(0x21b558U, 0x44U, node(0x44U) + 3U);
    }

    flags = node(0x30U);
    std::optional<Bytes> text;
    if (flags & 4U) {
      if (node(0x34U) == 0U)
        return finish();
      text = lookup(0x21b5e4U, node(0x34U));
    } else if (flags & 0x1000U) {
      if (node(0x34U) == 0xffffU)
        return finish();
      text = lookup(0x21b5e4U, node(0x34U));
    } else if ((flags & 0x100U) && selection == 0xffffffffU) {
      text = string(0x160378U);
    } else if (node(0x34U)) {
      const auto address =
          node(0x34U) + (alternative << 2U) + ((selection * node(0x38U)) & ~3U);
      text = lookup(0x21b5e4U, read(address));
    } else {
      text = string(0x160370U);
    }
    flags = node(0x30U);
    if (!(flags & 0x11e4U) && read(0x13d5c8U + selection, 1U) == 0U)
      text = string(0x160378U);
    if (flags & 0x200U) {
      const auto key = read(node(0x34U) + ((selection * node(0x38U)) & ~3U));
      if (key != 20178U && key != 20185U && key != 20189U) {
        const auto prefix = lookup(0x21b66cU, 20172U);
        if (!prefix || !text)
          fail("Original formatting has a null string argument");
        text = format(*prefix, *text);
        control(CK::format, 0x21b680U, 0x160380U);
      }
    }

    U32 local_flags = node(0x30U), anchor_x = 4U, anchor_y = 4U;
    if ((local_flags & 0x4004U) == 0x4004U && node(0x34U) == 21059U) {
      local_flags |= 1U;
      anchor_y = 12U;
    }
    if ((node(0x30U) & 0x800U) && read(0x13d510U + selection, 1U) == 0U) {
      local_flags |= 3U;
      text = lookup(0x21b6e0U, 20308U);
    }
    if (!text)
      text = string(0x160388U);
    U32 layout_flags = 8U;
    if (local_flags & 1U) {
      layout_flags = 9U;
      anchor_x = half_zero(node(0x20U));
    }
    if (local_flags & 2U) {
      layout_flags |= 2U;
      anchor_y = half_zero(node(0x24U));
    }

    if (!in.font_metrics)
      fail("Reached callback font has no original metrics");
    out.batch_reached = true;
    control(CK::begin, 0x21b73cU);
    const std::array<U32, 1U> ids{out.font_source_id};
    out.bindings = plan_rac_frontend_gs_bindings_v1(
        in.textures, ids, in.texture_payload, in.allocator_begin, 1U);
    tex0 = out.bindings.bindings.front().tex0;
    control(CK::bind, 0x21b744U, tex0);

    RacTextLayoutBoxV1 box;
    const auto top = read(0x160358U, 2U);
    box.top = s16(top);
    box.bottom = s16((node(0x24U) & 0xffffU) - top);
    box.left = 1;
    box.right = s16((node(0x20U) & 0xffffU) - 4U);
    box.anchor_x = s16(anchor_x);
    box.anchor_y = s16(anchor_y - asr4(node(0x3cU)));
    box.line_spacing = s16(read(0x160368U, 2U));
    box.flags = static_cast<std::uint16_t>(layout_flags);
    box.subpixel_y = s16(0U - (node(0x3cU) & 15U));
    if (node(0x30U) & 0x10000U)
      box.bottom = s16((node(0x24U) & 0xffffU) - 1U);
    const auto mixed =
        numeric(NK::color_mix_1fa8a8, 0x21b814U,
                {sx(read(0x1602b0U)), sx(0x80ffa888U), 0U}, 0x3f000000U);
    const auto color = numeric(NK::timed_color_21c6c0, 0x21b828U,
                               {sx(node(0x44U)), mixed, sx(0x80ffa888U)});
    for (U32 i = 0U; i < 8U; ++i)
      out.final_palette[i] = read(0x18cbf8U + i * 4U);
    out.final_inline_colors_enabled = read(0x15f59cU) != 0U;

    box.flags |= 4U;
    layout(0x21b854U, box, color, *text);
    box.flags ^= 4U;
    flags = node(0x30U);
    if ((flags & 0x2000U) ||
        static_cast<std::int32_t>(box.height) + 4 < box.bottom - box.top) {
      if (flags & 0x400U) {
        write(0x21b8ccU, 0x3cU, 0U);
        write(0x21b8d0U, 0x30U, flags ^ 0x400U);
      }
    } else if (!(flags & 0x400U)) {
      write(0x21b8a8U, 0x30U, flags | 0x400U);
      write(0x21b8b8U, 0x3cU, 0U - (node(0x24U) << 3U));
    }
    box.anchor_y = s16(anchor_y - asr4(node(0x3cU)));
    shadow_offset(box, true);
    colors(0x21b92cU, false);
    layout(0x21b94cU, box, 0x80000000ULL, *text);
    colors(0x21b954U, true);
    shadow_offset(box, false);
    if (node(0x30U) & 0x20000U)
      colors(0x21b9b8U, false);
    layout(0x21b9d4U, box, color, *text);
    if (node(0x30U) & 0x20000U)
      colors(0x21b9ecU, true);
    // Original21ba00 reads this word even when the no-scroll branch is taken.
    const auto repeat_spacing_word = read(0x160368U);
    if (node(0x30U) & 0x400U) {
      const auto gap = 3U * repeat_spacing_word;
      box.anchor_y = s16(static_cast<std::uint16_t>(box.anchor_y) +
                         static_cast<std::uint16_t>(box.height) + gap);
      shadow_offset(box, true);
      colors(0x21ba64U, false);
      layout(0x21ba84U, box, 0x80000000ULL, *text);
      colors(0x21ba8cU, true);
      shadow_offset(box, false);
      layout(0x21baf8U, box, color, *text);
      const auto final_spacing_word = read(0x160368U); // delay slot21bb0c
      if (node(0x30U) & 0x400U) {
        const auto divisor =
            (static_cast<U32>(box.height) + 3U * final_spacing_word) << 4U;
        const auto increment = (read(0x13cbe0U) & 1U) ? 10U : 3U;
        const auto next = node(0x3cU) + increment;
        write(0x21bb44U, 0x3cU, next);
        if (!divisor)
          fail("Original scroll remainder reaches division BREAK");
        const auto remainder = static_cast<std::int64_t>(s32(next)) %
                               static_cast<std::int64_t>(s32(divisor));
        write(0x21bb54U, 0x3cU, static_cast<U32>(remainder));
      }
    }
    control(CK::end, 0x21bb58U);
    out.returned_word = 2U;
    return finish();
  }

private:
  const RacFrontendNewGameInputsV1 &in;
  RacFrontendNewGamePlanV1 out;
  std::size_t numeric_cursor = 0U;
  U32 glyph_operations = 0U;
  U64 tex0 = 0U;

  [[noreturn]] static void fail(const char *message) {
    throw RacFrontendNewGameError(message);
  }
  void effect(RacFrontendNewGameEffectV1 value) {
    if (out.effects.size() >= in.limits.max_effects)
      fail("Callback effect budget exhausted");
    out.effects.push_back(std::move(value));
  }
  void control(CK kind, U32 pc, U64 value = 0U) {
    effect(Control{kind, pc, value});
  }
  std::span<const std::byte> owned(U32 address, U32 size) const {
    if (!address || static_cast<U64>(address) + size > 0x100000000ULL)
      fail("Invalid source callback read");
    if (address >= in.node_address &&
        static_cast<U64>(address) + size <=
            static_cast<U64>(in.node_address) + 80U)
      return std::span(out.final_node_bytes)
          .subspan(address - in.node_address, size);
    for (const auto &region : in.source)
      if (address >= region.address &&
          static_cast<U64>(address) + size <=
              static_cast<U64>(region.address) + region.bytes.size())
        return region.bytes.subspan(address - region.address, size);
    fail("Reached callback read has no bounded source owner");
  }
  U32 read(U32 address, U32 width = 4U) const {
    if ((address & (width - 1U)) != 0U)
      fail("Misaligned original callback scalar read");
    return static_cast<U32>(get(owned(address, width)));
  }
  U32 node(U32 offset) const { return read(in.node_address + offset); }
  void write(U32 pc, U32 offset, U32 value, U32 width = 4U) {
    if (offset > 80U || width > 80U - offset || (width != 2U && width != 4U) ||
        (offset & (width - 1U)))
      fail("Callback write escapes original node");
    if (width == 2U)
      value &= 0xffffU;
    put(out.final_node_bytes, offset, value, width);
    effect(RacFrontendNewGameWriteV1{pc, offset, width, value});
  }
  U32 active() const {
    const auto root = read(0x1d5f74U);
    if (!root)
      fail("Reached original active-node dereference is null");
    const auto selected = read(root + 0x40U);
    if (!selected)
      fail("Reached original selector pointer is null");
    return selected;
  }
  U64 numeric(NK kind, U32 pc, std::array<U64, 3U> args, U32 single = 0U) {
    if (numeric_cursor == in.numeric_observations.size())
      fail("Reached unqualified numeric call has no explicit observation");
    const auto &value = in.numeric_observations[numeric_cursor++];
    if (value.kind != kind || value.source_call_pc != pc ||
        value.arguments != args || value.single_argument_bits != single)
      fail("Numeric observation does not match actual reached source call");
    if (kind == NK::timer_1f98c0 &&
        value.returned_low64 != sx(static_cast<U32>(value.returned_low64)))
      fail("Timer observation violates original MFC1 sign extension");
    effect(value);
    return value.returned_low64;
  }
  U32 timer(U32 pc) {
    return static_cast<U32>(
        numeric(NK::timer_1f98c0, pc, {sx(read(0x1602b4U)), 0U, 0U}));
  }
  Bytes string(U32 address) const {
    std::span<const std::byte> owner;
    for (const auto &region : in.source)
      if (address >= region.address &&
          address < static_cast<U64>(region.address) + region.bytes.size()) {
        owner = region.bytes.subspan(address - region.address);
        break;
      }
    if (owner.empty())
      fail("Original callback string has no immutable source owner");
    Bytes result;
    for (std::size_t i = 0U; i < in.limits.max_text_bytes && i < owner.size();
         ++i) {
      const auto byte = owner[i];
      result.push_back(byte);
      if (byte == std::byte{0U})
        return result;
    }
    fail("Unterminated or oversized original callback text");
  }
  std::optional<Bytes> lookup(U32 pc, U32 key) {
    if (!in.text_bank ||
        in.text_bank->entries.size() > in.limits.max_bank_entries ||
        in.relocated_text_addresses.size() != in.text_bank->entries.size())
      fail("Callback has no bounded original text bank/relocation owners");
    const auto *entry = find_rac_text_bank_entry_v1(*in.text_bank, key);
    if (!entry) {
      effect(RacFrontendNewGameTextV1{pc, key, -1, 0x199a68U});
      return string(0x199a68U);
    }
    const auto row = static_cast<U32>(entry - in.text_bank->entries.data());
    const auto address = in.relocated_text_addresses[row];
    effect(RacFrontendNewGameTextV1{pc, key, static_cast<std::int32_t>(row),
                                    address});
    if (!address)
      return std::nullopt;
    const auto text = string(address);
    if (text.size() != entry->text_bytes.size() + 1U ||
        !std::equal(entry->text_bytes.begin(), entry->text_bytes.end(),
                    text.begin()))
      fail("Relocated source text differs from parsed bank ownership");
    return text;
  }
  Bytes format(const Bytes &prefix, const Bytes &body) const {
    const auto fmt = string(0x160380U);
    // Reached source format is two unqualified-width %s conversions. Parse
    // the owned format, never invoke a host printf or accept arbitrary format
    // execution. No truncation of the original64-byte local stack window.
    Bytes result;
    std::size_t substitutions = 0U;
    for (std::size_t i = 0U; i + 1U < fmt.size(); ++i) {
      if (fmt[i] != std::byte{'%'}) {
        result.push_back(fmt[i]);
      } else {
        if (++i + 1U >= fmt.size() || fmt[i] != std::byte{'s'} ||
            substitutions == 2U)
          fail("Unqualified original callback format conversion");
        const auto &part = substitutions++ == 0U ? prefix : body;
        result.insert(result.end(), part.begin(), part.end() - 1);
      }
      if (result.size() >= 64U)
        fail("Original callback format exceeds64-byte stack ownership");
    }
    if (substitutions != 2U)
      fail("Original callback format does not consume both source strings");
    result.push_back(std::byte{0U});
    return result;
  }
  void colors(U32 pc, bool enabled) {
    out.final_inline_colors_enabled = enabled;
    control(CK::colors, pc, enabled ? 1U : 0U);
  }
  void shadow_offset(RacTextLayoutBoxV1 &box, bool add) const {
    const auto y = read(0x1602bcU, 2U), x = read(0x1602b8U, 2U);
    auto offset = [add](std::int16_t &value, U32 amount) {
      const auto source = static_cast<std::uint16_t>(value);
      value = s16(add ? source + amount : source - amount);
    };
    offset(box.top, y);
    offset(box.bottom, y);
    offset(box.left, x);
    offset(box.right, x);
    offset(box.anchor_x, x);
    offset(box.anchor_y, y);
  }
  void layout(U32 pc, RacTextLayoutBoxV1 &box, U64 color, const Bytes &text) {
    RacFrontendNewGameLayoutV1 value;
    value.source_call_pc = pc;
    value.text = text;
    auto &request = value.request;
    request.box = box;
    request.rgbaq = color;
    request.tex0 = tex0;
    request.palette = out.final_palette;
    request.inline_colors_enabled = out.final_inline_colors_enabled;
    request.screen_width = read(0x13e600U);
    request.screen_height = read(0x13e604U);
    const auto &metrics = in.font_metrics->tables[out.font_source_id - 1U];
    value.plan = plan_rac_text_layout_v1(
        text, metrics, request, in.limits.max_layout_byte_visits_per_call);
    value.entry_scissor = emit_rac_frontend_scissor_v1(
        value.plan.entry_clip, request.screen_width, request.screen_height);
    value.exit_scissor = emit_rac_frontend_scissor_v1(
        value.plan.exit_clip, request.screen_width, request.screen_height);
    for (std::size_t i = 0U; i < value.plan.lines.size(); ++i) {
      const auto &line = value.plan.lines[i];
      if (!line.draw)
        continue;
      if (!line.draw->floating || line.first_byte < 0 ||
          static_cast<std::size_t>(line.first_byte) >= text.size())
        fail("Reached callback line escapes original floating text owner");
      out.final_palette[0] = line.draw->palette_zero_write;
      RacFloatGlyphStateV1 state;
      state.rgbaq = line.draw->initial_rgbaq;
      state.tex0 = tex0;
      state.palette = out.final_palette;
      state.color_controls_enabled = out.final_inline_colors_enabled;
      state.preserve_palette_zero = true;
      RacFloatGlyphLimitsV1 limits;
      limits.max_consumed_bytes = in.limits.max_text_bytes;
      limits.max_operations =
          in.limits.max_total_glyph_operations - glyph_operations;
      const auto count = static_cast<std::int32_t>(line.last_byte_inclusive) -
                         line.first_byte + 1;
      auto program = build_rac_float_glyph_program_v1(
          std::span(text).subspan(static_cast<std::size_t>(line.first_byte)),
          count, metrics, state, limits);
      glyph_operations += static_cast<U32>(program.operations.size());
      out.final_palette = program.final_palette;
      value.glyph_lines.push_back({static_cast<U32>(i), std::move(program)});
    }
    box = value.plan.box;
    effect(std::move(value));
  }
  RacFrontendNewGamePlanV1 finish() {
    if (numeric_cursor != in.numeric_observations.size())
      fail("Unused callback numeric observations do not match reached path");
    return std::move(out);
  }
};
} // namespace

RacFrontendNewGamePlanV1
plan_rac_frontend_new_game_v1(const RacFrontendNewGameInputsV1 &input) {
  return Owner(input).run();
}
} // namespace openrc
