#pragma once

#include "openrc/session_state.hpp"

#include <filesystem>
#include <span>

namespace openrc {

// Compiler-only projection of the qualified initial Veldin section installer.
// The resource writes existing canonical owners and stops before level entry.
// Persistent progress rows, entry initialization and admission are not reset.
// The caller adds the whole-image, boot and session-resource provenance.
[[nodiscard]] LevelPackageResourceV1 compile_rac_initial_level_installation_v1(
    const std::filesystem::path &image, std::span<const std::byte> boot,
    const SessionStateSchemaV1 &schema, SessionStateLimitsV1 limits);

} // namespace openrc
