#include "openrc/ps2_palette.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

static_assert(openrc::psmt8_clut_storage_index_v1(0x08U) == 0x10U);
static_assert(openrc::psmt8_clut_storage_index_v1(0x10U) == 0x08U);
static_assert(openrc::ps2_alpha_to_rgba8_v1(0x7fU) == 0xfeU);
static_assert(openrc::ps2_alpha_to_rgba8_v1(0x80U) == 0xffU);
static_assert(noexcept(openrc::psmt8_clut_storage_index_v1(0U)));
static_assert(noexcept(openrc::ps2_alpha_to_rgba8_v1(0U)));

void expect(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_clut_full_domain_bijection_and_involution() {
    // Independent tabular layout: every 32-entry group contains four runs of
    // eight colours; their storage order is logical runs 0, 2, 1, 3.
    constexpr std::array<std::size_t, 4U> storage_run{0U, 2U, 1U, 3U};
    std::array<bool, 256U> seen{};
    for (std::size_t logical = 0U; logical < seen.size(); ++logical) {
        const auto group_base = (logical / 32U) * 32U;
        const auto logical_run = (logical % 32U) / 8U;
        const auto expected =
            group_base + storage_run[logical_run] * 8U + logical % 8U;
        const auto actual = openrc::psmt8_clut_storage_index_v1(
            static_cast<std::uint8_t>(logical));
        expect(actual == expected,
               "PSMT8 CLUT index differs from the existing palette layout");
        expect(!seen[actual], "PSMT8 CLUT mapping is not one-to-one");
        seen[actual] = true;
        expect(openrc::psmt8_clut_storage_index_v1(actual) == logical,
               "PSMT8 CLUT mapping is not its own inverse");
        expect((actual & 0xe7U) == (logical & 0xe7U),
               "PSMT8 CLUT mapping changed a bit other than 3 or 4");
    }
    expect(std::all_of(seen.begin(), seen.end(), [](const bool value) {
               return value;
           }),
           "PSMT8 CLUT mapping does not cover every storage index");
}

void test_clut_known_edges() {
    struct Edge {
        std::uint8_t logical;
        std::uint8_t storage;
    };
    constexpr std::array edges{
        Edge{0x00U, 0x00U}, Edge{0x07U, 0x07U}, Edge{0x08U, 0x10U},
        Edge{0x0fU, 0x17U}, Edge{0x10U, 0x08U}, Edge{0x17U, 0x0fU},
        Edge{0x18U, 0x18U}, Edge{0x1fU, 0x1fU}, Edge{0x20U, 0x20U},
        Edge{0x28U, 0x30U}, Edge{0x30U, 0x28U}, Edge{0xffU, 0xffU},
    };
    for (const auto &edge : edges) {
        expect(openrc::psmt8_clut_storage_index_v1(edge.logical) == edge.storage,
               "PSMT8 CLUT known boundary mismatch");
    }
}

void test_alpha_full_domain_and_saturation() {
    std::uint8_t previous = 0U;
    for (std::uint32_t raw = 0U; raw < 256U; ++raw) {
        const auto expected = std::min<std::uint32_t>(255U, raw * 2U);
        const auto actual =
            openrc::ps2_alpha_to_rgba8_v1(static_cast<std::uint8_t>(raw));
        expect(actual == expected,
               "PS2 alpha differs from existing saturating RGBA expansion");
        expect(actual >= previous, "PS2 alpha expansion is not monotonic");
        previous = actual;
    }
    expect(openrc::ps2_alpha_to_rgba8_v1(0x00U) == 0x00U &&
               openrc::ps2_alpha_to_rgba8_v1(0x01U) == 0x02U &&
               openrc::ps2_alpha_to_rgba8_v1(0x40U) == 0x80U &&
               openrc::ps2_alpha_to_rgba8_v1(0x7fU) == 0xfeU &&
               openrc::ps2_alpha_to_rgba8_v1(0x80U) == 0xffU &&
               openrc::ps2_alpha_to_rgba8_v1(0x81U) == 0xffU &&
               openrc::ps2_alpha_to_rgba8_v1(0xffU) == 0xffU,
           "PS2 alpha known boundary mismatch");
}

} // namespace

int main() {
    try {
        test_clut_full_domain_bijection_and_involution();
        test_clut_known_edges();
        test_alpha_full_domain_and_saturation();
        std::cout << "PS2 palette tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
