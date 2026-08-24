#include "openrc/boundary_table.hpp"
#include "openrc/companion_wad_index.hpp"
#include "openrc/disc.hpp"
#include "openrc/disc_toc.hpp"
#include "openrc/dvp_vu.hpp"
#include "openrc/elf.hpp"
#include "openrc/map_art.hpp"
#include "openrc/paths.hpp"
#include "openrc/preparation.hpp"
#include "openrc/ps2_save_bundle.hpp"
#include "openrc/scene_block_directory.hpp"
#include "openrc/scene_block_vif.hpp"
#include "openrc/scene_block_vu.hpp"
#include "openrc/scene_block_vu_phase.hpp"
#include "openrc/sblk.hpp"
#include "openrc/sblk_audio.hpp"
#include "openrc/two_fip.hpp"
#include "openrc/vagp.hpp"
#include "openrc/wad.hpp"
#include "openrc/wad_bundle.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#endif

#ifndef OPENRC_VERSION
#define OPENRC_VERSION "0.1.0-dev"
#endif

namespace {

constexpr int kUsageError = 2;
constexpr int kOperationError = 3;
constexpr int kUnsupportedBuild = 4;
constexpr int kCancelled = 5;
constexpr std::uint64_t kMaximumCliDecodedWadBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliTwoFipPixels = 16U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliPs2SaveBundleBytes = 1U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliVagpBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCliVagpFrames = 1'000'000U;
constexpr std::uint64_t kMaximumCliVagpSamples =
    kMaximumCliVagpFrames * openrc::kPsAdpcmSamplesPerFrame;
constexpr std::uint64_t kMaximumCliElfBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumCliDvpVuListItems = 128U;
constexpr std::size_t kMapArtFirstGlobalSlot = 259;
constexpr std::size_t kPs2SaveBundleGlobalSlot = 1;

void print_usage() {
    std::cout
        << "OpenRC command-line tools " << OPENRC_VERSION << "\n\n"
        << "Usage:\n"
        << "  openrc-cli inspect <disc.iso>                    Inspect a PlayStation 2 disc image\n"
        << "  openrc-cli inventory <disc.iso>                  List files stored in a disc image\n"
        << "  openrc-cli toc <disc.iso>                        Inventory the Ratchet & Clank disc TOC\n"
        << "  openrc-cli toc-assets <disc.iso>                 Validate local TOC asset tables\n"
        << "  openrc-cli wad <disc.iso> <global-slot>          Decode one WadV1 (64 MiB cap)\n"
        << "  openrc-cli vagp <disc.iso> <global-slot> [output.wav]\n"
        << "                                                    Inspect/export VAGp as mono PCM\n"
        << "  openrc-cli boundary <disc.iso> <global-slot>     Inspect a seven-region payload table\n"
        << "  openrc-cli map-art <disc.iso> <level-id> [output.tga]\n"
        << "                                                    Inspect/export a 3-panel map preview\n"
        << "  openrc-cli ps2-save <disc.iso>                   Inspect the PS2D save/icon bundle\n"
        << "  openrc-cli sblk <disc.iso> <level-id>            Inspect a level SBlk audio bank\n"
        << "  openrc-cli scene-blocks <disc.iso> <level-id>    Inspect a level scene-block directory\n"
        << "  openrc-cli companion-wads <disc.iso> <level-id>  Inspect the terminal common WAD index\n"
        << "  openrc-cli wad-bundle <disc.iso> <lba> <sectors> Inspect a WadBundleV1 (64 MiB cap)\n"
        << "  openrc-cli twofip <disc.iso> <global-slot> [output.tga]\n"
        << "                                                    Inspect/export 2FIP (16 Mi pixels)\n"
        << "  openrc-cli prepare <disc.iso> [games-directory]  Extract and verify game files\n"
        << "  openrc-cli elf <executable>                      Inspect a PlayStation 2 ELF\n"
        << "  openrc-cli dvp-vu <elf> <entry-pairs> <overlay-sections>\n"
        << "                                                    Decode decimal CSV VU/ELF lists\n"
        << "  openrc-cli paths                                 Show application data directories\n"
        << "  openrc-cli help                                  Show this help\n";
}

[[nodiscard]] std::optional<std::uint64_t> parse_decimal_argument(
    const std::filesystem::path& argument) {
    const auto text = openrc::path_to_utf8(argument);
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(
        text.data(),
        text.data() + text.size(),
        value);
    if (text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<std::vector<std::uint16_t>>
parse_decimal_u16_list(
    const std::filesystem::path& argument,
    const std::size_t maximum_values) {
    const auto text = openrc::path_to_utf8(argument);
    if (text.empty() || maximum_values == 0U) {
        return std::nullopt;
    }

    std::vector<std::uint16_t> values;
    std::size_t begin = 0U;
    while (begin < text.size()) {
        const auto separator = text.find(',', begin);
        const auto end = separator == std::string::npos ? text.size()
                                                        : separator;
        std::uint32_t value = 0U;
        const auto parsed = std::from_chars(
            text.data() + begin,
            text.data() + end,
            value);
        if (begin == end || parsed.ec != std::errc{} ||
            parsed.ptr != text.data() + end ||
            value > std::numeric_limits<std::uint16_t>::max()) {
            return std::nullopt;
        }
        if (values.size() >= maximum_values) {
            return std::nullopt;
        }
        values.push_back(static_cast<std::uint16_t>(value));
        if (separator == std::string::npos) {
            break;
        }
        begin = separator + 1U;
    }
    if (values.empty() || text.back() == ',') {
        return std::nullopt;
    }
    return values;
}

[[nodiscard]] std::vector<std::byte> read_bounded_binary_file(
    const std::filesystem::path& path,
    const std::uint64_t maximum_bytes) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Cannot open the input file");
    }
    const auto end_position = input.tellg();
    if (end_position < 0) {
        throw std::runtime_error("Cannot determine the input file size");
    }
    const auto byte_count = static_cast<std::uint64_t>(end_position);
    if (byte_count > maximum_bytes ||
        byte_count > std::numeric_limits<std::size_t>::max() ||
        byte_count > static_cast<std::uint64_t>(
                         std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("The input file exceeds the CLI size limit");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(byte_count));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty() &&
        !input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Cannot read the complete input file");
    }
    return bytes;
}

[[nodiscard]] const char* wad_bundle_kind_name(
    const openrc::WadBundleRecordKind kind) noexcept {
    switch (kind) {
    case openrc::WadBundleRecordKind::empty:
        return "empty";
    case openrc::WadBundleRecordKind::nested_wad:
        return "WAD";
    case openrc::WadBundleRecordKind::elf:
        return "ELF";
    }
    return "unknown";
}

[[nodiscard]] std::string escaped_terminal_text(
    const std::string_view text) {
    constexpr std::array<char, 16> kHexDigits{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    };
    std::string escaped;
    escaped.reserve(text.size());
    for (const auto character : text) {
        const auto value = static_cast<unsigned char>(character);
        if (value >= 0x20U && value <= 0x7eU) {
            escaped.push_back(character);
            continue;
        }
        escaped.push_back('\\');
        escaped.push_back('x');
        escaped.push_back(kHexDigits[value >> 4U]);
        escaped.push_back(kHexDigits[value & 0x0fU]);
    }
    return escaped;
}

[[nodiscard]] std::vector<std::byte> read_disc_extent(
    const std::filesystem::path& image_path,
    const std::uint64_t logical_block,
    const std::uint64_t sector_count,
    const std::uint64_t maximum_bytes) {
    if (sector_count > maximum_bytes / openrc::kDiscTocSectorSize) {
        throw std::runtime_error("The requested disc extent exceeds the CLI size limit");
    }
    if (logical_block >
        std::numeric_limits<std::uint64_t>::max() / openrc::kDiscTocSectorSize) {
        throw std::runtime_error("The requested disc extent offset overflows");
    }
    const auto offset = logical_block * openrc::kDiscTocSectorSize;
    const auto byte_count = sector_count * openrc::kDiscTocSectorSize;
    if (byte_count > std::numeric_limits<std::size_t>::max() ||
        offset > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max())) {
        throw std::runtime_error("The requested disc extent exceeds host limits");
    }

    std::ifstream input(image_path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Cannot open the disc image");
    }
    const auto end_position = input.tellg();
    if (end_position < 0) {
        throw std::runtime_error(
            "Cannot determine the disc image size from the open stream");
    }
    const auto image_bytes = static_cast<std::uint64_t>(end_position);
    if (offset > image_bytes || byte_count > image_bytes - offset) {
        throw std::runtime_error("The requested disc extent lies outside the image");
    }

    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        throw std::runtime_error("Cannot seek to the requested disc extent");
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(byte_count));
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        throw std::runtime_error("Unexpected end of image while reading the disc extent");
    }
    return bytes;
}

void write_new_binary_file(
    const std::filesystem::path& output_path,
    const std::span<const std::byte> bytes) {
    if (output_path.empty()) {
        throw std::runtime_error("The output path is empty");
    }

#ifdef _WIN32
    const auto handle = CreateFileW(
        output_path.c_str(),
        GENERIC_WRITE | DELETE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
            throw std::runtime_error(
                "The output path already exists; refusing to overwrite it");
        }
        throw std::runtime_error(
            "Cannot exclusively create the output file (Windows error " +
            std::to_string(error) + ")");
    }

    std::size_t offset = 0;
    DWORD write_error = ERROR_SUCCESS;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto requested = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (WriteFile(
                handle,
                bytes.data() + offset,
                requested,
                &written,
                nullptr) == FALSE ||
            written != requested) {
            write_error = GetLastError();
            if (write_error == ERROR_SUCCESS) {
                write_error = ERROR_WRITE_FAULT;
            }
            break;
        }
        offset += written;
    }
    if (write_error == ERROR_SUCCESS && FlushFileBuffers(handle) == FALSE) {
        write_error = GetLastError();
    }
    if (write_error != ERROR_SUCCESS) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        const bool cleanup_scheduled = SetFileInformationByHandle(
            handle,
            FileDispositionInfo,
            &disposition,
            sizeof(disposition)) != FALSE;
        CloseHandle(handle);
        if (!cleanup_scheduled) {
            throw std::runtime_error(
                "Cannot write the complete output file, and Windows could not "
                "schedule the partial file for removal (write error " +
                std::to_string(write_error) + ")");
        }
        throw std::runtime_error(
            "Cannot write the complete output file (Windows error " +
            std::to_string(write_error) + ")");
    }
    if (CloseHandle(handle) == FALSE) {
        throw std::runtime_error(
            "Cannot close the completed output file (Windows error " +
            std::to_string(GetLastError()) + ")");
    }
#else
    auto temporary_template = output_path.string() + ".tmp-XXXXXX";
    std::vector<char> temporary_name(
        temporary_template.begin(),
        temporary_template.end());
    temporary_name.push_back('\0');
    const auto descriptor = ::mkstemp(temporary_name.data());
    if (descriptor < 0) {
        throw std::runtime_error(
            "Cannot create a unique temporary output file: " +
            std::string(std::strerror(errno)));
    }
    const std::filesystem::path temporary_path(temporary_name.data());
#ifdef FD_CLOEXEC
    if (::fcntl(descriptor, F_SETFD, FD_CLOEXEC) != 0) {
        const auto flag_error = errno;
        (void)::unlink(temporary_path.c_str());
        (void)::close(descriptor);
        throw std::runtime_error(
            "Cannot protect the temporary output descriptor: " +
            std::string(std::strerror(flag_error)));
    }
#endif

    std::size_t offset = 0;
    int write_error = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto requested = std::min<std::size_t>(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const auto written = ::write(
            descriptor,
            bytes.data() + offset,
            requested);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            write_error = errno;
            break;
        }
        if (written == 0) {
            write_error = EIO;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (write_error == 0 && ::fsync(descriptor) != 0) {
        write_error = errno;
    }
    if (::close(descriptor) != 0 && write_error == 0) {
        write_error = errno;
    }
    if (write_error != 0) {
        const bool removed = ::unlink(temporary_path.c_str()) == 0;
        const auto cleanup_error = removed ? 0 : errno;
        if (!removed) {
            throw std::runtime_error(
                "Cannot write the complete output file, and the unique "
                "temporary file could not be removed: " +
                std::string(std::strerror(cleanup_error)));
        }
        throw std::runtime_error(
            "Cannot write the complete output file: " +
            std::string(std::strerror(write_error)));
    }

    if (::link(temporary_path.c_str(), output_path.c_str()) != 0) {
        const auto publish_error = errno;
        const bool removed = ::unlink(temporary_path.c_str()) == 0;
        if (publish_error == EEXIST) {
            if (!removed) {
                throw std::runtime_error(
                    "The output path already exists, and the unique temporary "
                    "file could not be removed");
            }
            throw std::runtime_error(
                "The output path already exists; refusing to overwrite it");
        }
        throw std::runtime_error(
            std::string("Cannot atomically publish the output file") +
            (removed ? ": " : ", and the temporary file could not be removed: ") +
            std::strerror(publish_error));
    }
    if (::unlink(temporary_path.c_str()) != 0) {
        throw std::runtime_error(
            "The output was published, but its temporary hard link could not "
            "be removed: " + std::string(std::strerror(errno)));
    }
#endif
}

[[nodiscard]] const char* phase_name(const openrc::PreparationPhase phase) {
    switch (phase) {
    case openrc::PreparationPhase::scanning:
        return "scanning disc";
    case openrc::PreparationPhase::hashing_image:
        return "hashing image";
    case openrc::PreparationPhase::verifying_files:
        return "verifying files";
    case openrc::PreparationPhase::extracting_files:
        return "extracting files";
    case openrc::PreparationPhase::writing_manifest:
        return "writing manifest";
    case openrc::PreparationPhase::finalizing:
        return "finalizing";
    }
    return "working";
}

class ProgressPrinter final {
public:
    [[nodiscard]] bool update(const openrc::PreparationProgress& progress) {
        const auto now = std::chrono::steady_clock::now();
        const bool phase_changed = !last_phase_ || *last_phase_ != progress.phase;
        const bool completed = progress.total_bytes != 0 &&
                               progress.bytes_processed >= progress.total_bytes;
        const bool completion_changed = completed && !last_was_complete_;

        if (!phase_changed && !completion_changed &&
            now - last_update_ < std::chrono::milliseconds(250)) {
            return true;
        }

        if (phase_changed && has_line_) {
            std::cerr << '\n';
            line_width_ = 0;
        }

        std::ostringstream line;
        line << '[' << phase_name(progress.phase) << ']';

        if (progress.total_bytes != 0) {
            const auto bounded = std::min(progress.bytes_processed, progress.total_bytes);
            const double percent = 100.0 * static_cast<double>(bounded) /
                                   static_cast<double>(progress.total_bytes);
            line << ' ' << std::fixed << std::setprecision(1) << percent << "% ("
                 << progress.bytes_processed << '/' << progress.total_bytes << " bytes)";
        } else if (progress.bytes_processed != 0) {
            line << ' ' << progress.bytes_processed << " bytes";
        }

        if (progress.file_count != 0) {
            line << " file " << progress.file_index << '/' << progress.file_count;
        }
        if (!progress.current_path.empty()) {
            line << "  " << progress.current_path;
        }

        const std::string text = line.str();
        std::cerr << '\r' << text;
        if (text.size() < line_width_) {
            std::cerr << std::string(line_width_ - text.size(), ' ');
        }
        std::cerr << std::flush;

        line_width_ = text.size();
        has_line_ = true;
        last_phase_ = progress.phase;
        last_update_ = now;
        last_was_complete_ = completed;
        return true;
    }

    void finish() {
        if (has_line_) {
            std::cerr << '\n';
            has_line_ = false;
            line_width_ = 0;
        }
    }

private:
    std::optional<openrc::PreparationPhase> last_phase_;
    std::chrono::steady_clock::time_point last_update_{};
    std::size_t line_width_ = 0;
    bool has_line_ = false;
    bool last_was_complete_ = false;
};

[[nodiscard]] std::string hexadecimal(const std::uint64_t value, const int width = 0) {
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << std::setfill('0');
    if (width > 0) {
        output << std::setw(width);
    }
    output << value;
    return output.str();
}

int run(const std::vector<std::filesystem::path>& arguments) {
    const std::string command =
        arguments.empty() ? std::string{} : openrc::path_to_utf8(arguments[0]);

    if (arguments.empty() || command == "help" || command == "--help" || command == "-h") {
        print_usage();
        return 0;
    }

    if (command == "inspect") {
        if (arguments.size() != 2) {
            std::cerr << "error: inspect expects exactly one ISO path\n";
            return kUsageError;
        }

        try {
            const auto report = openrc::inspect_disc(arguments[1]);
            std::cout << openrc::format_disc_report(report);
            return report.supported_build ? 0 : kUnsupportedBuild;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "inventory") {
        if (arguments.size() != 2) {
            std::cerr << "error: inventory expects exactly one ISO path\n";
            return kUsageError;
        }

        try {
            const auto inventory = openrc::inventory_disc(arguments[1]);
            std::cout << openrc::format_disc_report(inventory.disc)
                      << "Inventory files: " << inventory.files.size() << '\n'
                      << "Total file bytes: " << inventory.total_file_bytes << "\n\n";

            for (const auto& file : inventory.files) {
                std::cout << file.iso_path << " -> " << file.output_path << '\n'
                          << "  size: " << file.size << " bytes; extents:";
                if (file.extents.empty()) {
                    std::cout << " none";
                } else {
                    for (const auto& extent : file.extents) {
                        std::cout << " [LBA " << extent.logical_block
                                  << ", " << extent.byte_length << " bytes]";
                    }
                }
                std::cout << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "toc") {
        if (arguments.size() != 2) {
            std::cerr << "error: toc expects exactly one ISO path\n";
            return kUsageError;
        }

        try {
            const auto report = openrc::inspect_disc_toc(arguments[1]);
            std::array<std::size_t, 5> signature_counts{};
            for (const auto& entry : report.global_extents) {
                ++signature_counts[static_cast<std::size_t>(entry.signature)];
            }

            std::cout
                << "OpenRC Ratchet & Clank disc TOC\n"
                << "Image:                " << openrc::path_to_utf8(report.image_path) << '\n'
                << "Image sectors:        " << report.image_sectors << '\n'
                << "Declared sectors:     " << report.declared_volume_sectors << '\n'
                << "TOC LBA:              " << openrc::kDiscTocGlobalLba << '\n'
                << "TOC version:          " << report.version << '\n'
                << "TOC bytes:            " << report.byte_size << '\n'
                << "Global extent slots:  " << report.global_extent_slot_count << '\n'
                << "Used global extents:  " << report.global_extents.size() << '\n'
                << "Empty global slots:   " << report.empty_global_extent_slot_count << '\n'
                << "Level descriptors:    " << report.levels.size() << "\n\n"
                << "Signatures:\n"
                << "  WAD:   "
                << signature_counts[static_cast<std::size_t>(openrc::DiscTocSignature::wad)]
                << '\n'
                << "  VAGp:  "
                << signature_counts[static_cast<std::size_t>(openrc::DiscTocSignature::vagp)]
                << '\n'
                << "  2FIP:  "
                << signature_counts[static_cast<std::size_t>(openrc::DiscTocSignature::two_fip)]
                << '\n'
                << "  PS2D:  "
                << signature_counts[static_cast<std::size_t>(openrc::DiscTocSignature::ps2d)]
                << '\n'
                << "  other: "
                << signature_counts[static_cast<std::size_t>(openrc::DiscTocSignature::other)]
                << "\n\nGlobal extents:\n";

            for (const auto& entry : report.global_extents) {
                const auto bytes =
                    static_cast<std::uint64_t>(entry.extent.sectors) *
                    openrc::kDiscTocSectorSize;
                std::cout
                    << "  [" << entry.slot << "] LBA " << entry.extent.lba
                    << ", " << entry.extent.sectors << " sectors, "
                    << bytes << " bytes, "
                    << openrc::disc_toc_signature_name(entry.signature) << '\n';
            }

            std::cout << "\nLevel TOCs:\n";
            for (const auto& level : report.levels) {
                std::cout
                    << "  level " << level.level_id
                    << ": TOC LBA " << level.toc_lba
                    << ", auxiliary " << level.auxiliary
                    << ", primary extents:";
                for (const auto& extent : level.primary_extents) {
                    std::cout
                        << " [LBA " << extent.lba
                        << ", " << extent.sectors << " sectors]";
                }
                std::cout << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "toc-assets") {
        if (arguments.size() != 2) {
            std::cerr << "error: toc-assets expects exactly one ISO path\n";
            return kUsageError;
        }

        try {
            const auto report = openrc::inspect_disc_toc_assets(arguments[1]);
            std::size_t vag_references = 0;
            std::size_t run_wads = 0;
            std::size_t terminal_zero_sectors = 0;
            std::size_t extent0_subranges = 0;
            std::size_t extent0_wads = 0;
            std::size_t primary_wads = 0;
            std::size_t opaque_pairs = 0;

            std::cout
                << "OpenRC local TOC asset report\n"
                << "Image:                  "
                << openrc::path_to_utf8(report.layout.image_path) << '\n'
                << "Levels:                 " << report.levels.size() << "\n\n"
                << "Per-level counts:\n";
            for (const auto& level : report.levels) {
                std::size_t level_run_wads = 0;
                std::size_t level_terminal_sectors = 0;
                for (const auto& block : level.local_tables.resource_blocks) {
                    for (const auto& run : block.wad_runs) {
                        level_run_wads += run.wads.size();
                        if (run.trailing_zero_sector_lba) {
                            ++level_terminal_sectors;
                        }
                    }
                }

                const auto level_extent0_wads = static_cast<std::size_t>(std::count_if(
                    level.primary_extent0.subranges.begin(),
                    level.primary_extent0.subranges.end(),
                    [](const openrc::DiscTocSubrange& subrange) {
                        return subrange.byte_size != 0 &&
                            subrange.signature == openrc::DiscTocSignature::wad;
                    }));
                std::size_t level_opaque_pairs = 0;
                for (const auto& table : level.primary_extent3.tables) {
                    level_opaque_pairs += table.size();
                }

                vag_references += level.referenced_vags.size();
                run_wads += level_run_wads;
                terminal_zero_sectors += level_terminal_sectors;
                extent0_subranges += level.primary_extent0.used_subrange_count;
                extent0_wads += level_extent0_wads;
                primary_wads += level.primary_wads.size();
                opaque_pairs += level_opaque_pairs;

                std::cout
                    << "  level " << std::setw(2) << level.level_id
                    << ": VAG " << level.referenced_vags.size()
                    << ", run WAD " << level_run_wads
                    << ", sentinels " << level_terminal_sectors
                    << ", extent0 " << level.primary_extent0.used_subrange_count
                    << " (" << level_extent0_wads << " WAD)"
                    << ", extent3 pairs " << level_opaque_pairs << '\n';
            }

            std::cout
                << "\nValidated totals:\n"
                << "  VAG references:        " << vag_references << '\n'
                << "  WAD-run records:       " << run_wads << '\n'
                << "  terminal zero sectors: " << terminal_zero_sectors << '\n'
                << "  extent0 subranges:     " << extent0_subranges << '\n'
                << "  extent0 WAD records:   " << extent0_wads << '\n'
                << "  primary WAD records:   " << primary_wads << '\n'
                << "  extent3 opaque pairs:  " << opaque_pairs << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "wad") {
        if (arguments.size() != 3) {
            std::cerr << "error: wad expects an ISO path and a global TOC slot\n";
            return kUsageError;
        }

        const auto requested_slot_value = parse_decimal_argument(arguments[2]);
        if (!requested_slot_value ||
            *requested_slot_value >= openrc::kDiscTocGlobalExtentSlotCount) {
            std::cerr << "error: global TOC slot must be a decimal number from 0 to "
                      << (openrc::kDiscTocGlobalExtentSlotCount - 1U) << '\n';
            return kUsageError;
        }
        const auto requested_slot = *requested_slot_value;

        try {
            const auto toc = openrc::inspect_disc_toc(arguments[1]);
            const auto entry = std::find_if(
                toc.global_extents.begin(),
                toc.global_extents.end(),
                [requested_slot](const openrc::DiscTocGlobalExtent& candidate) {
                    return candidate.slot == requested_slot;
                });
            if (entry == toc.global_extents.end()) {
                std::cerr << "error: global TOC slot " << requested_slot
                          << " is empty\n";
                return kOperationError;
            }
            if (entry->signature != openrc::DiscTocSignature::wad) {
                std::cerr << "error: global TOC slot " << requested_slot
                          << " is " << openrc::disc_toc_signature_name(entry->signature)
                          << ", not WAD\n";
                return kOperationError;
            }

            const auto decoded = openrc::decode_wad(
                arguments[1],
                entry->extent.lba,
                entry->extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto& report = decoded.source;
            std::cout
                << "OpenRC WadV1 report\n"
                << "Image:               " << openrc::path_to_utf8(report.image_path) << '\n'
                << "Global TOC slot:     " << requested_slot << '\n'
                << "LBA:                 " << report.logical_block << '\n'
                << "Sectors:             " << report.sector_count << '\n'
                << "Extent bytes:        " << report.extent_bytes << '\n'
                << "Compressed bytes:    " << report.total_bytes << '\n'
                << "Compressed payload:  " << report.compressed_payload_bytes << '\n'
                << "Sector padding:      " << report.padding_bytes << '\n'
                << "Compressed SHA-256:  " << report.sha256 << '\n'
                << "Decoded bytes:       " << decoded.bytes.size() << '\n'
                << "Decoded SHA-256:     " << decoded.sha256 << '\n'
                << "Auxiliary bytes:     ";
            for (const auto value : report.auxiliary_bytes) {
                std::cout << std::hex << std::uppercase << std::setfill('0')
                          << std::setw(2) << static_cast<unsigned int>(value);
            }
            std::cout << std::dec << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "vagp") {
        if (arguments.size() < 3 || arguments.size() > 4) {
            std::cerr
                << "error: vagp expects an ISO path, global TOC slot, "
                   "and optional WAV output path\n";
            return kUsageError;
        }

        const auto requested_slot_value = parse_decimal_argument(arguments[2]);
        if (!requested_slot_value ||
            *requested_slot_value >= openrc::kDiscTocGlobalExtentSlotCount) {
            std::cerr << "error: global TOC slot must be a decimal number from 0 to "
                      << (openrc::kDiscTocGlobalExtentSlotCount - 1U) << '\n';
            return kUsageError;
        }
        const auto requested_slot = *requested_slot_value;

        try {
            const auto toc = openrc::inspect_disc_toc(arguments[1]);
            const auto entry = std::find_if(
                toc.global_extents.begin(),
                toc.global_extents.end(),
                [requested_slot](const openrc::DiscTocGlobalExtent& candidate) {
                    return candidate.slot == requested_slot;
                });
            if (entry == toc.global_extents.end()) {
                throw std::runtime_error(
                    "Global TOC slot " + std::to_string(requested_slot) + " is empty");
            }
            if (entry->signature != openrc::DiscTocSignature::vagp) {
                throw std::runtime_error(
                    "Global TOC slot " + std::to_string(requested_slot) +
                    " is " +
                    std::string(openrc::disc_toc_signature_name(entry->signature)) +
                    ", not VAGp");
            }

            const auto extent_bytes = read_disc_extent(
                arguments[1],
                entry->extent.lba,
                entry->extent.sectors,
                kMaximumCliVagpBytes);
            const auto report = openrc::parse_vagp_v1(
                extent_bytes,
                openrc::VagpLimits{
                    kMaximumCliVagpBytes,
                    kMaximumCliVagpBytes,
                    kMaximumCliVagpFrames,
                    kMaximumCliVagpSamples});
            const auto content_frames =
                report.content_end_frame - report.content_begin_frame;
            const auto content_samples =
                content_frames * openrc::kPsAdpcmSamplesPerFrame;

            std::cout
                << "OpenRC VAGp V1 report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Global TOC slot:     " << requested_slot << '\n'
                << "LBA:                 " << entry->extent.lba << '\n'
                << "Sectors:             " << entry->extent.sectors << '\n'
                << "Extent bytes:        " << report.input_bytes << '\n'
                << "Logical bytes:       " << report.logical_bytes << '\n'
                << "Payload bytes:       " << report.payload_bytes << '\n'
                << "Sector padding:      " << report.padding_bytes << '\n'
                << "Version:             " << hexadecimal(report.version, 8) << '\n'
                << "Sample rate:         " << report.sample_rate << " Hz\n"
                << "Name:                "
                << (report.display_name.empty()
                        ? std::string("<empty>")
                        : escaped_terminal_text(report.display_name))
                << '\n'
                << "ADPCM frames:        " << report.frame_count << '\n'
                << "Linear samples:      " << report.sample_count << '\n'
                << "Content frame range: [" << report.content_begin_frame
                << ", " << report.content_end_frame << ")\n"
                << "Content frames:      " << content_frames << '\n'
                << "Content samples:     " << content_samples << '\n';

            if (arguments.size() == 4) {
                const auto wav = openrc::encode_vagp_pcm16_mono_wav(
                    report,
                    kMaximumCliVagpBytes);
                write_new_binary_file(arguments[3], wav);
                std::cout
                    << "WAV output:          "
                    << openrc::path_to_utf8(arguments[3]) << '\n'
                    << "WAV bytes:           " << wav.size() << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "wad-bundle") {
        if (arguments.size() != 4) {
            std::cerr << "error: wad-bundle expects an ISO path, LBA, and sector count\n";
            return kUsageError;
        }

        const auto logical_block = parse_decimal_argument(arguments[2]);
        const auto sector_count = parse_decimal_argument(arguments[3]);
        if (!logical_block || !sector_count || *sector_count == 0) {
            std::cerr << "error: LBA and sector count must be unsigned decimal numbers, "
                         "and sector count must be non-zero\n";
            return kUsageError;
        }

        try {
            const auto decoded = openrc::decode_wad(
                arguments[1],
                *logical_block,
                *sector_count,
                kMaximumCliDecodedWadBytes);
            const auto bundle = openrc::parse_wad_bundle_v1(decoded.bytes);
            const auto nested_count = 1U + static_cast<unsigned int>(std::count_if(
                bundle.slots.begin(),
                bundle.slots.end(),
                [](const openrc::WadBundleRecord& record) {
                    return record.kind == openrc::WadBundleRecordKind::nested_wad;
                }));
            const auto elf_count = std::count_if(
                bundle.slots.begin(),
                bundle.slots.end(),
                [](const openrc::WadBundleRecord& record) {
                    return record.kind == openrc::WadBundleRecordKind::elf;
                });
            const auto empty_count = std::count_if(
                bundle.slots.begin(),
                bundle.slots.end(),
                [](const openrc::WadBundleRecord& record) {
                    return record.kind == openrc::WadBundleRecordKind::empty;
                });

            std::cout
                << "OpenRC WadBundleV1 report\n"
                << "Image:               " << openrc::path_to_utf8(decoded.source.image_path) << '\n'
                << "LBA:                 " << decoded.source.logical_block << '\n'
                << "Sectors:             " << decoded.source.sector_count << '\n'
                << "Compressed bytes:    " << decoded.source.total_bytes << '\n'
                << "Compressed SHA-256:  " << decoded.source.sha256 << '\n'
                << "Decoded bytes:       " << bundle.decoded_size << '\n'
                << "Decoded SHA-256:     " << decoded.sha256 << '\n'
                << "Header bytes:        " << bundle.header_size << '\n'
                << "Nested WAD records:  " << nested_count << '\n'
                << "ELF records:         " << elf_count << '\n'
                << "Empty slots:         " << empty_count << "\n\n"
                << "Records:\n"
                << "  initial  WAD  offset " << bundle.initial_record.offset
                << ", size " << bundle.initial_record.size << '\n';
            for (std::size_t index = 0; index < bundle.slots.size(); ++index) {
                const auto& record = bundle.slots[index];
                std::cout << "  slot " << std::setw(2) << index << "  "
                          << std::setw(5) << wad_bundle_kind_name(record.kind);
                if (record.kind != openrc::WadBundleRecordKind::empty) {
                    std::cout << "  offset " << record.offset << ", size " << record.size;
                }
                std::cout << '\n';
                if (record.kind == openrc::WadBundleRecordKind::elf) {
                    const auto record_bytes = std::span<const std::byte>(
                        decoded.bytes.data() + record.offset,
                        record.size);
                    const auto elf = openrc::inspect_elf(record_bytes);
                    if (elf.iop_module_info) {
                        std::cout
                            << "           IOP module "
                            << elf.iop_module_info->name
                            << ", version "
                            << hexadecimal(elf.iop_module_info->version, 4)
                            << ", imports " << elf.iop_import_libraries.size()
                            << ", relocation sections "
                            << elf.relocation_summaries.size() << '\n';
                    }
                }
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "boundary") {
        if (arguments.size() != 3) {
            std::cerr
                << "error: boundary expects an ISO path and a global TOC slot\n";
            return kUsageError;
        }

        const auto requested_slot_value = parse_decimal_argument(arguments[2]);
        if (!requested_slot_value ||
            *requested_slot_value >= openrc::kDiscTocGlobalExtentSlotCount) {
            std::cerr << "error: global TOC slot must be a decimal number from 0 to "
                      << (openrc::kDiscTocGlobalExtentSlotCount - 1U) << '\n';
            return kUsageError;
        }
        const auto requested_slot = *requested_slot_value;

        try {
            const auto toc = openrc::inspect_disc_toc(arguments[1]);
            const auto entry = std::find_if(
                toc.global_extents.begin(),
                toc.global_extents.end(),
                [requested_slot](const openrc::DiscTocGlobalExtent& candidate) {
                    return candidate.slot == requested_slot;
                });
            if (entry == toc.global_extents.end()) {
                throw std::runtime_error(
                    "Global TOC slot " + std::to_string(requested_slot) + " is empty");
            }
            if (entry->signature != openrc::DiscTocSignature::wad) {
                throw std::runtime_error(
                    "Global TOC slot " + std::to_string(requested_slot) +
                    " is not a WadV1 record");
            }

            const auto decoded = openrc::decode_wad(
                arguments[1],
                entry->extent.lba,
                entry->extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto table = openrc::parse_boundary_table(
                decoded.bytes,
                openrc::BoundaryTableLimits{
                    kMaximumCliDecodedWadBytes,
                    kMaximumCliDecodedWadBytes});

            std::cout
                << "OpenRC boundary-table report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Global TOC slot:     " << requested_slot << '\n'
                << "LBA:                 " << entry->extent.lba << '\n'
                << "Sectors:             " << entry->extent.sectors << '\n'
                << "Decoded bytes:       " << table.input_size << '\n'
                << "Decoded SHA-256:     " << decoded.sha256 << "\n\n"
                << "Regions:\n";
            for (std::size_t index = 0; index < table.regions.size(); ++index) {
                const auto& region = table.regions[index];
                std::cout
                    << "  [" << index << "] offset "
                    << hexadecimal(region.offset, 8)
                    << ", size " << region.size << " bytes\n";
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "sblk") {
        if (arguments.size() != 3) {
            std::cerr << "error: sblk expects an ISO path and a level ID\n";
            return kUsageError;
        }

        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto assets = openrc::inspect_disc_toc_assets(arguments[1]);
            const auto level_assets = std::find_if(
                assets.levels.begin(),
                assets.levels.end(),
                [level_id](const openrc::DiscTocLevelAssets& candidate) {
                    return candidate.level_id == level_id;
                });
            const auto level_layout = std::find_if(
                assets.layout.levels.begin(),
                assets.layout.levels.end(),
                [level_id](const openrc::DiscTocLevelDescriptor& candidate) {
                    return candidate.level_id == level_id;
                });
            if (level_assets == assets.levels.end() ||
                level_layout == assets.layout.levels.end()) {
                throw std::runtime_error("The requested level is absent from DiscTocV1");
            }

            constexpr std::size_t kSBlkPrimarySubrangeIndex = 1;
            const auto& subrange =
                level_assets->primary_extent0.subranges[kSBlkPrimarySubrangeIndex];
            if (subrange.byte_size == 0) {
                throw std::runtime_error("The level's SBlk subrange is empty");
            }
            const auto& primary_extent =
                level_layout->primary_extents.front();
            const auto primary_bytes = read_disc_extent(
                arguments[1],
                primary_extent.lba,
                primary_extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto subrange_offset =
                static_cast<std::uint64_t>(subrange.relative_offset);
            const auto subrange_size =
                static_cast<std::uint64_t>(subrange.byte_size);
            if (subrange_offset > primary_bytes.size() ||
                subrange_size > primary_bytes.size() - subrange_offset) {
                throw std::runtime_error(
                    "The SBlk subrange lies outside primary extent 0");
            }

            const auto bundle = openrc::parse_sblk_bundle_v3(
                std::span<const std::byte>(primary_bytes).subspan(
                    static_cast<std::size_t>(subrange_offset),
                    static_cast<std::size_t>(subrange_size)),
                openrc::SBlkLimits{
                    kMaximumCliDecodedWadBytes,
                    1'000'000U,
                    1'000'000U,
                    kMaximumCliDecodedWadBytes});
            const auto audio = openrc::analyze_sblk_audio_v1(
                bundle,
                openrc::SBlkAudioLimits{
                    1'000'000U,
                    1'000'000U,
                    kMaximumCliDecodedWadBytes});
            std::uint64_t looped_blocks = 0;
            std::uint64_t padded_loops = 0;
            std::uint64_t retuned_blocks = 0;
            for (const auto& block : audio.blocks) {
                if (block.kind == openrc::SBlkAudioBlockKind::looped) {
                    ++looped_blocks;
                    if (block.trailing_padding_frame_count != 0U) {
                        ++padded_loops;
                    }
                }
                if (!block.references.empty()) {
                    const auto first_note = block.references.front().center_note;
                    const auto first_fine = block.references.front().center_fine;
                    if (std::any_of(
                            block.references.begin() + 1,
                            block.references.end(),
                            [first_note, first_fine](
                                const openrc::SBlkAudioReference& reference) {
                                return reference.center_note != first_note ||
                                    reference.center_fine != first_fine;
                            })) {
                        ++retuned_blocks;
                    }
                }
            }
            const auto one_shot_blocks =
                static_cast<std::uint64_t>(audio.block_count) - looped_blocks;

            std::cout
                << "OpenRC SBlkBundleV3 report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:            " << level_id << '\n'
                << "Primary extent LBA:  " << primary_extent.lba << '\n'
                << "Primary sectors:     " << primary_extent.sectors << '\n'
                << "Subrange offset:     "
                << hexadecimal(subrange.relative_offset, 8) << '\n'
                << "Subrange bytes:      " << subrange.byte_size << '\n'
                << "SBlk record:         offset "
                << hexadecimal(bundle.sblk_record.offset, 8)
                << ", size " << bundle.sblk_record.size << '\n'
                << "Secondary record:    offset "
                << hexadecimal(bundle.secondary_record.offset, 8)
                << ", size " << bundle.secondary_record.size << '\n'
                << "Opaque A/B:          "
                << hexadecimal(bundle.opaque_a, 8) << " / "
                << hexadecimal(bundle.opaque_b, 8) << '\n'
                << "Descriptor table:    "
                << hexadecimal(bundle.descriptor_table_offset, 8)
                << " - " << hexadecimal(bundle.descriptor_table_end, 8)
                << " (end exclusive)\n"
                << "Descriptors:         " << bundle.descriptors.size() << '\n'
                << "Items:               " << audio.item_count << '\n'
                << "Audio references:    " << audio.reference_count << '\n'
                << "Audio blocks:        " << audio.block_count << '\n'
                << "One-shot blocks:     " << one_shot_blocks << '\n'
                << "Looped blocks:       " << looped_blocks << '\n'
                << "Padded loops:        " << padded_loops << '\n'
                << "Retuned blocks:      " << retuned_blocks << '\n'
                << "Shared references:   "
                << (audio.reference_count - audio.block_count) << '\n'
                << "ADPCM frames:        " << audio.frame_count << '\n'
                << "Item bytes:          " << bundle.item_bytes.size() << '\n'
                << "Secondary bytes:     " << bundle.secondary_bytes.size() << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "scene-blocks") {
        if (arguments.size() != 3) {
            std::cerr
                << "error: scene-blocks expects an ISO path and a level ID\n";
            return kUsageError;
        }

        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto assets = openrc::inspect_disc_toc_assets(arguments[1]);
            const auto level_assets = std::find_if(
                assets.levels.begin(),
                assets.levels.end(),
                [level_id](const openrc::DiscTocLevelAssets& candidate) {
                    return candidate.level_id == level_id;
                });
            const auto level_layout = std::find_if(
                assets.layout.levels.begin(),
                assets.layout.levels.end(),
                [level_id](const openrc::DiscTocLevelDescriptor& candidate) {
                    return candidate.level_id == level_id;
                });
            if (level_assets == assets.levels.end() ||
                level_layout == assets.layout.levels.end()) {
                throw std::runtime_error("The requested level is absent from DiscTocV1");
            }

            constexpr std::size_t kSceneBlockSubrangeIndex = 10U;
            const auto& subrange =
                level_assets->primary_extent0.subranges[kSceneBlockSubrangeIndex];
            if (subrange.byte_size == 0U) {
                throw std::runtime_error(
                    "The level's scene-block WadV1 subrange is empty");
            }
            if (subrange.signature != openrc::DiscTocSignature::wad) {
                throw std::runtime_error(
                    "The level's scene-block subrange is not WadV1");
            }

            const auto& primary_extent =
                level_layout->primary_extents.front();
            const auto primary_bytes = read_disc_extent(
                arguments[1],
                primary_extent.lba,
                primary_extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto subrange_offset =
                static_cast<std::uint64_t>(subrange.relative_offset);
            const auto subrange_size =
                static_cast<std::uint64_t>(subrange.byte_size);
            if (subrange_offset > primary_bytes.size() ||
                subrange_size > primary_bytes.size() - subrange_offset) {
                throw std::runtime_error(
                    "The scene-block WadV1 subrange lies outside primary extent 0");
            }

            const auto logical_wad = std::span<const std::byte>(primary_bytes).subspan(
                static_cast<std::size_t>(subrange_offset),
                static_cast<std::size_t>(subrange_size));
            const auto decoded = openrc::decode_wad_bytes(
                logical_wad,
                kMaximumCliDecodedWadBytes);
            const auto directory = openrc::parse_scene_block_directory_v1(
                decoded.bytes,
                openrc::SceneBlockDirectoryLimits{
                    kMaximumCliDecodedWadBytes,
                    1'000'000U,
                    kMaximumCliDecodedWadBytes});

            const auto companion_count =
                level_assets->primary_extent3.tables.front().size();
            if (static_cast<std::uint64_t>(companion_count) !=
                directory.record_count) {
                throw std::runtime_error(
                    "The scene-block record count does not match companion extent-3 table 0");
            }
            const auto envelope_bytes =
                directory.chain_end - directory.directory_bytes;
            std::array<std::uint64_t, openrc::kSceneBlockSectionCount>
                section_total_bytes{};
            std::array<std::uint64_t, openrc::kSceneBlockSectionCount>
                section_minimum_bytes{};
            std::array<std::uint64_t, openrc::kSceneBlockSectionCount>
                section_maximum_bytes{};
            section_minimum_bytes.fill(
                std::numeric_limits<std::uint64_t>::max());

            constexpr std::array<openrc::SceneBlockVifOpcode, 8U> kVifOpcodes{
                openrc::SceneBlockVifOpcode::nop,
                openrc::SceneBlockVifOpcode::stcycl,
                openrc::SceneBlockVifOpcode::stmod,
                openrc::SceneBlockVifOpcode::strow,
                openrc::SceneBlockVifOpcode::unpack_v3_16,
                openrc::SceneBlockVifOpcode::unpack_v4_32,
                openrc::SceneBlockVifOpcode::unpack_v4_16,
                openrc::SceneBlockVifOpcode::unpack_v4_8,
            };
            constexpr std::array<std::string_view, kVifOpcodes.size()>
                kVifOpcodeNames{
                    "NOP",
                    "STCYCL",
                    "STMOD",
                    "STROW",
                    "UNPACK V3-16",
                    "UNPACK V4-32",
                    "UNPACK V4-16",
                    "UNPACK V4-8",
                };
            std::array<std::uint64_t, kVifOpcodes.size()> vif_opcode_counts{};
            std::uint64_t vif_stream_bytes = 0U;
            std::uint64_t vif_command_count = 0U;
            std::uint64_t vif_unpack_count = 0U;
            std::uint64_t vif_payload_bytes = 0U;
            std::uint64_t vif_padding_bytes = 0U;
            std::uint64_t vu_vector_writes = 0U;
            std::uint64_t vu_unique_qword_writes = 0U;
            std::uint64_t vu_overwrite_vector_writes = 0U;
            std::uint64_t vu_wrapped_vector_writes = 0U;
            std::uint64_t vu_fully_known_qwords = 0U;
            std::uint64_t vu_partially_known_qwords = 0U;
            std::uint64_t vu_indeterminate_qwords = 0U;
            std::uint64_t vu_phase_count = 0U;
            std::uint64_t vu_minimum_phases_per_stream =
                std::numeric_limits<std::uint64_t>::max();
            std::uint64_t vu_maximum_phases_per_stream = 0U;
            std::uint64_t vu_phase_destination_runs = 0U;
            std::uint64_t vu_internal_phase_overwrites = 0U;
            std::uint64_t vu_prior_phase_overwrites = 0U;
            for (const auto& entry : directory.entries) {
                for (std::size_t section_index = 0U;
                     section_index < openrc::kSceneBlockSectionCount;
                     ++section_index) {
                    const auto section_bytes =
                        entry.section_layout.ranges[section_index].size;
                    section_total_bytes[section_index] += section_bytes;
                    section_minimum_bytes[section_index] = std::min(
                        section_minimum_bytes[section_index],
                        section_bytes);
                    section_maximum_bytes[section_index] = std::max(
                        section_maximum_bytes[section_index],
                        section_bytes);
                }

                const auto vif_size = static_cast<std::size_t>(
                    entry.section_layout.relative_boundaries[
                        openrc::kSceneBlockVifSectionCount]);
                const auto prefix_size = static_cast<std::size_t>(
                    openrc::kSceneBlockDirectoryV1Stride);
                if (entry.block_bytes.size() < prefix_size ||
                    vif_size > entry.block_bytes.size() - prefix_size) {
                    throw std::runtime_error(
                        "A verified scene-block VIF range exceeds its owned envelope");
                }
                const auto vif_bytes = std::span<const std::byte>(
                    entry.block_bytes).subspan(prefix_size, vif_size);
                const auto execution = openrc::execute_scene_block_vu_v1(
                    vif_bytes,
                    openrc::SceneBlockVuExecutionOptionsV1{0U},
                    openrc::SceneBlockVuLimits{
                        openrc::SceneBlockVifLimits{
                            kMaximumCliDecodedWadBytes,
                            1'000'000U,
                            kMaximumCliDecodedWadBytes},
                        1'000'000U});
                const auto& vif = execution.stream;
                const auto phases =
                    openrc::group_scene_block_vu_phases_v1(execution);
                const auto stream_phase_count =
                    static_cast<std::uint64_t>(phases.size());
                vu_phase_count += stream_phase_count;
                vu_minimum_phases_per_stream = std::min(
                    vu_minimum_phases_per_stream,
                    stream_phase_count);
                vu_maximum_phases_per_stream = std::max(
                    vu_maximum_phases_per_stream,
                    stream_phase_count);
                for (const auto& phase : phases) {
                    vu_phase_destination_runs +=
                        static_cast<std::uint64_t>(
                            phase.unique_destination_runs.size());
                    vu_internal_phase_overwrites +=
                        phase.internal_overwrite_count;
                    vu_prior_phase_overwrites +=
                        phase.prior_phase_overwrite_count;
                }
                vif_stream_bytes += vif.input_bytes;
                vif_command_count +=
                    static_cast<std::uint64_t>(vif.commands.size());
                vif_payload_bytes += vif.total_payload_bytes;
                vif_padding_bytes += vif.total_padding_bytes;
                vu_vector_writes += execution.total_vector_writes;
                vu_unique_qword_writes += execution.unique_qword_writes;
                vu_overwrite_vector_writes +=
                    execution.overwrite_vector_writes;
                vu_wrapped_vector_writes += execution.wrapped_vector_writes;
                for (const auto& qword : execution.memory) {
                    if (qword.write_count == 0U) {
                        continue;
                    }
                    const auto known_lanes = std::count_if(
                        qword.lanes.begin(),
                        qword.lanes.end(),
                        [](const openrc::SceneBlockVuValueV1& lane) {
                            return lane.state ==
                                openrc::SceneBlockVuValueState::known;
                        });
                    if (known_lanes == qword.lanes.end() - qword.lanes.begin()) {
                        ++vu_fully_known_qwords;
                    } else if (known_lanes == 0) {
                        ++vu_indeterminate_qwords;
                    } else {
                        ++vu_partially_known_qwords;
                    }
                }
                for (const auto& vif_command : vif.commands) {
                    const auto opcode = std::find(
                        kVifOpcodes.begin(),
                        kVifOpcodes.end(),
                        vif_command.opcode);
                    if (opcode == kVifOpcodes.end()) {
                        throw std::runtime_error(
                            "The SceneBlock VIF parser returned an unknown opcode");
                    }
                    const auto opcode_index = static_cast<std::size_t>(
                        std::distance(kVifOpcodes.begin(), opcode));
                    ++vif_opcode_counts[opcode_index];
                    if (vif_command.component_count != 0U) {
                        ++vif_unpack_count;
                    }
                }
            }

            std::cout
                << "OpenRC SceneBlockDirectoryV1 report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:            " << level_id << '\n'
                << "Primary extent LBA:  " << primary_extent.lba << '\n'
                << "Primary sectors:     " << primary_extent.sectors << '\n'
                << "Subrange offset:     "
                << hexadecimal(subrange.relative_offset, 8) << '\n'
                << "Compressed bytes:    " << subrange.byte_size << '\n'
                << "Decoded bytes:       " << decoded.bytes.size() << '\n'
                << "Decoded SHA-256:     " << decoded.sha256 << '\n'
                << "Stride:              " << directory.stride_bytes << '\n'
                << "Declared count:      " << directory.declared_count << '\n'
                << "Records:             " << directory.record_count << '\n'
                << "Companion pairs:     " << companion_count << '\n'
                << "Header float:        " << directory.header_float << '\n'
                << "Directory bytes:     " << directory.directory_bytes << '\n'
                << "Overlapped entry:    offset "
                << hexadecimal(directory.overlapped_entry_range.offset, 8)
                << ", size " << directory.overlapped_entry_range.size << " bytes\n"
                << "Block chain end:     "
                << hexadecimal(directory.chain_end, 8) << '\n'
                << "Block envelopes:     " << envelope_bytes << " bytes\n"
                << "Trailing offset:     "
                << hexadecimal(directory.trailing_range.offset, 8) << '\n'
                << "Trailing bytes:      " << directory.trailing_range.size << '\n'
                << "Owned bytes:         " << directory.owned_byte_count << '\n'
                << "Sections per block:  "
                << openrc::kSceneBlockSectionCount << "\n\n"
                << "Neutral remainder sections:\n";
            for (std::size_t section_index = 0U;
                 section_index < openrc::kSceneBlockSectionCount;
                 ++section_index) {
                std::cout
                    << "  [" << section_index << "] total "
                    << section_total_bytes[section_index]
                    << " bytes, per-block range "
                    << section_minimum_bytes[section_index] << ".."
                    << section_maximum_bytes[section_index] << " bytes\n";
            }

            std::cout
                << "\nBounded VIF streams (sections 0-4):\n"
                << "  Streams:           " << directory.record_count << '\n'
                << "  Stream bytes:      " << vif_stream_bytes << '\n'
                << "  Commands:          " << vif_command_count << '\n'
                << "  UNPACK commands:   " << vif_unpack_count << '\n'
                << "  Payload bytes:     " << vif_payload_bytes << '\n'
                << "  Alignment bytes:   " << vif_padding_bytes << '\n'
                << "  Opcodes:\n";
            for (std::size_t opcode_index = 0U;
                 opcode_index < kVifOpcodes.size();
                 ++opcode_index) {
                std::cout
                    << "    " << kVifOpcodeNames[opcode_index] << ": "
                    << vif_opcode_counts[opcode_index] << '\n';
            }

            std::cout
                << "\nNeutral VU1 execution (relative TOPS=0):\n"
                << "  Vector writes:     " << vu_vector_writes << '\n'
                << "  Unique qwords:     " << vu_unique_qword_writes << '\n'
                << "  Overwrite writes:  " << vu_overwrite_vector_writes << '\n'
                << "  Wrapped writes:    " << vu_wrapped_vector_writes << '\n'
                << "  Final fully known: " << vu_fully_known_qwords << '\n'
                << "  Final partial:     " << vu_partially_known_qwords << '\n'
                << "  Final unknown:     " << vu_indeterminate_qwords << '\n'
                << "\nNeutral control-to-UNPACK phases:\n"
                << "  Phases:            " << vu_phase_count << '\n'
                << "  Per-stream range:  ";
            if (directory.entries.empty()) {
                std::cout << "none\n";
            } else {
                std::cout
                    << vu_minimum_phases_per_stream << ".."
                    << vu_maximum_phases_per_stream << '\n';
            }
            std::cout
                << "  Destination runs:  " << vu_phase_destination_runs << '\n'
                << "  Internal overwrite:" << ' '
                << vu_internal_phase_overwrites << '\n'
                << "  Prior-phase overwrite: "
                << vu_prior_phase_overwrites << '\n';

            if (!directory.entries.empty()) {
                const auto& first = directory.entries.front();
                const auto& last = directory.entries.back();
                std::cout
                    << "First block:         offset "
                    << hexadecimal(first.block_offset, 8)
                    << ", envelope " << first.block_range.size << " bytes\n"
                    << "Last block:          offset "
                    << hexadecimal(last.block_offset, 8)
                    << ", envelope " << last.block_range.size << " bytes\n";
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "companion-wads") {
        if (arguments.size() != 3) {
            std::cerr
                << "error: companion-wads expects an ISO path and a level ID\n";
            return kUsageError;
        }

        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::uint32_t>(*level_value);

        try {
            const auto assets = openrc::inspect_disc_toc_assets(arguments[1]);
            const auto level_assets = std::find_if(
                assets.levels.begin(),
                assets.levels.end(),
                [level_id](const openrc::DiscTocLevelAssets& candidate) {
                    return candidate.level_id == level_id;
                });
            const auto level_layout = std::find_if(
                assets.layout.levels.begin(),
                assets.layout.levels.end(),
                [level_id](const openrc::DiscTocLevelDescriptor& candidate) {
                    return candidate.level_id == level_id;
                });
            if (level_assets == assets.levels.end() ||
                level_layout == assets.layout.levels.end()) {
                throw std::runtime_error("The requested level is absent from DiscTocV1");
            }

            constexpr std::size_t kCompanionIndexSubrange = 2U;
            constexpr std::size_t kCompanionTargetSubrange = 10U;
            const auto& index_subrange =
                level_assets->primary_extent0.subranges[kCompanionIndexSubrange];
            const auto& target_subrange =
                level_assets->primary_extent0.subranges[kCompanionTargetSubrange];
            if (index_subrange.byte_size == 0U) {
                throw std::runtime_error(
                    "The level's companion index subrange is empty");
            }
            if (target_subrange.byte_size == 0U ||
                target_subrange.signature != openrc::DiscTocSignature::wad) {
                throw std::runtime_error(
                    "The level's companion target subrange is not WadV1");
            }

            const auto& primary_extent =
                level_layout->primary_extents.front();
            const auto primary_bytes = read_disc_extent(
                arguments[1],
                primary_extent.lba,
                primary_extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto bounded_subrange =
                [&primary_bytes](
                    const openrc::DiscTocSubrange& subrange,
                    const char* description) {
                    const auto offset =
                        static_cast<std::uint64_t>(subrange.relative_offset);
                    const auto size =
                        static_cast<std::uint64_t>(subrange.byte_size);
                    if (offset > primary_bytes.size() ||
                        size > primary_bytes.size() - offset) {
                        throw std::runtime_error(
                            std::string(description) +
                            " lies outside primary extent 0");
                    }
                    return std::span<const std::byte>(primary_bytes).subspan(
                        static_cast<std::size_t>(offset),
                        static_cast<std::size_t>(size));
                };

            const auto index_bytes = bounded_subrange(
                index_subrange,
                "The companion index subrange");
            const auto target_wad = bounded_subrange(
                target_subrange,
                "The companion target WadV1 subrange");
            const auto decoded = openrc::decode_wad_bytes(
                target_wad,
                kMaximumCliDecodedWadBytes);
            const auto index = openrc::parse_companion_terminal_wad_index_v1(
                index_bytes,
                decoded.bytes,
                openrc::CompanionTerminalWadIndexLimits{
                    kMaximumCliDecodedWadBytes,
                    kMaximumCliDecodedWadBytes,
                    1'000'000U,
                    kMaximumCliDecodedWadBytes});

            std::uint64_t nested_decoded_bytes = 0;
            for (const auto& record : index.records) {
                const auto remaining_decoded_bytes =
                    kMaximumCliDecodedWadBytes - nested_decoded_bytes;
                const auto nested = openrc::decode_wad_bytes(
                    record.wad_bytes,
                    remaining_decoded_bytes);
                if (nested.bytes.size() >
                    remaining_decoded_bytes) {
                    throw std::runtime_error(
                        "The companion WADs exceed the aggregate decoded-byte limit");
                }
                nested_decoded_bytes += nested.bytes.size();
            }

            std::cout
                << "OpenRC CompanionTerminalWadIndexV1 report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:            " << level_id << '\n'
                << "Primary extent LBA:  " << primary_extent.lba << '\n'
                << "Index offset:        "
                << hexadecimal(index_subrange.relative_offset, 8) << '\n'
                << "Index bytes:         " << index.index_input_bytes << '\n'
                << "Table offset:        "
                << hexadecimal(index.table_offset, 8) << '\n'
                << "Opaque header word:  "
                << hexadecimal(index.opaque_header_word, 8) << '\n'
                << "Target WAD offset:   "
                << hexadecimal(target_subrange.relative_offset, 8) << '\n'
                << "Target WAD bytes:    " << target_subrange.byte_size << '\n'
                << "Decoded target:      " << index.target_input_bytes << " bytes\n"
                << "Decoded SHA-256:     " << decoded.sha256 << '\n'
                << "Indexed WADs:        " << index.record_count << '\n'
                << "Pre-table bytes:     " << index.pre_table_bytes.size() << '\n'
                << "Opaque target prefix:"
                << ' ' << index.opaque_target_prefix_range.size << " bytes\n"
                << "Terminal chain:      offset "
                << hexadecimal(index.terminal_chain_range.offset, 8)
                << ", size " << index.terminal_chain_range.size << " bytes\n"
                << "Logical WAD bytes:   " << index.total_logical_bytes << '\n'
                << "Zero padding bytes:  " << index.total_padding_bytes << '\n'
                << "Nested decoded:      " << nested_decoded_bytes << " bytes\n";

            if (!index.records.empty()) {
                const auto& first = index.records.front();
                const auto& last = index.records.back();
                std::cout
                    << "First WAD:           offset "
                    << hexadecimal(first.target_offset, 8)
                    << ", logical " << first.logical_size << " bytes\n"
                    << "Last WAD:            offset "
                    << hexadecimal(last.target_offset, 8)
                    << ", logical " << last.logical_size << " bytes\n";
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "map-art") {
        if (arguments.size() < 3 || arguments.size() > 4) {
            std::cerr
                << "error: map-art expects an ISO path, level ID, "
                   "and optional TGA output path\n";
            return kUsageError;
        }

        const auto level_value = parse_decimal_argument(arguments[2]);
        if (!level_value || *level_value >= openrc::kDiscTocLevelCount) {
            std::cerr << "error: level ID must be a decimal number from 0 to "
                      << (openrc::kDiscTocLevelCount - 1U) << '\n';
            return kUsageError;
        }
        const auto level_id = static_cast<std::size_t>(*level_value);
        const auto requested_slot = kMapArtFirstGlobalSlot + level_id;

        try {
            const auto toc = openrc::inspect_disc_toc(arguments[1]);
            const auto entry = std::find_if(
                toc.global_extents.begin(),
                toc.global_extents.end(),
                [requested_slot](const openrc::DiscTocGlobalExtent& candidate) {
                    return candidate.slot == requested_slot;
                });
            if (entry == toc.global_extents.end() ||
                entry->signature != openrc::DiscTocSignature::wad) {
                throw std::runtime_error(
                    "The MapArtV1 global TOC slot is absent or is not WadV1");
            }

            const auto decoded = openrc::decode_wad(
                arguments[1],
                entry->extent.lba,
                entry->extent.sectors,
                kMaximumCliDecodedWadBytes);
            const auto map_art = openrc::parse_map_art_v1(
                decoded.bytes,
                openrc::MapArtLimits{
                    openrc::BoundaryTableLimits{
                        kMaximumCliDecodedWadBytes,
                        kMaximumCliDecodedWadBytes},
                    kMaximumCliTwoFipPixels});

            std::cout
                << "OpenRC MapArtV1 report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Level ID:            " << level_id << '\n'
                << "Global TOC slot:     " << requested_slot << '\n'
                << "LBA:                 " << entry->extent.lba << '\n'
                << "Sectors:             " << entry->extent.sectors << '\n'
                << "Decoded bytes:       " << decoded.bytes.size() << '\n'
                << "Decoded SHA-256:     " << decoded.sha256 << '\n'
                << "Region-0 records:    "
                << map_art.region0_records.size() << '\n'
                << "Region-0 logical:    " << map_art.region0_logical_bytes << '\n'
                << "Region-0 padding:    " << map_art.region0_padding_bytes << "\n\n"
                << "Owned non-image regions:\n";
            for (std::size_t index = 0;
                 index < map_art.owned_regions.size();
                 ++index) {
                std::cout
                    << "  [" << index << "] "
                    << map_art.owned_regions[index].size() << " bytes\n";
            }
            std::cout << "\n2FIP image regions:\n";
            for (std::size_t index = 0; index < map_art.images.size(); ++index) {
                const auto& image = map_art.images[index];
                std::cout
                    << "  [" << (openrc::kMapArtFirstImageRegion + index) << "] "
                    << image.width << 'x' << image.height
                    << ", id " << hexadecimal(image.opaque_identifier, 8)
                    << ", pixels " << image.indices.size() << '\n';
            }

            if (arguments.size() == 4) {
                const auto tga = openrc::encode_map_art_tga(map_art);
                write_new_binary_file(arguments[3], tga);
                std::cout
                    << "\nTGA output:          "
                    << openrc::path_to_utf8(arguments[3]) << '\n'
                    << "TGA dimensions:      384x128\n"
                    << "TGA bytes:           " << tga.size() << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "ps2-save") {
        if (arguments.size() != 2) {
            std::cerr << "error: ps2-save expects an ISO path\n";
            return kUsageError;
        }

        try {
            const auto toc = openrc::inspect_disc_toc(arguments[1]);
            const auto entry = std::find_if(
                toc.global_extents.begin(),
                toc.global_extents.end(),
                [](const openrc::DiscTocGlobalExtent& candidate) {
                    return candidate.slot == kPs2SaveBundleGlobalSlot;
                });
            if (entry == toc.global_extents.end() ||
                entry->signature != openrc::DiscTocSignature::ps2d) {
                throw std::runtime_error(
                    "Global TOC slot 1 is absent or does not have a PS2D signature");
            }

            const auto bytes = read_disc_extent(
                arguments[1],
                entry->extent.lba,
                entry->extent.sectors,
                kMaximumCliPs2SaveBundleBytes);
            const auto bundle = openrc::parse_ps2_save_bundle(
                bytes,
                openrc::Ps2SaveBundleLimits{
                    kMaximumCliPs2SaveBundleBytes,
                    65536U,
                    4096U,
                    65536U,
                    kMaximumCliPs2SaveBundleBytes});

            std::size_t repeated_tlv_entries = 0;
            for (const auto& record : bundle.save_template.repeated_records) {
                repeated_tlv_entries += record.entries.size();
            }

            std::cout
                << "OpenRC PS2 save-bundle report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Global TOC slot:     " << kPs2SaveBundleGlobalSlot << '\n'
                << "LBA:                 " << entry->extent.lba << '\n'
                << "Sectors:             " << entry->extent.sectors << '\n'
                << "Logical bytes:       " << bundle.logical_bytes << '\n'
                << "Sector padding:      " << bundle.padding_bytes << "\n\n"
                << "Outer records:\n";
            for (std::size_t index = 0; index < bundle.records.size(); ++index) {
                std::cout
                    << "  [" << index << "] offset "
                    << hexadecimal(bundle.records[index].offset, 8)
                    << ", size " << bundle.records[index].size << " bytes\n";
            }

            std::cout
                << "\nicon.sys:\n"
                << "  second-line offset: " << bundle.icon_sys.second_line_offset << '\n'
                << "  view icon:          " << bundle.icon_sys.icon_filenames[0] << '\n'
                << "  copy icon:          " << bundle.icon_sys.icon_filenames[1] << '\n'
                << "  delete icon:        " << bundle.icon_sys.icon_filenames[2] << '\n'
                << "\nMemory-card icon:\n"
                << "  version:            "
                << hexadecimal(bundle.icon_model.version, 8) << '\n'
                << "  shapes:             " << bundle.icon_model.shape_count << '\n'
                << "  texture type:       " << bundle.icon_model.texture_type << '\n'
                << "  vertices:           " << bundle.icon_model.vertices.size() << '\n'
                << "  texture bytes:      " << bundle.icon_model.texture_bytes.size() << '\n'
                << "\nSave template:\n"
                << "  primary bytes:      "
                << bundle.save_template.primary_record_size << '\n'
                << "  primary TLV entries:"
                << ' ' << bundle.save_template.primary_record.entries.size() << '\n'
                << "  repeated bytes:     "
                << bundle.save_template.repeated_record_size << '\n'
                << "  repeated records:   "
                << bundle.save_template.repeated_records.size() << '\n'
                << "  repeated TLVs:      " << repeated_tlv_entries << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "twofip") {
        if (arguments.size() < 3 || arguments.size() > 4) {
            std::cerr
                << "error: twofip expects an ISO path, global TOC slot, "
                   "and optional TGA output path\n";
            return kUsageError;
        }

        const auto requested_slot_value = parse_decimal_argument(arguments[2]);
        if (!requested_slot_value ||
            *requested_slot_value >= openrc::kDiscTocGlobalExtentSlotCount) {
            std::cerr << "error: global TOC slot must be a decimal number from 0 to "
                      << (openrc::kDiscTocGlobalExtentSlotCount - 1U) << '\n';
            return kUsageError;
        }
        const auto requested_slot = *requested_slot_value;

        try {
            const auto toc = openrc::inspect_disc_toc(arguments[1]);
            const auto entry = std::find_if(
                toc.global_extents.begin(),
                toc.global_extents.end(),
                [requested_slot](const openrc::DiscTocGlobalExtent& candidate) {
                    return candidate.slot == requested_slot;
                });
            if (entry == toc.global_extents.end()) {
                throw std::runtime_error(
                    "Global TOC slot " + std::to_string(requested_slot) + " is empty");
            }

            std::vector<std::byte> source_bytes;
            const char* source_kind = nullptr;
            if (entry->signature == openrc::DiscTocSignature::two_fip) {
                source_bytes = read_disc_extent(
                    arguments[1],
                    entry->extent.lba,
                    entry->extent.sectors,
                    kMaximumCliDecodedWadBytes);
                source_kind = "direct 2FIP extent";
            } else if (entry->signature == openrc::DiscTocSignature::wad) {
                auto decoded = openrc::decode_wad(
                    arguments[1],
                    entry->extent.lba,
                    entry->extent.sectors,
                    kMaximumCliDecodedWadBytes);
                source_bytes = std::move(decoded.bytes);
                source_kind = "decoded WadV1";
            } else {
                throw std::runtime_error(
                    "Global TOC slot " + std::to_string(requested_slot) +
                    " is " +
                    std::string(openrc::disc_toc_signature_name(entry->signature)) +
                    ", not a direct or WadV1-wrapped 2FIP asset");
            }

            const auto image = openrc::parse_two_fip(
                source_bytes,
                kMaximumCliTwoFipPixels);
            std::cout
                << "OpenRC 2FIP report\n"
                << "Image:               " << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Global TOC slot:     " << requested_slot << '\n'
                << "Source:              " << source_kind << '\n'
                << "LBA:                 " << entry->extent.lba << '\n'
                << "Sectors:             " << entry->extent.sectors << '\n'
                << "Opaque identifier:   " << hexadecimal(image.opaque_identifier, 8) << '\n'
                << "Dimensions:          " << image.width << 'x' << image.height << '\n'
                << "Pixel format:        PSMT8 (" << hexadecimal(
                       image.pixel_storage_format)
                << ")\n"
                << "Logical bytes:       " << image.logical_bytes << '\n'
                << "Storage padding:     " << image.padding_bytes << '\n';

            if (arguments.size() == 4) {
                const auto tga = openrc::encode_two_fip_tga(image);
                write_new_binary_file(arguments[3], tga);
                std::cout
                    << "TGA output:          "
                    << openrc::path_to_utf8(arguments[3]) << '\n'
                    << "TGA bytes:           " << tga.size() << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "prepare") {
        if (arguments.size() < 2 || arguments.size() > 3) {
            std::cerr << "error: prepare expects an ISO path and an optional games directory\n";
            return kUsageError;
        }

        ProgressPrinter progress;
        try {
            std::filesystem::path games_directory;
            if (arguments.size() == 3) {
                games_directory = arguments[2];
            } else {
                const auto paths = openrc::application_paths();
                openrc::ensure_application_directories(paths);
                games_directory = paths.local_data / "games";
            }

            const auto result = openrc::prepare_game_files(
                arguments[1], games_directory,
                [&progress](const openrc::PreparationProgress& update) {
                    return progress.update(update);
                });
            progress.finish();

            std::cout << (result.already_prepared
                              ? "Preparation already exists and was verified.\n"
                              : "Preparation completed.\n")
                      << "Destination:     " << openrc::path_to_utf8(result.destination) << '\n'
                      << "Manifest:        " << openrc::path_to_utf8(result.manifest_path) << '\n'
                      << "Boot executable: "
                      << openrc::path_to_utf8(result.boot_executable_path) << '\n'
                      << "Image SHA-256:   " << result.image_sha256 << '\n'
                      << "Files:           " << result.file_count << '\n'
                      << "Total bytes:     " << result.total_file_bytes << '\n';
            return 0;
        } catch (const openrc::PreparationCancelled& error) {
            progress.finish();
            std::cerr << "cancelled: " << error.what() << '\n';
            return kCancelled;
        } catch (const std::exception& error) {
            progress.finish();
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "dvp-vu") {
        if (arguments.size() != 4) {
            std::cerr
                << "error: dvp-vu expects an executable, a comma-separated "
                   "entrypoint list, and a comma-separated overlay-section list\n";
            return kUsageError;
        }

        const auto entrypoint_addresses =
            parse_decimal_u16_list(
                arguments[2], kMaximumCliDvpVuListItems);
        const auto overlay_section_indices =
            parse_decimal_u16_list(
                arguments[3], kMaximumCliDvpVuListItems);
        if (!entrypoint_addresses || !overlay_section_indices) {
            std::cerr
                << "error: VU pair entrypoints and ELF overlay sections must "
                   "be non-empty comma-separated decimal uint16 lists with at "
                   "most "
                << kMaximumCliDvpVuListItems << " items each\n";
            return kUsageError;
        }

        try {
            const auto bytes =
                read_bounded_binary_file(arguments[1], kMaximumCliElfBytes);
            const auto elf = openrc::inspect_elf(
                std::span<const std::byte>(bytes));
            if (!elf.dvp_overlay_table) {
                throw std::runtime_error(
                    "The executable has no DVP overlay table");
            }

            std::vector<openrc::ElfDvpOverlay> selected_overlays;
            selected_overlays.reserve(overlay_section_indices->size());
            for (const auto requested_index : *overlay_section_indices) {
                const auto overlay = std::find_if(
                    elf.dvp_overlay_table->overlays.begin(),
                    elf.dvp_overlay_table->overlays.end(),
                    [requested_index](const openrc::ElfDvpOverlay& candidate) {
                        return candidate.overlay_section_index ==
                            requested_index;
                    });
                if (overlay == elf.dvp_overlay_table->overlays.end()) {
                    throw std::runtime_error(
                        "A requested DVP overlay section was not found");
                }
                selected_overlays.push_back(*overlay);
            }

            const auto program = openrc::decode_dvp_vu_program_v1(
                std::span<const std::byte>(bytes),
                std::span<const openrc::ElfDvpOverlay>(selected_overlays),
                std::span<const std::uint16_t>(*entrypoint_addresses),
                openrc::DvpVuLimits{
                    kMaximumCliElfBytes,
                    kMaximumCliDvpVuListItems,
                    openrc::kDvpVu1MicroMemoryBytes,
                    kMaximumCliDvpVuListItems,
                    2U * openrc::kDvpVu1InstructionCount,
                    8U * openrc::kDvpVu1InstructionCount,
                });

            std::uint64_t flag_i_count = 0U;
            std::uint64_t flag_e_count = 0U;
            std::uint64_t flag_m_count = 0U;
            std::uint64_t flag_d_count = 0U;
            std::uint64_t flag_t_count = 0U;
            std::uint64_t xtop_count = 0U;
            std::uint64_t xitop_count = 0U;
            std::uint64_t xgkick_count = 0U;
            for (const auto& instruction : program.instructions) {
                flag_i_count += instruction.upper.immediate ? 1U : 0U;
                flag_e_count += instruction.upper.end ? 1U : 0U;
                flag_m_count += instruction.upper.m ? 1U : 0U;
                flag_d_count += instruction.upper.d ? 1U : 0U;
                flag_t_count += instruction.upper.t ? 1U : 0U;
                switch (instruction.lower.opcode) {
                case openrc::DvpVuLowerOpcode::xtop:
                    ++xtop_count;
                    break;
                case openrc::DvpVuLowerOpcode::xitop:
                    ++xitop_count;
                    break;
                case openrc::DvpVuLowerOpcode::xgkick:
                    ++xgkick_count;
                    break;
                default:
                    break;
                }
            }

            std::uint64_t vector_load_count = 0U;
            std::uint64_t vector_store_count = 0U;
            std::uint64_t integer_load_count = 0U;
            std::uint64_t integer_store_count = 0U;
            for (const auto& access : program.memory_accesses) {
                switch (access.kind) {
                case openrc::DvpVuMemoryAccessKind::vector_load:
                    ++vector_load_count;
                    break;
                case openrc::DvpVuMemoryAccessKind::vector_store:
                    ++vector_store_count;
                    break;
                case openrc::DvpVuMemoryAccessKind::integer_load:
                    ++integer_load_count;
                    break;
                case openrc::DvpVuMemoryAccessKind::integer_store:
                    ++integer_store_count;
                    break;
                }
            }

            std::uint64_t direct_transfer_count = 0U;
            std::uint64_t resolved_direct_transfer_count = 0U;
            std::uint64_t end_transfer_count = 0U;
            for (const auto& transfer : program.control_transfers) {
                if (transfer.direct_target_address) {
                    ++direct_transfer_count;
                    resolved_direct_transfer_count +=
                        transfer.direct_target_decoded ? 1U : 0U;
                }
                if (transfer.kind ==
                    openrc::DvpVuControlTransferKind::end_after_delay_slot) {
                    ++end_transfer_count;
                }
            }

            std::cout
                << "OpenRC DVP VU program report\n"
                << "Executable:           "
                << openrc::path_to_utf8(arguments[1]) << '\n'
                << "Input bytes:          " << program.input_bytes << '\n'
                << "Overlay chunks:       " << program.code_chunks.size() << '\n'
                << "Instruction runs:     "
                << program.instruction_runs.size() << '\n'
                << "Code bytes:           " << program.total_code_bytes << '\n'
                << "Instruction pairs:    " << program.instructions.size() << '\n'
                << "Entrypoints:          ";
            for (std::size_t index = 0U;
                 index < program.entrypoints.size();
                 ++index) {
                if (index != 0U) {
                    std::cout << ',';
                }
                std::cout << hexadecimal(
                    program.entrypoints[index].instruction_address, 3);
            }
            std::cout
                << '\n'
                << "Unknown upper/lower: "
                << program.unknown_upper_count << '/'
                << program.unknown_lower_count << '\n'
                << "Flags I/E/M/D/T:     "
                << flag_i_count << '/' << flag_e_count << '/'
                << flag_m_count << '/' << flag_d_count << '/'
                << flag_t_count << '\n'
                << "XTOP/XITOP/XGKICK:   "
                << xtop_count << '/' << xitop_count << '/'
                << xgkick_count << '\n'
                << "Vector load/store:   "
                << vector_load_count << '/' << vector_store_count << '\n'
                << "Integer load/store:  "
                << integer_load_count << '/' << integer_store_count << '\n'
                << "Direct targets:      "
                << resolved_direct_transfer_count << '/'
                << direct_transfer_count << " resolved\n"
                << "Indirect transfers:  "
                << program.indirect_control_transfer_count << '\n'
                << "End flags:            " << end_transfer_count << '\n'
                << "Missing delay slots:  "
                << program.missing_delay_slot_count << '\n'
                << "Basic blocks/edges:  "
                << program.basic_blocks.size() << '/'
                << program.cfg_edges.size() << '\n';

            std::cout << "\nChunks by VU virtual address:\n";
            for (const auto& chunk : program.code_chunks) {
                std::cout
                    << "  section " << chunk.overlay_section_index
                    << " vma="
                    << hexadecimal(chunk.virtual_byte_address, 4)
                    << " instructions=" << chunk.instruction_count
                    << " source=" << hexadecimal(chunk.source_range.offset, 8)
                    << '+' << hexadecimal(chunk.source_range.size) << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "elf") {
        if (arguments.size() != 2) {
            std::cerr << "error: elf expects exactly one executable path\n";
            return kUsageError;
        }

        try {
            const auto report = openrc::inspect_elf(arguments[1]);
            std::cout
                << "Executable:             "
                << openrc::path_to_utf8(report.executable_path) << '\n'
                << "File size:              " << report.file_size << " bytes\n"
                << "Format:                 ELF32, little-endian, MIPS\n"
                << "ELF type:               " << hexadecimal(report.type, 4) << '\n'
                << "Machine:                " << report.machine << '\n'
                << "Flags:                  " << hexadecimal(report.flags, 8) << '\n'
                << "Entry point:            " << hexadecimal(report.entry_point, 8) << '\n'
                << "Program headers:        " << report.program_header_count << '\n'
                << "Section headers:        " << report.section_header_count << '\n'
                << "Loadable segments:      " << report.loadable_segment_count << '\n';
            if (report.loadable_virtual_address_range) {
                std::cout
                    << "Load address range:     "
                    << hexadecimal(report.loadable_virtual_address_range->begin, 8) << " - "
                    << hexadecimal(report.loadable_virtual_address_range->end, 8)
                    << " (end exclusive)\n";
            } else {
                std::cout << "Load address range:     none\n";
            }

            if (report.iop_module_info) {
                const auto& module = *report.iop_module_info;
                std::cout
                    << "\nIOP module:\n"
                    << "  Name:                 " << module.name << '\n'
                    << "  Version:              " << hexadecimal(module.version, 4) << '\n'
                    << "  Module-info address:  "
                    << hexadecimal(module.module_info_address, 8) << '\n'
                    << "  Entry point:          "
                    << hexadecimal(module.entry_point, 8) << '\n'
                    << "  Global pointer:       "
                    << hexadecimal(module.global_pointer, 8) << '\n'
                    << "  Declared text/data/bss: "
                    << module.text_size << '/' << module.data_size << '/'
                    << module.bss_size << " bytes\n"
                    << "  Alloc size matches:   text="
                    << (module.text_size_matches_allocated_sections ? "yes" : "no")
                    << ", data="
                    << (module.data_size_matches_allocated_sections ? "yes" : "no")
                    << ", bss="
                    << (module.bss_size_matches_allocated_sections ? "yes" : "no")
                    << '\n';
            }

            if (!report.relocation_summaries.empty()) {
                std::cout << "\nRelocations:\n";
                for (const auto& summary : report.relocation_summaries) {
                    std::cout
                        << "  section " << summary.section_index
                        << " -> target " << summary.target_section_index
                        << ", entries " << summary.entry_count << ", types";
                    for (const auto& type : summary.types) {
                        std::cout << ' ' << static_cast<unsigned int>(type.type)
                                  << ':' << type.count;
                    }
                    std::cout << '\n';
                }
            }

            if (!report.iop_import_libraries.empty()) {
                std::cout << "\nIOP import libraries:\n";
                for (const auto& library : report.iop_import_libraries) {
                    std::cout
                        << "  " << library.name
                        << " version=" << hexadecimal(library.version, 4)
                        << " flags=" << hexadecimal(library.flags, 4)
                        << " ordinals=";
                    for (std::size_t index = 0; index < library.ordinals.size(); ++index) {
                        if (index != 0U) {
                            std::cout << ',';
                        }
                        std::cout << library.ordinals[index];
                    }
                    std::cout << '\n';
                }
            }

            if (report.dvp_overlay_table) {
                const auto& table = *report.dvp_overlay_table;
                std::cout
                    << "\nDVP overlays: "
                    << table.overlays.size()
                    << " (table section " << table.section_index
                    << ", strings section "
                    << table.string_table_section_index << ")\n";
                for (const auto& overlay : table.overlays) {
                    const auto& code_section =
                        report.section_headers[overlay.code_section_index];
                    std::cout
                        << "  [" << overlay.overlay_section_index << "] "
                        << overlay.name
                        << " lma="
                        << hexadecimal(overlay.load_memory_address, 8)
                        << " vma="
                        << hexadecimal(overlay.virtual_memory_address, 8)
                        << " size=" << hexadecimal(overlay.size)
                        << " code=" << code_section.name
                        << '+' << hexadecimal(
                            overlay.code_file_offset -
                                code_section.file_offset)
                        << " (file "
                        << hexadecimal(overlay.code_file_offset, 8)
                        << ")\n";
                }
            }

            std::cout << "\nProgram headers:\n";
            for (std::size_t index = 0; index < report.program_headers.size(); ++index) {
                const auto& header = report.program_headers[index];
                std::cout
                    << "  [" << index << "] type=" << header.type
                    << " offset=" << hexadecimal(header.file_offset, 8)
                    << " vaddr=" << hexadecimal(header.virtual_address, 8)
                    << " paddr=" << hexadecimal(header.physical_address, 8)
                    << " filesz=" << hexadecimal(header.file_size)
                    << " memsz=" << hexadecimal(header.memory_size)
                    << " flags=" << hexadecimal(header.flags)
                    << " align=" << hexadecimal(header.alignment) << '\n';
            }

            std::cout << "\nSection headers:\n";
            for (std::size_t index = 0; index < report.section_headers.size(); ++index) {
                const auto& section = report.section_headers[index];
                std::cout
                    << "  [" << index << "] "
                    << (section.name.empty() ? "<unnamed>" : section.name)
                    << " type=" << section.type
                    << " flags=" << hexadecimal(section.flags)
                    << " addr=" << hexadecimal(section.virtual_address, 8)
                    << " offset=" << hexadecimal(section.file_offset, 8)
                    << " size=" << hexadecimal(section.size)
                    << " link=" << section.link
                    << " info=" << section.info
                    << " align=" << hexadecimal(section.alignment)
                    << " entsize=" << hexadecimal(section.entry_size) << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    if (command == "paths") {
        try {
            const auto paths = openrc::application_paths();
            openrc::ensure_application_directories(paths);
            std::cout
                << "Roaming config: " << openrc::path_to_utf8(paths.roaming_config) << '\n'
                << "Local data:     " << openrc::path_to_utf8(paths.local_data) << '\n'
                << "Cache:          " << openrc::path_to_utf8(paths.cache) << '\n'
                << "Logs:           " << openrc::path_to_utf8(paths.logs) << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error: " << error.what() << '\n';
            return kOperationError;
        }
    }

    std::cerr << "error: unknown command\n\n";
    print_usage();
    return kUsageError;
}

} // namespace

#ifdef _WIN32
int wmain(const int argc, wchar_t* argv[]) {
    std::vector<std::filesystem::path> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return run(arguments);
}
#else
int main(const int argc, char* argv[]) {
    std::vector<std::filesystem::path> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return run(arguments);
}
#endif
