#include "openrc/wad_payload_probes.hpp"

#include "openrc/localized_subtitle_bank.hpp"
#include "openrc/map_art.hpp"
#include "openrc/rac_gameplay_bank.hpp"
#include "openrc/scene_animation_bank.hpp"
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
        limits.max_scene_block_records == 0U ||
        limits.max_scene_animation_actor_tracks == 0U ||
        limits.max_scene_animation_total_frame_ranges == 0U ||
        limits.max_localized_subtitle_entries == 0U) {
        throw WadPayloadKnownFormatProbeError(
            "The known WadV1 format-probe limits must all be non-zero");
    }

    const auto maximum_bytes = limits.max_decoded_payload_bytes;
    const auto max_scene_records = limits.max_scene_block_records;
    const auto max_scene_animation_actor_tracks =
        limits.max_scene_animation_actor_tracks;
    const auto max_scene_animation_total_frame_ranges =
        limits.max_scene_animation_total_frame_ranges;
    const auto max_subtitle_entries =
        limits.max_localized_subtitle_entries;

    std::vector<WadPayloadProbeV1> probes;
    probes.reserve(6U);
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
    probes.push_back(WadPayloadProbeV1{
        "SceneAnimationBankV1",
        [maximum_bytes,
         max_scene_animation_actor_tracks,
         max_scene_animation_total_frame_ranges,
         max_subtitle_entries](
            const std::span<const std::byte> bytes) {
            require_bounded_payload(bytes, maximum_bytes);
            return probe_wad_payload_with_parser_v1<
                SceneAnimationBankError>(
                bytes,
                [maximum_bytes,
                 max_scene_animation_actor_tracks,
                 max_scene_animation_total_frame_ranges,
                 max_subtitle_entries](
                    const std::span<const std::byte> input) {
                    return parse_scene_animation_bank_v1(
                        input,
                        SceneAnimationBankLimitsV1{
                            maximum_bytes,
                            max_scene_animation_actor_tracks,
                            max_scene_animation_total_frame_ranges,
                            max_subtitle_entries,
                            maximum_bytes});
                });
        }});
    probes.push_back(WadPayloadProbeV1{
        "RacGameplayBankV1",
        [maximum_bytes](const std::span<const std::byte> bytes) {
            require_bounded_payload(bytes, maximum_bytes);
            return probe_wad_payload_with_parser_v1<RacGameplayBankError>(
                bytes,
                [maximum_bytes](const std::span<const std::byte> input) {
                    return parse_rac_gameplay_bank_v1(
                        input,
                        RacGameplayBankLimitsV1{maximum_bytes});
                });
        }});
    return probes;
}

} // namespace openrc
