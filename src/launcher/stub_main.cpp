#include "openrc/paths.hpp"

#include <iostream>

int main() {
    const auto paths = openrc::application_paths();
    openrc::ensure_application_directories(paths);
    std::cout
        << "OpenRC Launcher currently has a native GUI on Windows only.\n"
        << "Use openrc-cli inspect <disc.iso> on this platform.\n";
    return 0;
}
