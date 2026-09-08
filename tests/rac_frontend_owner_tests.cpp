#include "openrc/rac_frontend_owner.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace openrc;
using K = RacFrontendOwnerCallKindV1;
void expect(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F operation, const char *message) {
  try {
    operation();
  } catch (const RacFrontendOwnerError &) {
    return;
  }
  throw std::runtime_error(message);
}
std::uint64_t value(std::span<const std::byte> bytes, std::size_t at,
                    std::size_t size = 8U) {
  std::uint64_t result = 0U;
  for (std::size_t i = 0U; i < size; ++i)
    result |= std::to_integer<std::uint64_t>(bytes[at + i]) << (i * 8U);
  return result;
}
std::vector<RacFrontendOwnerCallV1> calls(const RacFrontendOwnerPlanV1 &plan) {
  std::vector<RacFrontendOwnerCallV1> out;
  for (const auto &effect : plan.effects)
    if (const auto *call = std::get_if<RacFrontendOwnerCallV1>(&effect))
      out.push_back(*call);
  return out;
}
RacFrontendRttLiveV1 live() {
  RacFrontendRttLiveV1 out;
  out.allocator_limit_word = 0x300000U;
  out.frame_format_half = 0;
  out.depth_format_half = 0x32U;
  out.depth_base_half = 0x120;
  out.clear_color_word = 0x87654321U;
  out.clear_offset_reads = {0x7c00U, 0x7800U, 0x7c00U, 0x7800U};
  out.callback_node_after_setup = 0x510000U;
  out.callback_after_setup = 0x221000U;
  out.restore_command_cursor_word = 0x600000U;
  out.live_draw_environment = 0xa0230000U;
  out.restore_environment_region = {0x230030U, 144U};
  out.restore_width_half = 512;
  out.restore_height_half = 416;
  out.composite_tex0 = 0x123456789abcdef0ULL;
  return out;
}
RacFrontendDrawSlotV1 slot(std::uint32_t flags = 0U) {
  RacFrontendDrawSlotV1 out;
  out.source = {0x400000U, 1U, 1U};
  out.node_address = 0x500000U;
  out.node_flags = flags;
  out.callback_address = 0x220000U;
  out.x_y_width_height = {12U, 24U, 180U, 90U};
  out.callback_return_word = 2U;
  out.rtt_live = live();
  return out;
}
RacFrontendOwnerInputsV1 input_with_slot(std::uint32_t flags = 0U,
                                         unsigned index = 0U) {
  RacFrontendOwnerInputsV1 input;
  input.node_root_address = 0x300000U;
  input.draw_slots[(flags & 2U) ? 0U : 1U][index] = slot(flags);
  return input;
}
const RacFrontendOwnerCallV1 &
composite(const std::vector<RacFrontendOwnerCallV1> &list) {
  const auto found = std::find_if(list.begin(), list.end(), [](auto &call) {
    return call.kind == K::composite_quad_1f5800;
  });
  if (found == list.end())
    throw std::runtime_error("Missing composite");
  return *found;
}

void test_full_phase_order_and_fresh_snapshots() {
  RacFrontendOwnerInputsV1 input;
  input.initial_object_address = 0x123400U;
  input.node_root_address = 0x300000U;
  input.object_draw_slots[1] = {0x400000U, 1U, 0U};
  input.draw_slots[0][2] = slot(3U);
  input.draw_slots[0][2].callback_address = 0x210000U;
  input.draw_slots[1][2] = slot(1U);
  input.draw_slots[1][2].callback_address = 0x220000U;
  input.final_slots[3] = {0U, 1U, 0U};
  const auto plan = plan_rac_frontend_owner_v1(input);
  const auto list = calls(plan);
  const std::vector<K> expected{K::prepare_render_20e0c8,
                                K::save_scratch_20e040,
                                K::copy_scratch_20e068,
                                K::draw_object_20e180,
                                K::menu_camera_219c08,
                                K::camera_projection_1f2608,
                                K::draw_object_20e180,
                                K::callback,
                                K::restore_scratch_20e098,
                                K::submit_objects_20e200,
                                K::callback,
                                K::cold_batch_begin_1f4630,
                                K::final_object_2250b8,
                                K::cold_batch_end_1f4748};
  std::vector<K> actual;
  for (auto &call : list)
    actual.push_back(call.kind);
  expect(actual == expected,
         "Original full prepass/two-pass/final call order changed");
  expect(
      list[3].arguments[0] == input.initial_object_address &&
          list[3].arguments[1] == 4U && list[6].arguments[1] == 1U &&
          list[7].callback_address == 0x210000U &&
          list[10].callback_address == 0x220000U && list[12].arguments[0] == 0U,
      "Fresh source callback reads or final null-pointer call were normalized");
  expect(plan.direct_callbacks == 2U && plan.rtt_callbacks == 0U &&
             plan.composites == 0U,
         "Direct callback counters changed");
}

void test_selection_gate_precedence() {
  auto input = input_with_slot(3U, 6U);
  auto &node = input.draw_slots[0][6];
  using S = RacFrontendOwnerSkipV1;
  auto reason = [&] {
    const auto plan = plan_rac_frontend_owner_v1(input);
    for (auto &effect : plan.effects)
      if (auto *skip = std::get_if<RacFrontendOwnerSkippedV1>(&effect);
          skip && skip->pass == 0U && skip->slot == 6U)
        return skip->reason;
    throw std::runtime_error("Expected source node skip");
  };
  node.source.object_address = 0U;
  input.node_root_address = 0U;
  expect(reason() == S::absent_object, "Object/null-map gate order changed");
  node.source.object_address = 1U;
  expect(reason() == S::absent_node_table,
         "Missing captured node table ignored");
  input.node_root_address = 0x300000U;
  node.node_address = 0U;
  expect(reason() == S::absent_node, "Absent node ignored");
  node.node_address = 0x500000U;
  node.node_flags = 7U;
  node.source.enabled_word = 0U;
  expect(reason() == S::hidden_node, "Hidden gate moved after enabled gate");
  node.node_flags = 3U;
  expect(reason() == S::disabled_slot, "Disabled source slot ignored");
  node.source.enabled_word = 1U;
  node.callback_address = 0U;
  node.source.slot_six_enabled_word = 0U;
  expect(reason() == S::absent_callback,
         "Callback-null gate moved past special slot gate");
  node.callback_address = 0x220000U;
  expect(reason() == S::disabled_slot_six, "Special slot6 gate ignored");
  node.source.slot_six_enabled_word = 1U;
  node.node_flags = 1U;
  expect(reason() == S::different_pass,
         "Original two-pass flag selection changed");
}

void test_projected_bounds_and_raw_word_effects() {
  RacFrontendOwnerInputsV1 input;
  input.node_root_address = 0x300000U;
  auto &slot = input.prepass_slots[1];
  slot.source = {0x400000U, 1U, 0U};
  slot.projected_object_address = 0x410000U;
  slot.node_address = 0x500000U;
  slot.projection = RacFrontendProjectedBoundsV1{{100U, 50U, 0xffffffffU, 19U}};
  slot.source_color_word = 0x80443322U;
  input.prepass_offset_reads[1] = {0x8000U, 0x8000U, 0x8100U, 0x8200U};
  const auto plan = plan_rac_frontend_owner_v1(input);
  std::vector<RacFrontendOwnerWordWriteV1> writes;
  const RacFrontendOwnerPacketV1 *packet = nullptr;
  for (auto &effect : plan.effects) {
    if (auto *w = std::get_if<RacFrontendOwnerWordWriteV1>(&effect))
      writes.push_back(*w);
    if (auto *p = std::get_if<RacFrontendOwnerPacketV1>(&effect);
        p && p->source_owner == 0x201640U)
      packet = p;
  }
  expect(writes == std::vector<RacFrontendOwnerWordWriteV1>{{0x500020U, 100U},
                                                            {0x500024U, 50U},
                                                            {0x500018U, 0U},
                                                            {0x50001cU, 20U},
                                                            {0x410050U, 0U},
                                                            {0x410054U, 20U},
                                                            {0x410058U, 100U},
                                                            {0x41005cU, 50U}},
         "Prepass bounds writes or source x+1 wrapping changed");
  expect(packet && packet->bytes.size() == 64U &&
             value(packet->bytes, 40U) == 0xffffffff80443322ULL &&
             value(packet->bytes, 16U) == 0x4400000000000001ULL,
         "Original projected clear color or no-EOP tag changed");
  const auto list = calls(plan);
  const auto found = std::find_if(list.begin(), list.end(), [](auto &c) {
    return c.kind == K::project_bounds_238d90;
  });
  expect(found != list.end() && found->arguments[0] == 0x410000U &&
             found->arguments[1] == 0x410030U,
         "Original projector source vector ownership lost");
}

void test_rtt_setup_restore_and_early_return() {
  auto input = input_with_slot();
  auto &node = input.draw_slots[1][0];
  node.callback_return_word = 1U;
  node.rtt_live->composite_tex0.reset();
  const auto plan = plan_rac_frontend_owner_v1(input);
  expect(plan.rtt_callbacks == 1U && plan.composites == 0U,
         "RTT bit0 early return ignored");
  const auto actual_calls = calls(plan);
  const auto callback =
      std::find_if(actual_calls.begin(), actual_calls.end(),
                   [](const auto &c) { return c.kind == K::callback; });
  expect(callback != actual_calls.end() &&
             callback->callback_address == 0x221000U &&
             callback->arguments[0] == 0x510000U,
         "RTT did not re-read node/callback after source setup");
  const RacFrontendRttSetupV1 *target = nullptr;
  std::vector<const RacFrontendOwnerPacketV1 *> packets;
  std::vector<const RacFrontendViewportPlanV1 *> views;
  for (auto &effect : plan.effects) {
    if (auto *t = std::get_if<RacFrontendRttSetupV1>(&effect))
      target = t;
    if (auto *p = std::get_if<RacFrontendOwnerPacketV1>(&effect))
      packets.push_back(p);
    if (auto *v = std::get_if<RacFrontendViewportPlanV1>(&effect))
      views.push_back(v);
  }
  expect(target && target->width_exponent == 8U &&
             target->height_exponent == 7U &&
             target->source_base_word == 0x240000U &&
             target->packet.bytes.size() == 240U,
         "Pass1 source RTT allocation or240B packet changed");
  const auto &b = target->packet.bytes;
  expect(value(b, 32U) == 0x40120U && value(b, 40U) == 0x4cU &&
             value(b, 48U) == 0x02000120U && value(b, 104U) == 0x1aU &&
             value(b, 96U) == 1U && value(b, 160U) == 0x1000000000000001ULL &&
             value(b, 176U) == 0x30003U &&
             value(b, 192U) == 0x4400000000008001ULL,
         "Original drawenv/TEST/clear sprite bytes changed");
  const auto restored =
      std::find_if(packets.begin(), packets.end(),
                   [](auto *p) { return p->source_owner == 0x1fb498U; });
  expect(restored != packets.end() &&
             value((*restored)->bytes, 0U, 4U) == 0x30000009U &&
             value((*restored)->bytes, 4U, 4U) == 0x230030U &&
             (*restored)->external_reads ==
                 std::vector<RacFrontendGsRegionV1>{{0x230030U, 144U}},
         "Live restore REF9 mapping changed");
  expect(views.size() == 2U && views[0]->screen_words[0] == 256U &&
             views[1]->screen_words[0] == 512U &&
             views[1]->screen_words[1] == 416U,
         "Restore was replaced by arbitrary saved incoming viewport");
  node = input_with_slot(2U).draw_slots[0][0];
  input = input_with_slot(2U);
  const auto first = plan_rac_frontend_owner_v1(input);
  for (auto &effect : first.effects)
    if (auto *t = std::get_if<RacFrontendRttSetupV1>(&effect))
      expect(t->source_base_word == 0x2e0000U,
             "Pass0 did not allocate below live allocator boundary");
}

void test_return_crop_precedence_and_signed_division() {
  auto input = input_with_slot();
  auto &node = input.draw_slots[1][0];
  for (auto flags : {2U, 8U, 4U, 16U, 14U}) {
    node.callback_return_word = flags;
    const auto plan = plan_rac_frontend_owner_v1(input);
    const auto list = calls(plan);
    const auto &c = composite(list);
    expect(c.arguments[0] == 12U && c.arguments[1] == 24U &&
               c.arguments[2] == 180U && c.arguments[3] == 90U &&
               c.arguments[8] == 0x80808080ULL &&
               c.arguments[9] == 0x123456789abcdef0ULL,
           "Composite lost original output geometry/color/live TEX0");
    if (flags & 2U)
      expect(c.arguments[4] == 0U && c.arguments[5] == 0U &&
                 c.arguments[6] == 180U && c.arguments[7] == 90U,
             "Native-size return bit lost priority");
    else if (flags & 8U)
      expect(c.arguments[4] == 38U && c.arguments[5] == 19U &&
                 c.arguments[6] == 180U && c.arguments[7] == 90U,
             "Centered crop differs from source signed half rounding");
    else if (flags & 4U)
      expect(c.arguments[4] == 0U && c.arguments[5] == 32U &&
                 c.arguments[6] == 256U && c.arguments[7] == 64U,
             "Aspect crop differs from source low-word multiply/divide");
    else
      expect(c.arguments[6] == 256U && c.arguments[7] == 128U,
             "Full-texture composite changed");
  }
  node.callback_return_word = 0U;
  node.rtt_live->composite_tex0.reset();
  expect(plan_rac_frontend_owner_v1(input).composites == 0U,
         "No-crop/no-draw result requested a TEX0 read");
  node.rtt_live->composite_tex0 = 0U;
  struct Edge {
    std::uint32_t flags, width, height, u, v, extent_x, extent_y;
  };
  for (const auto edge :
       {Edge{4U, 0xfffffffdU, 5U, 102U, 0U, 0xffffffb4U, 128U},
        Edge{4U, 0x80000001U, 5U, 52U, 0U, 24U, 128U},
        Edge{8U, 0xffffffffU, 0xfffffffdU, 64U, 65U, 0U, 0xfffffffeU}}) {
    node.callback_return_word = edge.flags;
    node.x_y_width_height[2] = edge.width;
    node.x_y_width_height[3] = edge.height;
    const auto edge_calls = calls(plan_rac_frontend_owner_v1(input));
    const auto &draw = composite(edge_calls);
    expect(static_cast<std::uint32_t>(draw.arguments[4]) == edge.u &&
               static_cast<std::uint32_t>(draw.arguments[5]) == edge.v &&
               static_cast<std::uint32_t>(draw.arguments[6]) == edge.extent_x &&
               static_cast<std::uint32_t>(draw.arguments[7]) == edge.extent_y,
           "Signed/overflow source crop arithmetic changed");
  }
  node.callback_return_word = 4U;
  node.x_y_width_height[2] = 0U;
  node.x_y_width_height[3] = 0U;
  rejects([&] { (void)plan_rac_frontend_owner_v1(input); },
          "Original explicit divide-by-zero break ignored");
}

void test_viewport_symbolic_effects() {
  const auto setup = plan_rac_frontend_rtt_viewport_v1(256U, 128U, false);
  const auto restore =
      plan_rac_frontend_rtt_viewport_v1(0x1234ffffU, 0x98768000U, true);
  expect(setup.screen_words ==
             std::array<std::uint32_t, 8>{256U, 128U, 128U, 64U, 0x7800U,
                                          0x7c00U, 0x8800U, 0x8400U},
         "Original viewport word setters changed");
  expect(restore.screen_words[0] == 0xffffffffU &&
             restore.screen_words[1] == 0xffff8000U &&
             restore.screen_words[2] == 0xffffffffU &&
             restore.screen_words[3] == 0xffffc000U,
         "Restore live halfwords were not sign-extended before shifts");
  using O = RacFrontendViewportOpcodeV1;
  for (auto *view : {&setup, &restore}) {
    expect(view->effects.size() == 26U &&
               view->effects.back().opcode == O::call_projection_1f3140,
           "Full symbolic viewport effects or projection boundary lost");
    expect(std::count_if(view->effects.begin(), view->effects.end(),
                         [](auto &e) {
                           return e.opcode == O::convert_word_to_single;
                         }) == 2 &&
               std::count_if(
                   view->effects.begin(), view->effects.end(),
                   [](auto &e) { return e.opcode == O::multiply_single; }) == 4,
           "Viewport numeric source operations substituted or omitted");
  }
  expect(setup.effects[10].source_pc == 0x1f381cU &&
             setup.effects[11].source_pc == 0x1f3818U &&
             setup.effects[13].source_pc == 0x1f382cU &&
             setup.effects[14].source_pc == 0x1f3828U,
         "Original CVT call delay-slot writes reordered");
}

void test_immediate_restore_source_branch() {
  auto input = input_with_slot();
  auto &node = input.draw_slots[1][0];
  node.callback_return_word = 1U;
  node.rtt_live->restore_command_cursor_word = 0U;
  node.rtt_live->restore_environment_region = {1U, 3U};
  node.rtt_live->composite_tex0.reset();
  const auto plan = plan_rac_frontend_owner_v1(input);
  const auto list = calls(plan);
  const auto immediate =
      std::find_if(list.begin(), list.end(), [](const auto &c) {
        return c.kind == K::immediate_environment_1224b0;
      });
  expect(immediate != list.end() && immediate->source_call_pc == 0x1fb518U &&
             immediate->arguments[0] == 0xffffffffa0230030ULL,
         "Zero restore cursor lost original raw-address SDK call");
  expect(std::none_of(plan.effects.begin(), plan.effects.end(),
                      [](const auto &e) {
                        const auto *packet =
                            std::get_if<RacFrontendOwnerPacketV1>(&e);
                        return packet && packet->source_owner == 0x1fb498U;
                      }),
         "Immediate restore invented a REF9");
}

void test_bounded_reached_inputs_and_budget() {
  auto input = input_with_slot(1U);
  input.draw_slots[1][0].rtt_live.reset();
  input.draw_slots[1][0].callback_return_word.reset();
  expect(plan_rac_frontend_owner_v1(input).direct_callbacks == 1U,
         "Unreached RTT state required by direct route");
  auto exact = plan_rac_frontend_owner_v1(input).effects.size();
  input.max_effects = static_cast<std::uint32_t>(exact);
  expect(plan_rac_frontend_owner_v1(input).effects.size() == exact,
         "Exact owner effect capacity rejected");
  --input.max_effects;
  rejects([&] { (void)plan_rac_frontend_owner_v1(input); },
          "Owner effect budget ignored");
  input = input_with_slot();
  input.draw_slots[1][0].rtt_live.reset();
  rejects([&] { (void)plan_rac_frontend_owner_v1(input); },
          "Reached RTT state was invented");
  input = input_with_slot();
  input.draw_slots[1][0].callback_return_word.reset();
  rejects([&] { (void)plan_rac_frontend_owner_v1(input); },
          "Reached callback return was invented");
  input = input_with_slot();
  input.draw_slots[1][0].rtt_live->restore_environment_region.size = 128U;
  rejects([&] { (void)plan_rac_frontend_owner_v1(input); },
          "Truncated source restore environment accepted");
  input = input_with_slot();
  input.draw_slots[1][0].rtt_live->composite_tex0.reset();
  rejects([&] { (void)plan_rac_frontend_owner_v1(input); },
          "Source setup TEX0 silently used as post-callback TEX0");
  input = input_with_slot();
  input.draw_slots[1][0].x_y_width_height[2] = 0x40000001U;
  rejects([&] { (void)plan_rac_frontend_owner_v1(input); },
          "Nonterminating original dimension search accepted");
  input = input_with_slot();
  input.draw_slots[1][0].x_y_width_height[2] = 4096U;
  rejects([&] { (void)plan_rac_frontend_owner_v1(input); },
          "RTT GS dimension domain ignored");
  input = {};
  input.prepass_slots[0].source = {1U, 1U, 1U};
  input.prepass_slots[0].projected_object_address = 0x400000U;
  rejects([&] { (void)plan_rac_frontend_owner_v1(input); },
          "Reached source projection outputs invented");
}
} // namespace
int main() {
  try {
    test_full_phase_order_and_fresh_snapshots();
    test_selection_gate_precedence();
    test_projected_bounds_and_raw_word_effects();
    test_rtt_setup_restore_and_early_return();
    test_return_crop_precedence_and_signed_division();
    test_viewport_symbolic_effects();
    test_immediate_restore_source_branch();
    test_bounded_reached_inputs_and_budget();
    std::cout << "8 original frontend owner test groups passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
