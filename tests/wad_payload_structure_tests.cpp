#include "openrc/wad_payload_structure.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void expect_structure_error(Function&& function, const std::string& message) {
    try {
        std::invoke(std::forward<Function>(function));
    } catch (const openrc::WadPayloadStructureError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] std::vector<std::byte> make_golden_payload() {
    std::vector<std::byte> result(16U, std::byte{0});
    result[0U] = std::byte{'T'};
    result[1U] = std::byte{'E'};
    result[2U] = std::byte{'S'};
    result[3U] = std::byte{'T'};
    std::fill(
        result.begin() + 8,
        result.begin() + 12,
        std::byte{0xffU});
    std::fill(
        result.begin() + 12,
        result.end(),
        std::byte{0x11U});
    return result;
}

[[nodiscard]] std::vector<std::byte> golden_schema_key() {
    std::vector<std::byte> result{
        std::byte{'O'},
        std::byte{'R'},
        std::byte{'C'},
        std::byte{'W'},
        std::byte{'F'},
        std::byte{'A'},
        std::byte{'M'},
        std::byte{'1'},
        std::byte{5U},
        std::byte{4U},
        std::byte{1U},
        std::byte{'T'},
        std::byte{'E'},
        std::byte{'S'},
        std::byte{'T'},
        std::byte{2U},
        std::byte{0U},
        std::byte{1U},
        std::byte{3U},
    };
    result.insert(result.end(), 60U, std::byte{0xffU});
    return result;
}

void test_golden_canonical_schema_and_hash() {
    const auto payload = make_golden_payload();
    const auto fingerprint = openrc::fingerprint_wad_payload_structure_v1(
        payload,
        openrc::WadPayloadStructureLimitsV1{16U, 16U});

    expect(
        fingerprint.version ==
            openrc::kWadPayloadStructureFingerprintVersionV1,
        "the structure fingerprint version changed");
    expect(
        fingerprint.canonical_schema_key == golden_schema_key(),
        "the canonical WadV1 structure schema changed");
    expect(
        fingerprint.shape_sha256 ==
            "15d92fd2ffdd3c3cf865611fc7c9bc0b164f55da4b09e750c12c29a488d6da17",
        "the canonical WadV1 structure SHA-256 changed");
    expect(
        fingerprint.input_bytes == 16U &&
            fingerprint.prefix_hex == "5445535400000000ffffffff11111111" &&
            fingerprint.size_log2_bucket == 5U &&
            fingerprint.size_alignment_log2 == 4U &&
            fingerprint.available_header_words == 4U,
        "the golden WadV1 structure dimensions are wrong");
    expect(
        fingerprint.sampled_bytes == 16U &&
            fingerprint.sampled_zero_permille == 250U &&
            fingerprint.sampled_ff_permille == 250U &&
            fingerprint.sampled_printable_permille == 250U &&
            fingerprint.sampled_high_bit_permille == 250U,
        "the golden WadV1 sampling diagnostics are wrong");

    const auto sparsely_sampled =
        openrc::fingerprint_wad_payload_structure_v1(
            payload,
            openrc::WadPayloadStructureLimitsV1{16U, 2U});
    expect(
        sparsely_sampled.canonical_schema_key ==
                fingerprint.canonical_schema_key &&
            sparsely_sampled.shape_sha256 == fingerprint.shape_sha256,
        "the sampling budget changed the canonical family identity");
}

void test_empty_and_short_payloads() {
    const std::vector<std::byte> empty;
    const auto empty_fingerprint =
        openrc::fingerprint_wad_payload_structure_v1(
            empty,
            openrc::WadPayloadStructureLimitsV1{8U, 4U});
    expect(
        empty_fingerprint.input_bytes == 0U &&
            empty_fingerprint.prefix_hex.empty() &&
            empty_fingerprint.size_log2_bucket == 0U &&
            empty_fingerprint.size_alignment_log2 == 0xffU &&
            empty_fingerprint.available_header_words == 0U &&
            empty_fingerprint.sampled_bytes == 0U,
        "an empty WadV1 structure fingerprint has wrong dimensions");
    expect(
        empty_fingerprint.sampled_zero_permille == 0U &&
            empty_fingerprint.sampled_ff_permille == 0U &&
            empty_fingerprint.sampled_printable_permille == 0U &&
            empty_fingerprint.sampled_high_bit_permille == 0U,
        "an empty WadV1 structure fingerprint has non-zero diagnostics");
    expect(
        empty_fingerprint.canonical_schema_key.size() ==
            openrc::kWadPayloadStructureCanonicalKeyBytesV1,
        "the empty canonical WadV1 schema has the wrong size");

    const std::vector<std::byte> short_payload{
        std::byte{'A'},
        std::byte{0U},
        std::byte{0xffU},
    };
    const auto short_fingerprint =
        openrc::fingerprint_wad_payload_structure_v1(
            short_payload,
            openrc::WadPayloadStructureLimitsV1{3U, 2U});
    expect(
        short_fingerprint.input_bytes == 3U &&
            short_fingerprint.prefix_hex == "4100ff" &&
            short_fingerprint.size_log2_bucket == 2U &&
            short_fingerprint.size_alignment_log2 == 0U &&
            short_fingerprint.available_header_words == 0U &&
            short_fingerprint.sampled_bytes == 2U,
        "a short WadV1 structure fingerprint has wrong dimensions");
    expect(
        short_fingerprint.sampled_zero_permille == 0U &&
            short_fingerprint.sampled_ff_permille == 500U &&
            short_fingerprint.sampled_printable_permille == 500U &&
            short_fingerprint.sampled_high_bit_permille == 500U,
        "a short WadV1 structure fingerprint sampled the wrong bytes");
}

void test_identity_is_separate_from_diagnostics() {
    const std::vector<std::byte> low_bytes(8U, std::byte{1U});
    const std::vector<std::byte> high_bytes(8U, std::byte{0x80U});
    const auto low_fingerprint =
        openrc::fingerprint_wad_payload_structure_v1(
            low_bytes,
            openrc::WadPayloadStructureLimitsV1{8U, 8U});
    const auto high_fingerprint =
        openrc::fingerprint_wad_payload_structure_v1(
            high_bytes,
            openrc::WadPayloadStructureLimitsV1{8U, 8U});
    expect(
        low_fingerprint.prefix_hex != high_fingerprint.prefix_hex &&
            low_fingerprint.sampled_high_bit_permille == 0U &&
            high_fingerprint.sampled_high_bit_permille == 1000U,
        "the structure fixtures did not produce distinct diagnostics");
    expect(
        low_fingerprint.canonical_schema_key ==
                high_fingerprint.canonical_schema_key &&
            low_fingerprint.shape_sha256 == high_fingerprint.shape_sha256,
        "diagnostic prefix or sampling data leaked into WadV1 family identity");
}

void test_prefix_and_limits() {
    std::vector<std::byte> long_payload(40U, std::byte{'a'});
    const auto boundary = openrc::fingerprint_wad_payload_structure_v1(
        long_payload,
        openrc::WadPayloadStructureLimitsV1{40U, 1U});
    std::string expected_prefix;
    for (std::size_t index = 0U;
         index < openrc::kWadPayloadStructurePrefixBytesV1;
         ++index) {
        expected_prefix += "61";
    }
    expect(
        boundary.input_bytes == 40U && boundary.sampled_bytes == 1U,
        "exact WadV1 structure limits were not accepted");
    expect(
        boundary.prefix_hex == expected_prefix,
        "the diagnostic prefix was not capped at 32 bytes");

    expect_structure_error(
        [&] {
            (void)openrc::fingerprint_wad_payload_structure_v1(
                long_payload,
                openrc::WadPayloadStructureLimitsV1{39U, 1U});
        },
        "the WadV1 structure input-byte limit was ignored");
    expect_structure_error(
        [&] {
            (void)openrc::fingerprint_wad_payload_structure_v1(
                long_payload,
                openrc::WadPayloadStructureLimitsV1{0U, 1U});
        },
        "a zero WadV1 structure input-byte limit was accepted");
    expect_structure_error(
        [&] {
            (void)openrc::fingerprint_wad_payload_structure_v1(
                long_payload,
                openrc::WadPayloadStructureLimitsV1{40U, 0U});
        },
        "a zero WadV1 structure sampling limit was accepted");
}

} // namespace

int main() {
    try {
        test_golden_canonical_schema_and_hash();
        test_empty_and_short_payloads();
        test_identity_is_separate_from_diagnostics();
        test_prefix_and_limits();
        std::cout << "OpenRC WadPayloadStructureFingerprintV1 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC WadPayloadStructureFingerprintV1 tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
