#pragma once

#include "openrc/wad_payload_family.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

class WadPayloadFamilyTsvError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Returns indices into families.families in the same order used by the TSV.
[[nodiscard]] std::vector<std::size_t> rank_unknown_wad_payload_families_v1(
    const WadPayloadFamilyInventoryV1& families,
    std::uint32_t focus_level_id);

// Emits one deterministic row for every candidate family containing at least
// one currently unknown payload. Rows are ranked by unknown coverage in the
// requested focus level, then by corpus-wide unknown coverage and stable ID.
// The focus level is only a reporting preference and never changes family IDs.
[[nodiscard]] std::string encode_wad_payload_families_tsv_v1(
    const WadPayloadFamilyInventoryV1& families,
    std::uint32_t focus_level_id);

} // namespace openrc
