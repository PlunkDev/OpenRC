#pragma once

#include "openrc/rac_new_game_flow.hpp"
#include "openrc/session_state_io.hpp"

#include <filesystem>

namespace openrc {

struct RacFrontendStateCompilationV1 {
  SessionStateInitialV1 initial;
  SessionStateLimitsV1 state_limits;
  FrontendNoSavePlanV1 no_save;
  FrontendSequenceV1 new_game_continuation;
  // Compiler provenance and mappings; these are not package resources.
  RacFrontendResetTemplateV1 reset_template;
  std::vector<RacFrontendStateBindingV1> source_bindings;
  PreparedContentDigestV1 source_elf_sha256{},source_template_sha256{};
  // Three neutral resources: state, no-save input plan, presentation sequence.
  std::vector<LevelPackageResourceV1> resources;
};

[[nodiscard]] constexpr SessionStateLimitsV1 rac_frontend_state_limits_v1() {
  return frontend_session_state_limits_v1();
}

// Complete descriptor-owned reset storage plus named live frontend fields.
// Initial values are actual ELF load bytes (including declared zero-filled
// segment tails), not a fabricated ready-card/menu snapshot. Startup and card
// lifecycle consumers must update these same views before callbacks can act.
// Screen/object pointers are compiler-lowered to neutral tokens. The three
// established progress-admission row views alias these complete reset owners.
[[nodiscard]] RacFrontendStateCompilationV1 compile_rac_frontend_state_v1(
    const std::filesystem::path &source_image,std::span<const std::byte> source_elf,
    const RacNewGamePresentationInputsV1 &presentation,
    const RacNewGameFlowResourcesV1 &resources);

} // namespace openrc
