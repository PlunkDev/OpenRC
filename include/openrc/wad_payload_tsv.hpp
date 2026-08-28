#pragma once

#include "openrc/wad_payload_inventory.hpp"

#include <stdexcept>
#include <string>

namespace openrc {

class WadPayloadTsvError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Encodes one deterministic UTF-8/ASCII TSV row per observation. Format names
// percent-escape field/list separators and line breaks, so arbitrary registered
// probe names cannot change the row or column structure.
[[nodiscard]] std::string encode_wad_payload_inventory_tsv_v1(
    const WadPayloadInventoryV1& inventory);

} // namespace openrc
