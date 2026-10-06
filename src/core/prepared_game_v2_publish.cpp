#include "openrc/prepared_game_v2_publish.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#if defined(__linux__)
#include <linux/stat.h>
#include <sys/syscall.h>
#endif
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw PreparedGameV2PublishError(message);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows");
  }
  return left + right;
}

[[nodiscard]] std::filesystem::path
normalize_destination(const std::filesystem::path &destination) {
  if (destination.empty() || !destination.is_absolute() ||
      destination.root_path().empty()) {
    fail("The publication destination must be an explicit absolute path");
  }
  for (const auto &component : destination.relative_path()) {
    if (component.empty() || component == "." || component == "..") {
      fail("The publication destination contains a non-canonical "
                 "component");
    }
  }

  const auto normalized = destination.lexically_normal();
  if (normalized.empty() || !normalized.is_absolute() ||
      normalized == normalized.root_path() || normalized.filename().empty() ||
      normalized.parent_path().empty()) {
    fail("The publication destination cannot be normalized safely");
  }
  return normalized;
}

[[nodiscard]] bool path_exists_no_follow(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory) {
    return false;
  }
  if (error) {
    fail("Cannot inspect a publication path component");
  }
  return std::filesystem::exists(status);
}

void require_plain_directory(const std::filesystem::path &path) {
#ifdef _WIN32
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    fail("A publication path component is not a plain directory");
  }
#else
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || !std::filesystem::is_directory(status) ||
      std::filesystem::is_symlink(status)) {
    fail("A publication path component is not a plain directory");
  }
#endif
}

#ifndef _WIN32
[[nodiscard]] std::uint64_t
filesystem_boundary_id(const std::filesystem::path &path) {
#if defined(__linux__)
#if !defined(SYS_statx) || !defined(STATX_MNT_ID)
#error "OpenRC requires Linux statx mount-ID headers for safe publication"
#endif
  struct statx attributes {};
  if (::syscall(SYS_statx, AT_FDCWD, path.c_str(), AT_SYMLINK_NOFOLLOW,
                STATX_TYPE | STATX_MNT_ID, &attributes) != 0 ||
      (attributes.stx_mask & STATX_MNT_ID) == 0U) {
    fail("Cannot inspect a publication filesystem mount boundary");
  }
  return attributes.stx_mnt_id;
#else
  struct stat attributes {};
  if (::lstat(path.c_str(), &attributes) != 0) {
    fail("Cannot inspect a publication filesystem boundary");
  }
  return static_cast<std::uint64_t>(attributes.st_dev);
#endif
}

void require_same_filesystem_boundary(const std::filesystem::path &anchor,
                                      const std::filesystem::path &path) {
  if (filesystem_boundary_id(anchor) != filesystem_boundary_id(path)) {
    fail("A publication tree crosses a filesystem mount boundary");
  }
}
#endif

void require_tree_without_redirections(const std::filesystem::path &root) {
#ifndef _WIN32
  const auto root_boundary = filesystem_boundary_id(root);
#endif
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      root, std::filesystem::directory_options::none, error);
  const std::filesystem::recursive_directory_iterator end;
  if (error) {
    fail("Cannot safely enumerate a publication transaction tree");
  }
  while (iterator != end) {
    const auto path = iterator->path();
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
      iterator.disable_recursion_pending();
      fail("A publication transaction tree contains a filesystem redirection");
    }
#else
    if (filesystem_boundary_id(path) != root_boundary) {
      iterator.disable_recursion_pending();
      fail("A publication transaction tree crosses a filesystem mount boundary");
    }
#endif
    const auto status = iterator->symlink_status(error);
    if (error || std::filesystem::is_symlink(status) ||
        (!std::filesystem::is_directory(status) &&
         !std::filesystem::is_regular_file(status))) {
      iterator.disable_recursion_pending();
      fail("A publication transaction tree contains an unsafe entry");
    }
    iterator.increment(error);
    if (error) {
      fail("Cannot safely enumerate a publication transaction tree");
    }
  }
}

void require_plain_directory_chain(const std::filesystem::path &directory) {
  auto current = directory.root_path();
  require_plain_directory(current);
  for (const auto &component : directory.relative_path()) {
    current /= component;
    require_plain_directory(current);
  }
}

[[nodiscard]] std::uint64_t process_id() noexcept {
#ifdef _WIN32
  return static_cast<std::uint64_t>(_getpid());
#else
  return static_cast<std::uint64_t>(::getpid());
#endif
}

[[nodiscard]] std::string unique_token() {
  static std::atomic<std::uint64_t> sequence{0U};
  const auto ticks = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return std::to_string(process_id()) + "-" + std::to_string(ticks) + "-" +
         std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed));
}

[[nodiscard]] std::filesystem::path
create_unique_plain_staging(const std::filesystem::path &parent) {
  for (std::uint32_t attempt = 0U; attempt < 128U; ++attempt) {
    const auto candidate = parent / (".openrc-stage-" + unique_token() + "-" +
                                     std::to_string(attempt));
    if (candidate.parent_path() != parent) {
      fail("A staging path escaped the destination parent");
    }
    std::error_code error;
    if (std::filesystem::create_directory(candidate, error)) {
      require_plain_directory(candidate);
#ifndef _WIN32
      require_same_filesystem_boundary(parent, candidate);
#endif
      return candidate;
    }
    if (error) {
      fail("Cannot create the publication staging directory");
    }
  }
  fail("Cannot reserve a unique publication staging directory");
}

[[nodiscard]] std::filesystem::path
choose_unique_absent_sibling(const std::filesystem::path &parent,
                             const std::string_view prefix) {
  for (std::uint32_t attempt = 0U; attempt < 128U; ++attempt) {
    const auto candidate = parent / (std::string(prefix) + unique_token() +
                                     "-" + std::to_string(attempt));
    if (candidate.parent_path() != parent) {
      fail("A transaction path escaped the destination parent");
    }
    if (!path_exists_no_follow(candidate)) {
      return candidate;
    }
  }
  fail("Cannot reserve a unique publication transaction path");
}

void remove_transaction_tree(const std::filesystem::path &path,
                             const std::filesystem::path &parent) {
  if (path.empty() || path.parent_path() != parent ||
      path.filename().native().find(
          std::filesystem::path(".openrc-").native()) != 0U) {
    fail("Refusing to remove an untrusted publication transaction path");
  }
  if (!path_exists_no_follow(path)) {
    return;
  }
  require_plain_directory(path);
#ifndef _WIN32
  require_same_filesystem_boundary(parent, path);
#endif
  require_tree_without_redirections(path);
  std::error_code error;
  static_cast<void>(std::filesystem::remove_all(path, error));
  if (error || path_exists_no_follow(path)) {
    fail("Cannot remove a publication transaction directory");
  }
}

void rename_same_parent(const std::filesystem::path &source,
                        const std::filesystem::path &destination,
                        const std::filesystem::path &parent,
                        const char *const description) {
  if (source.parent_path() != parent || destination.parent_path() != parent) {
    fail("A publication rename escaped the destination parent");
  }
  std::error_code error;
  std::filesystem::rename(source, destination, error);
  if (error) {
    fail(std::string("Cannot ") + description);
  }
}

void ensure_plain_relative_directories(
    const std::filesystem::path &root,
    const std::filesystem::path &relative_parent) {
  auto current = root;
  for (const auto &component : relative_parent) {
    current /= component;
    std::error_code error;
    if (std::filesystem::create_directory(current, error)) {
      require_plain_directory(current);
      continue;
    }
    if (error) {
      fail("Cannot create a package directory in publication staging");
    }
    require_plain_directory(current);
  }
}

#ifdef _WIN32

class NativeFile final {
public:
  explicit NativeFile(const HANDLE handle = INVALID_HANDLE_VALUE) noexcept
      : handle_(handle) {}

  ~NativeFile() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
  }

  NativeFile(const NativeFile &) = delete;
  NativeFile &operator=(const NativeFile &) = delete;

  [[nodiscard]] HANDLE get() const noexcept { return handle_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return handle_ != INVALID_HANDLE_VALUE;
  }

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

void write_new_plain_file(const std::filesystem::path &path,
                          const std::span<const std::byte> bytes) {
  NativeFile file(
      CreateFileW(path.c_str(), GENERIC_WRITE, 0U, nullptr, CREATE_NEW,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                      FILE_FLAG_WRITE_THROUGH,
                  nullptr));
  if (!file) {
    fail("Cannot create a publication staging file");
  }

  BY_HANDLE_FILE_INFORMATION information{};
  if (GetFileInformationByHandle(file.get(), &information) == FALSE ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    fail("A publication staging output is not a plain regular file");
  }

  std::size_t position = 0U;
  while (position < bytes.size()) {
    const auto count = static_cast<DWORD>(std::min<std::size_t>(
        bytes.size() - position, std::numeric_limits<DWORD>::max()));
    DWORD written = 0U;
    if (WriteFile(file.get(), bytes.data() + position, count, &written,
                  nullptr) == FALSE ||
        written == 0U) {
      fail("Cannot write a complete publication staging file");
    }
    position += written;
  }
  if (FlushFileBuffers(file.get()) == FALSE) {
    fail("Cannot flush a publication staging file");
  }
}

#else

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

class NativeFile final {
public:
  explicit NativeFile(const int descriptor = -1) noexcept
      : descriptor_(descriptor) {}

  ~NativeFile() {
    if (descriptor_ >= 0) {
      ::close(descriptor_);
    }
  }

  NativeFile(const NativeFile &) = delete;
  NativeFile &operator=(const NativeFile &) = delete;

  [[nodiscard]] int get() const noexcept { return descriptor_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return descriptor_ >= 0;
  }

private:
  int descriptor_ = -1;
};

void write_new_plain_file(const std::filesystem::path &path,
                          const std::span<const std::byte> bytes) {
  NativeFile file(::open(path.c_str(),
                         O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                         S_IRUSR | S_IWUSR));
  if (!file) {
    fail("Cannot create a publication staging file");
  }
  struct stat information{};
  if (::fstat(file.get(), &information) != 0 || !S_ISREG(information.st_mode)) {
    fail("A publication staging output is not a plain regular file");
  }

  std::size_t position = 0U;
  while (position < bytes.size()) {
    const auto written =
        ::write(file.get(), bytes.data() + position, bytes.size() - position);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      fail("Cannot write a complete publication staging file");
    }
    position += static_cast<std::size_t>(written);
  }
  if (::fsync(file.get()) != 0) {
    fail("Cannot flush a publication staging file");
  }
}

#endif

[[nodiscard]] bool
same_level_reference(const PreparedGameLevelReferenceV2 &left,
                     const PreparedGameLevelReferenceV2 &right) noexcept {
  return left.level_id == right.level_id &&
         left.package_path == right.package_path &&
         left.package_bytes == right.package_bytes &&
         left.package_sha256 == right.package_sha256;
}

[[nodiscard]] bool
same_overlay_reference(const PreparedGameOverlayReferenceV2 &left,
                       const PreparedGameOverlayReferenceV2 &right) noexcept {
  return left.overlay_id == right.overlay_id &&
         left.priority == right.priority &&
         left.content_api_version == right.content_api_version &&
         left.manifest_path == right.manifest_path &&
         left.manifest_bytes == right.manifest_bytes &&
         left.manifest_sha256 == right.manifest_sha256 &&
         left.required_base_game_sha256 == right.required_base_game_sha256;
}

[[nodiscard]] bool same_canonical_manifest(const PreparedGameV2 &left,
                                           const PreparedGameV2 &right) {
  if (left.content_api_version != right.content_api_version ||
      left.provenance.game_id != right.provenance.game_id ||
      left.provenance.build_id != right.provenance.build_id ||
      left.provenance.compiler_id != right.provenance.compiler_id ||
      left.provenance.compiler_version != right.provenance.compiler_version ||
      left.provenance.source_image_bytes !=
          right.provenance.source_image_bytes ||
      left.provenance.source_image_sha256 !=
          right.provenance.source_image_sha256 ||
      left.provenance.prepared_game_v1_manifest_sha256 !=
          right.provenance.prepared_game_v1_manifest_sha256 ||
      left.shared_package != right.shared_package ||
      left.levels.size() != right.levels.size() ||
      left.overlays.size() != right.overlays.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < left.levels.size(); ++index) {
    if (!same_level_reference(left.levels[index], right.levels[index])) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < left.overlays.size(); ++index) {
    if (!same_overlay_reference(left.overlays[index], right.overlays[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::filesystem::path
canonical_package_path(const std::string &encoded) {
  // PreparedGameV2 parsing has already enforced its lower-case portable path
  // grammar. This independent filesystem check prevents platform-specific
  // path interpretation from changing that meaning.
  const std::filesystem::path path(encoded);
  if (path.empty() || path.is_absolute() || path.has_root_name() ||
      path.has_root_directory() || path.lexically_normal() != path) {
    fail("A level-package path is not a canonical relative filesystem path");
  }
  return path;
}

void register_virtual_file(const std::string &path,
                           std::set<std::string> &files,
                           std::set<std::string> &directories) {
  if (directories.contains(path) || !files.insert(path).second) {
    fail("PreparedGameV2 references have a file/directory path conflict");
  }
  std::size_t separator = path.find('/');
  while (separator != std::string::npos) {
    const auto directory = path.substr(0U, separator);
    if (files.contains(directory)) {
      fail("PreparedGameV2 references have a file/directory path "
                 "conflict");
    }
    directories.insert(directory);
    separator = path.find('/', separator + 1U);
  }
}

struct PublicationPlan {
  PreparedGameV2 manifest;
  std::vector<std::byte> manifest_bytes;
  std::map<std::uint32_t, std::span<const std::byte>> packages;
  // Internal uniform ownership list only; shared remains absent from the
  // manifest's planet references and public published level count.
  std::vector<PreparedGameLevelReferenceV2> base_references;
  std::uint64_t total_package_bytes = 0U;
};

[[nodiscard]] std::vector<PreparedGameLevelReferenceV2>
base_references(const PreparedGameV2 &manifest) {
  auto references = manifest.levels;
  if (manifest.shared_package) {
    const auto &shared = *manifest.shared_package;
    references.push_back({kPreparedGameSharedPackageIdV2, shared.package_path,
                           shared.package_bytes, shared.package_sha256});
  }
  return references;
}

void verify_base_package(const PreparedGameV2RootV1 &loaded,
                          const std::uint32_t id,
                          const PreparedGameV2FilesystemLimitsV1 &limits) {
  if (id == kPreparedGameSharedPackageIdV2) {
    (void)load_prepared_game_shared_package_v1(loaded, limits);
  } else {
    (void)load_prepared_game_level_package_v1(loaded, id, limits);
  }
}

[[nodiscard]] PublicationPlan make_publication_plan(
    const PreparedGameV2 &manifest,
    const std::span<const PreparedGameV2LevelPackageBytesV1> level_packages,
    const std::span<const std::byte> shared_package_bytes,
    const PreparedGameV2FilesystemLimitsV1 &limits) {
  PublicationPlan result;
  try {
    result.manifest_bytes = encode_prepared_game_v2(manifest, limits.manifest);
    result.manifest =
        parse_prepared_game_v2(result.manifest_bytes, limits.manifest);
  } catch (const PreparedGameV2Error &error) {
    fail("Invalid PreparedGameV2 publication manifest: " +
         std::string(error.what()));
  }
  if (!same_canonical_manifest(manifest, result.manifest)) {
    fail("PreparedGameV2 publication input is not in canonical order");
  }
  if (level_packages.size() != result.manifest.levels.size()) {
    fail("Explicit LevelPackageV1 inputs do not match the manifest 1:1");
  }
  if (result.manifest.shared_package.has_value() != !shared_package_bytes.empty()) {
    fail("Explicit shared package bytes do not match manifest presence");
  }
  result.base_references = base_references(result.manifest);

  for (const auto &input : level_packages) {
    if (input.level_id == kPreparedGameSharedPackageIdV2 || input.bytes.empty() ||
        !result.packages.emplace(input.level_id, input.bytes).second) {
      fail("Explicit LevelPackageV1 inputs contain an empty or duplicate "
                 "level");
    }
  }
  if (result.manifest.shared_package) {
    result.packages.emplace(kPreparedGameSharedPackageIdV2, shared_package_bytes);
  }

  std::set<std::string> virtual_files;
  std::set<std::string> virtual_directories;
  register_virtual_file(kPreparedGameV2ManifestFileName, virtual_files,
                        virtual_directories);
  for (const auto &overlay : result.manifest.overlays) {
    static_cast<void>(canonical_package_path(overlay.manifest_path));
    register_virtual_file(overlay.manifest_path, virtual_files,
                          virtual_directories);
  }

  for (const auto &reference : result.base_references) {
    const auto package = result.packages.find(reference.level_id);
    if (package == result.packages.end()) {
      fail("A manifest level has no explicit LevelPackageV1 input");
    }
    static_cast<void>(canonical_package_path(reference.package_path));
    register_virtual_file(reference.package_path, virtual_files,
                          virtual_directories);

    if (package->second.size() != reference.package_bytes ||
        prepared_content_sha256_v1(package->second) !=
            reference.package_sha256) {
      fail("Explicit LevelPackageV1 bytes disagree with their manifest");
    }
    try {
      if (reference.level_id == kPreparedGameSharedPackageIdV2) {
        (void)parse_prepared_game_shared_package_v1(
            result.manifest, package->second, limits.level_package);
      } else {
        (void)parse_prepared_game_level_package_v1(
            result.manifest, reference.level_id, package->second,
            limits.level_package);
      }
    } catch (const PreparedGameV2Error &error) {
      fail("Invalid explicit LevelPackageV1 input: " +
           std::string(error.what()));
    }
    result.total_package_bytes =
        checked_add(result.total_package_bytes, package->second.size(),
                    "Published LevelPackageV1 bytes");
  }
  return result;
}

void write_staging_tree(const std::filesystem::path &staging,
                        const PublicationPlan &plan) {
  for (const auto &reference : plan.base_references) {
    const auto relative = canonical_package_path(reference.package_path);
    ensure_plain_relative_directories(staging, relative.parent_path());
    const auto package = plan.packages.find(reference.level_id);
    if (package == plan.packages.end()) {
      fail("An internal publication plan lost a level package");
    }
    write_new_plain_file(staging / relative, package->second);
  }

  // A visible manifest always describes a complete staged base tree.
  write_new_plain_file(staging / kPreparedGameV2ManifestFileName,
                       plan.manifest_bytes);
}

void verify_tree(const std::filesystem::path &root, const PublicationPlan &plan,
                 const PreparedGameV2FilesystemLimitsV1 &limits) {
  PreparedGameV2RootV1 loaded;
  try {
    loaded = load_prepared_game_v2_root_v1(root, limits);
    if (loaded.manifest_sha256 !=
        prepared_content_sha256_v1(plan.manifest_bytes)) {
      fail("A published PreparedGameV2 manifest changed on disk");
    }
    for (const auto &reference : plan.base_references) {
      verify_base_package(loaded, reference.level_id, limits);
    }
  } catch (const PreparedGameV2FilesystemError &error) {
    fail("Cannot verify a published PreparedGameV2 tree: " +
         std::string(error.what()));
  }
}

void require_exact_owned_tree(const PreparedGameV2RootV1 &loaded) {
    std::set<std::string> expected_files{kPreparedGameV2ManifestFileName};
    std::set<std::string> expected_directories;
    for (const auto &reference : base_references(loaded.manifest)) {
        register_virtual_file(reference.package_path, expected_files,
                              expected_directories);
    }

    std::uint64_t visited_entries = 0U;
    const auto maximum_entries = checked_add(
        expected_files.size(), expected_directories.size(),
        "The PreparedGameV2 owned-tree entry count");
#ifndef _WIN32
    const auto root_boundary = filesystem_boundary_id(loaded.root);
#endif
    std::error_code filesystem_error;
    std::filesystem::recursive_directory_iterator iterator(
        loaded.root, std::filesystem::directory_options::none,
        filesystem_error);
    const std::filesystem::recursive_directory_iterator end;
    if (filesystem_error) {
        fail("Cannot enumerate an existing PreparedGameV2 publication");
    }

    while (iterator != end) {
        visited_entries = checked_add(
            visited_entries, 1U,
            "The existing PreparedGameV2 publication entry count");
        if (visited_entries > maximum_entries) {
            fail("Refusing to replace a PreparedGameV2 publication that "
                 "contains unowned filesystem entries");
        }

        const auto path = iterator->path();
        const auto relative = path.lexically_relative(loaded.root);
        const auto encoded = relative.generic_string();
        if (relative.empty() || relative.is_absolute() || encoded.empty()) {
            fail("An existing PreparedGameV2 entry escaped its root");
        }

#ifdef _WIN32
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
            iterator.disable_recursion_pending();
            fail("An existing PreparedGameV2 entry is not a plain file or "
                 "directory");
        }
#else
        if (filesystem_boundary_id(path) != root_boundary) {
            iterator.disable_recursion_pending();
            fail("An existing PreparedGameV2 entry crosses a filesystem "
                 "mount boundary");
        }
#endif
        const auto status = iterator->symlink_status(filesystem_error);
        if (filesystem_error || std::filesystem::is_symlink(status)) {
            iterator.disable_recursion_pending();
            fail("Cannot safely inspect an existing PreparedGameV2 entry");
        }
        if (std::filesystem::is_directory(status)) {
            if (!expected_directories.contains(encoded)) {
                iterator.disable_recursion_pending();
                fail("Refusing to replace a PreparedGameV2 publication that "
                     "contains an unowned directory");
            }
            require_plain_directory(path);
        } else if (std::filesystem::is_regular_file(status)) {
            if (!expected_files.contains(encoded)) {
                fail("Refusing to replace a PreparedGameV2 publication that "
                     "contains an unowned file");
            }
        } else {
            iterator.disable_recursion_pending();
            fail("An existing PreparedGameV2 entry is not a regular file or "
                 "directory");
        }

        iterator.increment(filesystem_error);
        if (filesystem_error) {
            fail("Cannot enumerate an existing PreparedGameV2 publication");
        }
    }
    if (visited_entries != maximum_entries) {
        fail("An existing PreparedGameV2 publication is missing an owned "
             "filesystem entry");
    }
}

void require_replaceable_existing_tree(
    const std::filesystem::path &root,
    const PreparedGameV2FilesystemLimitsV1 &limits) {
#ifndef _WIN32
    require_same_filesystem_boundary(root.parent_path(), root);
#endif
    try {
        const auto loaded = load_prepared_game_v2_root_v1(root, limits);
        for (const auto &reference : base_references(loaded.manifest)) {
            verify_base_package(loaded, reference.level_id, limits);
        }
        require_exact_owned_tree(loaded);
    } catch (const PreparedGameV2FilesystemError &error) {
        fail("Refusing to replace an existing directory that is not a complete "
             "verified PreparedGameV2 publication: " +
             std::string(error.what()));
    }
}

[[nodiscard]] bool cancellation_requested(
    const PreparedGameV2PublishControlV1 &control,
    const PreparedGameV2PublishCheckpointV1 checkpoint) noexcept {
  return control.cancel_requested != nullptr &&
         control.cancel_requested(checkpoint, control.context);
}

[[nodiscard]] std::string exception_message() {
  try {
    throw;
  } catch (const std::exception &error) {
    return error.what();
  } catch (...) {
    return "unknown publication failure";
  }
}

} // namespace

PublishedPreparedGameV2V1 publish_prepared_game_v2_v1(
    const std::filesystem::path &destination_root,
    const PreparedGameV2 &manifest,
    const std::span<const PreparedGameV2LevelPackageBytesV1> level_packages,
    const PreparedGameV2FilesystemLimitsV1 limits,
    const PreparedGameV2PublishControlV1 control) {
  return publish_prepared_game_v2_v1(destination_root, manifest, level_packages,
                                    {}, limits, control);
}

PublishedPreparedGameV2V1 publish_prepared_game_v2_v1(
    const std::filesystem::path &destination_root,
    const PreparedGameV2 &manifest,
    const std::span<const PreparedGameV2LevelPackageBytesV1> level_packages,
    const std::span<const std::byte> shared_package_bytes,
    const PreparedGameV2FilesystemLimitsV1 limits,
    const PreparedGameV2PublishControlV1 control) {
  const auto destination = normalize_destination(destination_root);
  const auto parent = destination.parent_path();
  require_plain_directory_chain(parent);
  if (path_exists_no_follow(destination)) {
    require_plain_directory(destination);
    require_replaceable_existing_tree(destination, limits);
  }

  const auto plan = make_publication_plan(manifest, level_packages,
                                         shared_package_bytes, limits);
  const auto staging = create_unique_plain_staging(parent);
  std::filesystem::path backup;
  bool backup_moved = false;
  bool staging_exists = true;
  bool destination_installed = false;

  try {
    write_staging_tree(staging, plan);
    verify_tree(staging, plan, limits);
    if (cancellation_requested(
            control, PreparedGameV2PublishCheckpointV1::staged_and_verified)) {
      fail("PreparedGameV2 publication was cancelled before commit");
    }

    if (path_exists_no_follow(destination)) {
      require_plain_directory(destination);
      // Staging a complete game can take minutes. Revalidate at the commit
      // boundary so a directory created or replaced meanwhile is never
      // treated as disposable merely because its path now exists.
      require_replaceable_existing_tree(destination, limits);
      backup = choose_unique_absent_sibling(parent, ".openrc-backup-");
      rename_same_parent(destination, backup, parent,
                         "back up the existing publication destination");
      backup_moved = true;
      // Validate the exact tree that was moved, not merely the earlier
      // occupant of the destination path. If it changed in the final race
      // window, the catch path restores it without deleting its contents.
      require_replaceable_existing_tree(backup, limits);
      if (cancellation_requested(
              control,
              PreparedGameV2PublishCheckpointV1::destination_backed_up)) {
        fail("PreparedGameV2 publication was cancelled during commit");
      }
    }

    rename_same_parent(staging, destination, parent,
                       "promote the verified publication staging directory");
    staging_exists = false;
    destination_installed = true;
    verify_tree(destination, plan, limits);
  } catch (...) {
    const auto primary_error = exception_message();
    std::string rollback_error;

    try {
      if (destination_installed) {
        const auto failed =
            choose_unique_absent_sibling(parent, ".openrc-failed-");
        rename_same_parent(destination, failed, parent,
                           "move a failed new destination aside");
        destination_installed = false;
        if (backup_moved) {
          rename_same_parent(backup, destination, parent,
                             "restore the previous publication destination");
          backup_moved = false;
        }
        remove_transaction_tree(failed, parent);
      } else if (backup_moved) {
        rename_same_parent(backup, destination, parent,
                           "restore the previous publication destination");
        backup_moved = false;
      }
      if (staging_exists) {
        remove_transaction_tree(staging, parent);
        staging_exists = false;
      }
    } catch (...) {
      rollback_error = exception_message();
    }

    if (!rollback_error.empty()) {
      throw PreparedGameV2PublishError(
          primary_error + "; rollback was incomplete: " + rollback_error);
    }
    throw PreparedGameV2PublishError(primary_error);
  }

  // The verified destination is committed at this point. Backup cleanup is a
  // separate, non-transactional phase: a partial deletion must never roll the
  // new publication back to a potentially partial old tree.
  if (backup_moved) {
    try {
      remove_transaction_tree(backup, parent);
      backup_moved = false;
    } catch (...) {
      throw PreparedGameV2PublishError(
                "PreparedGameV2 publication was committed successfully, but "
                "the "
                "previous backup could not be removed completely: " +
          exception_message());
    }
  }

  return PublishedPreparedGameV2V1{
      destination,
      prepared_content_sha256_v1(plan.manifest_bytes),
      static_cast<std::uint32_t>(plan.manifest.levels.size()),
      plan.total_package_bytes,
  };
}

} // namespace openrc
