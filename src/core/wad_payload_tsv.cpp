#include "openrc/wad_payload_tsv.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

namespace openrc {
namespace {

[[nodiscard]] const char* classification_name(
    const WadPayloadClassificationV1 classification) {
    switch (classification) {
    case WadPayloadClassificationV1::recognized:
        return "recognized";
    case WadPayloadClassificationV1::unknown:
        return "unknown";
    case WadPayloadClassificationV1::ambiguous:
        return "ambiguous";
    }
    throw WadPayloadTsvError("The WadV1 inventory contains an invalid classification");
}

[[nodiscard]] const char* origin_name(const WadPayloadOriginKindV1 kind) {
    switch (kind) {
    case WadPayloadOriginKindV1::global_toc:
        return "global-toc";
    case WadPayloadOriginKindV1::global_toc_tail:
        return "global-toc-tail";
    case WadPayloadOriginKindV1::local_wad_run:
        return "local-wad-run";
    case WadPayloadOriginKindV1::primary_extent0_subrange:
        return "primary-extent0-subrange";
    case WadPayloadOriginKindV1::primary_extent:
        return "primary-extent";
    case WadPayloadOriginKindV1::wad_bundle_record:
        return "wad-bundle-record";
    case WadPayloadOriginKindV1::companion_terminal_record:
        return "companion-terminal-record";
    case WadPayloadOriginKindV1::explicit_input:
        return "explicit-input";
    }
    throw WadPayloadTsvError("The WadV1 inventory contains an invalid origin kind");
}

template <typename Value>
[[nodiscard]] std::string optional_decimal(const std::optional<Value>& value) {
    return value ? std::to_string(*value) : std::string{};
}

[[nodiscard]] std::string encode_format_name(const std::string& value) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        if (character < 0x20U || character == 0x7fU ||
            character == static_cast<unsigned char>('%') ||
            character == static_cast<unsigned char>(',')) {
            result.push_back('%');
            result.push_back(kHex[(character >> 4U) & 0x0fU]);
            result.push_back(kHex[character & 0x0fU]);
        } else {
            result.push_back(static_cast<char>(character));
        }
    }
    return result;
}

} // namespace

std::string encode_wad_payload_inventory_tsv_v1(
    const WadPayloadInventoryV1& inventory) {
    std::ostringstream output;
    output
        << "observation\tunique\tclassification\tformats\tdecoded_bytes\t"
           "decoded_sha256\tunique_observations\torigin\tlevel\tcontainer\t"
           "record\tlba\tcontainer_offset\tencoded_bytes\tencoded_sha256\t"
           "parent_unique\tparent_observation\n";
    for (std::size_t observation_index = 0U;
         observation_index < inventory.observations.size();
         ++observation_index) {
        const auto& observation = inventory.observations[observation_index];
        if (observation.unique_payload_index >= inventory.unique_payloads.size()) {
            throw WadPayloadTsvError(
                "The WadV1 inventory contains an invalid unique index");
        }
        const auto& origin = observation.origin;
        if (origin.parent_unique_payload_index.has_value() !=
            origin.parent_observation_index.has_value()) {
            throw WadPayloadTsvError(
                "The WadV1 inventory contains an incomplete parent reference");
        }
        if (origin.parent_observation_index) {
            if (*origin.parent_unique_payload_index >=
                    inventory.unique_payloads.size() ||
                *origin.parent_observation_index >= observation_index) {
                throw WadPayloadTsvError(
                    "The WadV1 inventory contains an invalid parent reference");
            }
            const auto& parent = inventory.observations[
                static_cast<std::size_t>(*origin.parent_observation_index)];
            if (parent.unique_payload_index !=
                *origin.parent_unique_payload_index) {
                throw WadPayloadTsvError(
                    "The WadV1 inventory contains a mismatched parent reference");
            }
        }
        const auto& unique = inventory.unique_payloads[
            static_cast<std::size_t>(observation.unique_payload_index)];
        std::string formats;
        for (std::size_t index = 0U;
             index < unique.matched_format_names.size();
             ++index) {
            if (index != 0U) {
                formats.push_back(',');
            }
            formats += encode_format_name(unique.matched_format_names[index]);
        }
        output
            << observation_index << '\t'
            << observation.unique_payload_index << '\t'
            << classification_name(unique.classification) << '\t'
            << formats << '\t'
            << unique.decoded_bytes << '\t'
            << unique.decoded_sha256 << '\t'
            << unique.observation_count << '\t'
            << origin_name(origin.kind) << '\t'
            << optional_decimal(origin.level_id) << '\t'
            << optional_decimal(origin.container_index) << '\t'
            << optional_decimal(origin.record_index) << '\t'
            << optional_decimal(origin.lba) << '\t'
            << optional_decimal(origin.container_byte_offset) << '\t'
            << optional_decimal(origin.encoded_bytes) << '\t'
            << origin.encoded_sha256 << '\t'
            << optional_decimal(origin.parent_unique_payload_index) << '\t'
            << optional_decimal(origin.parent_observation_index) << '\n';
    }
    return output.str();
}

} // namespace openrc
