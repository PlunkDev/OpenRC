#pragma once

#include "openrc/actor_library.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kActorLibraryIoFormatVersionV1 = 1U;
inline constexpr std::uint32_t kActorLibraryIoHeaderBytesV1 = 0xa0U;

inline constexpr std::string_view kActorLibraryResourceIdV1 = "actors/library";
inline constexpr std::string_view kActorLibraryResourceTypeIdV1 =
    "openrc.actor-library";
inline constexpr std::uint32_t kActorLibraryResourceSchemaVersionV1 = 1U;

struct ActorLibraryIoLimitsV1 {
  std::uint64_t max_encoded_bytes = 0U;
  ActorLibraryLimitsV1 library;

  [[nodiscard]] bool operator==(const ActorLibraryIoLimitsV1 &) const = default;
};

class ActorLibraryIoError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// The canonical library is serialized field-by-field in little-endian order;
// native C++ layouts are never copied into the payload.
[[nodiscard]] std::vector<std::byte>
encode_actor_library_v1(const ActorLibraryV1 &library,
                        ActorLibraryIoLimitsV1 limits);

// Counts and aggregate byte totals in the fixed header are bounded before any
// table allocation. Readers reject unknown versions, non-zero reserved data,
// alternate ordering, stale digests, and every trailing byte.
[[nodiscard]] ActorLibraryV1
decode_actor_library_v1(std::span<const std::byte> bytes,
                        ActorLibraryIoLimitsV1 limits);

} // namespace openrc
