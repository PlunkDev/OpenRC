#pragma once

#include <cstdint>

namespace openrc {

// Compiler-side palette utilities shared by the existing PS2 asset decoders.
// Runtime packages contain prepared colours and do not require these mappings.

// The PSMT8 CLUT layout used by these decoders exchanges index bits 3 and 4.
// The input is one 8-bit palette index, not a GS address. This permutation is
// its own inverse, so it also maps a storage index back to a logical index.
[[nodiscard]] inline constexpr std::uint8_t psmt8_clut_storage_index_v1(
    const std::uint8_t logical_index) noexcept {
    return static_cast<std::uint8_t>(
        (logical_index & 0xe7U) | ((logical_index & 0x08U) << 1U) |
        ((logical_index & 0x10U) >> 1U));
}

// Preserve the existing decoders' export convention: min(255, 2 * ps2_alpha).
// Raw source alpha is retained separately where the public asset type needs it.
[[nodiscard]] inline constexpr std::uint8_t ps2_alpha_to_rgba8_v1(
    const std::uint8_t ps2_alpha) noexcept {
    return ps2_alpha < 0x80U
               ? static_cast<std::uint8_t>(
                     static_cast<std::uint16_t>(ps2_alpha) * 2U)
               : 0xffU;
}

} // namespace openrc
