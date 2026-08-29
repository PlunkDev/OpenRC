#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openrc {

enum class WadPayloadOriginKindV1 {
    global_toc,
    global_toc_tail,
    local_wad_run,
    primary_extent0_subrange,
    primary_extent,
    wad_bundle_record,
    companion_terminal_record,
    explicit_input,
};

struct WadPayloadOriginV1 {
    WadPayloadOriginKindV1 kind = WadPayloadOriginKindV1::explicit_input;

    // Optional coordinates are deliberately generic enough to represent every
    // current DiscTocV1 source without storing a path or borrowing source data.
    // container_index is, depending on kind, a global slot, local resource
    // block, primary subrange, primary extent, or enclosing record index.
    // run_index is populated for one lane of a local resource block.
    std::optional<std::uint32_t> level_id;
    std::optional<std::uint32_t> container_index;
    std::optional<std::uint32_t> run_index;
    std::optional<std::uint32_t> record_index;
    std::optional<std::uint64_t> lba;
    std::optional<std::uint64_t> container_byte_offset;

    // These identify the logical compressed WadV1 record when the caller has
    // that information. An empty encoded_sha256 means it was not supplied.
    std::optional<std::uint64_t> encoded_bytes;
    std::string encoded_sha256;

    // Nested records point at both the exact parent observation and its unique
    // decoded payload. Either both values are present or neither is; the
    // builder validates that the pair agrees with the retained observation.
    std::optional<std::uint64_t> parent_unique_payload_index;
    std::optional<std::uint64_t> parent_observation_index;

    [[nodiscard]] bool operator==(const WadPayloadOriginV1&) const = default;
};

enum class WadPayloadProbeDecisionV1 {
    no_match,
    match,
};

using WadPayloadProbeFunctionV1 =
    std::function<WadPayloadProbeDecisionV1(std::span<const std::byte>)>;

struct WadPayloadProbeV1 {
    std::string format_name;
    WadPayloadProbeFunctionV1 inspect;
};

// Adapts an existing strict parser to a format probe. Only the explicitly
// named parser rejection type becomes no_match. std::bad_alloc and every other
// foreign exception propagate to the caller and cannot silently classify a
// payload as unknown. ParserError must be the parser's dedicated final error
// type rather than std::exception or another catch-all base. A production
// probe must validate the complete payload; a prefix heuristic is not a match.
template <typename ParserError, typename Parser>
[[nodiscard]] WadPayloadProbeDecisionV1
probe_wad_payload_with_parser_v1(std::span<const std::byte> decoded_bytes, Parser&& parser) {
    static_assert(std::is_base_of_v<std::exception, ParserError>);
    static_assert(std::is_final_v<ParserError>,
                  "ParserError must be a dedicated final parser rejection type");
    try {
        (void)std::invoke(std::forward<Parser>(parser), decoded_bytes);
        return WadPayloadProbeDecisionV1::match;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const ParserError&) {
        return WadPayloadProbeDecisionV1::no_match;
    }
}

enum class WadPayloadClassificationV1 {
    recognized,
    unknown,
    ambiguous,
};

struct WadPayloadUniqueRecordV1 {
    std::uint64_t decoded_bytes = 0;
    std::string decoded_sha256;
    WadPayloadClassificationV1 classification = WadPayloadClassificationV1::unknown;

    // Registration order is preserved. recognized has exactly one name,
    // unknown has none, and ambiguous has at least two.
    std::vector<std::string> matched_format_names;
    std::uint64_t first_observation_index = 0;
    std::uint64_t observation_count = 0;
};

struct WadPayloadObservationV1 {
    WadPayloadOriginV1 origin;
    std::uint64_t unique_payload_index = 0;
};

struct WadPayloadAddResultV1 {
    std::uint64_t unique_payload_index = 0;
    std::uint64_t observation_index = 0;

    [[nodiscard]] bool operator==(const WadPayloadAddResultV1&) const = default;
};

struct WadPayloadInventoryV1 {
    // Both arrays preserve first-observation order. No decoded or encoded asset
    // bytes are retained in this result.
    std::vector<WadPayloadUniqueRecordV1> unique_payloads;
    std::vector<WadPayloadObservationV1> observations;

    std::uint64_t total_decoded_bytes = 0;
    std::uint64_t unique_decoded_bytes = 0;
    std::uint64_t probe_invocations = 0;
    std::uint64_t recognized_unique_payloads = 0;
    std::uint64_t unknown_unique_payloads = 0;
    std::uint64_t ambiguous_unique_payloads = 0;
};

struct WadPayloadInventoryLimitsV1 {
    std::uint64_t max_observations = 0;
    std::uint64_t max_unique_payloads = 0;
    std::uint64_t max_decoded_bytes_per_payload = 0;
    std::uint64_t max_total_decoded_bytes = 0;
    std::uint64_t max_probes = 0;
    std::uint64_t max_probe_invocations = 0;
};

class WadPayloadInventoryError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class WadPayloadInventoryBuilderV1 final {
  public:
    WadPayloadInventoryBuilderV1(std::vector<WadPayloadProbeV1> probes,
                                 WadPayloadInventoryLimitsV1 limits);

    WadPayloadInventoryBuilderV1(const WadPayloadInventoryBuilderV1&) = delete;
    WadPayloadInventoryBuilderV1& operator=(const WadPayloadInventoryBuilderV1&) = delete;
    WadPayloadInventoryBuilderV1(WadPayloadInventoryBuilderV1&&) noexcept = default;
    WadPayloadInventoryBuilderV1& operator=(WadPayloadInventoryBuilderV1&&) noexcept = default;

    // Processes exactly one already-decoded WadV1 payload. The bytes are used
    // only during this call. The return value identifies both the stable
    // unique payload and this exact observation, so later nested records can
    // retain unambiguous parent provenance even when the payload is deduped.
    [[nodiscard]] WadPayloadAddResultV1 add_decoded_payload(
        WadPayloadOriginV1 origin,
        std::span<const std::byte> decoded_bytes);

    // Finalization transfers the owned metadata and permanently closes the
    // builder. Calling it twice or adding another payload is an error.
    [[nodiscard]] WadPayloadInventoryV1 finalize();

  private:
    std::vector<WadPayloadProbeV1> probes_;
    WadPayloadInventoryLimitsV1 limits_;
    WadPayloadInventoryV1 inventory_;
    std::unordered_map<std::string, std::uint64_t> digest_index_;
    bool finalized_ = false;
};

} // namespace openrc
