#include "openrc/prepared_game_v2_publish.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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
void expect_publish_rejected(Function &&function, const std::string &message) {
  try {
    function();
  } catch (const openrc::PreparedGameV2PublishError &) {
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
    throw std::runtime_error("Cannot create test file");
  }
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("Cannot write test file");
  }
}

[[nodiscard]] std::vector<std::byte>
read_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw std::runtime_error("Cannot open test file");
  }
  const auto end = input.tellg();
  if (end < 0) {
    throw std::runtime_error("Cannot size test file");
  }
  std::vector<std::byte> result(static_cast<std::size_t>(end));
  input.seekg(0, std::ios::beg);
  input.read(reinterpret_cast<char *>(result.data()),
             static_cast<std::streamsize>(result.size()));
  if (!input && !result.empty()) {
    throw std::runtime_error("Cannot read test file");
  }
  return result;
}

class TemporaryTree final {
public:
  TemporaryTree() {
    const auto temporary_root =
        std::filesystem::temp_directory_path().lexically_normal();
    const auto stamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (std::uint32_t attempt = 0U; attempt < 64U; ++attempt) {
      root = temporary_root /
             ("openrc-prepared-v2-publish-" + std::to_string(stamp) + "-" +
              std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(root, error)) {
        return;
      }
      if (error) {
        throw std::runtime_error("Cannot reserve temporary test directory");
      }
    }
    throw std::runtime_error("Cannot choose a unique temporary test path");
  }

  ~TemporaryTree() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }

  TemporaryTree(const TemporaryTree &) = delete;
  TemporaryTree &operator=(const TemporaryTree &) = delete;

  std::filesystem::path root;
};

[[nodiscard]] openrc::LevelPackageV1
make_package(const std::uint32_t level_id, const std::string_view payload,
             const std::uint32_t content_api_version = 1U,
             std::string build_id = "SCES-50916-PAL-v2.00") {
  openrc::LevelPackageResourceV1 resource;
  resource.resource_id = "world/bootstrap";
  resource.type_id = "openrc.level-bootstrap";
  resource.schema_version = 1U;
  resource.flags = openrc::kLevelPackageResourceOverlayReplaceableV1;
  resource.provenance.push_back(openrc::LevelPackageProvenanceV1{
      openrc::LevelPackageProvenanceKindV1::generated,
      "compiler/test-bootstrap",
      0U,
      0U,
      {},
  });
  resource.payload = bytes_of(payload);

  openrc::LevelPackageV1 result;
  result.level_id = level_id;
  result.content_api_version = content_api_version;
  result.build_id = std::move(build_id);
  result.resources.push_back(std::move(resource));
  return result;
}

[[nodiscard]] openrc::PreparedGameV2
make_manifest(const std::span<const std::vector<std::byte>> packages) {
  openrc::PreparedGameV2 result;
  result.content_api_version = 1U;
  result.provenance.game_id = "openrc-rac-2002";
  result.provenance.build_id = "SCES-50916-PAL-v2.00";
  result.provenance.compiler_id = "openrc-test-compiler";
  result.provenance.compiler_version = "1.0";
  result.provenance.source_image_bytes = 4'700'000'000ULL;
  result.provenance.source_image_sha256 = digest_of("retail-image");
  for (std::size_t index = 0U; index < packages.size(); ++index) {
    result.levels.push_back(openrc::PreparedGameLevelReferenceV2{
        static_cast<std::uint32_t>(index),
        "levels/" + std::to_string(index) + ".orlvl",
        packages[index].size(),
        openrc::prepared_content_sha256_v1(packages[index]),
    });
  }
  return result;
}

struct Fixture {
  std::vector<std::byte> package_zero = openrc::encode_level_package_v1(
      make_package(0U, "zero-bootstrap"), kLimits.level_package);
  std::vector<std::byte> package_one = openrc::encode_level_package_v1(
      make_package(1U, "one-bootstrap"), kLimits.level_package);
  std::array<std::vector<std::byte>, 2U> package_array{package_zero,
                                                       package_one};
  openrc::PreparedGameV2 manifest = make_manifest(package_array);

  [[nodiscard]] std::array<openrc::PreparedGameV2LevelPackageBytesV1, 2U>
  inputs() const {
    return {
        openrc::PreparedGameV2LevelPackageBytesV1{0U, package_zero},
        openrc::PreparedGameV2LevelPackageBytesV1{1U, package_one},
    };
  }
};

[[nodiscard]] std::size_t
transaction_directory_count(const std::filesystem::path &parent) {
  std::size_t result = 0U;
  for (const auto &entry : std::filesystem::directory_iterator(parent)) {
    const auto name = entry.path().filename().string();
    if (name.starts_with(".openrc-stage-") ||
        name.starts_with(".openrc-backup-") ||
        name.starts_with(".openrc-failed-")) {
      ++result;
    }
  }
  return result;
}

void test_new_publication_and_determinism() {
  TemporaryTree tree;
  Fixture fixture;
  const auto inputs = fixture.inputs();
  const auto first_root = tree.root / "first";
  const auto published = openrc::publish_prepared_game_v2_v1(
      first_root, fixture.manifest, inputs, kLimits);

  const auto expected_manifest =
      openrc::encode_prepared_game_v2(fixture.manifest, kLimits.manifest);
  expect(published.root == first_root && published.level_count == 2U &&
             published.package_bytes ==
                 fixture.package_zero.size() + fixture.package_one.size() &&
             published.manifest_sha256 ==
                 openrc::prepared_content_sha256_v1(expected_manifest),
         "Publication result metadata is wrong");
  expect(read_bytes(first_root / openrc::kPreparedGameV2ManifestFileName) ==
                 expected_manifest &&
             read_bytes(first_root / "levels/0.orlvl") ==
                 fixture.package_zero &&
             read_bytes(first_root / "levels/1.orlvl") == fixture.package_one,
         "Publication did not preserve the exact canonical bytes");

  const auto loaded =
      openrc::load_prepared_game_v2_root_v1(first_root, kLimits);
  static_cast<void>(
      openrc::load_prepared_game_level_package_v1(loaded, 0U, kLimits));
  static_cast<void>(
      openrc::load_prepared_game_level_package_v1(loaded, 1U, kLimits));

  const auto second_root = tree.root / "second";
  static_cast<void>(openrc::publish_prepared_game_v2_v1(
      second_root, fixture.manifest, inputs, kLimits));
  expect(read_bytes(second_root / openrc::kPreparedGameV2ManifestFileName) ==
                 read_bytes(first_root /
                            openrc::kPreparedGameV2ManifestFileName) &&
             read_bytes(second_root / "levels/0.orlvl") ==
                 read_bytes(first_root / "levels/0.orlvl") &&
             transaction_directory_count(tree.root) == 0U,
         "Repeated publication was not deterministic or leaked staging");
}

void test_existing_destination_is_replaced() {
  TemporaryTree tree;
  Fixture fixture;
  const auto destination = tree.root / "prepared";
    const auto inputs = fixture.inputs();
    static_cast<void>(openrc::publish_prepared_game_v2_v1(destination, fixture.manifest, inputs, kLimits));

    const auto replacement_zero = openrc::encode_level_package_v1(
        make_package(0U, "replacement-bootstrap"), kLimits.level_package);
    const std::array<std::vector<std::byte>, 2U> replacement_packages{
        replacement_zero, fixture.package_one};
    const auto replacement_manifest = make_manifest(replacement_packages);
    const std::array<openrc::PreparedGameV2LevelPackageBytesV1, 2U>
        replacement_inputs{
            openrc::PreparedGameV2LevelPackageBytesV1{0U, replacement_zero},
            openrc::PreparedGameV2LevelPackageBytesV1{1U, fixture.package_one}};
    static_cast<void>(openrc::publish_prepared_game_v2_v1(
        destination, replacement_manifest, replacement_inputs, kLimits));

    expect(read_bytes(destination / "levels/0.orlvl") == replacement_zero &&
               transaction_directory_count(tree.root) == 0U,
           "Existing publication was not replaced cleanly");
}

void test_unrecognized_existing_destination_is_preserved() {
    TemporaryTree tree;
    Fixture fixture;
    const auto destination = tree.root / "ordinary-directory";
    const auto inputs = fixture.inputs();
    std::filesystem::create_directory(destination);
    const auto marker = bytes_of("must-remain-untouched");
    write_bytes(destination / "user-data.bin", marker);

    expect_publish_rejected(
        [&] {
            static_cast<void>(openrc::publish_prepared_game_v2_v1(
                destination, fixture.manifest, inputs, kLimits));
        },
        "Publisher replaced an unrecognized existing directory");
    expect(read_bytes(destination / "user-data.bin") == marker &&
             transaction_directory_count(tree.root) == 0U,
           "Rejected replacement changed an ordinary directory");
}

void test_existing_publication_with_unowned_content_is_preserved() {
    TemporaryTree tree;
    Fixture fixture;
    const auto destination = tree.root / "prepared-with-user-content";
    const auto inputs = fixture.inputs();
    static_cast<void>(openrc::publish_prepared_game_v2_v1(
        destination, fixture.manifest, inputs, kLimits));

    const auto sentinel = bytes_of("must-not-be-deleted");
    std::filesystem::create_directory(destination / "mods");
    write_bytes(destination / "mods/sentinel.bin", sentinel);
    expect_publish_rejected(
        [&] {
            static_cast<void>(openrc::publish_prepared_game_v2_v1(
                destination, fixture.manifest, inputs, kLimits));
        },
        "Publisher replaced a prepared root containing unowned content");
    expect(read_bytes(destination / "mods/sentinel.bin") == sentinel &&
               transaction_directory_count(tree.root) == 0U,
           "Rejected replacement changed unowned prepared-root content");
}

void test_exact_manifest_input_binding() {
  TemporaryTree tree;
  Fixture fixture;
  const auto destination = tree.root / "prepared";
  const auto inputs = fixture.inputs();

  expect_publish_rejected(
      [&] {
        static_cast<void>(openrc::publish_prepared_game_v2_v1(
            destination, fixture.manifest,
            std::span<const openrc::PreparedGameV2LevelPackageBytesV1>(
                inputs.data(), 1U),
            kLimits));
      },
      "Publisher accepted a missing level package");

  auto duplicate = inputs;
  duplicate[1].level_id = 0U;
  expect_publish_rejected(
      [&] {
        static_cast<void>(openrc::publish_prepared_game_v2_v1(
            destination, fixture.manifest, duplicate, kLimits));
      },
      "Publisher accepted duplicate explicit level IDs");

  auto wrong_hash = fixture.manifest;
  wrong_hash.levels[0].package_sha256 = digest_of("wrong-package");
  expect_publish_rejected(
      [&] {
        static_cast<void>(openrc::publish_prepared_game_v2_v1(
            destination, wrong_hash, inputs, kLimits));
      },
      "Publisher accepted a manifest/package SHA mismatch");

  auto wrong_size = fixture.manifest;
  ++wrong_size.levels[0].package_bytes;
  expect_publish_rejected(
      [&] {
        static_cast<void>(openrc::publish_prepared_game_v2_v1(
            destination, wrong_size, inputs, kLimits));
      },
      "Publisher accepted a manifest/package size mismatch");

  auto reversed = fixture.manifest;
  std::ranges::reverse(reversed.levels);
  expect_publish_rejected(
      [&] {
        static_cast<void>(openrc::publish_prepared_game_v2_v1(
            destination, reversed, inputs, kLimits));
      },
      "Publisher accepted a non-canonical manifest order");

  const auto wrong_identity_bytes = openrc::encode_level_package_v1(
      make_package(0U, "zero-bootstrap", 2U), kLimits.level_package);
  auto wrong_identity_manifest = fixture.manifest;
  wrong_identity_manifest.levels[0].package_bytes = wrong_identity_bytes.size();
  wrong_identity_manifest.levels[0].package_sha256 =
      openrc::prepared_content_sha256_v1(wrong_identity_bytes);
  auto wrong_identity_inputs = inputs;
  wrong_identity_inputs[0].bytes = wrong_identity_bytes;
  expect_publish_rejected(
      [&] {
        static_cast<void>(openrc::publish_prepared_game_v2_v1(
            destination, wrong_identity_manifest, wrong_identity_inputs,
            kLimits));
      },
      "Publisher accepted a mismatched package content identity");

  expect(!std::filesystem::exists(destination) &&
             transaction_directory_count(tree.root) == 0U,
         "Rejected input changed the destination or leaked staging");
}

void test_path_conflicts_and_traversal_are_rejected() {
  TemporaryTree tree;
  Fixture fixture;
  const auto inputs = fixture.inputs();
  const auto destination = tree.root / "prepared";

  auto traversal = fixture.manifest;
  traversal.levels[0].package_path = "../outside.orlvl";
  expect_publish_rejected(
      [&] {
        static_cast<void>(openrc::publish_prepared_game_v2_v1(
            destination, traversal, inputs, kLimits));
      },
      "Publisher accepted level-package path traversal");

  auto manifest_collision = fixture.manifest;
  manifest_collision.levels[0].package_path =
      openrc::kPreparedGameV2ManifestFileName;
  expect_publish_rejected(
      [&] {
        static_cast<void>(openrc::publish_prepared_game_v2_v1(
            destination, manifest_collision, inputs, kLimits));
      },
      "Publisher accepted a package collision with the manifest");

  auto prefix_collision = fixture.manifest;
  prefix_collision.levels[0].package_path = "levels";
  prefix_collision.levels[1].package_path = "levels/one.orlvl";
  expect_publish_rejected(
      [&] {
        static_cast<void>(openrc::publish_prepared_game_v2_v1(
            destination, prefix_collision, inputs, kLimits));
      },
      "Publisher accepted a file/directory prefix collision");

  expect_publish_rejected(
      [&] {
        static_cast<void>(openrc::publish_prepared_game_v2_v1(
            std::filesystem::path("relative/prepared"), fixture.manifest,
            inputs, kLimits));
      },
      "Publisher accepted a relative destination");
  expect(!std::filesystem::exists(tree.root / "outside.orlvl") &&
             transaction_directory_count(tree.root) == 0U,
         "A rejected path escaped the publication parent");
}

struct CancellationContext {
  openrc::PreparedGameV2PublishCheckpointV1 checkpoint =
      openrc::PreparedGameV2PublishCheckpointV1::staged_and_verified;
  std::uint32_t calls = 0U;
};

[[nodiscard]] bool
cancel_at_checkpoint(const openrc::PreparedGameV2PublishCheckpointV1 checkpoint,
                     void *const opaque) noexcept {
  auto &context = *static_cast<CancellationContext *>(opaque);
  ++context.calls;
  return checkpoint == context.checkpoint;
}

void test_cancellation_rolls_back_existing_destination() {
  TemporaryTree tree;
  Fixture fixture;
  const auto destination = tree.root / "prepared";
    const auto old_inputs = fixture.inputs();
    static_cast<void>(openrc::publish_prepared_game_v2_v1(destination, fixture.manifest, old_inputs, kLimits));
    const auto old_manifest =
        read_bytes(destination / openrc::kPreparedGameV2ManifestFileName);
    const auto old_level_zero = read_bytes(destination / "levels/0.orlvl");

    const auto replacement_zero = openrc::encode_level_package_v1(
        make_package(0U, "cancelled-replacement"), kLimits.level_package);
    const std::array<std::vector<std::byte>, 2U> replacement_packages{
        replacement_zero, fixture.package_one};
    const auto replacement_manifest = make_manifest(replacement_packages);
    const std::array<openrc::PreparedGameV2LevelPackageBytesV1, 2U>
        replacement_inputs{
            openrc::PreparedGameV2LevelPackageBytesV1{0U, replacement_zero},
            openrc::PreparedGameV2LevelPackageBytesV1{1U, fixture.package_one}};

  CancellationContext cancellation{
      openrc::PreparedGameV2PublishCheckpointV1::destination_backed_up,
      0U,
  };
  const openrc::PreparedGameV2PublishControlV1 control{
      cancel_at_checkpoint,
      &cancellation,
  };
    expect_publish_rejected(
      [&] {
        static_cast<void>(openrc::publish_prepared_game_v2_v1(
            destination, replacement_manifest, replacement_inputs, kLimits, control));
      },
      "Publisher ignored cancellation during commit");

  expect(cancellation.calls == 2U &&
             read_bytes(destination / openrc::kPreparedGameV2ManifestFileName) ==
                old_manifest &&
            read_bytes(
                 destination / "levels/0.orlvl") == old_level_zero &&
             transaction_directory_count(tree.root) == 0U,
         "Cancellation did not roll the previous destination back exactly");
}

void test_cancellation_before_commit_leaves_no_destination() {
  TemporaryTree tree;
  Fixture fixture;
  const auto destination = tree.root / "prepared";
  CancellationContext cancellation{
      openrc::PreparedGameV2PublishCheckpointV1::staged_and_verified,
      0U,
  };
  const openrc::PreparedGameV2PublishControlV1 control{
      cancel_at_checkpoint,
      &cancellation,
  };
  const auto inputs = fixture.inputs();
  expect_publish_rejected(
      [&] {
        static_cast<void>(openrc::publish_prepared_game_v2_v1(
            destination, fixture.manifest, inputs, kLimits, control));
      },
      "Publisher ignored cancellation before commit");
  expect(cancellation.calls == 1U && !std::filesystem::exists(destination) &&
             transaction_directory_count(tree.root) == 0U,
         "Pre-commit cancellation leaked a partial publication");
}

void test_optional_overlays_are_not_discovered_or_published() {
  TemporaryTree tree;
  Fixture fixture;
  fixture.manifest.overlays.push_back(openrc::PreparedGameOverlayReferenceV2{
      "mods/not-installed",
      10,
      1U,
      "mods/not-installed/manifest.ormod",
      123U,
      digest_of("optional-mod-manifest"),
      digest_of("base-game-manifest"),
  });
  const auto inputs = fixture.inputs();
  const auto destination = tree.root / "prepared";
  static_cast<void>(openrc::publish_prepared_game_v2_v1(
      destination, fixture.manifest, inputs, kLimits));

  const auto loaded =
      openrc::load_prepared_game_v2_root_v1(destination, kLimits);
  expect(loaded.manifest.overlays.size() == 1U &&
             !std::filesystem::exists(destination / "mods"),
         "Base publisher discovered or emitted optional mod content");
}

void test_symlink_and_reparse_rejections_when_supported() {
  TemporaryTree tree;
  Fixture fixture;
  const auto inputs = fixture.inputs();
  const auto external = tree.root / "external";
  std::filesystem::create_directory(external);
  const auto marker = bytes_of("must-remain-untouched");
  write_bytes(external / "marker", marker);

  std::error_code link_error;
  const auto destination_link = tree.root / "prepared-link";
  std::filesystem::create_directory_symlink(external, destination_link,
                                            link_error);
  if (!link_error) {
    expect_publish_rejected(
        [&] {
          static_cast<void>(openrc::publish_prepared_game_v2_v1(
              destination_link, fixture.manifest, inputs, kLimits));
        },
        "Publisher replaced a symlinked/reparse destination");
    expect(read_bytes(external / "marker") == marker,
           "Publisher modified a symlink target");
  }

  link_error.clear();
  const auto real_parent = tree.root / "real-parent";
  std::filesystem::create_directory(real_parent);
  const auto parent_link = tree.root / "parent-link";
  std::filesystem::create_directory_symlink(real_parent, parent_link,
                                            link_error);
  if (!link_error) {
    expect_publish_rejected(
        [&] {
          static_cast<void>(openrc::publish_prepared_game_v2_v1(
              parent_link / "prepared", fixture.manifest, inputs, kLimits));
        },
        "Publisher traversed a symlinked/reparse parent");
  }
}

} // namespace

int main() {
  try {
    test_new_publication_and_determinism();
    test_existing_destination_is_replaced();
        test_unrecognized_existing_destination_is_preserved();
        test_existing_publication_with_unowned_content_is_preserved();
        test_exact_manifest_input_binding();
    test_path_conflicts_and_traversal_are_rejected();
    test_cancellation_rolls_back_existing_destination();
    test_cancellation_before_commit_leaves_no_destination();
    test_optional_overlays_are_not_discovered_or_published();
    test_symlink_and_reparse_rejections_when_supported();
    std::cout << "OpenRC PreparedGameV2 publisher tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "OpenRC PreparedGameV2 publisher tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
