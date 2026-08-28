#include "openrc/wad_payload_inventory.hpp"

#include <array>
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

constexpr openrc::WadPayloadInventoryLimitsV1 kGenerousLimits{
    64U, 64U, 4096U, 16384U, 16U, 256U,
};

class SyntheticParserError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void expect_inventory_error(Function&& function, const std::string& message) {
    try {
        std::invoke(std::forward<Function>(function));
    } catch (const openrc::WadPayloadInventoryError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] std::vector<std::byte> bytes(const std::string& text) {
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const char character : text) {
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return result;
}

[[nodiscard]] bool starts_with(const std::span<const std::byte> input, const char expected) {
    return !input.empty() &&
           input.front() == static_cast<std::byte>(static_cast<unsigned char>(expected));
}

[[nodiscard]] openrc::WadPayloadProbeV1 prefix_probe(std::string name, const char expected,
                                                     std::uint64_t& invocation_count) {
    return openrc::WadPayloadProbeV1{
        std::move(name),
        [expected, &invocation_count](const std::span<const std::byte> input) {
            ++invocation_count;
            return starts_with(input, expected) ? openrc::WadPayloadProbeDecisionV1::match
                                                : openrc::WadPayloadProbeDecisionV1::no_match;
        },
    };
}

[[nodiscard]] openrc::WadPayloadOriginV1 make_origin(const std::uint32_t record_index) {
    openrc::WadPayloadOriginV1 result;
    result.kind = openrc::WadPayloadOriginKindV1::local_wad_run;
    result.level_id = 0U;
    result.container_index = 3U;
    result.record_index = record_index;
    result.lba = 1000U + record_index;
    result.encoded_bytes = 0x40U;
    result.encoded_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    return result;
}

void test_recognized_unknown_ambiguous_and_order() {
    std::uint64_t a_calls = 0;
    std::uint64_t b_calls = 0;
    openrc::WadPayloadInventoryBuilderV1 builder(
        {
            prefix_probe("format-a", 'A', a_calls),
            prefix_probe("also-a", 'A', b_calls),
        },
        kGenerousLimits);

    const auto alpha = bytes("Alpha");
    const auto beta = bytes("Beta");
    const auto empty = bytes("");
    expect(builder.add_decoded_payload(make_origin(0U), alpha) ==
               openrc::WadPayloadAddResultV1{0U, 0U},
           "the first unique payload index is wrong");
    expect(builder.add_decoded_payload(make_origin(1U), beta) ==
               openrc::WadPayloadAddResultV1{1U, 1U},
           "the second unique payload index is wrong");
    expect(builder.add_decoded_payload(make_origin(2U), empty) ==
               openrc::WadPayloadAddResultV1{2U, 2U},
           "the third unique payload index is wrong");

    const auto result = builder.finalize();
    expect(result.unique_payloads.size() == 3U && result.observations.size() == 3U,
           "unique or observation counts are wrong");
    expect(result.unique_payloads[0].classification ==
                   openrc::WadPayloadClassificationV1::ambiguous &&
               result.unique_payloads[0].matched_format_names ==
                   std::vector<std::string>{"format-a", "also-a"},
           "ambiguous classification or probe order is wrong");
    expect(result.unique_payloads[1].classification ==
                   openrc::WadPayloadClassificationV1::unknown &&
               result.unique_payloads[1].matched_format_names.empty(),
           "unknown classification is wrong");
    expect(result.unique_payloads[2].classification == openrc::WadPayloadClassificationV1::unknown,
           "empty input classification is wrong");
    expect(result.ambiguous_unique_payloads == 1U && result.unknown_unique_payloads == 2U &&
               result.recognized_unique_payloads == 0U,
           "classification totals are wrong");
    expect(a_calls == 3U && b_calls == 3U && result.probe_invocations == 6U,
           "probe invocation totals are wrong");
    expect(result.unique_payloads[0].first_observation_index == 0U &&
               result.unique_payloads[1].first_observation_index == 1U &&
               result.unique_payloads[2].first_observation_index == 2U,
           "first-observation order was not preserved");
}

void test_deduplication_and_owned_origins() {
    std::uint64_t calls = 0;
    openrc::WadPayloadInventoryBuilderV1 builder({prefix_probe("format-a", 'A', calls)},
                                                 kGenerousLimits);
    auto payload = bytes("Alpha");
    auto first_origin = make_origin(7U);
    auto second_origin = make_origin(9U);

    const auto first = builder.add_decoded_payload(first_origin, payload);
    payload[0] = std::byte{'X'};
    const auto distinct = builder.add_decoded_payload(second_origin, payload);
    payload[0] = std::byte{'A'};
    auto duplicate_origin = make_origin(11U);
    const auto duplicate = builder.add_decoded_payload(duplicate_origin, payload);
    payload.assign(payload.size(), std::byte{0});

    const auto result = builder.finalize();
    expect(first == openrc::WadPayloadAddResultV1{0U, 0U} &&
               distinct == openrc::WadPayloadAddResultV1{1U, 1U} &&
               duplicate == openrc::WadPayloadAddResultV1{0U, 2U},
           "SHA-256 deduplication returned the wrong indices");
    expect(result.unique_payloads.size() == 2U && result.observations.size() == 3U,
           "deduplication retained the wrong number of records");
    expect(result.unique_payloads[0].decoded_sha256 ==
                   "b1a96dd646bccaa24cef7a3db22a6f995f05658f4f1c3272913e258c03e6f"
                   "b24" &&
               result.unique_payloads[0].observation_count == 2U,
           "the unique digest or duplicate count is wrong");
    expect(result.observations[0].origin == first_origin &&
               result.observations[1].origin == second_origin &&
               result.observations[2].origin == duplicate_origin,
           "origins were not owned or did not preserve observation order");
    expect(result.observations[2].unique_payload_index == 0U,
           "the duplicate observation does not point at its unique payload");
    expect(calls == 2U && result.probe_invocations == 2U, "a duplicate payload was probed again");
    expect(result.total_decoded_bytes == 15U && result.unique_decoded_bytes == 10U,
           "decoded-byte accounting is wrong");
}

void test_recognized_result() {
    std::uint64_t calls = 0;
    openrc::WadPayloadInventoryBuilderV1 builder({prefix_probe("format-a", 'A', calls)},
                                                 kGenerousLimits);
    const auto payload = bytes("A");
    (void)builder.add_decoded_payload(make_origin(0U), payload);
    const auto result = builder.finalize();
    expect(result.unique_payloads[0].classification ==
                   openrc::WadPayloadClassificationV1::recognized &&
               result.unique_payloads[0].matched_format_names ==
                   std::vector<std::string>{"format-a"} &&
               result.recognized_unique_payloads == 1U,
           "single-probe recognition is wrong");
}

void test_limits() {
    const auto payload = bytes("AB");

    auto zero_probe_limits = kGenerousLimits;
    zero_probe_limits.max_probes = 0U;
    expect_inventory_error(
        [&] {
            openrc::WadPayloadInventoryBuilderV1 builder(
                {},
                zero_probe_limits);
        },
        "a zero probe-count limit was accepted");

    auto total_limits = kGenerousLimits;
    total_limits.max_total_decoded_bytes = 3U;
    openrc::WadPayloadInventoryBuilderV1 total_builder({}, total_limits);
    (void)total_builder.add_decoded_payload(make_origin(0U), payload);
    expect_inventory_error(
        [&] { (void)total_builder.add_decoded_payload(make_origin(1U), payload); },
        "the aggregate decoded-byte limit was ignored");
    const auto total_result = total_builder.finalize();
    expect(total_result.observations.size() == 1U && total_result.total_decoded_bytes == 2U,
           "a rejected aggregate-limit observation changed the inventory");

    std::uint64_t calls = 0;
    auto probe_limits = kGenerousLimits;
    probe_limits.max_probe_invocations = 1U;
    openrc::WadPayloadInventoryBuilderV1 probe_builder(
        {
            prefix_probe("a", 'A', calls),
            prefix_probe("b", 'B', calls),
        },
        probe_limits);
    expect_inventory_error(
        [&] { (void)probe_builder.add_decoded_payload(make_origin(0U), payload); },
        "the probe-invocation limit was ignored");
    expect(calls == 0U, "probes ran before their aggregate invocation limit was checked");
}

void test_parser_error_adapter_and_foreign_exception() {
    const auto payload = bytes("payload");
    const auto rejecting = [](const std::span<const std::byte>) {
        throw SyntheticParserError("not this format");
    };
    expect(openrc::probe_wad_payload_with_parser_v1<SyntheticParserError>(payload, rejecting) ==
               openrc::WadPayloadProbeDecisionV1::no_match,
           "the expected parser rejection was not converted to no_match");

    const auto accepting = [](const std::span<const std::byte>) { return 42; };
    expect(openrc::probe_wad_payload_with_parser_v1<SyntheticParserError>(payload, accepting) ==
               openrc::WadPayloadProbeDecisionV1::match,
           "an accepted strict parse was not converted to match");

    const auto foreign = [](const std::span<const std::byte>) -> int {
        throw std::runtime_error("foreign failure");
    };
    try {
        (void)openrc::probe_wad_payload_with_parser_v1<SyntheticParserError>(payload, foreign);
    } catch (const std::runtime_error& error) {
        expect(std::string(error.what()) == "foreign failure",
               "the foreign exception changed while propagating");
        return;
    }
    throw std::runtime_error("a foreign parser exception became no_match");
}

void test_bad_alloc_propagates_from_parser_adapter() {
    const auto payload = bytes("payload");
    const auto exhausted = [](const std::span<const std::byte>) -> int { throw std::bad_alloc{}; };
    try {
        (void)openrc::probe_wad_payload_with_parser_v1<SyntheticParserError>(payload, exhausted);
    } catch (const std::bad_alloc&) {
        return;
    }
    throw std::runtime_error("std::bad_alloc became no_match");
}

void test_foreign_probe_exception_does_not_classify() {
    const auto payload = bytes("payload");
    openrc::WadPayloadInventoryBuilderV1 builder(
        {
            openrc::WadPayloadProbeV1{
                "foreign",
                [](const std::span<const std::byte>) -> openrc::WadPayloadProbeDecisionV1 {
                    throw std::runtime_error("probe failed");
                },
            },
        },
        kGenerousLimits);
    try {
        (void)builder.add_decoded_payload(make_origin(0U), payload);
    } catch (const std::runtime_error& error) {
        expect(std::string(error.what()) == "probe failed",
               "the foreign probe exception changed while propagating");
        const auto result = builder.finalize();
        expect(result.observations.empty() && result.unique_payloads.empty() &&
                   result.probe_invocations == 1U,
               "a failed foreign probe classified or retained the payload");
        return;
    }
    throw std::runtime_error("a foreign probe exception was swallowed");
}

void test_probe_and_origin_validation() {
    expect_inventory_error(
        [] {
            openrc::WadPayloadInventoryBuilderV1 builder(
                {openrc::WadPayloadProbeV1{"",
                                           [](const std::span<const std::byte>) {
                                               return openrc::WadPayloadProbeDecisionV1::no_match;
                                           }}},
                kGenerousLimits);
        },
        "an empty format name was accepted");

    expect_inventory_error(
        [] {
            openrc::WadPayloadInventoryBuilderV1 builder(
                {
                    openrc::WadPayloadProbeV1{
                        "same",
                        [](const std::span<const std::byte>) {
                            return openrc::WadPayloadProbeDecisionV1::no_match;
                        }},
                    openrc::WadPayloadProbeV1{"same",
                                              [](const std::span<const std::byte>) {
                                                  return openrc::WadPayloadProbeDecisionV1::match;
                                              }},
                },
                kGenerousLimits);
        },
        "duplicate format names were accepted");

    openrc::WadPayloadInventoryBuilderV1 builder({}, kGenerousLimits);
    auto invalid_origin = make_origin(0U);
    invalid_origin.encoded_sha256 = "ABC";
    const auto payload = bytes("payload");
    expect_inventory_error([&] { (void)builder.add_decoded_payload(invalid_origin, payload); },
                           "an invalid encoded SHA-256 was accepted");

    auto missing_parent = make_origin(1U);
    missing_parent.parent_unique_payload_index = 0U;
    expect_inventory_error([&] { (void)builder.add_decoded_payload(missing_parent, payload); },
                           "an incomplete parent reference was accepted");

    auto missing_observation_parent = make_origin(2U);
    missing_observation_parent.parent_observation_index = 0U;
    expect_inventory_error(
        [&] { (void)builder.add_decoded_payload(missing_observation_parent, payload); },
        "a parent observation without a unique payload was accepted");

    const auto parent = builder.add_decoded_payload(make_origin(3U), bytes("parent"));
    auto child_origin = make_origin(4U);
    child_origin.parent_unique_payload_index = parent.unique_payload_index;
    child_origin.parent_observation_index = parent.observation_index;
    const auto child = builder.add_decoded_payload(child_origin, bytes("child"));
    expect(child == openrc::WadPayloadAddResultV1{1U, 1U},
           "a valid parent observation pair was not retained");

    auto mismatched_parent = make_origin(5U);
    mismatched_parent.parent_unique_payload_index = child.unique_payload_index;
    mismatched_parent.parent_observation_index = parent.observation_index;
    expect_inventory_error(
        [&] { (void)builder.add_decoded_payload(mismatched_parent, bytes("other")); },
        "a mismatched parent observation and unique payload were accepted");

    const auto result = builder.finalize();
    expect(result.observations[1].origin.parent_unique_payload_index ==
               parent.unique_payload_index &&
               result.observations[1].origin.parent_observation_index ==
               parent.observation_index,
           "exact parent provenance was not retained");
}

void test_finalize_closes_builder() {
    openrc::WadPayloadInventoryBuilderV1 builder({}, kGenerousLimits);
    const auto result = builder.finalize();
    expect(result.observations.empty(), "an empty inventory is not empty");
    const auto payload = bytes("payload");
    expect_inventory_error([&] { (void)builder.add_decoded_payload(make_origin(0U), payload); },
                           "a finalized inventory accepted another payload");
    expect_inventory_error([&] { (void)builder.finalize(); }, "an inventory was finalized twice");
}

} // namespace

int main() {
    try {
        test_recognized_unknown_ambiguous_and_order();
        test_deduplication_and_owned_origins();
        test_recognized_result();
        test_limits();
        test_parser_error_adapter_and_foreign_exception();
        test_bad_alloc_propagates_from_parser_adapter();
        test_foreign_probe_exception_does_not_classify();
        test_probe_and_origin_validation();
        test_finalize_closes_builder();
        std::cout << "OpenRC WadPayloadInventoryV1 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC WadPayloadInventoryV1 tests failed: " << error.what() << '\n';
        return 1;
    }
}
