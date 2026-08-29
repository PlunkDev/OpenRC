#include "openrc/wad_payload_family_tsv.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace openrc {
namespace {

using CountArray =
    std::array<std::uint64_t, kWadPayloadClassificationCountV1>;

[[nodiscard]] std::uint64_t checked_sum(
    const CountArray& values,
    const char* const description) {
    std::uint64_t result = 0U;
    for (const auto value : values) {
        if (value > std::numeric_limits<std::uint64_t>::max() - result) {
            throw WadPayloadFamilyTsvError(
                std::string("Integer overflow while validating ") +
                description);
        }
        result += value;
    }
    return result;
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw WadPayloadFamilyTsvError(
            std::string("Integer overflow while validating ") + description);
    }
    return left + right;
}

[[nodiscard]] const WadPayloadFamilyLevelCountV1* find_level(
    const WadPayloadCandidateFamilyV1& family,
    const std::uint32_t level_id) {
    const auto found = std::lower_bound(
        family.levels.begin(),
        family.levels.end(),
        level_id,
        [](const WadPayloadFamilyLevelCountV1& candidate,
           const std::uint32_t requested) {
            return candidate.level_id < requested;
        });
    return found != family.levels.end() && found->level_id == level_id
        ? &*found
        : nullptr;
}

[[nodiscard]] std::uint64_t level_unknown_unique(
    const WadPayloadCandidateFamilyV1& family,
    const std::uint32_t level_id) {
    const auto* const level = find_level(family, level_id);
    return level == nullptr
        ? 0U
        : level->classification_unique_payloads[
              kWadPayloadUnknownClassificationIndexV1];
}

[[nodiscard]] std::uint64_t level_unknown_observations(
    const WadPayloadCandidateFamilyV1& family,
    const std::uint32_t level_id) {
    const auto* const level = find_level(family, level_id);
    return level == nullptr
        ? 0U
        : level->classification_observations[
              kWadPayloadUnknownClassificationIndexV1];
}

void validate_family(const WadPayloadCandidateFamilyV1& family) {
    if (family.family_id.size() != 64U ||
        family.family_id != family.representative_structure.shape_sha256 ||
        family.representative_structure.version !=
            kWadPayloadStructureFingerprintVersionV1 ||
        family.representative_structure.canonical_schema_key.size() !=
            kWadPayloadStructureCanonicalKeyBytesV1 ||
        family.member_unique_payload_indices.size() != family.unique_payloads ||
        checked_sum(
            family.classification_unique_payloads,
            "candidate-family unique classifications") !=
            family.unique_payloads ||
        checked_sum(
            family.classification_observations,
            "candidate-family observation classifications") !=
            family.observations) {
        throw WadPayloadFamilyTsvError(
            "The WadV1 candidate-family summary is inconsistent");
    }
    std::uint32_t previous_level = 0U;
    bool has_previous_level = false;
    for (const auto& level : family.levels) {
        if ((has_previous_level && level.level_id <= previous_level) ||
            checked_sum(
                level.classification_unique_payloads,
                "candidate-family level unique classifications") !=
                level.unique_payloads ||
            checked_sum(
                level.classification_observations,
                "candidate-family level observation classifications") !=
                level.observations) {
            throw WadPayloadFamilyTsvError(
                "The WadV1 candidate-family level summary is inconsistent");
        }
        previous_level = level.level_id;
        has_previous_level = true;
    }
}

void append_levels(
    std::ostringstream& output,
    const WadPayloadCandidateFamilyV1& family) {
    for (std::size_t index = 0U; index < family.levels.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        const auto& level = family.levels[index];
        output
            << level.level_id << ':'
            << level.unique_payloads << ':'
            << level.observations << ':'
            << level.classification_unique_payloads[
                   kWadPayloadUnknownClassificationIndexV1] << ':'
            << level.classification_observations[
                   kWadPayloadUnknownClassificationIndexV1];
    }
}

void append_members(
    std::ostringstream& output,
    const WadPayloadCandidateFamilyV1& family) {
    for (std::size_t index = 0U;
         index < family.member_unique_payload_indices.size();
         ++index) {
        if (index != 0U) {
            output << ',';
        }
        output << family.member_unique_payload_indices[index];
    }
}

} // namespace

std::vector<std::size_t> rank_unknown_wad_payload_families_v1(
    const WadPayloadFamilyInventoryV1& families,
    const std::uint32_t focus_level_id) {
    std::vector<std::size_t> ranked;
    ranked.reserve(families.families_with_unknown_payloads);
    std::string previous_family_id;
    std::uint64_t profiled_unique_payloads = 0U;
    std::uint64_t unknown_unique_payloads = 0U;
    std::uint64_t unknown_observations = 0U;
    std::uint64_t families_with_unknown_payloads = 0U;
    for (std::size_t family_index = 0U;
         family_index < families.families.size();
         ++family_index) {
        const auto& family = families.families[family_index];
        validate_family(family);
        if (!previous_family_id.empty() &&
            family.family_id <= previous_family_id) {
            throw WadPayloadFamilyTsvError(
                "WadV1 candidate families are not uniquely ID-sorted");
        }
        previous_family_id = family.family_id;
        profiled_unique_payloads = checked_add(
            profiled_unique_payloads,
            family.unique_payloads,
            "profiled unique payloads");
        const auto family_unknown_unique =
            family.classification_unique_payloads[
                kWadPayloadUnknownClassificationIndexV1];
        unknown_unique_payloads = checked_add(
            unknown_unique_payloads,
            family_unknown_unique,
            "unknown unique payloads");
        unknown_observations = checked_add(
            unknown_observations,
            family.classification_observations[
                kWadPayloadUnknownClassificationIndexV1],
            "unknown observations");
        if (family_unknown_unique != 0U) {
            families_with_unknown_payloads = checked_add(
                families_with_unknown_payloads,
                1U,
                "candidate families containing unknown payloads");
            ranked.push_back(family_index);
        }
    }
    if (profiled_unique_payloads != families.profiled_unique_payloads ||
        unknown_unique_payloads != families.unknown_unique_payloads ||
        unknown_observations != families.unknown_observations ||
        families_with_unknown_payloads !=
            families.families_with_unknown_payloads ||
        ranked.size() != families.families_with_unknown_payloads) {
        throw WadPayloadFamilyTsvError(
            "The WadV1 candidate-family inventory totals are inconsistent");
    }

    std::sort(
        ranked.begin(),
        ranked.end(),
        [&families, focus_level_id](
            const std::size_t left_index,
            const std::size_t right_index) {
            const auto& left = families.families[left_index];
            const auto& right = families.families[right_index];
            const std::array left_rank{
                level_unknown_unique(left, focus_level_id),
                level_unknown_observations(left, focus_level_id),
                left.classification_unique_payloads[
                    kWadPayloadUnknownClassificationIndexV1],
                left.classification_observations[
                    kWadPayloadUnknownClassificationIndexV1]};
            const std::array right_rank{
                level_unknown_unique(right, focus_level_id),
                level_unknown_observations(right, focus_level_id),
                right.classification_unique_payloads[
                    kWadPayloadUnknownClassificationIndexV1],
                right.classification_observations[
                    kWadPayloadUnknownClassificationIndexV1]};
            if (left_rank != right_rank) {
                return left_rank > right_rank;
            }
            return left.family_id < right.family_id;
        });
    return ranked;
}

std::string encode_wad_payload_families_tsv_v1(
    const WadPayloadFamilyInventoryV1& families,
    const std::uint32_t focus_level_id) {
    const auto ranked = rank_unknown_wad_payload_families_v1(
        families, focus_level_id);

    std::ostringstream output;
    output
        << "rank\tfamily_id\trepresentative_unique\trepresentative_sha256\t"
           "size_log2\tsize_alignment_log2\tprefix_hex\tsampled_bytes\t"
           "zero_permille\tff_permille\tprintable_permille\thigh_bit_permille\t"
           "unique\tobservations\trecognized_unique\trecognized_observations\t"
           "unknown_unique\tunknown_observations\tambiguous_unique\t"
           "ambiguous_observations\tminimum_bytes\tmaximum_bytes\t"
           "total_unique_bytes\ttotal_observed_bytes\tglobal_toc\t"
           "global_toc_tail\tlocal_wad_run\tprimary_extent0_subrange\t"
           "primary_extent\twad_bundle_record\tcompanion_terminal_record\t"
           "explicit_input\tfocus_level\tfocus_unique\tfocus_observations\t"
           "focus_unknown_unique\tfocus_unknown_observations\tlevel_count\t"
           "levels_id_unique_observations_unknown_unique_unknown_observations\t"
           "member_unique_indices\n";
    for (std::size_t rank = 0U; rank < ranked.size(); ++rank) {
        const auto& family = families.families[ranked[rank]];
        const auto* const focus = find_level(family, focus_level_id);
        const auto& structure = family.representative_structure;
        output
            << (rank + 1U) << '\t'
            << family.family_id << '\t'
            << family.representative_unique_payload_index << '\t'
            << family.representative_decoded_sha256 << '\t'
            << static_cast<unsigned int>(structure.size_log2_bucket) << '\t'
            << static_cast<unsigned int>(structure.size_alignment_log2) << '\t'
            << structure.prefix_hex << '\t'
            << structure.sampled_bytes << '\t'
            << structure.sampled_zero_permille << '\t'
            << structure.sampled_ff_permille << '\t'
            << structure.sampled_printable_permille << '\t'
            << structure.sampled_high_bit_permille << '\t'
            << family.unique_payloads << '\t'
            << family.observations << '\t'
            << family.classification_unique_payloads[
                   kWadPayloadRecognizedClassificationIndexV1] << '\t'
            << family.classification_observations[
                   kWadPayloadRecognizedClassificationIndexV1] << '\t'
            << family.classification_unique_payloads[
                   kWadPayloadUnknownClassificationIndexV1] << '\t'
            << family.classification_observations[
                   kWadPayloadUnknownClassificationIndexV1] << '\t'
            << family.classification_unique_payloads[
                   kWadPayloadAmbiguousClassificationIndexV1] << '\t'
            << family.classification_observations[
                   kWadPayloadAmbiguousClassificationIndexV1] << '\t'
            << family.minimum_decoded_bytes << '\t'
            << family.maximum_decoded_bytes << '\t'
            << family.total_unique_decoded_bytes << '\t'
            << family.total_observed_decoded_bytes;
        for (const auto origin_count : family.origin_observations) {
            output << '\t' << origin_count;
        }
        output
            << '\t' << focus_level_id
            << '\t' << (focus == nullptr ? 0U : focus->unique_payloads)
            << '\t' << (focus == nullptr ? 0U : focus->observations)
            << '\t' << level_unknown_unique(family, focus_level_id)
            << '\t' << level_unknown_observations(family, focus_level_id)
            << '\t' << family.levels.size() << '\t';
        append_levels(output, family);
        output << '\t';
        append_members(output, family);
        output << '\n';
    }
    return output.str();
}

} // namespace openrc
