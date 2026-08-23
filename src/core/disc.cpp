#include "openrc/disc.hpp"

#include "iso9660.hpp"
#include "openrc/hash.hpp"
#include "openrc/paths.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <regex>
#include <span>
#include <sstream>
#include <string_view>

namespace openrc {
namespace {

constexpr std::uint64_t kMaximumSystemCnfSize = 64U * 1024U;
constexpr std::uint64_t kMaximumBootExecutableSize = 64U * 1024U * 1024U;

[[nodiscard]] bool ascii_space(const unsigned char character) {
    return character == ' ' || character == '\t' || character == '\r' ||
        character == '\n' || character == '\f' || character == '\v' ||
        character == 0;
}

[[nodiscard]] char upper_ascii_character(const char character) {
    const auto value = static_cast<unsigned char>(character);
    if (value >= static_cast<unsigned char>('a') &&
        value <= static_cast<unsigned char>('z')) {
        return static_cast<char>(value - static_cast<unsigned char>('a') +
            static_cast<unsigned char>('A'));
    }
    return character;
}

[[nodiscard]] std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](const char character) {
        return ascii_space(static_cast<unsigned char>(character));
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](const char character) {
        return ascii_space(static_cast<unsigned char>(character));
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

[[nodiscard]] std::string upper_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), upper_ascii_character);
    return value;
}

[[nodiscard]] bool ascii_iequals(
    const std::string_view left,
    const std::string_view right) {
    return left.size() == right.size() &&
        std::equal(left.begin(), left.end(), right.begin(), [](const char a, const char b) {
            return upper_ascii_character(a) == upper_ascii_character(b);
        });
}

[[nodiscard]] std::string remove_terminal_iso_version(std::string value) {
    const auto semicolon = value.rfind(';');
    if (semicolon == std::string::npos || semicolon + 1U == value.size()) {
        return value;
    }

    const auto version_begin = value.begin() + static_cast<std::ptrdiff_t>(semicolon + 1U);
    if (std::all_of(version_begin, value.end(), [](const unsigned char character) {
            return character >= static_cast<unsigned char>('0') &&
                character <= static_cast<unsigned char>('9');
        })) {
        value.erase(semicolon);
    }
    return value;
}

[[nodiscard]] const iso9660::File& find_root_system_cnf(const iso9660::Image& image) {
    const iso9660::File* result = nullptr;
    for (const auto& file : image.files()) {
        if (file.iso_path.empty() || file.iso_path.front() != '/' ||
            file.iso_path.find('/', 1) != std::string::npos) {
            continue;
        }

        if (!ascii_iequals(remove_terminal_iso_version(file.iso_path.substr(1)), "SYSTEM.CNF")) {
            continue;
        }
        if (result != nullptr) {
            throw DiscError("The disc contains conflicting root SYSTEM.CNF entries");
        }
        result = &file;
    }

    if (result == nullptr) {
        throw DiscError("SYSTEM.CNF was not found in the root of the disc image");
    }
    return *result;
}

void assign_boot_value(
    std::optional<std::string>& destination,
    std::string value,
    const std::string_view key) {
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = trim(value.substr(1, value.size() - 2));
    }
    if (value.empty()) {
        throw DiscError(std::string(key) + " in SYSTEM.CNF has an empty value");
    }
    if (destination && *destination != value) {
        throw DiscError(std::string("Conflicting ") + std::string(key) +
            " entries in SYSTEM.CNF");
    }
    destination = std::move(value);
}

[[nodiscard]] std::string find_boot_path(const std::string& system_cnf) {
    std::optional<std::string> boot;
    std::optional<std::string> boot2;
    std::istringstream lines(system_cnf);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const auto key = upper_ascii(trim(line.substr(0, equals)));
        if (key == "BOOT2") {
            assign_boot_value(boot2, line.substr(equals + 1), "BOOT2");
        } else if (key == "BOOT") {
            assign_boot_value(boot, line.substr(equals + 1), "BOOT");
        }
    }

    if (boot2) {
        return *boot2;
    }
    if (boot) {
        return *boot;
    }
    throw DiscError("SYSTEM.CNF does not contain a BOOT or BOOT2 entry");
}

[[nodiscard]] std::string iso_path_from_boot_path(std::string value) {
    value = trim(std::move(value));
    constexpr std::string_view prefix = "cdrom0:";
    if (value.size() < prefix.size() ||
        !ascii_iequals(std::string_view(value).substr(0, prefix.size()), prefix)) {
        throw DiscError("Unsupported BOOT device in SYSTEM.CNF");
    }
    value.erase(0, prefix.size());
    if (value.empty() || (value.front() != '\\' && value.front() != '/')) {
        throw DiscError("BOOT path is not absolute on the disc");
    }

    std::replace(value.begin(), value.end(), '\\', '/');
    while (!value.empty() && value.front() == '/') {
        value.erase(value.begin());
    }
    if (value.empty()) {
        throw DiscError("BOOT path does not name an executable");
    }

    std::istringstream components(value);
    std::string component;
    std::string result;
    while (std::getline(components, component, '/')) {
        if (component.empty() || component == "." || component == "..") {
            throw DiscError("BOOT path contains an invalid component");
        }
        if (std::any_of(component.begin(), component.end(), [](const unsigned char character) {
                return character < 0x20U || character == 0x7fU;
            })) {
            throw DiscError("BOOT path contains a control character");
        }
        result.push_back('/');
        result += component;
    }
    return result;
}

[[nodiscard]] const iso9660::File& find_boot_file(
    const iso9660::Image& image,
    const std::string_view wanted_path) {
    if (const auto* exact = image.find_exact(wanted_path)) {
        return *exact;
    }
    throw DiscError("The BOOT executable was not found at its exact disc path");
}

[[nodiscard]] std::string serial_from_boot_iso_path(const std::string& boot_iso_path) {
    const auto separator = boot_iso_path.find_last_of('/');
    const auto filename = separator == std::string::npos
        ? boot_iso_path
        : boot_iso_path.substr(separator + 1);
    static const std::regex serial_pattern(
        R"(([A-Za-z]{4})[_-]?(\d{3})\.(\d{2})(?:;[0-9]+)?)",
        std::regex::ECMAScript);
    std::smatch match;
    if (!std::regex_match(filename, match, serial_pattern)) {
        return {};
    }
    return upper_ascii(match[1].str()) + "-" + match[2].str() + match[3].str();
}

[[nodiscard]] std::string hash_iso_file(
    const iso9660::Image& image,
    const iso9660::File& file) {
    Sha256 hash;
    image.stream_file(file, [&hash](const std::span<const std::byte> bytes) {
        hash.update(bytes);
    });
    return hex_digest(hash.finish());
}

void identify_game(DiscReport& report) {
    if (report.serial == "SCES-50916") {
        report.game = GameId::ratchet_and_clank_2002;
        report.title = "Ratchet & Clank (2002)";
        report.region = "PAL";

        constexpr std::string_view supported_boot_sha256 =
            "17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b";
        report.supported_build =
            !supported_boot_sha256.empty() &&
            report.boot_sha256 == supported_boot_sha256;
    } else if (report.serial == "SCUS-97199") {
        report.game = GameId::ratchet_and_clank_2002;
        report.title = "Ratchet & Clank (2002)";
        report.region = "NTSC-U/C";
        report.supported_build = false;
    } else {
        report.title = "Unknown PlayStation 2 title";
        report.region = "Unknown";
        report.supported_build = false;
    }
}

[[nodiscard]] std::string yes_no(const bool value) {
    return value ? "yes" : "no";
}

} // namespace

DiscReport inspect_disc(const std::filesystem::path& image_path) {
    try {
        const auto image = iso9660::Image::open(image_path);
        const auto& system_file = find_root_system_cnf(image);
        const auto system_bytes = image.read_small_file(system_file, kMaximumSystemCnfSize);
        std::string system_cnf(
            reinterpret_cast<const char*>(system_bytes.data()),
            system_bytes.size());
        while (!system_cnf.empty() && system_cnf.back() == '\0') {
            system_cnf.pop_back();
        }

        DiscReport report;
        report.image_path = image_path;
        report.image_size = image.metadata().image_bytes;
        report.filesystem = "ISO 9660 (2048-byte logical blocks)";
        report.volume_id = image.metadata().volume_identifier;
        report.system_cnf = std::move(system_cnf);
        report.boot_path = find_boot_path(report.system_cnf);
        report.boot_iso_path = iso_path_from_boot_path(report.boot_path);

        const auto& boot_file = find_boot_file(image, report.boot_iso_path);
        report.boot_iso_path = boot_file.iso_path;
        if (boot_file.size > kMaximumBootExecutableSize) {
            throw DiscError("The BOOT executable is unexpectedly large");
        }
        report.boot_size = boot_file.size;
        report.boot_sha256 = hash_iso_file(image, boot_file);
        report.serial = serial_from_boot_iso_path(report.boot_iso_path);
        identify_game(report);
        return report;
    } catch (const DiscError&) {
        throw;
    } catch (const iso9660::Error& error) {
        throw DiscError(error.what());
    }
}

std::string format_disc_report(const DiscReport& report) {
    std::ostringstream output;
    output << "OpenRC disc report\n"
           << "Image:       " << path_to_utf8(report.image_path) << '\n'
           << "Image size:  " << report.image_size << " bytes\n"
           << "Filesystem:  " << report.filesystem << '\n'
           << "Volume ID:   " << (report.volume_id.empty() ? "<empty>" : report.volume_id) << '\n'
           << "Boot entry:  " << report.boot_path << '\n'
           << "Boot ISO:    " << report.boot_iso_path << '\n'
           << "Boot size:   " << report.boot_size << " bytes\n"
           << "Boot SHA-256:" << (report.boot_sha256.empty() ? " <unavailable>" : " " + report.boot_sha256) << '\n'
           << "Serial:      " << (report.serial.empty() ? "<unknown>" : report.serial) << '\n'
           << "Title:       " << report.title << '\n'
           << "Region:      " << report.region << '\n'
           << "Known game:  " << yes_no(report.game != GameId::unknown) << '\n'
           << "Supported ELF:" << (report.supported_build ? " yes" : " no") << '\n';
    return output.str();
}

} // namespace openrc
