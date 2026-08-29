#pragma once

#include "openrc/wad_payload_inventory.hpp"
#include "openrc/wad_payload_structure.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

inline constexpr std::size_t kWadPayloadOriginKindCountV1 = 8U;
inline constexpr std::size_t kWadPayloadGlobalTocOriginIndexV1 = 0U;
inline constexpr std::size_t kWadPayloadGlobalTocTailOriginIndexV1 = 1U;
inline constexpr std::size_t kWadPayloadLocalWadRunOriginIndexV1 = 2U;
inline constexpr std::size_t kWadPayloadPrimaryExtent0SubrangeOriginIndexV1 = 3U;
inline constexpr std::size_t kWadPayloadPrimaryExtentOriginIndexV1 = 4U;
inline constexpr std::size_t kWadPayloadWadBundleRecordOriginIndexV1 = 5U;
inline constexpr std::size_t kWadPayloadCompanionTerminalRecordOriginIndexV1 = 6U;
inline constexpr std::size_t kWadPayloadExplicitInputOriginIndexV1 = 7U;
inline constexpr std::size_t kWadPayloadClassificationCountV1 = 3U;
inline constexpr std::size_t kWadPayloadRecognizedClassificationIndexV1 = 0U;
inline constexpr std::size_t kWadPayloadUnknownClassificationIndexV1 = 1U;
inline constexpr std::size_t kWadPayloadAmbiguousClassificationIndexV1 = 2U;

struct WadPayloadFamilyLimitsV1 {
    std::uint64_t max_unique_payloads = 0U;
    std::uint64_t max_observations = 0U;
    std::uint64_t max_input_bytes_per_payload = 0U;
    std::uint64_t max_total_input_bytes = 0U;
    std::uint32_t max_sampled_bytes_per_payload = 0U;
    std::uint64_t max_total_sampled_bytes = 0U;
    std::uint64_t max_families = 0U;
    std::uint64_t max_members_per_family = 0U;
    std::uint64_t max_levels_per_family = 0U;
    std::uint64_t max_unique_level_memberships = 0U;
};

struct WadPayloadFamilyLevelCountV1 {
    std::uint32_t level_id = 0U;
    std::uint64_t unique_payloads = 0U;
    std::uint64_t observations = 0U;
    std::array<std::uint64_t, kWadPayloadClassificationCountV1>
        classification_unique_payloads{};
    std::array<std::uint64_t, kWadPayloadClassificationCountV1>
        classification_observations{};

    [[nodiscard]] bool operator==(
        const WadPayloadFamilyLevelCountV1&) const = default;
};

struct WadPayloadCandidateFamilyV1 {
    // Candidate families are heuristic structural groups, never semantic
    // format claims. Equality is based on the full canonical schema key in
    // representative_structure; family_id is its printable SHA-256 identity.
    std::string family_id;
    WadPayloadStructureFingerprintV1 representative_structure;
    std::uint64_t representative_unique_payload_index = 0U;
    std::string representative_decoded_sha256;
    std::uint64_t first_observation_index = 0U;
    std::vector<std::uint64_t> member_unique_payload_indices;
    std::uint64_t unique_payloads = 0U;
    std::uint64_t observations = 0U;
    std::array<std::uint64_t, kWadPayloadClassificationCountV1>
        classification_unique_payloads{};
    std::array<std::uint64_t, kWadPayloadClassificationCountV1>
        classification_observations{};
    std::uint64_t minimum_decoded_bytes = 0U;
    std::uint64_t maximum_decoded_bytes = 0U;
    std::uint64_t total_unique_decoded_bytes = 0U;
    std::uint64_t total_observed_decoded_bytes = 0U;
    std::array<std::uint64_t, kWadPayloadOriginKindCountV1>
        origin_observations{};
    std::vector<WadPayloadFamilyLevelCountV1> levels;
};

struct WadPayloadFamilyInventoryV1 {
    // Families are sorted by family_id. Members are sorted by decoded SHA-256,
    // and level counts are sorted by level_id.
    std::vector<WadPayloadCandidateFamilyV1> families;
    std::uint64_t profiled_unique_payloads = 0U;
    std::uint64_t unknown_unique_payloads = 0U;
    std::uint64_t unknown_observations = 0U;
    std::uint64_t families_with_unknown_payloads = 0U;
};

class WadPayloadFamilyError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class WadPayloadFamilyBuilderV1 final {
public:
    explicit WadPayloadFamilyBuilderV1(WadPayloadFamilyLimitsV1 limits);

    WadPayloadFamilyBuilderV1(const WadPayloadFamilyBuilderV1&) = delete;
    WadPayloadFamilyBuilderV1& operator=(const WadPayloadFamilyBuilderV1&) = delete;
    WadPayloadFamilyBuilderV1(WadPayloadFamilyBuilderV1&&) noexcept = default;
    WadPayloadFamilyBuilderV1& operator=(WadPayloadFamilyBuilderV1&&) noexcept = default;

    // Call immediately after every WadPayloadInventoryBuilderV1 observation.
    // Observation and unique indices must follow the inventory builder's exact
    // order. A new unique payload is profiled once; a duplicate is checked for
    // size consistency and does not rescan the bytes. This deliberately trusts
    // the WadPayloadAddResultV1 returned by the paired inventory-builder call.
    void observe(WadPayloadAddResultV1 added,
                 std::span<const std::byte> decoded_bytes);

    // Joins the retained structural profiles to the finalized semantic
    // inventory and closes the builder. All classifications are grouped so a
    // later strict probe does not change family identity; callers can filter
    // families through classification_unique_payloads[unknown].
    [[nodiscard]] WadPayloadFamilyInventoryV1 finalize(
        const WadPayloadInventoryV1& inventory);

private:
    WadPayloadFamilyLimitsV1 limits_;
    std::vector<WadPayloadStructureFingerprintV1> profiles_;
    std::uint64_t total_profiled_input_bytes_ = 0U;
    std::uint64_t total_sampled_bytes_ = 0U;
    std::uint64_t observed_observations_ = 0U;
    bool finalized_ = false;
};

} // namespace openrc
