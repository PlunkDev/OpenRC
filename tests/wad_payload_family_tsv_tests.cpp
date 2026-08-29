#include "openrc/wad_payload_family_tsv.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using ClassificationCounts =
    std::array<std::uint64_t, openrc::kWadPayloadClassificationCountV1>;

constexpr std::uint32_t kRankingFocusLevel = 17U;

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void expect_tsv_error(Function&& function, const std::string& message) {
    try {
        std::invoke(std::forward<Function>(function));
    } catch (const openrc::WadPayloadFamilyTsvError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] std::string digest(const char digit) {
    return std::string(64U, digit);
}

[[nodiscard]] openrc::WadPayloadCandidateFamilyV1 make_family_shell(
    const char id_digit,
    const std::uint64_t representative_index,
    const std::uint64_t representative_bytes,
    std::string prefix_hex) {
    openrc::WadPayloadCandidateFamilyV1 family;
    family.family_id = digest(id_digit);
    family.representative_structure.shape_sha256 = family.family_id;
    family.representative_structure.canonical_schema_key.assign(
        openrc::kWadPayloadStructureCanonicalKeyBytesV1,
        static_cast<std::byte>(static_cast<unsigned char>(id_digit)));
    family.representative_structure.input_bytes = representative_bytes;
    family.representative_structure.prefix_hex = std::move(prefix_hex);
    family.representative_unique_payload_index = representative_index;
    family.representative_decoded_sha256 = digest(id_digit);
    return family;
}

[[nodiscard]] openrc::WadPayloadFamilyLevelCountV1 make_level(
    const std::uint32_t level_id,
    const ClassificationCounts unique,
    const ClassificationCounts observations) {
    openrc::WadPayloadFamilyLevelCountV1 level;
    level.level_id = level_id;
    level.classification_unique_payloads = unique;
    level.classification_observations = observations;
    for (const auto count : unique) {
        level.unique_payloads += count;
    }
    for (const auto count : observations) {
        level.observations += count;
    }
    return level;
}

void summarize(openrc::WadPayloadFamilyInventoryV1& inventory) {
    inventory.profiled_unique_payloads = 0U;
    inventory.unknown_unique_payloads = 0U;
    inventory.unknown_observations = 0U;
    inventory.families_with_unknown_payloads = 0U;
    for (const auto& family : inventory.families) {
        inventory.profiled_unique_payloads += family.unique_payloads;
        inventory.unknown_unique_payloads +=
            family.classification_unique_payloads[
                openrc::kWadPayloadUnknownClassificationIndexV1];
        inventory.unknown_observations +=
            family.classification_observations[
                openrc::kWadPayloadUnknownClassificationIndexV1];
        if (family.classification_unique_payloads[
                openrc::kWadPayloadUnknownClassificationIndexV1] != 0U) {
            ++inventory.families_with_unknown_payloads;
        }
    }
}

[[nodiscard]] openrc::WadPayloadFamilyInventoryV1 make_tsv_inventory() {
    auto first = make_family_shell(
        'a',
        0U,
        16U,
        "00112233445566778899aabbccddeeff");
    first.representative_structure.size_log2_bucket = 4U;
    first.representative_structure.size_alignment_log2 = 4U;
    first.representative_structure.available_header_words = 4U;
    first.representative_structure.sampled_bytes = 16U;
    first.representative_structure.sampled_zero_permille = 62U;
    first.representative_structure.sampled_ff_permille = 62U;
    first.representative_structure.sampled_printable_permille = 375U;
    first.representative_structure.sampled_high_bit_permille = 500U;
    first.first_observation_index = 0U;
    first.member_unique_payload_indices = {0U};
    first.unique_payloads = 1U;
    first.observations = 1U;
    first.classification_unique_payloads = {0U, 1U, 0U};
    first.classification_observations = {0U, 1U, 0U};
    first.minimum_decoded_bytes = 16U;
    first.maximum_decoded_bytes = 16U;
    first.total_unique_decoded_bytes = 16U;
    first.total_observed_decoded_bytes = 16U;
    first.origin_observations[
        openrc::kWadPayloadGlobalTocOriginIndexV1] =
        1U;
    first.levels.push_back(make_level(
        7U,
        ClassificationCounts{0U, 1U, 0U},
        ClassificationCounts{0U, 1U, 0U}));

    auto second = make_family_shell(
        'b',
        1U,
        8U,
        "aabbccdd00112233");
    second.representative_structure.size_log2_bucket = 3U;
    second.representative_structure.size_alignment_log2 = 3U;
    second.representative_structure.available_header_words = 2U;
    second.representative_structure.sampled_bytes = 8U;
    second.representative_structure.sampled_zero_permille = 125U;
    second.representative_structure.sampled_ff_permille = 0U;
    second.representative_structure.sampled_printable_permille = 250U;
    second.representative_structure.sampled_high_bit_permille = 500U;
    second.first_observation_index = 1U;
    second.member_unique_payload_indices = {1U, 2U};
    second.unique_payloads = 2U;
    second.observations = 3U;
    second.classification_unique_payloads = {1U, 1U, 0U};
    second.classification_observations = {1U, 2U, 0U};
    second.minimum_decoded_bytes = 8U;
    second.maximum_decoded_bytes = 24U;
    second.total_unique_decoded_bytes = 32U;
    second.total_observed_decoded_bytes = 56U;
    second.origin_observations[
        openrc::kWadPayloadGlobalTocTailOriginIndexV1] = 1U;
    second.origin_observations[
        openrc::kWadPayloadLocalWadRunOriginIndexV1] = 1U;
    second.origin_observations[
        openrc::kWadPayloadWadBundleRecordOriginIndexV1] = 1U;
    second.levels.push_back(make_level(
        3U,
        ClassificationCounts{1U, 0U, 0U},
        ClassificationCounts{1U, 0U, 0U}));
    second.levels.push_back(make_level(
        9U,
        ClassificationCounts{0U, 1U, 0U},
        ClassificationCounts{0U, 2U, 0U}));

    openrc::WadPayloadFamilyInventoryV1 inventory;
    inventory.families.push_back(std::move(first));
    inventory.families.push_back(std::move(second));
    summarize(inventory);
    return inventory;
}

void test_deterministic_complete_schema_and_missing_focus_level() {
    const auto inventory = make_tsv_inventory();
    const auto first =
        openrc::encode_wad_payload_families_tsv_v1(inventory, 99U);
    const auto second =
        openrc::encode_wad_payload_families_tsv_v1(inventory, 99U);
    expect(first == second, "WadV1 candidate-family TSV is not deterministic");

    const auto expected =
        std::string(
            "rank\tfamily_id\trepresentative_unique\trepresentative_sha256\t"
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
            "member_unique_indices\n") +
        "1\t" + digest('b') + "\t1\t" + digest('b') +
        "\t3\t3\taabbccdd00112233\t8\t125\t0\t250\t500\t2\t3\t1\t1\t"
        "1\t2\t0\t0\t8\t24\t32\t56\t0\t1\t1\t0\t0\t1\t0\t0\t99\t0\t0\t"
        "0\t0\t2\t3:1:1:0:0,9:1:2:1:2\t1,2\n" +
        "2\t" + digest('a') + "\t0\t" + digest('a') +
        "\t4\t4\t00112233445566778899aabbccddeeff\t16\t62\t62\t375\t500\t"
        "1\t1\t0\t0\t1\t1\t0\t0\t16\t16\t16\t16\t1\t0\t0\t0\t0\t0\t0\t"
        "0\t99\t0\t0\t0\t0\t1\t7:1:1:1:1\t0\n";
    expect(
        first == expected,
        "WadV1 candidate-family TSV schema, rows, or missing-focus fields changed");
}

[[nodiscard]] openrc::WadPayloadCandidateFamilyV1 make_unknown_family(
    const char id_digit,
    const std::uint64_t first_member,
    const std::uint64_t unknown_unique,
    const std::uint64_t unknown_observations,
    const std::uint64_t decoded_bytes,
    const std::uint64_t focus_unknown_unique,
    const std::uint64_t focus_unknown_observations) {
    auto family = make_family_shell(
        id_digit,
        first_member,
        decoded_bytes,
        "00000000");
    family.first_observation_index = first_member;
    family.member_unique_payload_indices.reserve(
        static_cast<std::size_t>(unknown_unique));
    for (std::uint64_t index = 0U; index < unknown_unique; ++index) {
        family.member_unique_payload_indices.push_back(first_member + index);
    }
    family.unique_payloads = unknown_unique;
    family.observations = unknown_observations;
    family.classification_unique_payloads = {0U, unknown_unique, 0U};
    family.classification_observations = {0U, unknown_observations, 0U};
    family.minimum_decoded_bytes = decoded_bytes;
    family.maximum_decoded_bytes = decoded_bytes;
    family.total_unique_decoded_bytes = decoded_bytes * unknown_unique;
    family.total_observed_decoded_bytes = decoded_bytes * unknown_observations;
    family.origin_observations[
        openrc::kWadPayloadExplicitInputOriginIndexV1] =
        unknown_observations;
    family.levels.push_back(make_level(
        kRankingFocusLevel,
        ClassificationCounts{0U, focus_unknown_unique, 0U},
        ClassificationCounts{0U, focus_unknown_observations, 0U}));
    return family;
}

void test_ranking_uses_focus_then_corpus_then_stable_id() {
    openrc::WadPayloadFamilyInventoryV1 inventory;
    inventory.families.push_back(
        make_unknown_family('1', 0U, 2U, 2U, 4U, 1U, 1U));
    inventory.families.push_back(
        make_unknown_family('2', 2U, 2U, 2U, 64U, 1U, 1U));
    inventory.families.push_back(
        make_unknown_family('3', 4U, 3U, 3U, 8U, 1U, 1U));
    inventory.families.push_back(
        make_unknown_family('4', 7U, 2U, 2U, 8U, 1U, 2U));
    inventory.families.push_back(
        make_unknown_family('5', 9U, 2U, 2U, 8U, 2U, 2U));
    inventory.families.push_back(
        make_unknown_family('6', 11U, 3U, 4U, 8U, 1U, 1U));
    inventory.families.push_back(
        make_unknown_family('7', 14U, 2U, 2U, 64U, 1U, 1U));
    inventory.families.push_back(
        make_unknown_family('8', 16U, 2U, 2U, 4U, 1U, 1U));
    summarize(inventory);

    const auto ranked = openrc::rank_unknown_wad_payload_families_v1(
        inventory,
        kRankingFocusLevel);
    expect(
        ranked == std::vector<std::size_t>{4U, 3U, 5U, 2U, 0U, 1U, 6U, 7U},
        "WadV1 candidate-family ranking did not use focus unknown coverage, "
        "corpus unknown coverage, and stable ID in that order");
}

void test_inconsistent_counts_and_id_order_are_rejected() {
    {
        auto inventory = make_tsv_inventory();
        ++inventory.families.front()
              .classification_unique_payloads[
                  openrc::kWadPayloadUnknownClassificationIndexV1];
        expect_tsv_error(
            [&] {
                (void)openrc::rank_unknown_wad_payload_families_v1(
                    inventory,
                    7U);
            },
            "WadV1 family TSV accepted inconsistent family counts");
    }
    {
        auto inventory = make_tsv_inventory();
        ++inventory.families.front()
              .levels.front()
              .classification_observations[
                  openrc::kWadPayloadUnknownClassificationIndexV1];
        expect_tsv_error(
            [&] {
                (void)openrc::rank_unknown_wad_payload_families_v1(
                    inventory,
                    7U);
            },
            "WadV1 family TSV accepted inconsistent level counts");
    }

    const std::array<
        std::function<void(openrc::WadPayloadFamilyInventoryV1&)>,
        4U> top_level_count_mutations{
        [](openrc::WadPayloadFamilyInventoryV1& inventory) {
            ++inventory.profiled_unique_payloads;
        },
        [](openrc::WadPayloadFamilyInventoryV1& inventory) {
            ++inventory.unknown_unique_payloads;
        },
        [](openrc::WadPayloadFamilyInventoryV1& inventory) {
            ++inventory.unknown_observations;
        },
        [](openrc::WadPayloadFamilyInventoryV1& inventory) {
            ++inventory.families_with_unknown_payloads;
        },
    };
    for (const auto& mutate : top_level_count_mutations) {
        auto inventory = make_tsv_inventory();
        mutate(inventory);
        expect_tsv_error(
            [&] {
                (void)openrc::encode_wad_payload_families_tsv_v1(
                    inventory,
                    7U);
            },
            "WadV1 family TSV accepted inconsistent inventory counts");
    }

    {
        auto inventory = make_tsv_inventory();
        std::swap(inventory.families[0U], inventory.families[1U]);
        expect_tsv_error(
            [&] {
                (void)openrc::rank_unknown_wad_payload_families_v1(
                    inventory,
                    7U);
            },
            "WadV1 family TSV accepted families that were not ID-sorted");
    }
}

} // namespace

int main() {
    try {
        test_deterministic_complete_schema_and_missing_focus_level();
        test_ranking_uses_focus_then_corpus_then_stable_id();
        test_inconsistent_counts_and_id_order_are_rejected();
        std::cout << "OpenRC WadPayloadFamilyInventoryV1 TSV tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC WadPayloadFamilyInventoryV1 TSV tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
