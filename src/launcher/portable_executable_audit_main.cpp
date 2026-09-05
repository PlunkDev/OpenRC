#include "portable_executable.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

int main(const int argument_count, char **arguments) {
  if (argument_count != 2) {
    std::cerr << "usage: openrc-portable-executable-audit <executable>\n";
    return 2;
  }

  try {
    const auto result = openrc::launcher::check_runtime_executable(
        std::filesystem::path(arguments[1]));
    if (!result.accepted) {
      std::cerr << "portable executable audit failed for " << arguments[1]
                << ": " << result.detail << '\n';
      return 1;
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "portable executable audit could not inspect " << arguments[1]
              << ": " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "portable executable audit could not inspect " << arguments[1]
              << ": unknown error\n";
    return 1;
  }
}
