#include "openrc/wad_payload_inventory.hpp"

#include "openrc/hash.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string& message) { throw WadPayloadInventoryError(message); }

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left, const std::uint64_t right,
                                        const char* description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while counting ") + description);
    }
    return left + right;
}

[[nodiscard]] bool is_lower_hex_digest(const std::string_view value) {
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

void validate_origin(const WadPayloadOriginV1& origin,
                     const WadPayloadInventoryV1& inventory) {
    if (!origin.encoded_sha256.empty() && !is_lower_hex_digest(origin.encoded_sha256)) {
        fail("A WadV1 origin has an invalid encoded SHA-256 digest");
    }
    if (origin.parent_unique_payload_index.has_value() !=
        origin.parent_observation_index.has_value()) {
        fail("A WadV1 origin must identify both its parent observation and unique payload");
    }
    if (!origin.parent_unique_payload_index) {
        return;
    }
    if (*origin.parent_unique_payload_index >= inventory.unique_payloads.size() ||
        *origin.parent_observation_index >= inventory.observations.size()) {
        fail("A WadV1 origin refers to a parent that has not been observed");
    }
    const auto& parent = inventory.observations[
        static_cast<std::size_t>(*origin.parent_observation_index)];
    if (parent.unique_payload_index != *origin.parent_unique_payload_index) {
        fail("A WadV1 origin's parent observation and unique payload disagree");
    }
}

void validate_limits(const WadPayloadInventoryLimitsV1 limits) {
    if (limits.max_observations == 0U || limits.max_unique_payloads == 0U ||
        limits.max_decoded_bytes_per_payload == 0U ||
        limits.max_total_decoded_bytes == 0U || limits.max_probes == 0U ||
        limits.max_probe_invocations == 0U) {
        fail("WadPayloadInventoryV1 size and count limits must be non-zero");
    }
}

void validate_probes(const std::vector<WadPayloadProbeV1>& probes,
                     const WadPayloadInventoryLimitsV1 limits) {
    if (static_cast<std::uint64_t>(probes.size()) > limits.max_probes) {
        fail("WadPayloadInventoryV1 probes exceed the caller's limit");
    }

    for (std::size_t index = 0; index < probes.size(); ++index) {
        const auto& probe = probes[index];
        if (probe.format_name.empty()) {
            fail("A WadPayloadInventoryV1 probe has an empty format name");
        }
        if (!probe.inspect) {
            fail("A WadPayloadInventoryV1 probe has no inspection function");
        }
        const auto duplicate =
            std::find_if(probes.begin(), probes.begin() + static_cast<std::ptrdiff_t>(index),
                         [&probe](const WadPayloadProbeV1& candidate) {
                             return candidate.format_name == probe.format_name;
                         });
        if (duplicate != probes.begin() + static_cast<std::ptrdiff_t>(index)) {
            fail("WadPayloadInventoryV1 probe format names must be unique");
        }
    }
}

[[nodiscard]] WadPayloadClassificationV1
classify_match_count(const std::size_t match_count) noexcept {
    if (match_count == 0U) {
        return WadPayloadClassificationV1::unknown;
    }
    if (match_count == 1U) {
        return WadPayloadClassificationV1::recognized;
    }
    return WadPayloadClassificationV1::ambiguous;
}

} // namespace

WadPayloadInventoryBuilderV1::WadPayloadInventoryBuilderV1(std::vector<WadPayloadProbeV1> probes,
                                                           const WadPayloadInventoryLimitsV1 limits)
    : probes_(std::move(probes)), limits_(limits) {
    validate_limits(limits_);
    validate_probes(probes_, limits_);
}

WadPayloadAddResultV1
WadPayloadInventoryBuilderV1::add_decoded_payload(WadPayloadOriginV1 origin,
                                                  const std::span<const std::byte> decoded_bytes) {
    if (finalized_) {
        fail("Cannot add a WadV1 payload after inventory finalization");
    }
    validate_origin(origin, inventory_);

    const auto decoded_size = static_cast<std::uint64_t>(decoded_bytes.size());
    if (decoded_size > limits_.max_decoded_bytes_per_payload) {
        fail("A decoded WadV1 payload exceeds the caller's per-payload limit");
    }
    if (inventory_.observations.size() >= limits_.max_observations) {
        fail("WadPayloadInventoryV1 observations exceed the caller's limit");
    }
    const auto next_total_decoded_bytes =
        checked_add(inventory_.total_decoded_bytes, decoded_size, "observed decoded WadV1 bytes");
    if (next_total_decoded_bytes > limits_.max_total_decoded_bytes) {
        fail("WadPayloadInventoryV1 decoded bytes exceed the caller's aggregate "
             "limit");
    }

    Sha256 hash;
    hash.update(decoded_bytes);
    const auto decoded_sha256 = hex_digest(hash.finish());
    const auto existing = digest_index_.find(decoded_sha256);
    const auto observation_index =
        static_cast<std::uint64_t>(inventory_.observations.size());

    inventory_.observations.reserve(inventory_.observations.size() + 1U);
    if (existing != digest_index_.end()) {
        const auto unique_index = existing->second;
        if (unique_index >= inventory_.unique_payloads.size()) {
            fail("WadPayloadInventoryV1 contains an invalid digest index");
        }
        auto& unique = inventory_.unique_payloads[static_cast<std::size_t>(unique_index)];
        if (unique.decoded_bytes != decoded_size) {
            fail("Two decoded WadV1 payload sizes share one SHA-256 digest");
        }
        unique.observation_count =
            checked_add(unique.observation_count, 1U, "duplicate WadV1 observations");
        inventory_.observations.push_back(WadPayloadObservationV1{
            std::move(origin),
            unique_index,
        });
        inventory_.total_decoded_bytes = next_total_decoded_bytes;
        return WadPayloadAddResultV1{unique_index, observation_index};
    }

    if (inventory_.unique_payloads.size() >= limits_.max_unique_payloads) {
        fail("WadPayloadInventoryV1 unique payloads exceed the caller's limit");
    }
    const auto required_probe_invocations =
        checked_add(inventory_.probe_invocations, static_cast<std::uint64_t>(probes_.size()),
                    "WadPayloadInventoryV1 probe invocations");
    if (required_probe_invocations > limits_.max_probe_invocations) {
        fail("WadPayloadInventoryV1 probe invocations exceed the caller's limit");
    }

    std::vector<std::string> matched_formats;
    matched_formats.reserve(probes_.size());
    for (const auto& probe : probes_) {
        inventory_.probe_invocations = checked_add(inventory_.probe_invocations, 1U,
                                                   "WadPayloadInventoryV1 probe invocations");
        switch (probe.inspect(decoded_bytes)) {
        case WadPayloadProbeDecisionV1::no_match:
            break;
        case WadPayloadProbeDecisionV1::match:
            matched_formats.push_back(probe.format_name);
            break;
        default:
            fail("A WadPayloadInventoryV1 probe returned an invalid decision");
        }
    }

    const auto next_unique_decoded_bytes =
        checked_add(inventory_.unique_decoded_bytes, decoded_size, "unique decoded WadV1 bytes");
    const auto unique_index = static_cast<std::uint64_t>(inventory_.unique_payloads.size());
    const auto first_observation_index = static_cast<std::uint64_t>(inventory_.observations.size());
    const auto classification = classify_match_count(matched_formats.size());

    WadPayloadUniqueRecordV1 unique_record{
        decoded_size,
        decoded_sha256,
        classification,
        std::move(matched_formats),
        first_observation_index,
        1U,
    };
    WadPayloadObservationV1 observation{
        std::move(origin),
        unique_index,
    };
    static_assert(std::is_nothrow_move_constructible_v<WadPayloadUniqueRecordV1>);
    static_assert(std::is_nothrow_move_constructible_v<WadPayloadObservationV1>);

    inventory_.unique_payloads.reserve(inventory_.unique_payloads.size() + 1U);
    const auto [inserted, was_inserted] = digest_index_.emplace(decoded_sha256, unique_index);
    if (!was_inserted) {
        fail("WadPayloadInventoryV1 digest insertion was inconsistent");
    }
    (void)inserted;
    inventory_.unique_payloads.push_back(std::move(unique_record));
    inventory_.observations.push_back(std::move(observation));
    inventory_.total_decoded_bytes = next_total_decoded_bytes;
    inventory_.unique_decoded_bytes = next_unique_decoded_bytes;
    switch (classification) {
    case WadPayloadClassificationV1::recognized:
        ++inventory_.recognized_unique_payloads;
        break;
    case WadPayloadClassificationV1::unknown:
        ++inventory_.unknown_unique_payloads;
        break;
    case WadPayloadClassificationV1::ambiguous:
        ++inventory_.ambiguous_unique_payloads;
        break;
    }
    return WadPayloadAddResultV1{unique_index, observation_index};
}

WadPayloadInventoryV1 WadPayloadInventoryBuilderV1::finalize() {
    if (finalized_) {
        fail("WadPayloadInventoryV1 was already finalized");
    }
    finalized_ = true;
    probes_.clear();
    digest_index_.clear();
    return std::move(inventory_);
}

} // namespace openrc
