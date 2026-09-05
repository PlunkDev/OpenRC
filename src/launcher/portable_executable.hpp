#pragma once

#include <filesystem>
#include <string>

namespace openrc::launcher {

struct PortableExecutableCheck {
    bool accepted = false;
    std::string detail;
};

// Validates the PE structure, architecture, and direct imports without loading
// the image or resolving any of its dependencies.
[[nodiscard]] PortableExecutableCheck check_runtime_executable(
    const std::filesystem::path& path) noexcept;

} // namespace openrc::launcher
