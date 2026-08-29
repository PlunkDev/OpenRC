#include "openrc/wad_payload_family.hpp"

#include "openrc/hash.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw WadPayloadFamilyError(message);
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while counting ") + description);
    }
    return left + right;
}

void validate_limits(const WadPayloadFamilyLimitsV1 limits) {
    if (limits.max_unique_payloads == 0U ||
        limits.max_observations == 0U ||
        limits.max_input_bytes_per_payload == 0U ||
        limits.max_total_input_bytes == 0U ||
        limits.max_sampled_bytes_per_payload == 0U ||
        limits.max_total_sampled_bytes == 0U ||
        limits.max_families == 0U ||
        limits.max_members_per_family == 0U ||
        limits.max_levels_per_family == 0U ||
        limits.max_unique_level_memberships == 0U) {
        fail("WadV1 candidate-family limits must all be non-zero");
    }
}

[[nodiscard]] std::size_t origin_index(
    const WadPayloadOriginKindV1 kind) {
    switch (kind) {
    case WadPayloadOriginKindV1::global_toc:
        return kWadPayloadGlobalTocOriginIndexV1;
    case WadPayloadOriginKindV1::global_toc_tail:
        return kWadPayloadGlobalTocTailOriginIndexV1;
    case WadPayloadOriginKindV1::local_wad_run:
        return kWadPayloadLocalWadRunOriginIndexV1;
    case WadPayloadOriginKindV1::primary_extent0_subrange:
        return kWadPayloadPrimaryExtent0SubrangeOriginIndexV1;
    case WadPayloadOriginKindV1::primary_extent:
        return kWadPayloadPrimaryExtentOriginIndexV1;
    case WadPayloadOriginKindV1::wad_bundle_record:
        return kWadPayloadWadBundleRecordOriginIndexV1;
    case WadPayloadOriginKindV1::companion_terminal_record:
        return kWadPayloadCompanionTerminalRecordOriginIndexV1;
    case WadPayloadOriginKindV1::explicit_input:
        return kWadPayloadExplicitInputOriginIndexV1;
    }
    fail("A WadV1 observation has an invalid origin kind");
}

[[nodiscard]] std::size_t classification_index(
    const WadPayloadClassificationV1 classification) {
    switch (classification) {
    case WadPayloadClassificationV1::recognized:
        return kWadPayloadRecognizedClassificationIndexV1;
    case WadPayloadClassificationV1::unknown:
        return kWadPayloadUnknownClassificationIndexV1;
    case WadPayloadClassificationV1::ambiguous:
        return kWadPayloadAmbiguousClassificationIndexV1;
    }
    fail("A WadV1 unique payload has an invalid classification");
}

[[nodiscard]] WadPayloadFamilyLevelCountV1& find_or_add_level(
    WadPayloadCandidateFamilyV1& family,
    const std::uint32_t level_id,
    const std::uint64_t maximum_levels) {
    const auto existing = std::find_if(
        family.levels.begin(),
        family.levels.end(),
        [level_id](const WadPayloadFamilyLevelCountV1& candidate) {
            return candidate.level_id == level_id;
        });
    if (existing != family.levels.end()) {
        return *existing;
    }
    if (family.levels.size() >= maximum_levels) {
        fail("A WadV1 candidate family exceeds the caller's level limit");
    }
    family.levels.push_back(WadPayloadFamilyLevelCountV1{
        level_id, 0U, 0U, {}, {}});
    return family.levels.back();
}

void validate_profile(
    const WadPayloadStructureFingerprintV1& profile,
    const WadPayloadUniqueRecordV1& unique) {
    if (profile.version != kWadPayloadStructureFingerprintVersionV1 ||
        profile.canonical_schema_key.size() !=
            kWadPayloadStructureCanonicalKeyBytesV1 ||
        profile.input_bytes != unique.decoded_bytes) {
        fail("A WadV1 family profile disagrees with the semantic inventory");
    }
    constexpr std::array<std::byte, 8U> kDomain{
        std::byte{'O'}, std::byte{'R'}, std::byte{'C'}, std::byte{'W'},
        std::byte{'F'}, std::byte{'A'}, std::byte{'M'}, std::byte{'1'}};
    if (!std::equal(
            kDomain.begin(),
            kDomain.end(),
            profile.canonical_schema_key.begin())) {
        fail("A WadV1 family profile has an invalid schema-key domain");
    }
    Sha256 hash;
    hash.update(profile.canonical_schema_key);
    if (profile.shape_sha256 != hex_digest(hash.finish())) {
        fail("A WadV1 family profile has an invalid schema-key digest");
    }
}

} // namespace

WadPayloadFamilyBuilderV1::WadPayloadFamilyBuilderV1(
    const WadPayloadFamilyLimitsV1 limits)
    : limits_(limits) {
    validate_limits(limits_);
}

void WadPayloadFamilyBuilderV1::observe(
    const WadPayloadAddResultV1 added,
    const std::span<const std::byte> decoded_bytes) {
    if (finalized_) {
        fail("Cannot profile a WadV1 payload after family finalization");
    }
    if (added.observation_index != observed_observations_) {
        fail("A WadV1 family profile skipped or reordered an observation");
    }
    if (observed_observations_ >= limits_.max_observations) {
        fail("WadV1 family profiles exceed the caller's observation limit");
    }
    if (decoded_bytes.size() > limits_.max_input_bytes_per_payload) {
        fail("A decoded WadV1 payload exceeds the family profiler byte limit");
    }
    if (added.unique_payload_index > profiles_.size()) {
        fail("A WadV1 family profile skipped a sequential unique payload index");
    }
    if (added.unique_payload_index < profiles_.size()) {
        const auto& existing = profiles_[
            static_cast<std::size_t>(added.unique_payload_index)];
        if (existing.input_bytes != decoded_bytes.size()) {
            fail("A duplicate WadV1 family profile changed decoded size");
        }
        observed_observations_ = checked_add(
            observed_observations_, 1U, "profiled WadV1 observations");
        return;
    }
    if (profiles_.size() >= limits_.max_unique_payloads) {
        fail("WadV1 family profiles exceed the caller's unique-payload limit");
    }
    const auto next_total_input_bytes = checked_add(
        total_profiled_input_bytes_,
        decoded_bytes.size(),
        "profiled WadV1 candidate-family input bytes");
    if (next_total_input_bytes > limits_.max_total_input_bytes) {
        fail("WadV1 family profiles exceed the caller's aggregate input-byte limit");
    }
    auto profile = fingerprint_wad_payload_structure_v1(
        decoded_bytes,
        WadPayloadStructureLimitsV1{
            limits_.max_input_bytes_per_payload,
            limits_.max_sampled_bytes_per_payload});
    const auto next_total_sampled_bytes = checked_add(
        total_sampled_bytes_,
        profile.sampled_bytes,
        "sampled WadV1 candidate-family bytes");
    if (next_total_sampled_bytes > limits_.max_total_sampled_bytes) {
        fail("WadV1 family profiles exceed the caller's aggregate sampling limit");
    }
    profiles_.push_back(std::move(profile));
    total_profiled_input_bytes_ = next_total_input_bytes;
    total_sampled_bytes_ = next_total_sampled_bytes;
    observed_observations_ = checked_add(
        observed_observations_, 1U, "profiled WadV1 observations");
}

WadPayloadFamilyInventoryV1 WadPayloadFamilyBuilderV1::finalize(
    const WadPayloadInventoryV1& inventory) {
    if (finalized_) {
        fail("WadV1 candidate families were already finalized");
    }
    finalized_ = true;
    if (inventory.unique_payloads.size() != profiles_.size()) {
        fail("WadV1 family profiles do not cover every unique payload");
    }
    if (inventory.observations.size() != observed_observations_) {
        fail("WadV1 family profiles do not cover every observation");
    }
    if (inventory.unique_payloads.size() > limits_.max_unique_payloads ||
        inventory.observations.size() > limits_.max_observations) {
        fail("The WadV1 inventory exceeds the family builder's limits");
    }

    std::vector<std::uint64_t> actual_observation_counts(
        inventory.unique_payloads.size(), 0U);
    std::vector<std::uint64_t> actual_first_observations(
        inventory.unique_payloads.size(),
        std::numeric_limits<std::uint64_t>::max());
    std::uint64_t actual_total_observed_bytes = 0U;
    std::uint64_t actual_unknown_observations = 0U;
    for (std::size_t observation_index = 0U;
         observation_index < inventory.observations.size();
         ++observation_index) {
        const auto unique_index =
            inventory.observations[observation_index].unique_payload_index;
        if (unique_index >= inventory.unique_payloads.size()) {
            fail("A WadV1 observation has an invalid unique-payload index");
        }
        const auto index = static_cast<std::size_t>(unique_index);
        actual_observation_counts[index] = checked_add(
            actual_observation_counts[index],
            1U,
            "WadV1 unique-payload observations");
        actual_first_observations[index] = std::min(
            actual_first_observations[index],
            static_cast<std::uint64_t>(observation_index));
        actual_total_observed_bytes = checked_add(
            actual_total_observed_bytes,
            inventory.unique_payloads[index].decoded_bytes,
            "observed WadV1 decoded bytes");
        if (inventory.unique_payloads[index].classification ==
            WadPayloadClassificationV1::unknown) {
            actual_unknown_observations = checked_add(
                actual_unknown_observations,
                1U,
                "unknown WadV1 observations");
        }
    }

    std::array<std::uint64_t, kWadPayloadClassificationCountV1>
        actual_unique_classifications{};
    std::uint64_t actual_unique_decoded_bytes = 0U;
    for (std::size_t unique_index = 0U;
         unique_index < inventory.unique_payloads.size();
         ++unique_index) {
        const auto& unique = inventory.unique_payloads[unique_index];
        if (actual_observation_counts[unique_index] == 0U ||
            actual_observation_counts[unique_index] !=
                unique.observation_count ||
            actual_first_observations[unique_index] !=
                unique.first_observation_index) {
            fail("A WadV1 unique payload has inconsistent observation metadata");
        }
        const auto classified = classification_index(unique.classification);
        actual_unique_classifications[classified] = checked_add(
            actual_unique_classifications[classified],
            1U,
            "classified WadV1 unique payloads");
        actual_unique_decoded_bytes = checked_add(
            actual_unique_decoded_bytes,
            unique.decoded_bytes,
            "unique WadV1 decoded bytes");
    }
    if (actual_unique_classifications[
            kWadPayloadRecognizedClassificationIndexV1] !=
            inventory.recognized_unique_payloads ||
        actual_unique_classifications[
            kWadPayloadUnknownClassificationIndexV1] !=
            inventory.unknown_unique_payloads ||
        actual_unique_classifications[
            kWadPayloadAmbiguousClassificationIndexV1] !=
            inventory.ambiguous_unique_payloads ||
        actual_unique_decoded_bytes != inventory.unique_decoded_bytes ||
        actual_unique_decoded_bytes != total_profiled_input_bytes_ ||
        actual_total_observed_bytes != inventory.total_decoded_bytes) {
        fail("The WadV1 inventory aggregate metadata is inconsistent");
    }

    WadPayloadFamilyInventoryV1 result;
    result.profiled_unique_payloads = profiles_.size();
    std::map<std::vector<std::byte>, std::size_t> family_by_schema;
    std::map<std::string, std::vector<std::byte>> schema_by_family_id;

    for (std::size_t unique_index = 0U;
         unique_index < inventory.unique_payloads.size();
         ++unique_index) {
        const auto& unique = inventory.unique_payloads[unique_index];
        const auto& profile = profiles_[unique_index];
        validate_profile(profile, unique);
        const auto unique_classification_index =
            classification_index(unique.classification);
        if (unique.classification == WadPayloadClassificationV1::unknown) {
            result.unknown_unique_payloads = checked_add(
                result.unknown_unique_payloads,
                1U,
                "unknown WadV1 unique payloads");
        }

        const auto digest_entry = schema_by_family_id.find(profile.shape_sha256);
        if (digest_entry != schema_by_family_id.end() &&
            digest_entry->second != profile.canonical_schema_key) {
            fail("Two distinct WadV1 family schema keys share one SHA-256 identity");
        }
        if (digest_entry == schema_by_family_id.end()) {
            schema_by_family_id.emplace(
                profile.shape_sha256,
                profile.canonical_schema_key);
        }

        auto family_entry = family_by_schema.find(profile.canonical_schema_key);
        if (family_entry == family_by_schema.end()) {
            if (result.families.size() >= limits_.max_families) {
                fail("WadV1 candidate families exceed the caller's limit");
            }
            const auto family_index = result.families.size();
            result.families.push_back(WadPayloadCandidateFamilyV1{
                profile.shape_sha256,
                profile,
                unique_index,
                unique.decoded_sha256,
                unique.first_observation_index,
                {},
                0U,
                0U,
                {},
                {},
                unique.decoded_bytes,
                unique.decoded_bytes,
                0U,
                0U,
                {},
                {},
            });
            family_entry = family_by_schema.emplace(
                profile.canonical_schema_key,
                family_index).first;
        }

        auto& family = result.families[family_entry->second];
        if (family.member_unique_payload_indices.size() >=
            limits_.max_members_per_family) {
            fail("A WadV1 candidate family exceeds the caller's member limit");
        }
        family.member_unique_payload_indices.push_back(unique_index);
        family.unique_payloads = checked_add(
            family.unique_payloads,
            1U,
            "WadV1 candidate-family members");
        family.classification_unique_payloads[unique_classification_index] =
            checked_add(
                family.classification_unique_payloads[
                    unique_classification_index],
                1U,
                "classified WadV1 candidate-family members");
        family.minimum_decoded_bytes = std::min(
            family.minimum_decoded_bytes,
            unique.decoded_bytes);
        family.maximum_decoded_bytes = std::max(
            family.maximum_decoded_bytes,
            unique.decoded_bytes);
        family.total_unique_decoded_bytes = checked_add(
            family.total_unique_decoded_bytes,
            unique.decoded_bytes,
            "unique WadV1 candidate-family bytes");
        family.first_observation_index = std::min(
            family.first_observation_index,
            unique.first_observation_index);
        if (unique.decoded_sha256 < family.representative_decoded_sha256) {
            family.representative_unique_payload_index = unique_index;
            family.representative_decoded_sha256 = unique.decoded_sha256;
            family.representative_structure = profile;
        }
    }

    std::sort(
        result.families.begin(),
        result.families.end(),
        [](const auto& left, const auto& right) {
            return left.family_id < right.family_id;
        });
    std::vector<std::optional<std::size_t>> unique_to_family(
        inventory.unique_payloads.size());
    for (std::size_t family_index = 0U;
         family_index < result.families.size();
         ++family_index) {
        auto& family = result.families[family_index];
        std::sort(
            family.member_unique_payload_indices.begin(),
            family.member_unique_payload_indices.end(),
            [&inventory](const auto left, const auto right) {
                const auto& left_unique = inventory.unique_payloads[
                    static_cast<std::size_t>(left)];
                const auto& right_unique = inventory.unique_payloads[
                    static_cast<std::size_t>(right)];
                if (left_unique.decoded_sha256 != right_unique.decoded_sha256) {
                    return left_unique.decoded_sha256 < right_unique.decoded_sha256;
                }
                return left < right;
            });
        for (const auto unique_index : family.member_unique_payload_indices) {
            if (unique_index >= unique_to_family.size() ||
                unique_to_family[static_cast<std::size_t>(unique_index)]) {
                fail("A WadV1 unique payload belongs to multiple candidate families");
            }
            unique_to_family[static_cast<std::size_t>(unique_index)] =
                family_index;
        }
    }

    std::set<std::tuple<std::size_t, std::uint32_t, std::uint64_t>>
        unique_level_memberships;
    for (const auto& observation : inventory.observations) {
        if (observation.unique_payload_index >= unique_to_family.size()) {
            fail("A WadV1 observation has an invalid unique-payload index");
        }
        const auto family_index = unique_to_family[
            static_cast<std::size_t>(observation.unique_payload_index)];
        if (!family_index) {
            continue;
        }
        auto& family = result.families[*family_index];
        const auto& unique = inventory.unique_payloads[
            static_cast<std::size_t>(observation.unique_payload_index)];
        const auto observation_classification_index =
            classification_index(unique.classification);
        family.observations = checked_add(
            family.observations,
            1U,
            "WadV1 candidate-family observations");
        family.classification_observations[
            observation_classification_index] = checked_add(
                family.classification_observations[
                    observation_classification_index],
                1U,
                "classified WadV1 candidate-family observations");
        family.total_observed_decoded_bytes = checked_add(
            family.total_observed_decoded_bytes,
            unique.decoded_bytes,
            "observed WadV1 candidate-family bytes");
        const auto source_index = origin_index(observation.origin.kind);
        family.origin_observations[source_index] = checked_add(
            family.origin_observations[source_index],
            1U,
            "WadV1 candidate-family origin observations");
        if (unique.classification == WadPayloadClassificationV1::unknown) {
            result.unknown_observations = checked_add(
                result.unknown_observations,
                1U,
                "unknown WadV1 observations");
        }

        if (!observation.origin.level_id) {
            continue;
        }
        auto& level = find_or_add_level(
            family,
            *observation.origin.level_id,
            limits_.max_levels_per_family);
        level.observations = checked_add(
            level.observations,
            1U,
            "WadV1 candidate-family level observations");
        level.classification_observations[observation_classification_index] =
            checked_add(
                level.classification_observations[
                    observation_classification_index],
                1U,
                "classified WadV1 candidate-family level observations");
        const auto membership = std::make_tuple(
            *family_index,
            *observation.origin.level_id,
            observation.unique_payload_index);
        const auto [unused, inserted] =
            unique_level_memberships.insert(membership);
        (void)unused;
        if (inserted) {
            if (unique_level_memberships.size() >
                limits_.max_unique_level_memberships) {
                fail("WadV1 candidate-family unique/level memberships exceed the caller's limit");
            }
            level.unique_payloads = checked_add(
                level.unique_payloads,
                1U,
                "WadV1 candidate-family unique level members");
            level.classification_unique_payloads[
                observation_classification_index] = checked_add(
                    level.classification_unique_payloads[
                        observation_classification_index],
                    1U,
                    "classified WadV1 candidate-family unique level members");
        }
    }

    for (auto& family : result.families) {
        std::sort(
            family.levels.begin(),
            family.levels.end(),
            [](const auto& left, const auto& right) {
                return left.level_id < right.level_id;
            });
        std::uint64_t expected_observations = 0U;
        for (const auto unique_index : family.member_unique_payload_indices) {
            expected_observations = checked_add(
                expected_observations,
                inventory.unique_payloads[
                    static_cast<std::size_t>(unique_index)].observation_count,
                "WadV1 candidate-family expected observations");
        }
        if (expected_observations != family.observations) {
            fail("A WadV1 candidate-family observation count is inconsistent");
        }
        if (family.classification_unique_payloads[
                kWadPayloadUnknownClassificationIndexV1] != 0U) {
            result.families_with_unknown_payloads = checked_add(
                result.families_with_unknown_payloads,
                1U,
                "WadV1 candidate families containing unknown payloads");
        }
    }
    if (result.unknown_unique_payloads != inventory.unknown_unique_payloads) {
        fail("WadV1 candidate families do not cover every unknown unique payload");
    }
    if (result.unknown_observations != actual_unknown_observations) {
        fail("WadV1 candidate families do not cover every unknown observation");
    }
    return result;
}

} // namespace openrc
