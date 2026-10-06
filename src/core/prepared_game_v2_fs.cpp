#include "openrc/prepared_game_v2_fs.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw PreparedGameV2FilesystemError(message);
}

void validate_limits(const PreparedGameV2FilesystemLimitsV1 &limits) {
  if (limits.manifest.max_input_bytes < kPreparedGameHeaderBytesV2 ||
      limits.level_package.max_input_bytes < kLevelPackageHeaderBytesV1) {
    fail("PreparedGameV2 filesystem byte limits are too small");
  }
}

[[nodiscard]] std::filesystem::path
normalize_explicit_root(const std::filesystem::path &root) {
  if (root.empty() || !root.is_absolute() || root.root_path().empty()) {
    fail("The PreparedGameV2 root must be an explicit absolute path");
  }
  for (const auto &component : root.relative_path()) {
    if (component.empty() || component == "." || component == "..") {
      fail("The PreparedGameV2 root contains a non-canonical component");
    }
  }
  const auto normalized = root.lexically_normal();
  if (normalized.empty() || !normalized.is_absolute()) {
    fail("The PreparedGameV2 root cannot be normalized safely");
  }
  return normalized;
}

[[nodiscard]] bool
canonical_path_character(const unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('a') &&
          value <= static_cast<unsigned char>('z')) ||
         (value >= static_cast<unsigned char>('0') &&
          value <= static_cast<unsigned char>('9')) ||
         value == static_cast<unsigned char>('.') ||
         value == static_cast<unsigned char>('_') ||
         value == static_cast<unsigned char>('-') ||
         value == static_cast<unsigned char>('/');
}

[[nodiscard]] std::filesystem::path
canonical_relative_path(const std::string_view encoded) {
  if (encoded.empty() || encoded.front() == '/' || encoded.back() == '/') {
    fail("A PreparedGameV2 file reference is not a canonical relative path");
  }
  for (const auto character : encoded) {
    if (!canonical_path_character(static_cast<unsigned char>(character))) {
      fail("A PreparedGameV2 file reference contains an unsafe character");
    }
  }

  std::size_t begin = 0U;
  while (begin < encoded.size()) {
    const auto separator = encoded.find('/', begin);
    const auto end =
        separator == std::string_view::npos ? encoded.size() : separator;
    const auto component = encoded.substr(begin, end - begin);
    if (component.empty() || component == "." || component == "..") {
      fail("A PreparedGameV2 file reference contains path traversal");
    }
    if (separator == std::string_view::npos) {
      break;
    }
    begin = separator + 1U;
  }

  const std::filesystem::path path(encoded);
  if (path.empty() || path.is_absolute() || path.has_root_name() ||
      path.has_root_directory() || path.lexically_normal() != path) {
    fail("A PreparedGameV2 file reference is not root-relative");
  }
  return path;
}

[[nodiscard]] std::size_t checked_host_size(const std::uint64_t value,
                                            const char *const description) {
  if (value > std::numeric_limits<std::size_t>::max()) {
    fail(std::string(description) + " exceeds host size_t");
  }
  return static_cast<std::size_t>(value);
}

void validate_file_size(const std::uint64_t file_bytes,
                        const std::uint64_t maximum_bytes,
                        const std::optional<std::uint64_t> expected_bytes) {
  if (file_bytes > maximum_bytes) {
    fail("A PreparedGameV2 file exceeds its caller byte limit");
  }
  if (expected_bytes && file_bytes != *expected_bytes) {
    fail("A PreparedGameV2 file size disagrees with its manifest");
  }
  static_cast<void>(checked_host_size(file_bytes, "A PreparedGameV2 file"));
}

#ifdef _WIN32

class NativeHandle final {
public:
  explicit NativeHandle(const HANDLE handle = INVALID_HANDLE_VALUE) noexcept
      : handle_(handle) {}

  ~NativeHandle() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
  }

  NativeHandle(const NativeHandle &) = delete;
  NativeHandle &operator=(const NativeHandle &) = delete;

  NativeHandle(NativeHandle &&other) noexcept
      : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}

  NativeHandle &operator=(NativeHandle &&other) noexcept {
    if (this != &other) {
      if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
      }
      handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
    }
    return *this;
  }

  [[nodiscard]] HANDLE get() const noexcept { return handle_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return handle_ != INVALID_HANDLE_VALUE;
  }

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

[[nodiscard]] NativeHandle
open_plain_directory(const std::filesystem::path &path) {
  NativeHandle handle(CreateFileW(
      path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!handle) {
    fail("Cannot open a PreparedGameV2 directory component (Windows error " +
         std::to_string(GetLastError()) + ")");
  }

  BY_HANDLE_FILE_INFORMATION information{};
  if (GetFileInformationByHandle(handle.get(), &information) == FALSE ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    fail("A PreparedGameV2 directory component is not a plain directory");
  }
  return handle;
}

[[nodiscard]] std::vector<NativeHandle>
lock_plain_directory_chain(const std::filesystem::path &root,
                           const std::filesystem::path &relative_parent) {
  std::vector<NativeHandle> handles;
  auto current = root.root_path();
  handles.push_back(open_plain_directory(current));
  for (const auto &component : root.relative_path()) {
    current /= component;
    handles.push_back(open_plain_directory(current));
  }
  for (const auto &component : relative_parent) {
    current /= component;
    handles.push_back(open_plain_directory(current));
  }
  return handles;
}

[[nodiscard]] std::vector<std::byte>
read_plain_regular_file(const std::filesystem::path &root,
                        const std::filesystem::path &relative,
                        const std::uint64_t maximum_bytes,
                        const std::optional<std::uint64_t> expected_bytes) {
  const auto directory_locks =
      lock_plain_directory_chain(root, relative.parent_path());
  static_cast<void>(directory_locks);
  const auto path = root / relative;
  NativeHandle file(CreateFileW(
      path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
          FILE_FLAG_SEQUENTIAL_SCAN,
      nullptr));
  if (!file) {
    fail("Cannot open a PreparedGameV2 file (Windows error " +
         std::to_string(GetLastError()) + ")");
  }

  BY_HANDLE_FILE_INFORMATION information{};
  LARGE_INTEGER native_size{};
  if (GetFileInformationByHandle(file.get(), &information) == FALSE ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
      GetFileSizeEx(file.get(), &native_size) == FALSE ||
      native_size.QuadPart < 0) {
    fail("A PreparedGameV2 input is not a plain regular file");
  }
  const auto file_bytes = static_cast<std::uint64_t>(native_size.QuadPart);
  validate_file_size(file_bytes, maximum_bytes, expected_bytes);

  std::vector<std::byte> result(
      checked_host_size(file_bytes, "A PreparedGameV2 file"));
  std::size_t position = 0U;
  while (position < result.size()) {
    const auto count = static_cast<DWORD>(std::min<std::size_t>(
        result.size() - position, std::numeric_limits<DWORD>::max()));
    DWORD read_bytes = 0U;
    if (ReadFile(file.get(), result.data() + position, count, &read_bytes,
                 nullptr) == FALSE ||
        read_bytes == 0U) {
      fail("A PreparedGameV2 file was truncated while reading");
    }
    position += read_bytes;
  }

  std::byte extra{};
  DWORD extra_bytes = 0U;
  if (ReadFile(file.get(), &extra, 1U, &extra_bytes, nullptr) == FALSE ||
      extra_bytes != 0U || GetFileSizeEx(file.get(), &native_size) == FALSE ||
      native_size.QuadPart < 0 ||
      static_cast<std::uint64_t>(native_size.QuadPart) != file_bytes) {
    fail("A PreparedGameV2 file changed while reading");
  }
  return result;
}

#else

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

class NativeDescriptor final {
public:
  explicit NativeDescriptor(const int descriptor = -1) noexcept
      : descriptor_(descriptor) {}

  ~NativeDescriptor() {
    if (descriptor_ >= 0) {
      ::close(descriptor_);
    }
  }

  NativeDescriptor(const NativeDescriptor &) = delete;
  NativeDescriptor &operator=(const NativeDescriptor &) = delete;

  NativeDescriptor(NativeDescriptor &&other) noexcept
      : descriptor_(std::exchange(other.descriptor_, -1)) {}

  NativeDescriptor &operator=(NativeDescriptor &&other) noexcept {
    if (this != &other) {
      if (descriptor_ >= 0) {
        ::close(descriptor_);
      }
      descriptor_ = std::exchange(other.descriptor_, -1);
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return descriptor_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return descriptor_ >= 0;
  }

private:
  int descriptor_ = -1;
};

[[nodiscard]] NativeDescriptor
open_plain_child_directory(const int parent,
                           const std::filesystem::path &component) {
  NativeDescriptor result(
      ::openat(parent, component.c_str(),
               O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (!result) {
    fail("Cannot open a plain PreparedGameV2 directory component");
  }
  return result;
}

[[nodiscard]] std::vector<std::byte>
read_plain_regular_file(const std::filesystem::path &root,
                        const std::filesystem::path &relative,
                        const std::uint64_t maximum_bytes,
                        const std::optional<std::uint64_t> expected_bytes) {
  NativeDescriptor current(
      ::open(root.root_path().c_str(),
             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (!current) {
    fail("Cannot open the PreparedGameV2 filesystem root");
  }
  for (const auto &component : root.relative_path()) {
    current = open_plain_child_directory(current.get(), component);
  }
  for (const auto &component : relative.parent_path()) {
    current = open_plain_child_directory(current.get(), component);
  }

  NativeDescriptor file(::openat(current.get(), relative.filename().c_str(),
                                 O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
  if (!file) {
    fail("Cannot open a plain PreparedGameV2 file");
  }
  struct stat information{};
  if (::fstat(file.get(), &information) != 0 || !S_ISREG(information.st_mode) ||
      information.st_size < 0) {
    fail("A PreparedGameV2 input is not a plain regular file");
  }
  const auto file_bytes = static_cast<std::uint64_t>(information.st_size);
  validate_file_size(file_bytes, maximum_bytes, expected_bytes);

  std::vector<std::byte> result(
      checked_host_size(file_bytes, "A PreparedGameV2 file"));
  std::size_t position = 0U;
  while (position < result.size()) {
    const auto read_bytes =
        ::read(file.get(), result.data() + position, result.size() - position);
    if (read_bytes < 0 && errno == EINTR) {
      continue;
    }
    if (read_bytes <= 0) {
      fail("A PreparedGameV2 file was truncated while reading");
    }
    position += static_cast<std::size_t>(read_bytes);
  }

  std::byte extra{};
  ssize_t extra_bytes = 0;
  do {
    extra_bytes = ::read(file.get(), &extra, 1U);
  } while (extra_bytes < 0 && errno == EINTR);
  struct stat final_information{};
  if (extra_bytes != 0 || ::fstat(file.get(), &final_information) != 0 ||
      final_information.st_size != information.st_size) {
    fail("A PreparedGameV2 file changed while reading");
  }
  return result;
}

#endif

[[nodiscard]] std::vector<std::byte> read_prepared_file(
    const std::filesystem::path &root, const std::string_view relative_path,
    const std::uint64_t maximum_bytes,
    const std::optional<std::uint64_t> expected_bytes = std::nullopt) {
  const auto normalized_root = normalize_explicit_root(root);
  const auto relative = canonical_relative_path(relative_path);
  return read_plain_regular_file(normalized_root, relative, maximum_bytes,
                                 expected_bytes);
}

} // namespace

PreparedGameV2RootV1
load_prepared_game_v2_root_v1(const std::filesystem::path &prepared_root,
                              const PreparedGameV2FilesystemLimitsV1 limits) {
  validate_limits(limits);
  const auto normalized_root = normalize_explicit_root(prepared_root);
  const auto manifest_bytes =
      read_prepared_file(normalized_root, kPreparedGameV2ManifestFileName,
                         limits.manifest.max_input_bytes);

  PreparedGameV2RootV1 result;
  result.root = normalized_root;
  result.manifest_sha256 = prepared_content_sha256_v1(manifest_bytes);
  try {
    result.manifest = parse_prepared_game_v2(manifest_bytes, limits.manifest);
  } catch (const PreparedGameV2Error &error) {
    fail("Invalid prepared-v2.orpg: " + std::string(error.what()));
  }
  return result;
}

LevelPackageV1 load_prepared_game_level_package_v1(
    const PreparedGameV2RootV1 &prepared, const std::uint32_t level_id,
    const PreparedGameV2FilesystemLimitsV1 limits) {
  validate_limits(limits);
  const auto *const reference =
      find_prepared_game_level_v2(prepared.manifest, level_id);
  if (reference == nullptr) {
    fail("PreparedGameV2 does not reference the requested level");
  }
  if (reference->package_bytes > limits.level_package.max_input_bytes) {
    fail("A referenced LevelPackageV1 exceeds its caller byte limit");
  }
  const auto package_bytes = read_prepared_file(
      prepared.root, reference->package_path,
      limits.level_package.max_input_bytes, reference->package_bytes);
  try {
    return parse_prepared_game_level_package_v1(
        prepared.manifest, level_id, package_bytes, limits.level_package);
  } catch (const PreparedGameV2Error &error) {
    fail("Invalid referenced LevelPackageV1: " + std::string(error.what()));
  }
}

LevelPackageV1 load_prepared_game_shared_package_v1(
    const PreparedGameV2RootV1 &prepared,
    const PreparedGameV2FilesystemLimitsV1 limits) {
  validate_limits(limits);
  if (!prepared.manifest.shared_package) {
    fail("PreparedGameV2 does not reference a shared package");
  }
  const auto &reference = *prepared.manifest.shared_package;
  if (reference.package_bytes > limits.level_package.max_input_bytes) {
    fail("A referenced shared LevelPackageV1 exceeds its caller byte limit");
  }
  const auto package_bytes = read_prepared_file(
      prepared.root, reference.package_path,
      limits.level_package.max_input_bytes, reference.package_bytes);
  try {
    return parse_prepared_game_shared_package_v1(
        prepared.manifest, package_bytes, limits.level_package);
  } catch (const PreparedGameV2Error &error) {
    fail("Invalid referenced shared LevelPackageV1: " + std::string(error.what()));
  }
}

ResolvedLevelPackageV1 load_resolved_prepared_game_level_package_v1(
    const PreparedGameV2RootV1 &prepared, const std::uint32_t level_id,
    const std::span<const ExplicitLevelPackageOverlayBytesV1> explicit_overlays,
    const PreparedGameV2FilesystemLimitsV1 limits) {
  validate_limits(limits);
  if (explicit_overlays.size() > limits.level_package.max_overlays_to_resolve) {
    fail("Explicit LevelPackageV1 overlays exceed their count limit");
  }

  std::uint64_t total_overlay_bytes = 0U;
  std::vector<LevelPackageV1> overlays;
  overlays.reserve(explicit_overlays.size());
  for (const auto &explicit_overlay : explicit_overlays) {
    if (explicit_overlay.bytes.empty() ||
        explicit_overlay.bytes.size() > limits.level_package.max_input_bytes ||
        explicit_overlay.bytes.size() >
            limits.max_total_explicit_overlay_bytes - total_overlay_bytes) {
      fail("Explicit LevelPackageV1 overlay bytes exceed caller limits");
    }
    total_overlay_bytes += explicit_overlay.bytes.size();
    try {
      overlays.push_back(
          parse_level_package_v1(explicit_overlay.bytes, limits.level_package));
    } catch (const LevelPackageV1Error &error) {
      fail("Invalid explicit LevelPackageV1 overlay: " +
           std::string(error.what()));
    }
  }

  const auto base =
      load_prepared_game_level_package_v1(prepared, level_id, limits);
  try {
    return resolve_level_package_v1(base, overlays, limits.level_package);
  } catch (const LevelPackageV1Error &error) {
    fail("Cannot resolve explicit LevelPackageV1 overlays: " +
         std::string(error.what()));
  }
}

} // namespace openrc
