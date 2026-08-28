#include "openrc/wad_payload_probes.hpp"

#include "openrc/map_art.hpp"
#include "openrc/scene_block_directory.hpp"
#include "openrc/two_fip.hpp"
#include "openrc/wad_bundle.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openrc {
namespace {

void require_bounded_payload(
    const std::span<const std::byte> bytes,
    const std::uint64_t maximum_bytes) {
    if (bytes.size() > maximum_bytes) {
        throw WadPayloadKnownFormatProbeError(
            "A decoded WadV1 payload exceeds the known-format probe limit");
    }
}

} // namespace

std::vector<WadPayloadProbeV1> make_known_wad_payload_probes_v1(
    const WadPayloadKnownFormatProbeLimitsV1 limits) {
    if (limits.max_decoded_payload_bytes == 0U ||
        limits.max_scene_block_records == 0U) {
        throw WadPayloadKnownFormatProbeError(
            "The known WadV1 format-probe byte and scene-record limits must be non-zero");
    }

    const auto maximum_bytes = limits.max_decoded_payload_bytes;
    const auto max_scene_records = limits.max_scene_block_records;

    std::vector<WadPayloadProbeV1> probes;
    probes.reserve(4U);
    probes.push_back(WadPayloadProbeV1{
        "TwoFipV1",
        [maximum_bytes](const std::span<const std::byte> bytes) {
            require_bounded_payload(bytes, maximum_bytes);
            return probe_wad_payload_with_parser_v1<TwoFipError>(
                bytes,
                [maximum_bytes](const std::span<const std::byte> input) {
                    return parse_two_fip(input, maximum_bytes);
                });
        }});
    probes.push_back(WadPayloadProbeV1{
        "MapArtV1",
        [maximum_bytes](const std::span<const std::byte> bytes) {
            require_bounded_payload(bytes, maximum_bytes);
            return probe_wad_payload_with_parser_v1<MapArtError>(
                bytes,
                [maximum_bytes](const std::span<const std::byte> input) {
                    return parse_map_art_v1(
                        input,
                        MapArtLimits{
                            BoundaryTableLimits{
                                maximum_bytes,
                                maximum_bytes},
                            maximum_bytes});
                });
        }});
    probes.push_back(WadPayloadProbeV1{
        "SceneBlockDirectoryV1",
        [maximum_bytes, max_scene_records](
            const std::span<const std::byte> bytes) {
            require_bounded_payload(bytes, maximum_bytes);
            return probe_wad_payload_with_parser_v1<SceneBlockDirectoryError>(
                bytes,
                [maximum_bytes, max_scene_records](
                    const std::span<const std::byte> input) {
                    return parse_scene_block_directory_v1(
                        input,
                        SceneBlockDirectoryLimits{
                            maximum_bytes,
                            max_scene_records,
                            maximum_bytes});
                });
        }});
    probes.push_back(WadPayloadProbeV1{
        "WadBundleV1",
        [maximum_bytes](const std::span<const std::byte> bytes) {
            require_bounded_payload(bytes, maximum_bytes);
            return probe_wad_payload_with_parser_v1<WadBundleError>(
                bytes,
                [](const std::span<const std::byte> input) {
                    return parse_wad_bundle_v1(input);
                });
        }});
    return probes;
}

} // namespace openrc
