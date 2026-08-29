#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kWadPayloadStructureFingerprintVersionV1 = 1U;
inline constexpr std::size_t kWadPayloadStructurePrefixBytesV1 = 32U;
inline constexpr std::size_t kWadPayloadStructureHeaderWordsV1 = 64U;
inline constexpr std::size_t kWadPayloadStructureCanonicalKeyBytesV1 =
    8U + 1U + 1U + 1U + 4U + kWadPayloadStructureHeaderWordsV1;

struct WadPayloadStructureLimitsV1 {
    std::uint64_t max_input_bytes = 0U;
    std::uint32_t max_sampled_bytes = 0U;
};

struct WadPayloadStructureFingerprintV1 {
    std::uint32_t version = kWadPayloadStructureFingerprintVersionV1;
    // SHA-256 of the documented V1 structural feature vector. This is a
    // heuristic family key, not a semantic-format or byte-identity claim.
    std::string shape_sha256;
    // Full equality key. shape_sha256 is only its stable printable identifier;
    // callers must not merge distinct keys solely because their hashes agree.
    std::vector<std::byte> canonical_schema_key;
    std::uint64_t input_bytes = 0U;
    std::string prefix_hex;
    std::uint8_t size_log2_bucket = 0U;
    // Exact trailing-zero count (capped at 31) for diagnostics, or 0xff for an
    // empty payload. The canonical key coarsens non-empty values to unaligned /
    // 4-byte / 16-byte-or-better.
    std::uint8_t size_alignment_log2 = 0U;
    std::uint16_t available_header_words = 0U;
    std::uint32_t sampled_bytes = 0U;
    std::uint16_t sampled_zero_permille = 0U;
    std::uint16_t sampled_ff_permille = 0U;
    std::uint16_t sampled_printable_permille = 0U;
    std::uint16_t sampled_high_bit_permille = 0U;

    [[nodiscard]] bool operator==(
        const WadPayloadStructureFingerprintV1&) const = default;

    // Whole-object equality above deliberately includes representative-only
    // diagnostics. Candidate-family identity uses only the version and exact
    // canonical key; the SHA-256 string is its printable identifier.
    [[nodiscard]] bool has_same_candidate_family_as(
        const WadPayloadStructureFingerprintV1& other) const noexcept {
        return version == other.version &&
            canonical_schema_key == other.canonical_schema_key;
    }
};

class WadPayloadStructureError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Builds a deterministic, offset-normalized sketch from one complete decoded
// payload. V1 hashes its logarithmic size/alignment classes, an exact printable
// four-byte signature when present, and token classes for the first 64 LE32
// words. It never retains or interprets the payload as a proven format.
[[nodiscard]] WadPayloadStructureFingerprintV1
fingerprint_wad_payload_structure_v1(
    std::span<const std::byte> decoded_bytes,
    WadPayloadStructureLimitsV1 limits);

} // namespace openrc
