#include "openrc/rac_moby_rotation.hpp"

#include "openrc/elf.hpp"
#include "openrc/hash.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint64_t kMaximumElfBytes = 32U * 1024U * 1024U;
constexpr std::uint16_t kEntrypoint = 0xd18U / 8U;
constexpr std::uint16_t kFinalDelay = 0xe18U / 8U;
constexpr std::uint16_t kEndAddress = 0xf70U / 8U;

void require(bool value, const char *message) {
  if (!value) throw RacMobyPostError(message);
}

void qualify(std::span<const std::byte> bytes, const char *expected) {
  Sha256 hash;
  hash.update(bytes);
  require(hex_digest(hash.finish()) == expected,
          "Moby d18 source revision is not qualified");
}

std::uint32_t visits(const DvpVuExecutionResultV1 &result,
                     std::uint16_t byte_address) {
  return static_cast<std::uint32_t>(std::count(result.instruction_trace.begin(),
      result.instruction_trace.end(), byte_address / 8U));
}

} // namespace

RacMobyRotationSourceV1::RacMobyRotationSourceV1(
    const std::span<const std::byte> boot) {
  require(!boot.empty() && boot.size() <= kMaximumElfBytes,
          "Moby d18 ELF size exceeds its source bound");
  qualify(boot, "17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b");
  const auto elf = inspect_elf(boot);
  require(elf.dvp_overlay_table.has_value() &&
              elf.dvp_overlay_table->overlays.size() > 32U,
          "Moby d18 ELF has no qualified resident overlay");
  const auto &source = elf.dvp_overlay_table->overlays[32U];
  require(source.name == ".DVP.overlay..0xc80.28259.24.0" &&
              source.load_memory_address == 0x10e4d0U &&
              source.virtual_memory_address == 0xc80U &&
              source.code_file_offset == 0xf450U && source.size == 0x2f0U &&
              source.code_file_offset <= boot.size() &&
              source.size <= boot.size() - source.code_file_offset,
          "Moby d18 resident overlay mapping differs");
  // inspect_elf resolves the LMA through PT_LOAD. The zero-filled named
  // placeholder section is not the instruction payload.
  const auto bytes = boot.subspan(static_cast<std::size_t>(source.code_file_offset), source.size);
  qualify(bytes, "f92033827dd132fa121e9303eecd4742fe79a39424cba78ddcdb2834c8c42be9");
  qualify(bytes.subspan(0xd18U - 0xc80U, 0xf70U - 0xd18U),
          "466a7c0f82778a3791ca8e24081afc7f9cd0677d4af3aa04493bc2b12f9f5ea6");
  const std::array selected{source};
  const std::array entries{kEntrypoint};
  program_ = decode_dvp_vu_program_v1(boot, selected, entries,
      DvpVuLimits{kMaximumElfBytes, 1U, 0x2f0U, 1U, 256U, 512U});
  require(program_.instructions.size() == 94U && program_.code_chunks.size() == 1U &&
              program_.total_code_bytes == 0x2f0U && program_.entrypoints.size() == 1U &&
              program_.entrypoints.front().instruction_address == kEntrypoint &&
              program_.unknown_upper_count == 0U && program_.unknown_lower_count == 0U &&
              program_.memory_accesses.empty(),
          "Moby d18 decoded program differs from its register-only source contract");
}

RacMobyRotationEvaluationV1 RacMobyRotationSourceV1::evaluate(
    const RacMobyPostRotationRequestV1 &request) const {
  auto initial = make_dvp_vu_execution_state_v1();
  for (std::size_t lane = 0; lane < 3U; ++lane) {
    const auto raw = request.rotation_bits[lane];
    require((raw & 0x7fffffffU) <= 0x40490fdaU,
            "Moby d18 rotation exceeds its qualified source angle domain");
    initial.vf[1U].lanes[lane] = {raw, 0xffffffffU};
  }
  // The full original graph has three bounded polynomial calls and two
  // matrix compositions. It reads no data memory, Q, P or external tables.
  auto executed = execute_dvp_vu_program_v1(program_, std::move(initial),
      DvpVuExecutionOptionsV1{kEntrypoint, false},
      DvpVuExecutionLimitsV1{256U, 1U, 1U, 1U});
  require(executed.termination == DvpVuTerminationV1::program_end &&
              !executed.instruction_trace.empty() &&
              executed.instruction_trace.front() == kEntrypoint &&
              executed.instruction_trace.back() == kFinalDelay &&
              executed.executed_instruction_pairs == executed.instruction_trace.size(),
          "Moby d18 did not complete its original E delay instruction");
  require(std::all_of(executed.instruction_trace.begin(), executed.instruction_trace.end(),
                      [](std::uint16_t pc) { return pc >= kEntrypoint && pc < kEndAddress; }) &&
              executed.xgkick_events.empty(),
          "Moby d18 execution left its qualified call graph");
  const auto &final = executed.final_state;
  require(std::all_of(final.data_memory.begin(), final.data_memory.end(),
                      [](const DvpVuVectorV1 &v) { return v == DvpVuVectorV1{}; }) &&
              final.scalar_q == DvpVuWordV1{} && final.clip_flags == DvpVuWordV1{} &&
              final.xtop_qword == DvpVuWordV1{},
          "Moby d18 changed an unowned memory or scalar input");
  for (const auto reg : {8U, 17U, 18U, 19U})
    require(final.vf[reg] == DvpVuVectorV1{},
            "Moby d18 overwrote a concurrently owned post-step register");

  RacMobyRotationEvaluationV1 result;
  result.executed_instruction_pairs = executed.executed_instruction_pairs;
  result.polynomial_calls = visits(executed, 0xe20U);
  result.composition_calls = visits(executed, 0xf28U);
  require(result.polynomial_calls <= 3U && result.composition_calls <= 2U,
          "Moby d18 exceeded its original axis-call count");
  for (std::size_t column = 0; column < 4U; ++column) {
    for (std::size_t lane = 0; lane < 4U; ++lane) {
      const auto value = final.vf[20U + column].lanes[lane];
      require(value.known_mask == 0xffffffffU &&
                  (value.bits & 0x7f800000U) != 0x7f800000U,
              "Moby d18 output is unknown or outside the finite projection domain");
      if (column < 3U) result.rotation.columns[column][lane] = value.bits;
      else require(value.bits == (lane == 3U ? 0x3f800000U : 0U),
                   "Moby d18 did not initialize the architectural fourth column");
    }
    if (column < 3U)
      require((result.rotation.columns[column][3U] & 0x7fffffffU) == 0U,
              "Moby d18 rotation column W has nonzero magnitude");
  }
  result.warnings = std::move(executed.warnings);
  return result;
}

} // namespace openrc
