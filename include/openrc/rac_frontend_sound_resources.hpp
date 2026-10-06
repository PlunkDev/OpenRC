#pragma once

#include "openrc/prepared_game_v2.hpp"

#include <filesystem>
#include <span>
#include <vector>

namespace openrc {

// Compiler-only extraction of the five class1138 menu one-shots. The separate
// scenic global cue programs and their looping voices are not represented by
// this result. Output has the original active startup settings: effects1024,
// stereo. A host must admit those settings before using these baked gains.
[[nodiscard]] std::vector<LevelPackageResourceV1>
compile_rac_frontend_sound_resources_v1(
    const std::filesystem::path& image,
    std::span<const std::byte> boot_executable);

// Thirteen source-qualified scenic streams, four prepared gain families, the
// five-program control graph, its voice binding bank and scene cue table (20 resources).
[[nodiscard]] std::vector<LevelPackageResourceV1>
compile_rac_frontend_ambient_resources_v1(
    const std::filesystem::path& image,
    std::span<const std::byte> boot_executable);

} // namespace openrc
