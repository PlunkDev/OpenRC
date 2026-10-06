#include "openrc/rac_moby_rotation.hpp"

#include "openrc/elf.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

namespace {
using namespace openrc;

void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

template<class Function> void rejects(Function &&function) {
  try { function(); }
  catch (const RacMobyPostError &) { return; }
  throw std::runtime_error("Invalid d18 source/input was accepted");
}

void source_admission() {
  rejects([] { static_cast<void>(RacMobyRotationSourceV1({})); });
  const std::array<std::byte, 64U> unknown{};
  rejects([&] { static_cast<void>(RacMobyRotationSourceV1(unknown)); });
}

std::vector<std::byte> read_source(const std::filesystem::path &path) {
  const auto size = std::filesystem::file_size(path);
  check(size > 0U && size <= 32U * 1024U * 1024U, "Source ELF test input exceeds bound");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  std::ifstream in(path, std::ios::binary);
  check(static_cast<bool>(in.read(reinterpret_cast<char *>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size()))),
        "Cannot read source ELF test input");
  return bytes;
}

constexpr RacMobyPostMatrixV1 kIdentity{{
    {0x3f800000U, 0U, 0U, 0U},
    {0U, 0x3f800000U, 0U, 0U},
    {0U, 0U, 0x3f800000U, 0U}}};

bool has_warning(const RacMobyRotationEvaluationV1 &result,
                 DvpVuExecutionWarningV1 warning) {
  return std::find(result.warnings.begin(), result.warnings.end(), warning) != result.warnings.end();
}

void reached_post_stage(const RacMobyRotationSourceV1 &source) {
  RacMobyPostActorV1 actor;
  actor.rotation_bits = {0x3f000000U, 0xbe800000U, 0x3f800000U, 0U};
  actor.cached_header_bits = {0x3f800000U, 0U, 0U, 0x40000000U};
  actor.scale_bits = 0x3f800000U;
  actor.spatial_reference = 0x2000U;
  actor.packed_bounds_bits = 0x80807f7fU;
  const RacMobyPostBindingsV1 bindings{0x1000U, {}, {}};
  RacMobyPostV1 post(actor, bindings, {1U, 1U, 7U});
  const auto *rotation = std::get_if<RacMobyPostRotationRequestV1>(&post.continuation());
  check(rotation != nullptr && post.writes().empty(), "Post did not request source rotation first");
  const auto evaluated = source.evaluate(*rotation);
  post.resume(evaluated.rotation);
  check(std::holds_alternative<RacMobyPostScaleRequestV1>(post.continuation()) &&
            post.staged_actor().matrix_bits == evaluated.rotation.columns &&
            post.writes().size() == 3U && post.staged_actor().packed_bounds_bits == actor.packed_bounds_bits,
        "Source rotation did not advance only the matrix/scale boundary");
  for (std::size_t i = 0; i < 3U; ++i)
    check(post.writes()[i].field == static_cast<RacMobyPostFieldV1>(
              static_cast<unsigned>(RacMobyPostFieldV1::matrix0) + i) &&
              post.writes()[i].value_bits == evaluated.rotation.columns[i],
          "Post lost source matrix write order");
  post.evaluate_scale_reference();
  check(std::holds_alternative<RacMobyPostDerivedRequestV1>(post.continuation()) &&
            post.staged_actor().counter_word_bits == actor.counter_word_bits &&
            post.staged_actor().packed_bounds_bits == actor.packed_bounds_bits,
        "Rotation adapter fabricated derived/spatial completion");
}

void source_execution(const std::filesystem::path &path) {
  const auto boot = read_source(path);
  const RacMobyRotationSourceV1 source(boot);
  auto changed = boot;
  changed.back() ^= std::byte{1U};
  rejects([&] { static_cast<void>(RacMobyRotationSourceV1(changed)); });
  rejects([&] { static_cast<void>(RacMobyRotationSourceV1(
      std::span<const std::byte>(boot).first(boot.size() - 1U))); });

  // Independent source-context check: execute the same original bytes using
  // two fully poisoned callers. This tests the adapter's ownership reduction,
  // not a second arithmetic implementation or a physical-console oracle.
  const auto elf = inspect_elf(boot);
  const std::array overlays{elf.dvp_overlay_table->overlays.at(32U)};
  const std::array<std::uint16_t, 1U> entries{0xd18U / 8U};
  const auto program = decode_dvp_vu_program_v1(boot, overlays, entries,
      {32U * 1024U * 1024U, 1U, 0x2f0U, 1U, 256U, 512U});

  struct Input { RacMobyPostVectorV1 rotation; unsigned polynomials, compositions; };
  const std::array cases{
      Input{{0U, 0U, 0U, 0U}, 0U, 0U},
      Input{{0x80000000U, 0U, 0x80000000U, 0x7f800000U}, 0U, 0U},
      Input{{1U, 0x807fffffU, 0U, 0xffffffffU}, 0U, 0U},
      Input{{0x3f000000U, 0U, 0U, 0U}, 1U, 0U},
      Input{{0U, 0xbe800000U, 0U, 0U}, 1U, 1U},
      Input{{0U, 0U, 0x3f800000U, 0U}, 1U, 1U},
      Input{{0x3f000000U, 0xbe800000U, 0U, 0U}, 2U, 1U},
      Input{{0x3f000000U, 0U, 0x3f800000U, 0U}, 2U, 1U},
      Input{{0U, 0xbe800000U, 0x3f800000U, 0U}, 2U, 2U},
      Input{{0x3f000000U, 0xbe800000U, 0x3f800000U, 0U}, 3U, 2U},
      Input{{0x40490fdaU, 0xc0490fdaU, 0x3fc90fdaU, 0U}, 3U, 2U}};
  for (const auto &input : cases) {
    const auto actual = source.evaluate({input.rotation});
    check(actual.polynomial_calls == input.polynomials &&
              actual.composition_calls == input.compositions &&
              actual.executed_instruction_pairs > 0U && actual.executed_instruction_pairs < 256U,
          "d18 zero flags or source call counts differ");
    check(has_warning(actual, DvpVuExecutionWarningV1::vu_add_sub_reference_model) &&
              has_warning(actual, DvpVuExecutionWarningV1::vu_mul_reference_model),
          "d18 lost the reference arithmetic qualification");
    if (!input.polynomials) {
      check(actual.rotation.columns == kIdentity && actual.executed_instruction_pairs == 15U,
            "Zero/denormal source gate did not execute initialized identity");
    } else {
      check(actual.rotation.columns != kIdentity &&
                has_warning(actual, DvpVuExecutionWarningV1::vu_madd_reference_model) &&
                has_warning(actual, DvpVuExecutionWarningV1::host_float_approximation),
            "Nonzero d18 path skipped the source polynomial or its qualifications");
    }
    auto ignored_w = input.rotation;
    ignored_w[3U] ^= 0xffffffffU;
    check(source.evaluate({ignored_w}).rotation.columns == actual.rotation.columns,
          "Unused VF1.W affected d18 output");
    for (unsigned poison = 1U; poison <= 2U; ++poison) {
      auto state = make_dvp_vu_execution_state_v1();
      for (std::size_t reg = 1U; reg < state.vf.size(); ++reg)
        for (std::size_t lane = 0U; lane < 4U; ++lane)
          state.vf[reg].lanes[lane] = {
              (poison == 1U ? 0x3f000000U : 0xc0000000U) |
                  static_cast<std::uint32_t>(reg * 67U + lane * 19U), 0xffffffffU};
      for (std::size_t reg = 1U; reg < state.vi.size(); ++reg)
        state.vi[reg] = {static_cast<std::uint32_t>(reg * 31U + poison), 0xffffU};
      for (std::size_t lane = 0U; lane < 4U; ++lane) {
        state.accumulator.lanes[lane] = {0xffffffffU, 0xffffffffU};
        state.accumulator_overflow[lane] = {1U, 1U};
      }
      state.scalar_i = {0x41000000U + poison, 0xffffffffU};
      state.scalar_q = {0x42000000U + poison, 0xffffffffU};
      state.mac_flags = {0xaaaaU / poison, 0xffffU};
      state.status_flags = {0xaaaU / poison, 0xfffU};
      state.clip_flags = {0xaaaaaaU / poison, 0xffffffU};
      for (std::size_t lane = 0U; lane < 3U; ++lane)
        state.vf[1U].lanes[lane] = {input.rotation[lane], 0xffffffffU};
      const auto expected_q = state.scalar_q;
      const auto direct = execute_dvp_vu_program_v1(program, std::move(state),
          {0xd18U / 8U, false}, {256U, 1U, 1U, 1U});
      check(direct.termination == DvpVuTerminationV1::program_end &&
                direct.executed_instruction_pairs == actual.executed_instruction_pairs &&
                direct.final_state.scalar_q == expected_q && direct.xgkick_events.empty(),
            "d18 control or unrelated scalar depended on poisoned caller");
      for (std::size_t column = 0U; column < 3U; ++column)
        for (std::size_t lane = 0U; lane < 4U; ++lane)
          check(direct.final_state.vf[20U + column].lanes[lane] ==
                    DvpVuWordV1{actual.rotation.columns[column][lane], 0xffffffffU},
                "d18 column depended on unrelated caller state");
    }
  }
  for (const auto invalid : {0x40490fdbU, 0x7f800000U, 0xff800000U, 0x7fc00000U})
    for (std::size_t lane = 0; lane < 3U; ++lane) {
      RacMobyPostRotationRequestV1 request;
      request.rotation_bits[lane] = invalid;
      rejects([&] { static_cast<void>(source.evaluate(request)); });
    }
  reached_post_stage(source);
  std::cout << "Original d18 cases=" << cases.size()
            << " poisoned_callers_per_case=2 post_rotation_to_derived_boundary=PASS\n"
               "Qualification: existing bounded VU reference; no hardware or spatial completion claim\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 1 || argc == 2, "Expected an optional owned original ELF path");
    source_admission();
    if (argc == 2) source_execution(std::filesystem::path(argv[1]));
    else std::cout << "Source admission rejection checks passed; actual d18 execution not requested\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
