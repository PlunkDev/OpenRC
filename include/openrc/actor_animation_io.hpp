#pragma once

#include "openrc/actor_animation.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kActorAnimationIoFormatVersionV1 = 1U;
inline constexpr std::uint32_t kActorAnimationIoPayloadTypeV1 = 1U;
inline constexpr std::uint32_t kActorAnimationIoHeaderBytesV1 = 0x80U;
inline constexpr std::uint32_t kActorAnimationIoClipRecordBytesV1 = 0x60U;
inline constexpr std::uint32_t kActorAnimationIoFrameRecordBytesV1 = 0x20U;
inline constexpr std::uint32_t kActorAnimationIoJointPoseRecordBytesV1 = 0x40U;

inline constexpr std::string_view kActorAnimationResourceIdV1 =
    "actors/animations";
inline constexpr std::string_view kActorAnimationResourceTypeIdV1 =
    "openrc.actor-animation-bank";
inline constexpr std::uint32_t kActorAnimationResourceSchemaVersionV1 = 1U;

struct ActorAnimationIoLimitsV1 {
  std::uint64_t max_encoded_bytes = 0U;
  ActorAnimationLimitsV1 bank;

  [[nodiscard]] bool
  operator==(const ActorAnimationIoLimitsV1 &) const = default;
};

class ActorAnimationIoError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// The encoder canonicalizes clip order, signed zero, and quaternion length and
// sign. Every field is serialized explicitly in little-endian order; native
// C++ object layouts are never copied into the payload.
[[nodiscard]] std::vector<std::byte>
encode_actor_animation_bank_v1(const ActorAnimationBankV1 &bank,
                               ActorAnimationIoLimitsV1 limits);

// Counts are bounded before allocation. Only the exact canonical V1 table and
// key partitions, record sizes, offsets, reserved zeros, and final byte size
// are accepted.
[[nodiscard]] ActorAnimationBankV1
decode_actor_animation_bank_v1(std::span<const std::byte> bytes,
                               ActorAnimationIoLimitsV1 limits);

} // namespace openrc
