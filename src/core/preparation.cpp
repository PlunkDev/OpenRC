#include "openrc/preparation.hpp"

#include "iso9660.hpp"
#include "openrc/hash.hpp"
#include "openrc/paths.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#if defined(__APPLE__)
#include <stdio.h>
#endif
#endif

namespace openrc {
namespace {

constexpr std::size_t kImageHashChunkSize = 1024U * 1024U;
constexpr std::uintmax_t kMaximumManifestSize = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumStagingAttempts = 128U;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

[[nodiscard]] std::uint64_t checked_add(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while calculating ") + description);
    }
    return left + right;
}

void publish_progress(
    const PreparationProgressCallback& callback,
    const PreparationProgress& progress) {
    if (callback && !callback(progress)) {
        throw PreparationCancelled();
    }
}

[[nodiscard]] std::uintmax_t source_size(const std::filesystem::path& image_path) {
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(image_path, filesystem_error) || filesystem_error) {
        fail("The source ISO is not a readable regular file");
    }
    const auto result = std::filesystem::file_size(image_path, filesystem_error);
    if (filesystem_error) {
        fail("Cannot determine the source ISO size: " + filesystem_error.message());
    }
    return result;
}

class SourceFileGuard final {
public:
    explicit SourceFileGuard(std::filesystem::path path)
        : path_(std::move(path)) {
#ifdef _WIN32
        handle_ = CreateFileW(
            path_.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            fail("Cannot lock the source ISO (Windows error " +
                std::to_string(GetLastError()) + ")");
        }

        BY_HANDLE_FILE_INFORMATION information{};
        if (GetFileInformationByHandle(handle_, &information) == FALSE) {
            const auto error = GetLastError();
            close();
            fail("Cannot inspect the locked source ISO (Windows error " +
                std::to_string(error) + ")");
        }
        if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            close();
            fail("The source ISO must be a plain regular file, not a reparse point");
        }
        if (GetFileInformationByHandleEx(
                handle_,
                FileIdInfo,
                &identity_,
                sizeof(identity_)) == FALSE) {
            const auto error = GetLastError();
            close();
            fail("Cannot determine the source ISO file identity (Windows error " +
                std::to_string(error) + ")");
        }
#else
        descriptor_ = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (descriptor_ < 0) {
            fail("Cannot lock the source ISO: " + std::string(std::strerror(errno)));
        }
        if (::fstat(descriptor_, &identity_) != 0) {
            const auto error = errno;
            close();
            fail("Cannot inspect the locked source ISO: " +
                std::string(std::strerror(error)));
        }
        if (!S_ISREG(identity_.st_mode)) {
            close();
            fail("The source ISO must be a plain regular file, not a symbolic link");
        }
#endif
    }

    SourceFileGuard(const SourceFileGuard&) = delete;
    SourceFileGuard& operator=(const SourceFileGuard&) = delete;

    ~SourceFileGuard() { close(); }

    void require_path_identity() const {
#ifdef _WIN32
        const auto current_handle = CreateFileW(
            path_.c_str(),
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (current_handle == INVALID_HANDLE_VALUE) {
            fail("The source ISO path changed while game files were being prepared");
        }
        BY_HANDLE_FILE_INFORMATION information{};
        FILE_ID_INFO current_identity{};
        const bool information_ok =
            GetFileInformationByHandle(current_handle, &information) != FALSE;
        const bool identity_ok = GetFileInformationByHandleEx(
            current_handle,
            FileIdInfo,
            &current_identity,
            sizeof(current_identity)) != FALSE;
        CloseHandle(current_handle);
        if (!information_ok || !identity_ok ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            current_identity.VolumeSerialNumber != identity_.VolumeSerialNumber ||
            std::memcmp(
                current_identity.FileId.Identifier,
                identity_.FileId.Identifier,
                sizeof(identity_.FileId.Identifier)) != 0) {
            fail("The source ISO path changed while game files were being prepared");
        }
#else
        const auto current_descriptor =
            ::open(path_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (current_descriptor < 0) {
            fail("The source ISO path changed while game files were being prepared");
        }
        struct stat current_identity {};
        const bool identity_ok = ::fstat(current_descriptor, &current_identity) == 0;
        ::close(current_descriptor);
        if (!identity_ok || !S_ISREG(current_identity.st_mode) ||
            current_identity.st_dev != identity_.st_dev ||
            current_identity.st_ino != identity_.st_ino) {
            fail("The source ISO path changed while game files were being prepared");
        }
#endif
    }

private:
    void close() noexcept {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (descriptor_ >= 0) {
            ::close(descriptor_);
            descriptor_ = -1;
        }
#endif
    }

    std::filesystem::path path_;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    FILE_ID_INFO identity_{};
#else
    int descriptor_ = -1;
    struct stat identity_ {};
#endif
};

struct SourceSnapshot {
    std::uintmax_t size = 0;
    std::filesystem::file_time_type last_write_time{};
};

[[nodiscard]] std::filesystem::path absolute_normalized_path(
    const std::filesystem::path& path,
    const char* description) {
    if (path.empty()) {
        fail(std::string(description) + " path is empty");
    }
    std::error_code filesystem_error;
    auto result = std::filesystem::absolute(path, filesystem_error);
    if (filesystem_error) {
        fail(std::string("Cannot make the ") + description +
            " path absolute: " + filesystem_error.message());
    }
    return result.lexically_normal();
}

[[nodiscard]] SourceSnapshot snapshot_source(const std::filesystem::path& image_path) {
    SourceSnapshot snapshot;
    snapshot.size = source_size(image_path);
    std::error_code filesystem_error;
    snapshot.last_write_time = std::filesystem::last_write_time(image_path, filesystem_error);
    if (filesystem_error) {
        fail("Cannot determine the source ISO modification time: " + filesystem_error.message());
    }
    return snapshot;
}

void require_unchanged_source(
    const std::filesystem::path& image_path,
    const SourceSnapshot& expected,
    const SourceFileGuard& guard) {
    guard.require_path_identity();
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(image_path, filesystem_error) || filesystem_error) {
        fail("The source ISO changed while game files were being prepared");
    }
    const auto current_size = std::filesystem::file_size(image_path, filesystem_error);
    if (filesystem_error || current_size != expected.size) {
        fail("The source ISO changed while game files were being prepared");
    }
    const auto current_last_write_time =
        std::filesystem::last_write_time(image_path, filesystem_error);
    if (filesystem_error || current_last_write_time != expected.last_write_time) {
        fail("The source ISO changed while game files were being prepared");
    }
    guard.require_path_identity();
}

[[nodiscard]] DiscInventory inventory_from_image(
    const std::filesystem::path& image_path,
    const iso9660::Image& image) {
    DiscInventory inventory;
    inventory.disc = inspect_disc(image_path);
    inventory.files.reserve(image.files().size());

    for (const auto& source_file : image.files()) {
        DiscFile file;
        file.iso_path = source_file.iso_path;
        file.output_path = source_file.output_path;
        file.size = source_file.size;
        file.extents.reserve(source_file.extents.size());

        for (const auto& source_extent : source_file.extents) {
            const auto data_block = static_cast<std::uint64_t>(source_extent.logical_block) +
                source_extent.extended_attribute_blocks;
            if (data_block > std::numeric_limits<std::uint32_t>::max()) {
                fail("ISO data extent block cannot be represented in the public inventory");
            }
            file.extents.push_back(DiscExtent{
                static_cast<std::uint32_t>(data_block),
                source_extent.byte_length});
        }

        inventory.total_file_bytes = checked_add(
            inventory.total_file_bytes,
            file.size,
            "the total extracted byte count");
        inventory.files.push_back(std::move(file));
    }
    return inventory;
}

[[nodiscard]] std::filesystem::path relative_output_path(const std::string& value) {
    if (value.empty() || value.front() == '/' || value.front() == '\\' ||
        value.find('\\') != std::string::npos) {
        fail("ISO reader returned an unsafe extraction path");
    }

    const auto result = path_from_utf8(value);
    if (result.empty() || result.is_absolute() || result.has_root_name() ||
        result.has_root_directory()) {
        fail("ISO reader returned an absolute extraction path");
    }
    for (const auto& component : result) {
        if (component.empty() || component == "." || component == "..") {
            fail("ISO reader returned a traversing extraction path");
        }
    }
    return result;
}

[[nodiscard]] std::string game_identifier(const GameId game) {
    switch (game) {
    case GameId::ratchet_and_clank_2002:
        return "ratchet_and_clank_2002";
    case GameId::unknown:
        return "unknown";
    }
    return "unknown";
}

[[nodiscard]] std::string serial_directory_name(const std::string& serial) {
    if (serial.empty()) {
        return "unknown";
    }
    const auto safe = std::all_of(serial.begin(), serial.end(), [](const char character) {
        return (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_';
    });
    if (!safe || serial == "." || serial == "..") {
        fail("Disc serial cannot be used as a preparation directory name");
    }
    return serial;
}

[[nodiscard]] std::string hash_image(
    const std::filesystem::path& image_path,
    const std::uintmax_t expected_size,
    const std::size_t file_count,
    const PreparationProgressCallback& progress) {
    std::ifstream input(image_path, std::ios::binary);
    if (!input) {
        fail("Cannot open the source ISO for hashing");
    }

    Sha256 hash;
    std::vector<std::byte> buffer(kImageHashChunkSize);
    std::uint64_t processed = 0;
    const auto source_name = path_to_utf8(image_path);
    publish_progress(progress, PreparationProgress{
        PreparationPhase::hashing_image,
        0,
        file_count,
        0,
        expected_size,
        source_name});

    while (processed < expected_size) {
        const auto requested = static_cast<std::size_t>(std::min<std::uintmax_t>(
            buffer.size(),
            expected_size - processed));
        input.read(
            reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(requested));
        if (input.gcount() != static_cast<std::streamsize>(requested)) {
            fail("Unexpected end of the source ISO while hashing");
        }

        hash.update(std::span<const std::byte>(buffer.data(), requested));
        processed += requested;
        publish_progress(progress, PreparationProgress{
            PreparationPhase::hashing_image,
            0,
            file_count,
            processed,
            expected_size,
            source_name});
    }
    return hex_digest(hash.finish());
}

class ExclusiveOutput final {
public:
    explicit ExclusiveOutput(const std::filesystem::path& path)
        : path_(path) {
#ifdef _WIN32
        handle_ = CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            fail(
                "Cannot exclusively create output file " + path_to_utf8(path) +
                " (Windows error " + std::to_string(GetLastError()) + ")");
        }
#else
        descriptor_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (descriptor_ < 0) {
            fail(
                "Cannot exclusively create output file " + path_to_utf8(path) +
                ": " + std::strerror(errno));
        }
#endif
    }

    ExclusiveOutput(const ExclusiveOutput&) = delete;
    ExclusiveOutput& operator=(const ExclusiveOutput&) = delete;

    ~ExclusiveOutput() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
#else
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
#endif
    }

    void write(const std::span<const std::byte> bytes) {
        std::size_t position = 0;
        while (position < bytes.size()) {
#ifdef _WIN32
            const auto count = static_cast<DWORD>(std::min<std::size_t>(
                bytes.size() - position,
                std::numeric_limits<DWORD>::max()));
            DWORD written = 0;
            if (WriteFile(handle_, bytes.data() + position, count, &written, nullptr) == FALSE ||
                written == 0) {
                fail(
                    "Failed to write output file " + path_to_utf8(path_) +
                    " (Windows error " + std::to_string(GetLastError()) + ")");
            }
            position += written;
#else
            const auto result = ::write(
                descriptor_,
                bytes.data() + position,
                bytes.size() - position);
            if (result < 0 && errno == EINTR) {
                continue;
            }
            if (result <= 0) {
                fail(
                    "Failed to write output file " + path_to_utf8(path_) +
                    ": " + std::strerror(errno));
            }
            position += static_cast<std::size_t>(result);
#endif
        }
    }

    void write(const std::string_view text) {
        write(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(text.data()),
            text.size()));
    }

    void close() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            const auto handle = handle_;
            handle_ = INVALID_HANDLE_VALUE;
            if (CloseHandle(handle) == FALSE) {
                fail(
                    "Failed to close output file " + path_to_utf8(path_) +
                    " (Windows error " + std::to_string(GetLastError()) + ")");
            }
        }
#else
        if (descriptor_ >= 0) {
            const auto descriptor = descriptor_;
            descriptor_ = -1;
            if (::close(descriptor) != 0) {
                fail(
                    "Failed to close output file " + path_to_utf8(path_) +
                    ": " + std::strerror(errno));
            }
        }
#endif
    }

private:
    std::filesystem::path path_;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

class StagingDirectory final {
public:
    explicit StagingDirectory(std::filesystem::path path)
        : path_(std::move(path)) {}

    StagingDirectory(const StagingDirectory&) = delete;
    StagingDirectory& operator=(const StagingDirectory&) = delete;

    ~StagingDirectory() {
        if (owned_) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    void release() noexcept { owned_ = false; }

private:
    std::filesystem::path path_;
    bool owned_ = true;
};

[[nodiscard]] std::filesystem::path create_staging_directory(
    const std::filesystem::path& parent,
    const std::string& image_sha256) {
    std::error_code filesystem_error;
    std::filesystem::create_directories(parent, filesystem_error);
    if (filesystem_error) {
        fail("Cannot create the game preparation directory: " + filesystem_error.message());
    }

    std::random_device random;
    const auto timestamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (std::size_t attempt = 0; attempt < kMaximumStagingAttempts; ++attempt) {
        std::ostringstream name;
        name << '.' << image_sha256 << ".tmp-" << std::hex << timestamp << '-'
             << static_cast<std::uint64_t>(random()) << '-' << attempt;
        const auto candidate = parent / name.str();

        filesystem_error.clear();
        if (std::filesystem::create_directory(candidate, filesystem_error)) {
            return candidate;
        }
        if (filesystem_error) {
            fail("Cannot create a staging directory: " + filesystem_error.message());
        }
    }
    fail("Could not reserve a unique game-file staging directory");
}

[[nodiscard]] std::string json_string(const std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() + 2U);
    result.push_back('"');
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (byte < 0x20U) {
                result += "\\u00";
                result.push_back(kHex[byte >> 4U]);
                result.push_back(kHex[byte & 0x0fU]);
            } else {
                result.push_back(character);
            }
            break;
        }
    }
    result.push_back('"');
    return result;
}

struct PreparedFile {
    std::string sha256;
};

struct ManifestSource {
    std::uintmax_t image_size = 0;
    std::string image_sha256;
    std::string volume_id;
    std::uint16_t logical_block_size = 0;
};

[[nodiscard]] std::string canonical_manifest_bytes(
    const ManifestSource& source,
    const DiscInventory& inventory,
    const std::vector<PreparedFile>& prepared_files,
    const std::size_t boot_index) {
    if (prepared_files.size() != inventory.files.size() || boot_index >= prepared_files.size()) {
        fail("Internal preparation manifest state is inconsistent");
    }

    std::string manifest;
    const auto emit = [&manifest](const std::string_view text) {
        if (text.size() > kMaximumManifestSize - manifest.size()) {
            fail("The canonical preparation manifest exceeds the supported size");
        }
        manifest.append(text);
    };
    emit("{\n");
    emit("  \"format\": \"openrc-prepared-game\",\n");
    emit("  \"format_version\": 1,\n");
    emit("  \"image_sha256\": " + json_string(source.image_sha256) + ",\n");
    emit("  \"source\": {\n");
    emit("    \"image_size\": " + std::to_string(source.image_size) + ",\n");
    emit("    \"image_sha256\": " + json_string(source.image_sha256) + ",\n");
    emit("    \"volume_id\": " + json_string(source.volume_id) + ",\n");
    emit("    \"logical_block_size\": " +
        std::to_string(source.logical_block_size) + "\n");
    emit("  },\n");
    emit("  \"game\": {\n");
    emit("    \"id\": " + json_string(game_identifier(inventory.disc.game)) + ",\n");
    emit("    \"title\": " + json_string(inventory.disc.title) + ",\n");
    emit("    \"serial\": " + json_string(inventory.disc.serial) + ",\n");
    emit("    \"region\": " + json_string(inventory.disc.region) + ",\n");
    emit(std::string("    \"supported_build\": ") +
        (inventory.disc.supported_build ? "true\n" : "false\n"));
    emit("  },\n");
    emit("  \"boot\": {\n");
    emit("    \"system_path\": " + json_string(inventory.disc.boot_path) + ",\n");
    emit("    \"iso_path\": " + json_string(inventory.disc.boot_iso_path) + ",\n");
    emit("    \"output_path\": " +
        json_string(inventory.files[boot_index].output_path) + ",\n");
    emit("    \"size\": " + std::to_string(inventory.files[boot_index].size) + ",\n");
    emit("    \"sha256\": " + json_string(prepared_files[boot_index].sha256) + "\n");
    emit("  },\n");
    emit("  \"files\": [\n");

    for (std::size_t file_index = 0; file_index < inventory.files.size(); ++file_index) {
        const auto& file = inventory.files[file_index];
        emit("    {\n");
        emit("      \"iso_path\": " + json_string(file.iso_path) + ",\n");
        emit("      \"output_path\": " + json_string(file.output_path) + ",\n");
        emit("      \"size\": " + std::to_string(file.size) + ",\n");
        emit("      \"sha256\": " + json_string(prepared_files[file_index].sha256) + ",\n");
        emit("      \"extents\": [");
        if (!file.extents.empty()) {
            emit("\n");
            for (std::size_t extent_index = 0; extent_index < file.extents.size(); ++extent_index) {
                const auto& extent = file.extents[extent_index];
                emit("        {\"logical_block\": " + std::to_string(extent.logical_block) +
                    ", \"byte_length\": " + std::to_string(extent.byte_length) + "}");
                emit(extent_index + 1U == file.extents.size() ? "\n" : ",\n");
            }
            emit("      ]\n");
        } else {
            emit("]\n");
        }
        emit(file_index + 1U == inventory.files.size() ? "    }\n" : "    },\n");
    }
    emit("  ]\n");
    emit("}\n");
    return manifest;
}

void write_manifest(
    const std::filesystem::path& path,
    const ManifestSource& source,
    const DiscInventory& inventory,
    const std::vector<PreparedFile>& prepared_files,
    const std::size_t boot_index) {
    const auto manifest = canonical_manifest_bytes(
        source,
        inventory,
        prepared_files,
        boot_index);
    ExclusiveOutput output(path);
    output.write(manifest);
    output.close();
}

[[nodiscard]] bool is_plain_directory(const std::filesystem::path& path) {
    std::error_code filesystem_error;
    const auto status = std::filesystem::symlink_status(path, filesystem_error);
    if (filesystem_error || status.type() != std::filesystem::file_type::directory) {
        return false;
    }
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return false;
    }
#endif
    return true;
}

[[nodiscard]] bool is_plain_regular_file(const std::filesystem::path& path) {
    std::error_code filesystem_error;
    const auto status = std::filesystem::symlink_status(path, filesystem_error);
    if (filesystem_error || status.type() != std::filesystem::file_type::regular) {
        return false;
    }
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }
#endif
    return true;
}

void require_plain_existing_directory_path(
    const std::filesystem::path& path,
    const char* description) {
    const auto root = path.root_path();
    if (root.empty() || !is_plain_directory(root)) {
        fail(std::string("The ") + description +
            " root is not a plain directory (links and reparse points are not allowed)");
    }

    auto current = root;
    for (const auto& component : path.relative_path()) {
        current /= component;
        std::error_code filesystem_error;
        const auto status = std::filesystem::symlink_status(current, filesystem_error);
        if (filesystem_error == std::errc::no_such_file_or_directory ||
            (!filesystem_error && status.type() == std::filesystem::file_type::not_found)) {
            return;
        }
        if (filesystem_error) {
            fail(std::string("Cannot inspect the ") + description +
                " path: " + filesystem_error.message());
        }
        if (!is_plain_directory(current)) {
            fail(std::string("An existing component of the ") + description +
                " path is not a plain directory (links and reparse points are not allowed)");
        }
    }
}

[[nodiscard]] bool output_parents_are_plain(
    const std::filesystem::path& files_directory,
    const std::filesystem::path& relative_parent) {
    if (!is_plain_directory(files_directory)) {
        return false;
    }
    auto current = files_directory;
    for (const auto& component : relative_parent) {
        current /= component;
        if (!is_plain_directory(current)) {
            return false;
        }
    }
    return true;
}

void ensure_plain_output_parents(
    const std::filesystem::path& files_directory,
    const std::filesystem::path& relative_parent) {
    if (!is_plain_directory(files_directory)) {
        fail("The staging files path is not a plain directory");
    }
    auto current = files_directory;
    for (const auto& component : relative_parent) {
        current /= component;
        std::error_code filesystem_error;
        const auto status = std::filesystem::symlink_status(current, filesystem_error);
        const bool missing =
            filesystem_error == std::errc::no_such_file_or_directory ||
            (!filesystem_error && status.type() == std::filesystem::file_type::not_found);
        if (filesystem_error && !missing) {
            fail("Cannot inspect an extraction directory: " + filesystem_error.message());
        }
        if (missing) {
            filesystem_error.clear();
            if (!std::filesystem::create_directory(current, filesystem_error) &&
                filesystem_error) {
                fail("Cannot create an extraction directory: " + filesystem_error.message());
            }
        }
        if (!is_plain_directory(current)) {
            fail("An extraction path parent is a link, reparse point, or non-directory");
        }
    }
}

[[nodiscard]] bool hash_plain_regular_file_exact(
    const std::filesystem::path& path,
    const std::uint64_t expected_size,
    std::string& digest,
    const PreparationProgressCallback& progress = {},
    const PreparationProgress& progress_state = {}) {
    if (!is_plain_regular_file(path)) {
        return false;
    }

    std::error_code filesystem_error;
    const auto size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error || size != expected_size) {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    Sha256 hash;
    std::vector<std::byte> buffer(kImageHashChunkSize);
    std::uint64_t remaining = expected_size;
    std::uint64_t processed = 0;
    while (remaining != 0) {
        const auto requested = static_cast<std::size_t>(std::min<std::uint64_t>(
            buffer.size(),
            remaining));
        input.read(
            reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(requested));
        if (input.gcount() != static_cast<std::streamsize>(requested)) {
            return false;
        }
        hash.update(std::span<const std::byte>(buffer.data(), requested));
        remaining -= requested;
        processed += requested;
        if (progress) {
            auto update = progress_state;
            update.bytes_processed = checked_add(
                progress_state.bytes_processed,
                processed,
                "the verified byte count");
            publish_progress(progress, update);
        }
    }

    char trailing_byte = 0;
    input.read(&trailing_byte, 1);
    if (input.gcount() != 0 || !is_plain_regular_file(path)) {
        return false;
    }

    filesystem_error.clear();
    if (std::filesystem::file_size(path, filesystem_error) != expected_size ||
        filesystem_error) {
        return false;
    }
    if (expected_size == 0 && progress) {
        publish_progress(progress, progress_state);
    }
    digest = hex_digest(hash.finish());
    return true;
}

[[nodiscard]] std::string hash_bytes(const std::string_view bytes) {
    Sha256 hash;
    hash.update(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(bytes.data()),
        bytes.size()));
    return hex_digest(hash.finish());
}

[[nodiscard]] bool prepared_destination_is_valid(
    const std::filesystem::path& destination,
    const ManifestSource& source,
    const DiscInventory& inventory,
    const std::size_t boot_index,
    const PreparationProgressCallback& progress) {
    const auto files_directory = destination / "files";
    if (!is_plain_directory(destination) || !is_plain_directory(files_directory) ||
        boot_index >= inventory.files.size()) {
        return false;
    }

    std::vector<PreparedFile> prepared_files;
    prepared_files.reserve(inventory.files.size());
    std::uint64_t verified_bytes = 0;
    publish_progress(progress, PreparationProgress{
        PreparationPhase::verifying_files,
        0,
        inventory.files.size(),
        0,
        inventory.total_file_bytes,
        {}});
    for (std::size_t file_index = 0; file_index < inventory.files.size(); ++file_index) {
        const auto& file = inventory.files[file_index];
        const auto relative_path = relative_output_path(file.output_path);
        if (!output_parents_are_plain(files_directory, relative_path.parent_path())) {
            return false;
        }
        std::string digest;
        if (!hash_plain_regular_file_exact(
                files_directory / relative_path,
                file.size,
                digest,
                progress,
                PreparationProgress{
                    PreparationPhase::verifying_files,
                    file_index + 1U,
                    inventory.files.size(),
                    verified_bytes,
                    inventory.total_file_bytes,
                    file.output_path})) {
            return false;
        }
        verified_bytes = checked_add(
            verified_bytes,
            file.size,
            "the verified byte count");
        prepared_files.push_back(PreparedFile{std::move(digest)});
    }
    if (!inventory.disc.boot_sha256.empty() &&
        inventory.disc.boot_sha256 != prepared_files[boot_index].sha256) {
        return false;
    }

    const auto canonical = canonical_manifest_bytes(
        source,
        inventory,
        prepared_files,
        boot_index);
    const auto manifest_path = destination / "manifest.json";
    if (!is_plain_regular_file(manifest_path)) {
        return false;
    }
    std::error_code filesystem_error;
    const auto manifest_size = std::filesystem::file_size(manifest_path, filesystem_error);
    if (filesystem_error || manifest_size > kMaximumManifestSize ||
        manifest_size != canonical.size()) {
        return false;
    }
    std::string actual_manifest_digest;
    return hash_plain_regular_file_exact(
               manifest_path,
               static_cast<std::uint64_t>(canonical.size()),
               actual_manifest_digest) &&
        actual_manifest_digest == hash_bytes(canonical);
}

enum class CommitResult {
    committed,
    destination_exists,
};

[[nodiscard]] CommitResult commit_directory_no_replace(
    const std::filesystem::path& staging,
    const std::filesystem::path& destination) {
#ifdef _WIN32
    if (MoveFileExW(staging.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE) {
        return CommitResult::committed;
    }
    const auto error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) {
        return CommitResult::destination_exists;
    }
    fail("Cannot finalize prepared game files (Windows error " + std::to_string(error) + ")");
#elif defined(__linux__) && defined(SYS_renameat2)
    if (::syscall(
            SYS_renameat2,
            AT_FDCWD,
            staging.c_str(),
            AT_FDCWD,
            destination.c_str(),
            1U) == 0) {
        return CommitResult::committed;
    }
    if (errno == EEXIST || errno == ENOTEMPTY) {
        return CommitResult::destination_exists;
    }
    fail("Cannot finalize prepared game files: " + std::string(std::strerror(errno)));
#elif defined(__APPLE__)
    if (::renameatx_np(
            AT_FDCWD,
            staging.c_str(),
            AT_FDCWD,
            destination.c_str(),
            RENAME_EXCL) == 0) {
        return CommitResult::committed;
    }
    if (errno == EEXIST || errno == ENOTEMPTY) {
        return CommitResult::destination_exists;
    }
    fail("Cannot finalize prepared game files: " + std::string(std::strerror(errno)));
#else
    static_cast<void>(staging);
    static_cast<void>(destination);
    fail("This platform does not provide an atomic no-replace directory rename");
#endif
}

[[nodiscard]] PreparationResult make_result(
    const std::filesystem::path& destination,
    const std::filesystem::path& boot_relative_path,
    const std::string& image_sha256,
    const DiscInventory& inventory,
    const bool already_prepared) {
    PreparationResult result;
    result.destination = destination;
    result.manifest_path = destination / "manifest.json";
    result.boot_executable_path = destination / "files" / boot_relative_path;
    result.image_sha256 = image_sha256;
    result.file_count = inventory.files.size();
    result.total_file_bytes = inventory.total_file_bytes;
    result.already_prepared = already_prepared;
    return result;
}

} // namespace

PreparationCancelled::PreparationCancelled()
    : std::runtime_error("Game file preparation was cancelled") {}

DiscInventory inventory_disc(const std::filesystem::path& image_path) {
    const auto normalized_image_path = absolute_normalized_path(image_path, "source ISO");
    const auto image = iso9660::Image::open(normalized_image_path);
    return inventory_from_image(normalized_image_path, image);
}

PreparationResult prepare_game_files(
    const std::filesystem::path& image_path,
    const std::filesystem::path& games_directory,
    const PreparationProgressCallback& progress) {
    const auto normalized_image_path = absolute_normalized_path(image_path, "source ISO");
    const auto normalized_games_directory =
        absolute_normalized_path(games_directory, "games directory");
    const SourceFileGuard source_guard(normalized_image_path);
    const auto source_snapshot = snapshot_source(normalized_image_path);
    require_plain_existing_directory_path(normalized_games_directory, "games directory");
    publish_progress(progress, PreparationProgress{
        PreparationPhase::scanning,
        0,
        0,
        0,
        0,
        path_to_utf8(normalized_image_path)});

    const auto image = iso9660::Image::open(normalized_image_path);
    if (image.metadata().image_bytes != source_snapshot.size) {
        fail("The source ISO size changed while it was being inventoried");
    }
    const auto inventory = inventory_from_image(normalized_image_path, image);
    require_unchanged_source(normalized_image_path, source_snapshot, source_guard);
    publish_progress(progress, PreparationProgress{
        PreparationPhase::scanning,
        inventory.files.size(),
        inventory.files.size(),
        inventory.total_file_bytes,
        inventory.total_file_bytes,
        {}});
    require_unchanged_source(normalized_image_path, source_snapshot, source_guard);

    const auto* boot_file = image.find_exact(inventory.disc.boot_iso_path);
    if (inventory.disc.boot_iso_path.empty() || boot_file == nullptr) {
        fail("The exact BOOT executable path was not found in the ISO inventory");
    }
    const auto boot_iterator = std::lower_bound(
        inventory.files.begin(),
        inventory.files.end(),
        inventory.disc.boot_iso_path,
        [](const DiscFile& file, const std::string& iso_path) {
            return file.iso_path < iso_path;
        });
    if (boot_iterator == inventory.files.end() ||
        boot_iterator->iso_path != inventory.disc.boot_iso_path) {
        fail("The BOOT executable is missing from the public disc inventory");
    }
    const auto boot_index = static_cast<std::size_t>(
        std::distance(inventory.files.begin(), boot_iterator));
    const auto boot_relative_path = relative_output_path(boot_file->output_path);

    const auto image_sha256 = hash_image(
        normalized_image_path,
        source_snapshot.size,
        inventory.files.size(),
        progress);
    require_unchanged_source(normalized_image_path, source_snapshot, source_guard);

    const ManifestSource manifest_source{
        source_snapshot.size,
        image_sha256,
        image.metadata().volume_identifier,
        image.metadata().logical_block_size};

    require_plain_existing_directory_path(normalized_games_directory, "games directory");
    const auto serial_directory =
        normalized_games_directory / serial_directory_name(inventory.disc.serial);
    require_plain_existing_directory_path(serial_directory, "game serial directory");
    const auto destination = serial_directory / image_sha256;
    std::error_code filesystem_error;
    const auto destination_status = std::filesystem::symlink_status(destination, filesystem_error);
    if (filesystem_error &&
        filesystem_error != std::errc::no_such_file_or_directory) {
        fail("Cannot inspect the prepared-game destination: " + filesystem_error.message());
    }
    if (!filesystem_error && std::filesystem::exists(destination_status)) {
        if (!prepared_destination_is_valid(
                destination,
                manifest_source,
                inventory,
                boot_index,
                progress)) {
            fail("The prepared-game destination already exists but is incomplete or belongs to another image");
        }
        publish_progress(progress, PreparationProgress{
            PreparationPhase::finalizing,
            inventory.files.size(),
            inventory.files.size(),
            inventory.total_file_bytes,
            inventory.total_file_bytes,
            path_to_utf8(destination)});
        require_unchanged_source(normalized_image_path, source_snapshot, source_guard);
        return make_result(
            destination,
            boot_relative_path,
            image_sha256,
            inventory,
            true);
    }

    StagingDirectory staging(create_staging_directory(serial_directory, image_sha256));
    require_plain_existing_directory_path(normalized_games_directory, "games directory");
    require_plain_existing_directory_path(serial_directory, "game serial directory");
    if (!is_plain_directory(staging.path())) {
        fail("A game preparation directory is a link, reparse point, or non-directory");
    }
    const auto files_directory = staging.path() / "files";
    filesystem_error.clear();
    if (!std::filesystem::create_directory(files_directory, filesystem_error) || filesystem_error) {
        fail("Cannot create the staging files directory: " + filesystem_error.message());
    }

    std::vector<PreparedFile> prepared_files;
    prepared_files.reserve(inventory.files.size());
    std::uint64_t total_written = 0;
    publish_progress(progress, PreparationProgress{
        PreparationPhase::extracting_files,
        0,
        inventory.files.size(),
        0,
        inventory.total_file_bytes,
        {}});

    for (std::size_t file_index = 0; file_index < image.files().size(); ++file_index) {
        const auto& source_file = image.files()[file_index];
        const auto relative_path = relative_output_path(source_file.output_path);
        const auto output_path = files_directory / relative_path;
        ensure_plain_output_parents(files_directory, relative_path.parent_path());

        ExclusiveOutput output(output_path);
        Sha256 file_hash;
        std::uint64_t file_written = 0;
        image.stream_file(source_file, [&](const std::span<const std::byte> chunk) {
            if (chunk.size() > source_file.size - file_written) {
                fail("ISO reader streamed more bytes than the file inventory declares");
            }
            output.write(chunk);
            file_hash.update(chunk);
            file_written += chunk.size();
            total_written = checked_add(total_written, chunk.size(), "the extracted byte count");
            if (total_written > inventory.total_file_bytes) {
                fail("Extracted byte count exceeds the disc inventory");
            }
            publish_progress(progress, PreparationProgress{
                PreparationPhase::extracting_files,
                file_index + 1U,
                inventory.files.size(),
                total_written,
                inventory.total_file_bytes,
                source_file.output_path});
        });
        output.close();
        if (file_written != source_file.size) {
            fail("Extracted file byte count does not match the disc inventory");
        }
        prepared_files.push_back(PreparedFile{hex_digest(file_hash.finish())});
    }

    if (total_written != inventory.total_file_bytes) {
        fail("Total extracted byte count does not match the disc inventory");
    }
    if (!inventory.disc.boot_sha256.empty() &&
        inventory.disc.boot_sha256 != prepared_files[boot_index].sha256) {
        fail("The extracted BOOT executable hash changed during preparation");
    }
    require_unchanged_source(normalized_image_path, source_snapshot, source_guard);

    publish_progress(progress, PreparationProgress{
        PreparationPhase::writing_manifest,
        inventory.files.size(),
        inventory.files.size(),
        inventory.total_file_bytes,
        inventory.total_file_bytes,
        "manifest.json"});
    write_manifest(
        staging.path() / "manifest.json",
        manifest_source,
        inventory,
        prepared_files,
        boot_index);
    require_unchanged_source(normalized_image_path, source_snapshot, source_guard);

    publish_progress(progress, PreparationProgress{
        PreparationPhase::finalizing,
        inventory.files.size(),
        inventory.files.size(),
        inventory.total_file_bytes,
        inventory.total_file_bytes,
        path_to_utf8(destination)});
    require_unchanged_source(normalized_image_path, source_snapshot, source_guard);
    require_plain_existing_directory_path(normalized_games_directory, "games directory");
    require_plain_existing_directory_path(serial_directory, "game serial directory");
    const auto commit_result = commit_directory_no_replace(staging.path(), destination);
    if (commit_result == CommitResult::committed) {
        staging.release();
        return make_result(
            destination,
            boot_relative_path,
            image_sha256,
            inventory,
            false);
    }

    if (!prepared_destination_is_valid(
            destination,
            manifest_source,
            inventory,
            boot_index,
            progress)) {
        fail("Another preparation created an invalid or conflicting destination");
    }
    return make_result(
        destination,
        boot_relative_path,
        image_sha256,
        inventory,
        true);
}

} // namespace openrc
