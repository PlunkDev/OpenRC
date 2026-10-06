#pragma once

#include "openrc/dvp_vu.hpp"
#include "openrc/rac_gameplay_bank.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Compiler-owned source state after 1ea104..1ea174: the directional table is
// explicitly cleared before at most twelve authored records are copied.
// Initial TIE/shrub point-light selectors are 0xffff, written by the loader.
// Later dynamic point-light updates are outside this initial-palette API.
struct RacInstanceLightingSourceV1 {
  DvpVuProgramV1 palette_program;
  std::array<std::array<std::array<std::uint32_t,4>,4>,16> directional{};
  std::array<std::uint32_t,4> length_weights{};
};

struct RacInstancePaletteV1 {
  // Original GS modulation bytes, including the original 0x80 alpha scale.
  // A neutral RenderScene consumer converts these bytes according to its
  // existing GS color contract; no original instruction or pointer is kept.
  std::vector<std::array<std::uint8_t,4>> rgba;
  std::uint64_t executed_instruction_pairs = 0;
  static constexpr bool physical_console_qualified = false;
};

class RacInstanceLightingError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};

// Source bytes are the decoded gameplay bank, not the containing frontend WAD.
// Reads the original resident VU0 overlay 436083 and vf21 constant from ELF.
[[nodiscard]] RacInstanceLightingSourceV1 make_rac_instance_lighting_source_v1(
    std::span<const std::byte> resident_elf,
    std::span<const std::byte> source_gameplay);

// Original initial-load consumers 238688 and 22b8f8. These return exactly 64
// TIE or 24 shrub colors by executing all four resident palette entries through
// the common VU executor. Matrix preparation retains source operand order,
// separate ACC overflow history, and the shared integer SQRT/DIV references.
[[nodiscard]] RacInstancePaletteV1 compile_rac_tie_initial_palette_v1(
    const RacInstanceLightingSourceV1& source,
    std::span<const std::byte> source_class,
    const RacGameplayTieInstanceV1& instance);
[[nodiscard]] RacInstancePaletteV1 compile_rac_shrub_initial_palette_v1(
    const RacInstanceLightingSourceV1& source,
    std::span<const std::byte> source_class,
    const RacGameplayShrubInstanceV1& instance);

} // namespace openrc
