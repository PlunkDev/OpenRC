#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace openrc {

enum class GameId {
    unknown,
    ratchet_and_clank_2002,
};

struct DiscReport {
    std::filesystem::path image_path;
    std::uintmax_t image_size = 0;
    std::string filesystem;
    std::string volume_id;
    std::string system_cnf;
    std::string boot_path;
    std::string boot_iso_path;
    std::string serial;
    std::uint64_t boot_size = 0;
    std::string boot_sha256;
    GameId game = GameId::unknown;
    std::string title;
    std::string region;
    bool supported_build = false;
};

class DiscError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] DiscReport inspect_disc(const std::filesystem::path& image_path);
[[nodiscard]] std::string format_disc_report(const DiscReport& report);

} // namespace openrc
