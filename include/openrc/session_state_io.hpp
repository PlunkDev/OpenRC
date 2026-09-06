#pragma once

#include "openrc/session_state.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kSessionStateIoFormatVersionV1 = 1U;
inline constexpr std::uint32_t kSessionStateIoPayloadTypeV1 = 1U;
inline constexpr std::uint32_t kSessionStateIoHeaderBytesV1 = 0xc0U;
inline constexpr std::uint32_t kSessionStateIoBufferRecordBytesV1 = 48U;
inline constexpr std::uint32_t kSessionStateIoViewRecordBytesV1 = 64U;

struct SessionStateIoLimitsV1 {
    // The same bound applies to encoded output and decoded input.
    std::uint64_t max_input_bytes = 0U;
    SessionStateLimitsV1 state;
};

class SessionStateIoError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One canonical initial-state artifact, not a runtime snapshot or save-file
// format. Encodes the schema and every buffer image field-by-field in LE;
// aliases refer to shared storage, and bytes outside views are preserved.
[[nodiscard]] std::vector<std::byte> encode_session_state_initial_v1(
    const SessionStateInitialV1& initial, SessionStateIoLimitsV1 limits);

// Counts, aggregates and exact partitions are checked before allocations.
// Requires canonical key ordering, complete storage, matching schema/initial
// digests, zero reserved fields, and no trailing or alternate-layout bytes.
[[nodiscard]] SessionStateInitialV1 decode_session_state_initial_v1(
    std::span<const std::byte> bytes, SessionStateIoLimitsV1 limits);

} // namespace openrc
