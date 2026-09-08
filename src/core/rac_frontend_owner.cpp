#include "openrc/rac_frontend_owner.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace openrc {
namespace {
std::int32_t signed_word(std::uint32_t value) {
  return std::bit_cast<std::int32_t>(value);
}
std::uint32_t raw(std::int32_t value) {
  return std::bit_cast<std::uint32_t>(value);
}
std::uint64_t widen(std::uint32_t value) {
  return static_cast<std::uint64_t>(
      static_cast<std::int64_t>(signed_word(value)));
}
std::uint32_t arithmetic_right(std::uint32_t value, unsigned shift) {
  if (shift == 0U)
    return value;
  const auto high = (value & 0x80000000U) != 0U ? (~0U << (32U - shift)) : 0U;
  return (value >> shift) | high;
}
std::uint32_t half_toward_zero(std::uint32_t value) {
  return arithmetic_right(value + (value >> 31U), 1U);
}
void put(std::span<std::byte> bytes, std::size_t at, std::uint64_t value,
         std::size_t width = 8U) {
  for (std::size_t i = 0; i < width; ++i)
    bytes[at + i] = static_cast<std::byte>(value >> (i * 8U));
}
RacFrontendOwnerPacketV1 ad(std::uint32_t owner, std::uint8_t address,
                            std::uint64_t value) {
  RacFrontendOwnerPacketV1 result{owner, std::vector<std::byte>(48U), {}};
  put(result.bytes, 0U, 0x10000002U, 4U);
  put(result.bytes, 12U, 0x50000002U, 4U);
  put(result.bytes, 16U, 0x1000000000008001ULL);
  put(result.bytes, 24U, 0xeU);
  put(result.bytes, 32U, value);
  put(result.bytes, 40U, address);
  return result;
}
RacFrontendOwnerCallV1 call(RacFrontendOwnerCallKindV1 kind, std::uint32_t pc,
                            std::initializer_list<std::uint64_t> args = {}) {
  RacFrontendOwnerCallV1 result;
  result.kind = kind;
  result.source_call_pc = pc;
  std::copy(args.begin(), args.end(), result.arguments.begin());
  return result;
}
bool selected(const RacFrontendSlotSourceV1 &slot, std::uint32_t index) {
  return slot.enabled_word != 0U && slot.object_address != 0U &&
         (index != 6U || slot.slot_six_enabled_word != 0U);
}
std::uint32_t node_table(std::uint32_t root) {
  if (root == 0U)
    return 0U;
  if (root > 0xffffffbbU)
    throw RacFrontendOwnerError("Source node table addition wrapped");
  return root + 0x44U;
}
void source_span(std::uint32_t address, std::uint32_t size) {
  if (address == 0U || address > 0xffffffffU - size)
    throw RacFrontendOwnerError("Unowned or wrapping source object fields");
}
std::uint32_t exponent(std::uint32_t desired) {
  std::uint32_t result = 7U;
  if (signed_word(desired) <= 128)
    return result;
  if (desired > 0x40000000U)
    throw RacFrontendOwnerError(
        "Original RTT exponent search does not terminate");
  while ((1U << result) < desired)
    ++result;
  return result;
}
std::uint32_t quotient(std::uint32_t numerator, std::uint32_t denominator) {
  if (denominator == 0U)
    throw RacFrontendOwnerError(
        "Original RTT crop reaches explicit division break");
  const auto n = static_cast<std::int64_t>(signed_word(numerator));
  const auto d = static_cast<std::int64_t>(signed_word(denominator));
  return static_cast<std::uint32_t>(n / d);
}
RacFrontendOwnerPacketV1 sprite(std::uint32_t owner,
                                std::array<std::uint32_t, 4> corners,
                                std::uint32_t color,
                                const std::array<std::uint32_t, 4> &offsets,
                                bool far_depth) {
  RacFrontendOwnerPacketV1 result{owner, std::vector<std::byte>(64U), {}};
  put(result.bytes, 0U, 0x10000003U, 4U);
  put(result.bytes, 12U, 0x50000003U, 4U);
  put(result.bytes, 16U, 0x4400000000000001ULL);
  put(result.bytes, 24U, 0x4410U);
  put(result.bytes, 32U, 0x46U);
  put(result.bytes, 40U, widen(color));
  for (std::size_t i = 0U; i < 2U; ++i) {
    const auto x = (corners[i * 2U] << 4U) + offsets[i * 2U + 1U] - 16U;
    const auto y = (corners[i * 2U + 1U] << 4U) + offsets[i * 2U] - 16U;
    put(result.bytes, 48U + i * 8U,
        widen(x) | (widen(y) << 16U) |
            (far_depth ? 0x00fffff000000000ULL : 0U));
  }
  return result;
}
RacFrontendRttSetupV1 setup(std::uint32_t tw, std::uint32_t th,
                            std::uint32_t pass,
                            const RacFrontendRttLiveV1 &live) {
  if (tw > 11U || th > 11U || tw + th > 17U)
    throw RacFrontendOwnerError(
        "RTT dimensions exceed bounded lossless GS transfer domain");
  RacFrontendRttSetupV1 result;
  result.width_exponent = tw;
  result.height_exponent = th;
  const auto width = 1U << tw, height = 1U << th;
  auto base = raw(static_cast<std::int32_t>(live.depth_base_half)) << 13U;
  if (pass == 0U) {
    const auto allocation = 4U << std::min(16U, tw + th);
    base = arithmetic_right(live.allocator_limit_word - allocation, 13U) << 13U;
  }
  result.source_base_word = base;
  // The original subword writes/ORs are retained even for unusual signed bases.
  const auto blocks = arithmetic_right(base, 8U);
  const auto dbw_shift =
      tw > 6U ? tw - 6U : 1U; // actual1fb620 MOVZ gives exponent1.
  result.tex0_write = widen(blocks) | (std::uint64_t{1U << dbw_shift} << 14U) |
                      (std::uint64_t{tw} << 26U) | (std::uint64_t{th} << 30U) |
                      (1ULL << 34U);
  result.width_height_base_halves = {
      static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
      static_cast<std::uint16_t>(arithmetic_right(base, 13U))};
  auto &packet = result.packet;
  packet.source_owner = 0x1fb608U;
  packet.bytes.resize(240U);
  put(packet.bytes, 0U, 0x1000000eU, 4U);
  put(packet.bytes, 12U, 0x5000000eU, 4U);
  put(packet.bytes, 16U, 0x1000000000000008ULL);
  put(packet.bytes, 24U, 0xeU);
  const auto frame =
      (std::uint64_t{(width + 63U) / 64U & 63U} << 16U) |
      (std::uint64_t{static_cast<std::uint16_t>(live.frame_format_half) & 15U}
       << 24U) |
      (arithmetic_right(base, 13U) & 0x1ffU);
  const auto depth =
      widen(raw(static_cast<std::int32_t>(live.depth_base_half))) |
      (std::uint64_t{live.depth_format_half & 15U} << 24U);
  const auto xy = std::uint64_t{(2048U - width / 2U) * 16U} |
                  (std::uint64_t{(2048U - height / 2U) * 16U} << 32U);
  const auto scissor =
      (std::uint64_t{width - 1U} << 16U) | (std::uint64_t{height - 1U} << 48U);
  const std::array<std::uint64_t, 8> values{
      frame,
      depth,
      xy,
      scissor,
      1U,
      1U,
      (static_cast<std::uint16_t>(live.frame_format_half) & 2U) ? 1U : 0U,
      0x70000U};
  constexpr std::array<std::uint8_t, 8> registers{0x4cU, 0x4eU, 0x18U, 0x40U,
                                                  0x1aU, 0x46U, 0x45U, 0x47U};
  for (std::size_t i = 0U; i < values.size(); ++i) {
    put(packet.bytes, 32U + i * 16U, values[i]);
    put(packet.bytes, 40U + i * 16U, registers[i]);
  }
  put(packet.bytes, 160U, 0x1000000000000001ULL);
  put(packet.bytes, 168U, 0xeU);
  put(packet.bytes, 176U, 0x30003U);
  put(packet.bytes, 184U, 0x47U);
  put(packet.bytes, 192U, 0x4400000000008001ULL);
  put(packet.bytes, 200U, 0x4410U);
  put(packet.bytes, 208U, 0x106U);
  put(packet.bytes, 224U,
      (32768U - width * 8U) | (std::uint64_t{32768U - height * 8U} << 16U));
  put(packet.bytes, 232U,
      (32768U + width * 8U) | (std::uint64_t{32752U + height * 8U} << 16U));
  return result;
}
RacFrontendOwnerEffectV1 restore(const RacFrontendRttLiveV1 &live) {
  if (live.restore_command_cursor_word == 0U) {
    return call(RacFrontendOwnerCallKindV1::immediate_environment_1224b0,
                0x1fb518U, {widen(live.live_draw_environment + 0x30U)});
  }
  const auto address = (live.live_draw_environment + 0x30U) & 0x0fffffffU;
  if (address == 0U || (address & 15U) != 0U || address > 0x10000000U - 144U ||
      live.restore_environment_region != RacFrontendGsRegionV1{address, 144U})
    throw RacFrontendOwnerError(
        "RTT restore REF9 has no exact caller-owned live environment");
  RacFrontendOwnerPacketV1 result{0x1fb498U,
                                  std::vector<std::byte>(16U),
                                  {live.restore_environment_region}};
  put(result.bytes, 0U, 0x30000009U, 4U);
  put(result.bytes, 4U, address, 4U);
  put(result.bytes, 12U, 0x50000009U, 4U);
  return result;
}
} // namespace

RacFrontendViewportPlanV1
plan_rac_frontend_rtt_viewport_v1(std::uint32_t width, std::uint32_t height,
                                  bool restore_mode) {
  if (restore_mode) {
    width = raw(static_cast<std::int16_t>(width & 0xffffU));
    height = raw(static_cast<std::int16_t>(height & 0xffffU));
  }
  const auto hx = arithmetic_right(width, 1U),
             hy = arithmetic_right(height, 1U);
  RacFrontendViewportPlanV1 out;
  out.screen_words = {width,
                      height,
                      hx,
                      hy,
                      (2048U - hx) << 4U,
                      (2048U - hy) << 4U,
                      (2048U + hx) << 4U,
                      (2048U + hy) << 4U};
  using O = RacFrontendViewportOpcodeV1;
  auto wr = [&](std::uint32_t pc, std::uint32_t address, std::uint32_t value) {
    out.effects.push_back({O::write_raw_word, pc, address, value, 0U});
  };
  auto cvt = [&](std::uint32_t pc, std::uint32_t id, std::uint32_t word) {
    out.effects.push_back({O::convert_word_to_single, pc, id, word, 0U});
  };
  auto mul = [&](std::uint32_t pc, std::uint32_t id, std::uint32_t left,
                 std::uint32_t bits) {
    out.effects.push_back({O::multiply_single, pc, id, left, bits});
  };
  auto store = [&](std::uint32_t pc, std::uint32_t address, std::uint32_t id) {
    out.effects.push_back({O::write_single_value, pc, address, id, 0U});
  };
  constexpr std::uint32_t screen = 0x13e600U, camera = 0x18ce00U;
  if (!restore_mode) {
    wr(0x1f37c0U, screen + 28U, out.screen_words[7]);
    wr(0x1f37c8U, camera + 0xb0U, 0x3f800000U);
    wr(0x1f37d0U, screen, width);
    wr(0x1f37f0U, screen + 16U, out.screen_words[4]);
    wr(0x1f37f8U, screen + 20U, out.screen_words[5]);
    wr(0x1f37fcU, screen + 24U, out.screen_words[6]);
    wr(0x1f3800U, camera + 0xa4U, 0x49000000U);
    wr(0x1f3804U, camera + 0xa0U, 0x42000000U);
    wr(0x1f3808U, screen + 4U, height);
    wr(0x1f3814U, screen + 8U, hx);
    wr(0x1f381cU, screen + 12U, hy);
    cvt(0x1f3818U, 0U, width);
    mul(0x1f3820U, 1U, 0U, 0x3f000000U);
    store(0x1f382cU, camera + 0x200U, 1U);
    cvt(0x1f3828U, 2U, height);
    mul(0x1f3830U, 3U, 2U, 0x3f000000U);
    wr(0x1f3840U, camera + 0x22cU, 0U);
    mul(0x1f3844U, 4U, 3U, 0x40800000U);
    wr(0x1f3848U, camera + 0x218U, 0U);
    mul(0x1f384cU, 5U, 1U, 0x40800000U);
    wr(0x1f3850U, camera + 0x21cU, 0x49000000U);
    wr(0x1f3854U, camera + 0x228U, 0x437f0000U);
    store(0x1f3858U, camera + 0x20cU, 4U);
    store(0x1f385cU, camera + 0x208U, 5U);
    store(0x1f3864U, camera + 0x204U, 3U);
    out.effects.push_back({O::call_projection_1f3140, 0x1f3860U, 0U, 0U, 0U});
  } else {
    wr(0x1f3070U, screen + 28U, out.screen_words[7]);
    wr(0x1f3080U, screen, width);
    wr(0x1f309cU, screen + 4U, height);
    wr(0x1f30a4U, screen + 16U, out.screen_words[4]);
    wr(0x1f30a8U, screen + 20U, out.screen_words[5]);
    wr(0x1f30acU, screen + 24U, out.screen_words[6]);
    wr(0x1f30b0U, camera + 0xa0U, 0x42000000U);
    wr(0x1f30b4U, camera + 0xb0U, 0x3f2147aeU);
    wr(0x1f30b8U, camera + 0xa4U, 0x49360000U);
    wr(0x1f30c4U, screen + 8U, hx);
    wr(0x1f30ccU, screen + 12U, hy);
    cvt(0x1f30c8U, 0U, width);
    mul(0x1f30d0U, 1U, 0U, 0x3f000000U);
    store(0x1f30dcU, camera + 0x200U, 1U);
    cvt(0x1f30d8U, 2U, height);
    mul(0x1f30e0U, 3U, 2U, 0x3f000000U);
    mul(0x1f30f8U, 4U, 3U, 0x40800000U);
    mul(0x1f3104U, 5U, 1U, 0x40800000U);
    wr(0x1f3108U, camera + 0x21cU, 0x49000000U);
    wr(0x1f310cU, camera + 0x228U, 0x437f0000U);
    store(0x1f3110U, camera + 0x20cU, 4U);
    store(0x1f3114U, camera + 0x208U, 5U);
    wr(0x1f3118U, camera + 0x22cU, 0U);
    store(0x1f311cU, camera + 0x204U, 3U);
    wr(0x1f3120U, camera + 0x218U, 0U);
    out.effects.push_back({O::call_projection_1f3140, 0x1f7b58U, 0U, 0U, 0U});
  }
  return out;
}

RacFrontendOwnerPlanV1
plan_rac_frontend_owner_v1(const RacFrontendOwnerInputsV1 &input) {
  RacFrontendOwnerPlanV1 out;
  auto add = [&](RacFrontendOwnerEffectV1 effect) {
    if (out.effects.size() >= input.max_effects)
      throw RacFrontendOwnerError("Frontend owner effect budget exceeded");
    out.effects.push_back(std::move(effect));
  };
  using K = RacFrontendOwnerCallKindV1;
  add(ad(0x21a610U, 0x47U, 0x5360bU));
  add(call(K::prepare_render_20e0c8, 0x21a654U));
  add(call(K::save_scratch_20e040, 0x21a65cU));
  add(call(K::copy_scratch_20e068, 0x21a668U));
  add(call(K::draw_object_20e180, 0x21a680U,
           {widen(input.initial_object_address), 4U}));
  add(call(K::menu_camera_219c08, 0x21a688U));
  add(call(K::camera_projection_1f2608, 0x21a690U));
  for (std::uint32_t i = 0U; i < 14U; ++i)
    if (selected(input.object_draw_slots[i], i))
      add(call(K::draw_object_20e180, 0x21a6ccU,
               {widen(input.object_draw_slots[i].object_address), 1U}));
  const auto table = node_table(input.node_root_address);
  for (std::uint32_t i = 0U; i < 14U; ++i) {
    const auto &slot = input.prepass_slots[i];
    if (!selected(slot.source, i))
      continue;
    source_span(slot.projected_object_address, 0x60U);
    if (!slot.projection)
      throw RacFrontendOwnerError(
          "Selected prepass lacks original projector outputs");
    add(call(K::project_bounds_238d90, 0x21a7b4U,
             {slot.projected_object_address,
              slot.projected_object_address + 0x30U}));
    const auto bounds = slot.projection->width_height_x_y;
    const auto x = bounds[2] + 1U, y = bounds[3] + 1U;
    if (table != 0U && slot.node_address != 0U) {
      source_span(slot.node_address, 0x28U);
      add(RacFrontendOwnerWordWriteV1{slot.node_address + 0x20U, bounds[0]});
      add(RacFrontendOwnerWordWriteV1{slot.node_address + 0x24U, bounds[1]});
      add(RacFrontendOwnerWordWriteV1{slot.node_address + 0x18U, x});
      add(RacFrontendOwnerWordWriteV1{slot.node_address + 0x1cU, y});
    }
    add(RacFrontendOwnerWordWriteV1{slot.projected_object_address + 0x50U, x});
    add(RacFrontendOwnerWordWriteV1{slot.projected_object_address + 0x54U, y});
    add(RacFrontendOwnerWordWriteV1{slot.projected_object_address + 0x58U,
                                    bounds[0]});
    add(RacFrontendOwnerWordWriteV1{slot.projected_object_address + 0x5cU,
                                    bounds[1]});
    add(sprite(0x201640U,
               {x + 1U, y + 1U, x + bounds[0] - 1U, y + bounds[1] - 1U},
               slot.source_color_word, input.prepass_offset_reads[i], true));
  }
  for (std::uint32_t pass = 0U; pass < 2U; ++pass) {
    for (std::uint32_t i = 0U; i < 14U; ++i) {
      const auto &slot = input.draw_slots[pass][i];
      std::optional<RacFrontendOwnerSkipV1> reason;
      using S = RacFrontendOwnerSkipV1;
      if (!slot.source.object_address)
        reason = S::absent_object;
      else if (!table)
        reason = S::absent_node_table;
      else if (!slot.node_address)
        reason = S::absent_node;
      else if (slot.node_flags & 4U)
        reason = S::hidden_node;
      else if (!slot.source.enabled_word)
        reason = S::disabled_slot;
      else if (!slot.callback_address)
        reason = S::absent_callback;
      else if (i == 6U && !slot.source.slot_six_enabled_word)
        reason = S::disabled_slot_six;
      else if (((slot.node_flags & 2U) != 0U) != (pass == 0U))
        reason = S::different_pass;
      if (reason) {
        add(RacFrontendOwnerSkippedV1{pass, i, *reason});
        continue;
      }
      auto callback =
          call(K::callback, (slot.node_flags & 1U) ? 0x21a968U : 0x21aa84U,
               {widen(slot.node_address)});
      callback.callback_address = slot.callback_address;
      if (slot.node_flags & 1U) {
        add(callback);
        ++out.direct_callbacks;
        continue;
      }
      if (!slot.rtt_live || !slot.callback_return_word)
        throw RacFrontendOwnerError(
            "Reached RTT callback lacks explicit live state/return word");
      const auto &live = *slot.rtt_live;
      const auto [x, y, width, height] = slot.x_y_width_height;
      auto tw = exponent(width), th = exponent(height);
      while (tw + th >= 18U && th != 0U)
        --th;
      const auto target = setup(tw, th, pass, live);
      const auto target_width = 1U << tw, target_height = 1U << th;
      add(target);
      add(plan_rac_frontend_rtt_viewport_v1(target_width, target_height,
                                            false));
      add(ad(0x1f7a50U, 0x47U, pass != 0U ? 0U : 0x30000U));
      add(ad(0x1f7a50U, 0x42U, 0x8000000044ULL));
      add(sprite(0x2017c8U, {0U, 0U, target_width, target_height},
                 live.clear_color_word, live.clear_offset_reads, false));
      source_span(live.callback_node_after_setup, 8U);
      if (live.callback_after_setup == 0U ||
          (live.callback_after_setup & 3U) != 0U)
        throw RacFrontendOwnerError("RTT re-read callback has no source entry");
      callback.callback_address = live.callback_after_setup;
      callback.arguments[0] = widen(live.callback_node_after_setup);
      add(callback);
      ++out.rtt_callbacks;
      add(restore(live));
      add(plan_rac_frontend_rtt_viewport_v1(
          raw(live.restore_width_half), raw(live.restore_height_half), true));
      const auto flags = *slot.callback_return_word;
      if (flags & 1U)
        continue;
      std::uint32_t u = 0U, v = 0U, right = target_width,
                    bottom = target_height;
      if (flags & 2U) {
        right = width;
        bottom = height;
      } else if (flags & 8U) {
        u = half_toward_zero(right - width);
        v = half_toward_zero(bottom - height);
        right -= u;
        bottom -= v;
        if (signed_word(right) > signed_word(target_width))
          right = target_width;
        if (signed_word(bottom) > signed_word(target_height))
          bottom = target_height;
        if (signed_word(u) < 0)
          u = 0U;
        if (signed_word(v) < 0)
          v = 0U;
      } else if (flags & 4U) {
        if (signed_word(width) < signed_word(height)) {
          u = half_toward_zero(right) - quotient(width * right, height << 1U);
          right -= u;
        } else {
          v = half_toward_zero(bottom) - quotient(height * bottom, width << 1U);
          bottom -= v;
        }
      } else if (!(flags & 16U))
        continue;
      if (!live.composite_tex0)
        throw RacFrontendOwnerError(
            "Reached RTT composite lacks actual post-restore TEX0 read");
      add(ad(0x21a610U, 0x42U, 0x8000000064ULL));
      add(ad(0x21a610U, 0x47U, 0x43U));
      add(call(K::composite_quad_1f5800, 0x21abf0U,
               {widen(x), widen(y), widen(width), widen(height), widen(u),
                widen(v), widen(right - u), widen(bottom - v), 0x80808080ULL,
                *live.composite_tex0}));
      ++out.composites;
    }
    if (pass == 0U) {
      add(call(K::restore_scratch_20e098, 0x21ac14U));
      add(call(K::submit_objects_20e200, 0x21ac1cU));
    }
  }
  add(call(K::cold_batch_begin_1f4630, 0x21ac38U, {0U}));
  for (std::uint32_t i = 0U; i < 14U; ++i) {
    const auto &slot = input.final_slots[i];
    // Unlike the earlier passes, source does NOT test object pointer for zero.
    if (slot.enabled_word && (i != 6U || slot.slot_six_enabled_word))
      add(call(K::final_object_2250b8, 0x21ac80U,
               {widen(slot.object_address)}));
  }
  add(call(K::cold_batch_end_1f4748, 0x21ac9cU));
  return out;
}
} // namespace openrc
