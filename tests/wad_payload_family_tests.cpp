#include "openrc/wad_payload_family.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr openrc::WadPayloadInventoryLimitsV1 kInventoryLimits{
    128U,
    64U,
    4096U,
    65536U,
    16U,
    1024U,
};

constexpr openrc::WadPayloadFamilyLimitsV1 kFamilyLimits{
    64U,
    128U,
    4096U,
    65536U,
    256U,
    16384U,
    64U,
    64U,
    64U,
    256U,
};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void expect_family_error(Function&& function, const std::string& message) {
    try {
        std::invoke(std::forward<Function>(function));
    } catch (const openrc::WadPayloadFamilyError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] std::vector<std::byte> two_small_words(
    const std::uint8_t first,
    const std::uint8_t second) {
    std::vector<std::byte> result(8U, std::byte{0});
    result[0U] = static_cast<std::byte>(first);
    result[4U] = static_cast<std::byte>(second);
    return result;
}

[[nodiscard]] std::vector<std::byte> different_shape_payload() {
    std::vector<std::byte> result(8U, std::byte{0});
    std::fill(result.begin(), result.begin() + 4, std::byte{0xffU});
    return result;
}

[[nodiscard]] bool first_byte_is(
    const std::span<const std::byte> bytes,
    const std::uint8_t expected) {
    return !bytes.empty() &&
        std::to_integer<std::uint8_t>(bytes.front()) == expected;
}

[[nodiscard]] std::vector<openrc::WadPayloadProbeV1>
classification_probes() {
    return {
        openrc::WadPayloadProbeV1{
            "format-primary",
            [](const std::span<const std::byte> bytes) {
                return first_byte_is(bytes, 1U) || first_byte_is(bytes, 5U)
                    ? openrc::WadPayloadProbeDecisionV1::match
                    : openrc::WadPayloadProbeDecisionV1::no_match;
            }},
        openrc::WadPayloadProbeV1{
            "format-secondary",
            [](const std::span<const std::byte> bytes) {
                return first_byte_is(bytes, 5U)
                    ? openrc::WadPayloadProbeDecisionV1::match
                    : openrc::WadPayloadProbeDecisionV1::no_match;
            }},
    };
}

[[nodiscard]] openrc::WadPayloadOriginV1 make_origin(
    const openrc::WadPayloadOriginKindV1 kind,
    const std::optional<std::uint32_t> level_id = std::nullopt) {
    openrc::WadPayloadOriginV1 result;
    result.kind = kind;
    result.level_id = level_id;
    return result;
}

void add_and_observe(
    openrc::WadPayloadInventoryBuilderV1& inventory_builder,
    openrc::WadPayloadFamilyBuilderV1& family_builder,
    openrc::WadPayloadOriginV1 origin,
    const std::span<const std::byte> payload) {
    const auto added = inventory_builder.add_decoded_payload(
        std::move(origin),
        payload);
    family_builder.observe(added, payload);
}

[[nodiscard]] const openrc::WadPayloadCandidateFamilyV1& family_with_members(
    const openrc::WadPayloadFamilyInventoryV1& result,
    const std::uint64_t members) {
    const auto found = std::find_if(
        result.families.begin(),
        result.families.end(),
        [members](const openrc::WadPayloadCandidateFamilyV1& family) {
            return family.unique_payloads == members;
        });
    if (found == result.families.end()) {
        throw std::runtime_error("the expected WadV1 candidate family is missing");
    }
    return *found;
}

void test_grouping_classification_representative_and_counts() {
    const auto recognized = two_small_words(1U, 2U);
    const auto unknown = two_small_words(3U, 4U);
    const auto ambiguous = two_small_words(5U, 6U);
    const auto other_unknown = different_shape_payload();

    openrc::WadPayloadInventoryBuilderV1 inventory_builder(
        classification_probes(),
        kInventoryLimits);
    openrc::WadPayloadFamilyBuilderV1 family_builder(kFamilyLimits);

    add_and_observe(
        inventory_builder,
        family_builder,
        make_origin(openrc::WadPayloadOriginKindV1::global_toc, 7U),
        recognized);
    add_and_observe(
        inventory_builder,
        family_builder,
        make_origin(openrc::WadPayloadOriginKindV1::global_toc_tail, 9U),
        unknown);
    add_and_observe(
        inventory_builder,
        family_builder,
        make_origin(openrc::WadPayloadOriginKindV1::local_wad_run, 7U),
        recognized);
    add_and_observe(
        inventory_builder,
        family_builder,
        make_origin(
            openrc::WadPayloadOriginKindV1::primary_extent0_subrange,
            7U),
        ambiguous);
    add_and_observe(
        inventory_builder,
        family_builder,
        make_origin(openrc::WadPayloadOriginKindV1::primary_extent, 7U),
        unknown);
    add_and_observe(
        inventory_builder,
        family_builder,
        make_origin(openrc::WadPayloadOriginKindV1::wad_bundle_record),
        ambiguous);
    add_and_observe(
        inventory_builder,
        family_builder,
        make_origin(
            openrc::WadPayloadOriginKindV1::companion_terminal_record,
            9U),
        recognized);
    add_and_observe(
        inventory_builder,
        family_builder,
        make_origin(openrc::WadPayloadOriginKindV1::explicit_input, 7U),
        ambiguous);
    add_and_observe(
        inventory_builder,
        family_builder,
        make_origin(openrc::WadPayloadOriginKindV1::global_toc),
        other_unknown);

    const auto inventory = inventory_builder.finalize();
    const auto result = family_builder.finalize(inventory);

    expect(
        result.families.size() == 2U &&
            result.profiled_unique_payloads == 4U &&
            result.unknown_unique_payloads == 2U &&
            result.unknown_observations == 3U &&
            result.families_with_unknown_payloads == 2U,
        "the top-level WadV1 candidate-family counts are wrong");
    expect(
        result.families[0U].family_id < result.families[1U].family_id,
        "WadV1 candidate families are not sorted by stable identity");

    const auto& grouped = family_with_members(result, 3U);
    expect(
        grouped.unique_payloads == 3U && grouped.observations == 8U &&
            grouped.classification_unique_payloads ==
                std::array<std::uint64_t, 3U>{1U, 1U, 1U} &&
            grouped.classification_observations ==
                std::array<std::uint64_t, 3U>{3U, 2U, 3U},
        "WadV1 family classification counts are wrong");
    expect(
        grouped.minimum_decoded_bytes == 8U &&
            grouped.maximum_decoded_bytes == 8U &&
            grouped.total_unique_decoded_bytes == 24U &&
            grouped.total_observed_decoded_bytes == 64U &&
            grouped.first_observation_index == 0U,
        "WadV1 family byte or first-observation counts are wrong");
    expect(
        grouped.origin_observations ==
            std::array<std::uint64_t, 8U>{1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U},
        "WadV1 family origin counts are wrong");
    expect(
        grouped.levels ==
            std::vector<openrc::WadPayloadFamilyLevelCountV1>{
                {
                    7U,
                    3U,
                    5U,
                    {1U, 1U, 1U},
                    {2U, 1U, 2U},
                },
                {
                    9U,
                    2U,
                    2U,
                    {1U, 1U, 0U},
                    {1U, 1U, 0U},
                }},
        "WadV1 family level counts, deduplication, or ordering are wrong");

    std::vector<std::uint64_t> expected_members{0U, 1U, 2U};
    std::sort(
        expected_members.begin(),
        expected_members.end(),
        [&inventory](const auto left, const auto right) {
            return inventory.unique_payloads[static_cast<std::size_t>(left)]
                       .decoded_sha256 <
                inventory.unique_payloads[static_cast<std::size_t>(right)]
                    .decoded_sha256;
        });
    expect(
        grouped.member_unique_payload_indices == expected_members,
        "WadV1 family members are not sorted by decoded SHA-256");
    const auto representative_index = expected_members.front();
    expect(
        grouped.representative_unique_payload_index == representative_index &&
            grouped.representative_decoded_sha256 ==
                inventory.unique_payloads[
                    static_cast<std::size_t>(representative_index)]
                    .decoded_sha256,
        "the stable WadV1 family representative is wrong");

    const std::array<std::span<const std::byte>, 3U> grouped_payloads{
        recognized,
        unknown,
        ambiguous,
    };
    const auto expected_structure =
        openrc::fingerprint_wad_payload_structure_v1(
            grouped_payloads[static_cast<std::size_t>(representative_index)],
            openrc::WadPayloadStructureLimitsV1{
                kFamilyLimits.max_input_bytes_per_payload,
                kFamilyLimits.max_sampled_bytes_per_payload});
    expect(
        grouped.family_id == expected_structure.shape_sha256 &&
            grouped.representative_structure == expected_structure,
        "the representative WadV1 structure does not define the family identity");
}

struct StableGroupingResult {
    openrc::WadPayloadInventoryV1 inventory;
    openrc::WadPayloadFamilyInventoryV1 families;
};

[[nodiscard]] StableGroupingResult build_stable_grouping(
    const std::array<std::vector<std::byte>, 4U>& payloads,
    std::vector<openrc::WadPayloadProbeV1> probes) {
    openrc::WadPayloadInventoryBuilderV1 inventory_builder(
        std::move(probes),
        kInventoryLimits);
    openrc::WadPayloadFamilyBuilderV1 family_builder(kFamilyLimits);
    for (const auto& payload : payloads) {
        add_and_observe(
            inventory_builder,
            family_builder,
            make_origin(openrc::WadPayloadOriginKindV1::explicit_input),
            payload);
    }
    auto inventory = inventory_builder.finalize();
    auto families = family_builder.finalize(inventory);
    return {std::move(inventory), std::move(families)};
}

[[nodiscard]] std::vector<std::string> member_digests(
    const StableGroupingResult& result,
    const openrc::WadPayloadCandidateFamilyV1& family) {
    std::vector<std::string> digests;
    for (const auto unique_index : family.member_unique_payload_indices) {
        digests.push_back(
            result.inventory.unique_payloads[
                static_cast<std::size_t>(unique_index)]
                .decoded_sha256);
    }
    return digests;
}

void test_grouping_is_stable_across_order_and_classification() {
    const auto first = two_small_words(1U, 2U);
    const auto second = two_small_words(3U, 4U);
    const auto third = two_small_words(5U, 6U);
    const auto other = different_shape_payload();
    const auto classified = build_stable_grouping(
        {first, second, third, other},
        classification_probes());
    const auto unclassified = build_stable_grouping(
        {other, third, first, second},
        {});

    expect(
        classified.families.families.size() == 2U &&
            unclassified.families.families.size() == 2U,
        "stable WadV1 grouping produced the wrong family count");
    for (std::size_t family_index = 0U; family_index < 2U; ++family_index) {
        const auto& left = classified.families.families[family_index];
        const auto& right = unclassified.families.families[family_index];
        expect(
            left.family_id == right.family_id &&
                left.representative_structure.canonical_schema_key ==
                    right.representative_structure.canonical_schema_key,
            "WadV1 grouping changed with discovery order or classification");
        expect(
            member_digests(classified, left) ==
                member_digests(unclassified, right),
            "stable WadV1 grouping changed its member set");
        expect(
            left.representative_decoded_sha256 ==
                right.representative_decoded_sha256,
            "the WadV1 representative changed with discovery order");
    }
}

void test_observation_order_and_duplicate_validation() {
    const auto payload = two_small_words(1U, 2U);
    const auto shorter = std::vector<std::byte>(4U, std::byte{0});

    expect_family_error(
        [&] {
            openrc::WadPayloadFamilyBuilderV1 builder(kFamilyLimits);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 1U}, payload);
        },
        "the first WadV1 family observation skipped index zero");
    expect_family_error(
        [&] {
            openrc::WadPayloadFamilyBuilderV1 builder(kFamilyLimits);
            builder.observe(openrc::WadPayloadAddResultV1{1U, 0U}, payload);
        },
        "a WadV1 family observation skipped a unique-payload index");
    expect_family_error(
        [&] {
            openrc::WadPayloadFamilyBuilderV1 builder(kFamilyLimits);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 0U}, payload);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 0U}, payload);
        },
        "a WadV1 family observation index was repeated");
    expect_family_error(
        [&] {
            openrc::WadPayloadFamilyBuilderV1 builder(kFamilyLimits);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 0U}, payload);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 2U}, payload);
        },
        "a WadV1 family observation index was skipped");
    expect_family_error(
        [&] {
            openrc::WadPayloadFamilyBuilderV1 builder(kFamilyLimits);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 0U}, payload);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 1U}, shorter);
        },
        "a duplicate WadV1 family payload changed decoded size");

    auto limits = kFamilyLimits;
    limits.max_total_input_bytes = payload.size();
    limits.max_total_sampled_bytes = payload.size();
    openrc::WadPayloadInventoryBuilderV1 inventory_builder(
        {},
        kInventoryLimits);
    openrc::WadPayloadFamilyBuilderV1 family_builder(limits);
    add_and_observe(
        inventory_builder,
        family_builder,
        make_origin(openrc::WadPayloadOriginKindV1::explicit_input),
        payload);
    add_and_observe(
        inventory_builder,
        family_builder,
        make_origin(openrc::WadPayloadOriginKindV1::explicit_input),
        payload);
    const auto inventory = inventory_builder.finalize();
    const auto result = family_builder.finalize(inventory);
    expect(
        result.families.size() == 1U &&
            result.families.front().unique_payloads == 1U &&
            result.families.front().observations == 2U,
        "a duplicate WadV1 observation was rescanned or not counted");
}

[[nodiscard]] std::vector<openrc::WadPayloadFamilyLimitsV1>
zero_limit_cases() {
    std::vector<openrc::WadPayloadFamilyLimitsV1> result;
    auto add_case = [&result](auto clear_limit) {
        auto limits = kFamilyLimits;
        clear_limit(limits);
        result.push_back(limits);
    };
    add_case([](auto& limits) { limits.max_unique_payloads = 0U; });
    add_case([](auto& limits) { limits.max_observations = 0U; });
    add_case([](auto& limits) {
        limits.max_input_bytes_per_payload = 0U;
    });
    add_case([](auto& limits) { limits.max_total_input_bytes = 0U; });
    add_case([](auto& limits) {
        limits.max_sampled_bytes_per_payload = 0U;
    });
    add_case([](auto& limits) { limits.max_total_sampled_bytes = 0U; });
    add_case([](auto& limits) { limits.max_families = 0U; });
    add_case([](auto& limits) { limits.max_members_per_family = 0U; });
    add_case([](auto& limits) { limits.max_levels_per_family = 0U; });
    add_case([](auto& limits) {
        limits.max_unique_level_memberships = 0U;
    });
    return result;
}

void test_constructor_and_observation_limits() {
    for (const auto& limits : zero_limit_cases()) {
        expect_family_error(
            [limits] {
                openrc::WadPayloadFamilyBuilderV1 builder(limits);
            },
            "a zero WadV1 candidate-family limit was accepted");
    }

    const auto first = two_small_words(1U, 2U);
    const auto second = two_small_words(3U, 4U);
    auto limits = kFamilyLimits;
    limits.max_input_bytes_per_payload = first.size() - 1U;
    expect_family_error(
        [&] {
            openrc::WadPayloadFamilyBuilderV1 builder(limits);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 0U}, first);
        },
        "the per-payload WadV1 family input limit was ignored");

    limits = kFamilyLimits;
    limits.max_total_input_bytes = first.size();
    expect_family_error(
        [&] {
            openrc::WadPayloadFamilyBuilderV1 builder(limits);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 0U}, first);
            builder.observe(openrc::WadPayloadAddResultV1{1U, 1U}, second);
        },
        "the aggregate WadV1 family input limit was ignored");

    limits = kFamilyLimits;
    limits.max_sampled_bytes_per_payload = 4U;
    limits.max_total_sampled_bytes = 4U;
    expect_family_error(
        [&] {
            openrc::WadPayloadFamilyBuilderV1 builder(limits);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 0U}, first);
            builder.observe(openrc::WadPayloadAddResultV1{1U, 1U}, second);
        },
        "the aggregate WadV1 family sampling limit was ignored");

    limits = kFamilyLimits;
    limits.max_unique_payloads = 1U;
    expect_family_error(
        [&] {
            openrc::WadPayloadFamilyBuilderV1 builder(limits);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 0U}, first);
            builder.observe(openrc::WadPayloadAddResultV1{1U, 1U}, second);
        },
        "the WadV1 family unique-payload limit was ignored");

    limits = kFamilyLimits;
    limits.max_observations = 1U;
    expect_family_error(
        [&] {
            openrc::WadPayloadFamilyBuilderV1 builder(limits);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 0U}, first);
            builder.observe(openrc::WadPayloadAddResultV1{0U, 1U}, first);
        },
        "the WadV1 family observation limit was ignored");
}

template <typename ConfigureLimits>
void expect_finalize_limit_error(
    const std::vector<std::pair<openrc::WadPayloadOriginV1,
                                std::vector<std::byte>>>& observations,
    ConfigureLimits&& configure_limits,
    const std::string& message) {
    auto limits = kFamilyLimits;
    std::invoke(std::forward<ConfigureLimits>(configure_limits), limits);
    openrc::WadPayloadInventoryBuilderV1 inventory_builder(
        {},
        kInventoryLimits);
    openrc::WadPayloadFamilyBuilderV1 family_builder(limits);
    for (const auto& [origin, payload] : observations) {
        add_and_observe(
            inventory_builder,
            family_builder,
            origin,
            payload);
    }
    const auto inventory = inventory_builder.finalize();
    expect_family_error(
        [&] { (void)family_builder.finalize(inventory); },
        message);
}

void test_finalize_limits() {
    const auto first = two_small_words(1U, 2U);
    const auto second = two_small_words(3U, 4U);
    const auto other = different_shape_payload();

    expect_finalize_limit_error(
        {
            {make_origin(openrc::WadPayloadOriginKindV1::explicit_input), first},
            {make_origin(openrc::WadPayloadOriginKindV1::explicit_input), other},
        },
        [](auto& limits) { limits.max_families = 1U; },
        "the WadV1 candidate-family count limit was ignored");
    expect_finalize_limit_error(
        {
            {make_origin(openrc::WadPayloadOriginKindV1::explicit_input), first},
            {make_origin(openrc::WadPayloadOriginKindV1::explicit_input), second},
        },
        [](auto& limits) { limits.max_members_per_family = 1U; },
        "the WadV1 candidate-family member limit was ignored");
    expect_finalize_limit_error(
        {
            {make_origin(openrc::WadPayloadOriginKindV1::explicit_input, 1U), first},
            {make_origin(openrc::WadPayloadOriginKindV1::explicit_input, 2U), first},
        },
        [](auto& limits) { limits.max_levels_per_family = 1U; },
        "the per-family WadV1 level limit was ignored");
    expect_finalize_limit_error(
        {
            {make_origin(openrc::WadPayloadOriginKindV1::explicit_input, 1U), first},
            {make_origin(openrc::WadPayloadOriginKindV1::explicit_input, 1U), second},
        },
        [](auto& limits) { limits.max_unique_level_memberships = 1U; },
        "the WadV1 unique/level membership limit was ignored");
}

void test_finalize_validation_and_closure() {
    const auto payload = two_small_words(1U, 2U);

    openrc::WadPayloadInventoryBuilderV1 incomplete_inventory_builder(
        {},
        kInventoryLimits);
    openrc::WadPayloadFamilyBuilderV1 incomplete_family_builder(kFamilyLimits);
    add_and_observe(
        incomplete_inventory_builder,
        incomplete_family_builder,
        make_origin(openrc::WadPayloadOriginKindV1::explicit_input),
        payload);
    (void)incomplete_inventory_builder.add_decoded_payload(
        make_origin(openrc::WadPayloadOriginKindV1::explicit_input),
        payload);
    const auto incomplete_inventory = incomplete_inventory_builder.finalize();
    expect_family_error(
        [&] { (void)incomplete_family_builder.finalize(incomplete_inventory); },
        "WadV1 family finalization accepted an unobserved semantic observation");

    openrc::WadPayloadInventoryBuilderV1 uncovered_inventory_builder(
        {},
        kInventoryLimits);
    (void)uncovered_inventory_builder.add_decoded_payload(
        make_origin(openrc::WadPayloadOriginKindV1::explicit_input),
        payload);
    const auto uncovered_inventory = uncovered_inventory_builder.finalize();
    openrc::WadPayloadFamilyBuilderV1 uncovered_family_builder(kFamilyLimits);
    expect_family_error(
        [&] { (void)uncovered_family_builder.finalize(uncovered_inventory); },
        "WadV1 family finalization accepted an unprofiled unique payload");

    openrc::WadPayloadInventoryV1 empty_inventory;
    openrc::WadPayloadFamilyBuilderV1 closed_builder(kFamilyLimits);
    const auto empty_result = closed_builder.finalize(empty_inventory);
    expect(
        empty_result.families.empty() &&
            empty_result.profiled_unique_payloads == 0U,
        "empty WadV1 family finalization is not empty");
    expect_family_error(
        [&] { (void)closed_builder.finalize(empty_inventory); },
        "a WadV1 family builder was finalized twice");
    expect_family_error(
        [&] {
            closed_builder.observe(
                openrc::WadPayloadAddResultV1{0U, 0U},
                payload);
        },
        "a finalized WadV1 family builder accepted another observation");
}

} // namespace

int main() {
    try {
        test_grouping_classification_representative_and_counts();
        test_grouping_is_stable_across_order_and_classification();
        test_observation_order_and_duplicate_validation();
        test_constructor_and_observation_limits();
        test_finalize_limits();
        test_finalize_validation_and_closure();
        std::cout << "OpenRC WadPayloadFamilyInventoryV1 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC WadPayloadFamilyInventoryV1 tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
