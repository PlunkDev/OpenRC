#pragma once

#include "openrc/session_state.hpp"

namespace openrc {

inline constexpr std::uint32_t kStateInstallationSchemaVersionV1 = 1U;

// An ordered installation into existing canonical state. Keys and values are
// neutral prepared data; no executable/source addresses or implicit fill.
struct StateInstallationV1 {
  std::uint32_t schema_version = kStateInstallationSchemaVersionV1;
  std::uint32_t level_id = 0U;
  PreparedContentDigestV1 state_schema_sha256{};
  std::vector<game::SessionStateWriteV1> writes;
};

struct StateInstallationLimitsV1 {
  std::uint64_t max_bytes = 64U * 1024U * 1024U;
  std::uint64_t max_writes = 1048576U;
  std::uint32_t max_key_bytes = 256U;
};

class StateInstallationError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Shape/encoding validation does not claim that the destination views exist.
// Empty installations, unknown types and out-of-width values are rejected.
void validate_state_installation_v1(const StateInstallationV1 &installation,
                                   StateInstallationLimitsV1 limits = {});
[[nodiscard]] std::vector<std::byte>
encode_state_installation_v1(const StateInstallationV1 &installation,
                             StateInstallationLimitsV1 limits = {});
[[nodiscard]] StateInstallationV1
decode_state_installation_v1(std::span<const std::byte> bytes,
                             StateInstallationLimitsV1 limits = {});

// Validate the level, schema and entire write batch before changing any byte.
// A successful installation increments this same session's revision once.
// Zero, duplicate and aliased writes retain their original order. This does
// not create a session, select its active level or admit gameplay entities.
void execute_state_installation_v1(const StateInstallationV1 &installation,
                                  std::uint32_t expected_level,
                                  game::SessionStateV1 &state,
                                  std::uint64_t expected_revision,
                                  StateInstallationLimitsV1 limits = {});

} // namespace openrc
