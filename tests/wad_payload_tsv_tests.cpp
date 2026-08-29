#include "openrc/wad_payload_tsv.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] openrc::WadPayloadInventoryV1 make_inventory() {
    openrc::WadPayloadInventoryV1 inventory;
    inventory.unique_payloads.push_back(openrc::WadPayloadUniqueRecordV1{
        4U,
        "0000000000000000000000000000000000000000000000000000000000000000",
        openrc::WadPayloadClassificationV1::unknown,
        {},
        0U,
        1U,
    });
    inventory.unique_payloads.push_back(openrc::WadPayloadUniqueRecordV1{
        12U,
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        openrc::WadPayloadClassificationV1::ambiguous,
        {"Map,Art", "line\tbreak\n100%"},
        1U,
        1U,
    });
    inventory.observations.push_back(openrc::WadPayloadObservationV1{
        openrc::WadPayloadOriginV1{},
        0U,
    });
    openrc::WadPayloadOriginV1 origin;
    origin.kind = openrc::WadPayloadOriginKindV1::wad_bundle_record;
    origin.level_id = 7U;
    origin.container_index = 3U;
    origin.run_index = 1U;
    origin.record_index = 2U;
    origin.lba = 1234U;
    origin.container_byte_offset = 64U;
    origin.encoded_bytes = 16U;
    origin.encoded_sha256 =
        "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
    origin.parent_unique_payload_index = 0U;
    origin.parent_observation_index = 0U;
    inventory.observations.push_back(openrc::WadPayloadObservationV1{
        std::move(origin),
        1U,
    });
    return inventory;
}

void test_deterministic_schema_and_escaping() {
    const auto inventory = make_inventory();
    const auto first = openrc::encode_wad_payload_inventory_tsv_v1(inventory);
    const auto second = openrc::encode_wad_payload_inventory_tsv_v1(inventory);
    expect(first == second, "WadV1 TSV encoding is not deterministic");
    expect(
        first ==
            "observation\tunique\tclassification\tformats\tdecoded_bytes\t"
            "decoded_sha256\tunique_observations\torigin\tlevel\tcontainer\t"
            "run\trecord\tlba\tcontainer_offset\tencoded_bytes\tencoded_sha256\t"
            "parent_unique\tparent_observation\n"
            "0\t0\tunknown\t\t4\t"
            "0000000000000000000000000000000000000000000000000000000000000000\t"
            "1\texplicit-input\t\t\t\t\t\t\t\t\t\t\n"
            "1\t1\tambiguous\tMap%2CArt,line%09break%0A100%25\t12\t"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\t"
            "1\twad-bundle-record\t7\t3\t1\t2\t1234\t64\t16\t"
            "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210\t"
            "0\t0\n",
        "WadV1 TSV schema, provenance, or escaping changed");
}

void test_invalid_unique_index_is_rejected() {
    auto inventory = make_inventory();
    inventory.observations.front().unique_payload_index = 2U;
    bool rejected = false;
    try {
        (void)openrc::encode_wad_payload_inventory_tsv_v1(inventory);
    } catch (const openrc::WadPayloadTsvError&) {
        rejected = true;
    }
    expect(rejected, "WadV1 TSV accepted an invalid unique payload index");
}

} // namespace

int main() {
    try {
        test_deterministic_schema_and_escaping();
        test_invalid_unique_index_is_rejected();
        std::cout << "OpenRC WadPayloadInventoryV1 TSV tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC WadPayloadInventoryV1 TSV tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
