#pragma once

#include "openrc/wad_payload_inventory.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace openrc {

struct WadPayloadKnownFormatProbeLimitsV1 {
    // This must match or exceed the inventory builder's per-payload limit.
    std::uint64_t max_decoded_payload_bytes = 0U;
    // Kept independent from the byte envelope because each scene descriptor
    // expands into substantially more owned metadata than its 0x40-byte input.
    std::uint64_t max_scene_block_records = 0U;
    // Scene animations own one actor descriptor per track and one compact
    // metadata record per frame while leaving the large bodies zero-copy.
    std::uint64_t max_scene_animation_actor_tracks = 0U;
    std::uint64_t max_scene_animation_total_frame_ranges = 0U;
    // Localized entries own five text vectors apiece and therefore keep an
    // allocation-count limit independent of their 16-bit on-disc offsets.
    std::uint64_t max_localized_subtitle_entries = 0U;
};

class WadPayloadKnownFormatProbeError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Returns strict, deterministic probes for semantic formats already proven by
// OpenRC. Neutral helper structures are not registered as competing formats;
// for example, MapArtV1 owns its boundary-table structure.
[[nodiscard]] std::vector<WadPayloadProbeV1>
make_known_wad_payload_probes_v1(WadPayloadKnownFormatProbeLimitsV1 limits);

} // namespace openrc
