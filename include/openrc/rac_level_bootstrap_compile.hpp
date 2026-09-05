#pragma once

#include "openrc/level_bootstrap.hpp"
#include "openrc/rac_gameplay_bank.hpp"

#include <cstdint>
#include <stdexcept>

namespace openrc {

class RacLevelBootstrapCompileError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Clean-room semantic adapter from a decoded RAC1 gameplay bank. Exactly one
// static class-0 Moby is the authored player spawn. Ship fields are unrelated
// level metadata and are deliberately not consulted.
[[nodiscard]] LevelBootstrapV1
compile_rac_level_bootstrap_v1(const RacGameplayBankV1 &gameplay,
                               std::uint32_t level_id);

} // namespace openrc
