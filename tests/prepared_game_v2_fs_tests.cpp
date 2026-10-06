#include "openrc/prepared_game_v2_fs.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr openrc::PreparedGameV2FilesystemLimitsV1 kLimits{
    {
        1024U * 1024U,
        32U,
        16U,
        256U,
        32U * 1024U * 1024U,
        1024U * 1024U,
    },
    {
        1024U * 1024U,
        256U,
        16U,
        1024U,
        256U,
        512U * 1024U,
        768U * 1024U,
        16U,
    },
    1024U * 1024U,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Function>
void expect_filesystem_rejected(Function &&function,
                                const std::string &message) {
  try {
    function();
  } catch (const openrc::PreparedGameV2FilesystemError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] std::vector<std::byte> bytes_of(const std::string_view value) {
  return std::vector<std::byte>(
      reinterpret_cast<const std::byte *>(value.data()),
      reinterpret_cast<const std::byte *>(value.data() + value.size()));
}

[[nodiscard]] openrc::PreparedContentDigestV1
digest_of(const std::string_view value) {
  const auto bytes = bytes_of(value);
  return openrc::prepared_content_sha256_v1(bytes);
}

void write_bytes(const std::filesystem::path &path,
                 const std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("Cannot create test input file");
  }
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("Cannot write test input file");
  }
}

class TemporaryTree final {
public:
  TemporaryTree() {
    const auto temporary_root =
        std::filesystem::temp_directory_path().lexically_normal();
    const auto stamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (std::uint32_t attempt = 0U; attempt < 64U; ++attempt) {
      container_ =
          temporary_root / ("openrc-prepared-v2-fs-" + std::to_string(stamp) +
                            "-" + std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(container_, error)) {
        root = container_ / "prepared";
        std::filesystem::create_directories(root / "levels");
        std::filesystem::create_directories(container_ / "external");
        return;
      }
      if (error) {
        throw std::runtime_error("Cannot reserve a temporary test directory");
      }
    }
    throw std::runtime_error("Cannot choose a unique temporary test path");
  }

  ~TemporaryTree() {
    std::error_code ignored;
    std::filesystem::remove_all(container_, ignored);
  }

  TemporaryTree(const TemporaryTree &) = delete;
  TemporaryTree &operator=(const TemporaryTree &) = delete;

  [[nodiscard]] const std::filesystem::path &container() const noexcept {
    return container_;
  }

  std::filesystem::path root;

private:
  std::filesystem::path container_;
};

[[nodiscard]] openrc::LevelPackageV1 make_base_package() {
  openrc::LevelPackageResourceV1 collision;
  collision.resource_id = "world/collision";
  collision.type_id = "openrc.collision-mesh";
  collision.schema_version = 1U;
  collision.flags = openrc::kLevelPackageResourceOverlayReplaceableV1;
  collision.provenance.push_back(openrc::LevelPackageProvenanceV1{
      openrc::LevelPackageProvenanceKindV1::generated,
      "compiler/test-collision",
      0U,
      0U,
      {},
  });
  collision.payload = bytes_of("base-collision");

  openrc::LevelPackageV1 result;
  result.level_id = 0U;
  result.content_api_version = 1U;
  result.build_id = "SCES-50916-PAL-v2.00";
  result.resources.push_back(std::move(collision));
  return result;
}

[[nodiscard]] openrc::PreparedGameV2
make_manifest(const std::span<const std::byte> package_bytes) {
  openrc::PreparedGameV2 result;
  result.content_api_version = 1U;
  result.provenance.game_id = "openrc-rac-2002";
  result.provenance.build_id = "SCES-50916-PAL-v2.00";
  result.provenance.compiler_id = "openrc-test-compiler";
  result.provenance.compiler_version = "1.0";
  result.provenance.source_image_bytes = 4'700'000'000ULL;
  result.provenance.source_image_sha256 = digest_of("retail-image");
  result.levels.push_back(openrc::PreparedGameLevelReferenceV2{
      0U,
      "levels/000.orlvl",
      package_bytes.size(),
      openrc::prepared_content_sha256_v1(package_bytes),
  });
  // This deliberately points to a file that is never created. Loading the
  // base must not discover or open it.
  result.overlays.push_back(openrc::PreparedGameOverlayReferenceV2{
      "mods/not-installed",
      10,
      1U,
      "mods/not-installed/manifest.ormod",
      100U,
      digest_of("not-installed-manifest"),
      digest_of("base-manifest-without-overlays"),
  });
  return result;
}

struct PreparedFixture {
  TemporaryTree tree;
  openrc::LevelPackageV1 base = make_base_package();
  std::vector<std::byte> package_bytes =
      openrc::encode_level_package_v1(base, kLimits.level_package);
  openrc::PreparedGameV2 manifest = make_manifest(package_bytes);
  std::vector<std::byte> manifest_bytes =
      openrc::encode_prepared_game_v2(manifest, kLimits.manifest);

  PreparedFixture() {
    write_package();
    write_manifest();
  }

  void write_package() const {
    write_bytes(tree.root / "levels" / "000.orlvl", package_bytes);
  }

  void write_manifest() const {
    write_bytes(tree.root / openrc::kPreparedGameV2ManifestFileName,
                manifest_bytes);
  }
};

void test_plain_root_and_level_load() {
  PreparedFixture fixture;
  const auto prepared =
      openrc::load_prepared_game_v2_root_v1(fixture.tree.root, kLimits);
  expect(prepared.root == fixture.tree.root.lexically_normal() &&
             prepared.manifest_sha256 ==
                 openrc::prepared_content_sha256_v1(fixture.manifest_bytes) &&
             prepared.manifest.overlays.size() == 1U,
         "PreparedGameV2 filesystem root load lost metadata");

  const auto package =
      openrc::load_prepared_game_level_package_v1(prepared, 0U, kLimits);
  expect(package.level_id == 0U && package.content_api_version == 1U &&
             package.resources.size() == 1U &&
             package.resources.front().payload == bytes_of("base-collision"),
         "PreparedGameV2 filesystem level load is wrong");

  // The absent manifest-referenced mod above did not affect loading.
  const auto resolved_without_discovery =
      openrc::load_resolved_prepared_game_level_package_v1(prepared, 0U, {},
                                                           kLimits);
  expect(resolved_without_discovery.resources.front().payload ==
                 bytes_of("base-collision") &&
             resolved_without_discovery.applied_overlays.empty(),
         "PreparedGameV2 filesystem reader auto-discovered an overlay");
}

void test_explicit_overlay_bytes() {
  PreparedFixture fixture;
  const auto prepared =
      openrc::load_prepared_game_v2_root_v1(fixture.tree.root, kLimits);

  openrc::LevelPackageResourceV1 replacement;
  replacement.resource_id = "world/collision";
  replacement.type_id = "openrc.collision-mesh";
  replacement.schema_version = 1U;
  replacement.provenance.push_back(openrc::LevelPackageProvenanceV1{
      openrc::LevelPackageProvenanceKindV1::mod_resource,
      "mod/test/collision",
      0U,
      18U,
      digest_of("mod-source-content"),
  });
  replacement.payload = bytes_of("overlay-collision");

  openrc::LevelPackageV1 overlay;
  overlay.level_id = 0U;
  overlay.content_api_version = 1U;
  overlay.build_id = fixture.base.build_id;
  overlay.layer_kind = openrc::LevelPackageLayerKindV1::overlay;
  overlay.layer_id = "mods/test";
  overlay.priority = 10;
  overlay.required_base_package_sha256 =
      openrc::level_package_sha256_v1(fixture.base, kLimits.level_package);
  overlay.resources.push_back(std::move(replacement));
  const auto overlay_bytes =
      openrc::encode_level_package_v1(overlay, kLimits.level_package);
  const std::array explicit_overlays{
      openrc::ExplicitLevelPackageOverlayBytesV1{overlay_bytes},
  };

  const auto resolved = openrc::load_resolved_prepared_game_level_package_v1(
      prepared, 0U, explicit_overlays, kLimits);
  expect(resolved.resources.size() == 1U &&
             resolved.resources.front().payload ==
                 bytes_of("overlay-collision") &&
             resolved.applied_overlays.size() == 1U &&
             resolved.applied_overlays.front().layer_id == "mods/test",
         "Explicit filesystem overlay resolution is wrong");

  auto tight_limits = kLimits;
  tight_limits.max_total_explicit_overlay_bytes = overlay_bytes.size() - 1U;
  expect_filesystem_rejected(
      [&] {
        static_cast<void>(openrc::load_resolved_prepared_game_level_package_v1(
            prepared, 0U, explicit_overlays, tight_limits));
      },
      "Explicit overlay aggregate byte limit was ignored");
}

void test_package_tamper_and_truncation() {
  PreparedFixture fixture;
  const auto prepared =
      openrc::load_prepared_game_v2_root_v1(fixture.tree.root, kLimits);

  fixture.package_bytes.back() ^= std::byte{1U};
  fixture.write_package();
  expect_filesystem_rejected(
      [&] {
        static_cast<void>(
            openrc::load_prepared_game_level_package_v1(prepared, 0U, kLimits));
      },
      "Filesystem reader accepted a tampered LevelPackageV1");

  fixture.package_bytes.resize(fixture.package_bytes.size() / 2U);
  fixture.write_package();
  expect_filesystem_rejected(
      [&] {
        static_cast<void>(
            openrc::load_prepared_game_level_package_v1(prepared, 0U, kLimits));
      },
      "Filesystem reader accepted a truncated LevelPackageV1");
}

void test_manifest_tamper_truncation_and_size_limit() {
  PreparedFixture fixture;
  fixture.manifest_bytes.back() ^= std::byte{1U};
  fixture.write_manifest();
  expect_filesystem_rejected(
      [&] {
        static_cast<void>(
            openrc::load_prepared_game_v2_root_v1(fixture.tree.root, kLimits));
      },
      "Filesystem reader accepted a tampered PreparedGameV2 manifest");

  fixture.manifest_bytes.resize(fixture.manifest_bytes.size() / 2U);
  fixture.write_manifest();
  expect_filesystem_rejected(
      [&] {
        static_cast<void>(
            openrc::load_prepared_game_v2_root_v1(fixture.tree.root, kLimits));
      },
      "Filesystem reader accepted a truncated PreparedGameV2 manifest");

  PreparedFixture fresh_fixture;
  auto tight_limits = kLimits;
  tight_limits.manifest.max_input_bytes =
      openrc::kPreparedGameHeaderBytesV2 + 1U;
  expect_filesystem_rejected(
      [&] {
        static_cast<void>(openrc::load_prepared_game_v2_root_v1(
            fresh_fixture.tree.root, tight_limits));
      },
      "Filesystem reader allocated a manifest beyond its byte limit");
}

void test_root_and_relative_path_rejections() {
  PreparedFixture fixture;
  expect_filesystem_rejected(
      [&] {
        static_cast<void>(openrc::load_prepared_game_v2_root_v1(
            std::filesystem::path("relative-prepared-root"), kLimits));
      },
      "Filesystem reader accepted a relative prepared root");

  const auto ordinary_file = fixture.tree.container() / "not-a-root.bin";
  write_bytes(ordinary_file, bytes_of("file"));
  expect_filesystem_rejected(
      [&] {
        static_cast<void>(
            openrc::load_prepared_game_v2_root_v1(ordinary_file, kLimits));
      },
      "Filesystem reader accepted a regular file as its root");

  auto prepared =
      openrc::load_prepared_game_v2_root_v1(fixture.tree.root, kLimits);
  prepared.manifest.levels.front().package_path = "../outside.orlvl";
  expect_filesystem_rejected(
      [&] {
        static_cast<void>(
            openrc::load_prepared_game_level_package_v1(prepared, 0U, kLimits));
      },
      "Filesystem reader accepted root-relative path traversal");

  prepared = openrc::load_prepared_game_v2_root_v1(fixture.tree.root, kLimits);
  prepared.manifest.levels.front().package_bytes =
      std::numeric_limits<std::uint64_t>::max();
  expect_filesystem_rejected(
      [&] {
        static_cast<void>(
            openrc::load_prepared_game_level_package_v1(prepared, 0U, kLimits));
      },
      "Filesystem reader accepted an unbounded manifest file size");
}

void test_symlink_and_reparse_rejections_when_supported() {
  PreparedFixture fixture;

  const auto root_alias = fixture.tree.container() / "prepared-link";
  std::error_code link_error;
  std::filesystem::create_directory_symlink(fixture.tree.root, root_alias,
                                            link_error);
  if (!link_error) {
    expect_filesystem_rejected(
        [&] {
          static_cast<void>(
              openrc::load_prepared_game_v2_root_v1(root_alias, kLimits));
        },
        "Filesystem reader traversed a symlinked prepared root");
  }

  link_error.clear();
  const auto external = fixture.tree.container() / "external";
  write_bytes(external / "000.orlvl", fixture.package_bytes);
  const auto descendant_link = fixture.tree.root / "linked";
  std::filesystem::create_directory_symlink(external, descendant_link,
                                            link_error);
  if (!link_error) {
    auto prepared =
        openrc::load_prepared_game_v2_root_v1(fixture.tree.root, kLimits);
    prepared.manifest.levels.front().package_path = "linked/000.orlvl";
    expect_filesystem_rejected(
        [&] {
          static_cast<void>(openrc::load_prepared_game_level_package_v1(
              prepared, 0U, kLimits));
        },
        "Filesystem reader traversed a descendant directory symlink");
  }
}

void test_shared_package_hardened_loading() {
  PreparedFixture fixture;
  auto shared = fixture.base;
  shared.level_id = openrc::kPreparedGameSharedPackageIdV2;
  const auto bytes = openrc::encode_level_package_v1(shared, kLimits.level_package);
  fixture.manifest.shared_package = openrc::PreparedGameSharedReferenceV2{
      "shared/global.orlvl", bytes.size(), openrc::prepared_content_sha256_v1(bytes)};
  fixture.manifest_bytes = openrc::encode_prepared_game_v2(fixture.manifest, kLimits.manifest);
  fixture.write_manifest();
  auto prepared = openrc::load_prepared_game_v2_root_v1(fixture.tree.root, kLimits);
  expect_filesystem_rejected([&] {
    (void)openrc::load_prepared_game_shared_package_v1(prepared, kLimits);
  }, "A missing shared package cannot be treated as an empty resource");
  std::filesystem::create_directory(fixture.tree.root / "shared");
  const auto file = fixture.tree.root / "shared/global.orlvl";
  write_bytes(file, bytes);
  expect(openrc::load_prepared_game_shared_package_v1(prepared, kLimits).level_id ==
             openrc::kPreparedGameSharedPackageIdV2,
         "Shared packages must load through the existing hardened path reader");
  auto corrupt = bytes;
  corrupt.back() ^= std::byte{1U};
  write_bytes(file, corrupt);
  expect_filesystem_rejected([&] {
    (void)openrc::load_prepared_game_shared_package_v1(prepared, kLimits);
  }, "Shared payload tamper must reject");
  corrupt.resize(corrupt.size() - 1U);
  write_bytes(file, corrupt);
  expect_filesystem_rejected([&] {
    (void)openrc::load_prepared_game_shared_package_v1(prepared, kLimits);
  }, "Shared file must match its exact promised size before allocation");
  write_bytes(file, bytes);
  auto limited = kLimits;
  limited.level_package.max_input_bytes = bytes.size() - 1U;
  expect_filesystem_rejected([&] {
    (void)openrc::load_prepared_game_shared_package_v1(prepared, limited);
  }, "Shared reader must apply its package byte budget");
  auto unsafe = prepared;
  unsafe.manifest.shared_package->package_path = "../external/global.orlvl";
  expect_filesystem_rejected([&] {
    (void)openrc::load_prepared_game_shared_package_v1(unsafe, kLimits);
  }, "Mutating a loaded shared reference cannot authorize traversal");
  unsafe.manifest.shared_package.reset();
  expect_filesystem_rejected([&] {
    (void)openrc::load_prepared_game_shared_package_v1(unsafe, kLimits);
  }, "Legacy publications must report absent shared content explicitly");

  const auto external = fixture.tree.container() / "external/global.orlvl";
  write_bytes(external, bytes);
  std::filesystem::remove(file);
  std::error_code link_error;
  std::filesystem::create_symlink(external, file, link_error);
  if (!link_error) {
    expect_filesystem_rejected([&] {
      (void)openrc::load_prepared_game_shared_package_v1(prepared, kLimits);
    }, "Shared reader must revalidate and reject a replaced symlink file");
    std::filesystem::remove(file);
  }
  std::filesystem::remove(fixture.tree.root / "shared");
  link_error.clear();
  std::filesystem::create_directory_symlink(fixture.tree.container() / "external",
                                            fixture.tree.root / "shared", link_error);
  if (!link_error) {
    expect_filesystem_rejected([&] {
      (void)openrc::load_prepared_game_shared_package_v1(prepared, kLimits);
    }, "Shared reader must reject a replaced directory component");
  }
}

} // namespace

int main() {
  try {
    test_plain_root_and_level_load();
    test_shared_package_hardened_loading();
    test_explicit_overlay_bytes();
    test_package_tamper_and_truncation();
    test_manifest_tamper_truncation_and_size_limit();
    test_root_and_relative_path_rejections();
    test_symlink_and_reparse_rejections_when_supported();
    std::cout << "OpenRC PreparedGameV2 filesystem tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "OpenRC PreparedGameV2 filesystem tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
