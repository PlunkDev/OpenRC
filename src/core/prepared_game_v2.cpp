#include "openrc/prepared_game_v2.hpp"

#include "openrc/hash.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace openrc {
namespace {

enum class FormatKind {
  prepared_game,
  level_package,
};

constexpr std::array<std::byte, 8U> kPreparedGameMagicV2{
    std::byte{'O'}, std::byte{'R'}, std::byte{'P'}, std::byte{'R'},
    std::byte{'E'}, std::byte{'P'}, std::byte{'2'}, std::byte{0},
};

constexpr std::array<std::byte, 8U> kLevelPackageMagicV1{
    std::byte{'O'}, std::byte{'R'}, std::byte{'L'}, std::byte{'V'},
    std::byte{'L'}, std::byte{'P'}, std::byte{'K'}, std::byte{'1'},
};

[[noreturn]] void fail(const FormatKind kind, const std::string &message) {
  if (kind == FormatKind::prepared_game) {
    throw PreparedGameV2Error(message);
  }
  throw LevelPackageV1Error(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] bool digest_less(const PreparedContentDigestV1 &left,
                               const PreparedContentDigestV1 &right) noexcept {
  return std::lexicographical_compare(left.begin(), left.end(), right.begin(),
                                      right.end());
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const FormatKind kind,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(kind, std::string(description) + " overflows");
  }
  return left + right;
}

void validate_visible_ascii(const std::string_view value,
                            const std::uint32_t maximum_bytes,
                            const FormatKind kind,
                            const char *const description) {
  if (value.empty() || value.size() > maximum_bytes) {
    fail(kind, std::string(description) + " has an invalid length");
  }
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x21U || byte > 0x7eU) {
      fail(kind, std::string(description) + " is not canonical visible ASCII");
    }
  }
}

[[nodiscard]] bool identifier_character(const unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('a') &&
          value <= static_cast<unsigned char>('z')) ||
         (value >= static_cast<unsigned char>('0') &&
          value <= static_cast<unsigned char>('9')) ||
         value == static_cast<unsigned char>('.') ||
         value == static_cast<unsigned char>('_') ||
         value == static_cast<unsigned char>('-') ||
         value == static_cast<unsigned char>('/');
}

void validate_identifier(const std::string_view value,
                         const std::uint32_t maximum_bytes,
                         const FormatKind kind, const char *const description) {
  if (value.empty() || value.size() > maximum_bytes || value.front() == '/' ||
      value.back() == '/') {
    fail(kind,
         std::string(description) + " is not a canonical relative identifier");
  }
  for (const auto character : value) {
    if (!identifier_character(static_cast<unsigned char>(character))) {
      fail(kind,
           std::string(description) + " contains a non-canonical character");
    }
  }

  std::size_t component_begin = 0U;
  while (component_begin < value.size()) {
    const auto separator = value.find('/', component_begin);
    const auto component_end =
        separator == std::string_view::npos ? value.size() : separator;
    const auto component =
        value.substr(component_begin, component_end - component_begin);
    if (component.empty() || component == "." || component == "..") {
      fail(kind,
           std::string(description) + " contains an unsafe path component");
    }
    if (separator == std::string_view::npos) {
      break;
    }
    component_begin = separator + 1U;
  }
}

void validate_prepared_game_limits(const PreparedGameV2Limits &limits) {
  if (limits.max_input_bytes < kPreparedGameHeaderBytesV2 ||
      limits.max_levels == 0U || limits.max_string_bytes == 0U ||
      limits.max_total_referenced_package_bytes == 0U) {
    fail(FormatKind::prepared_game,
         "PreparedGameV2 limits must provide non-zero core bounds");
  }
}

void validate_level_package_limits(const LevelPackageV1Limits &limits) {
  if (limits.max_input_bytes < kLevelPackageHeaderBytesV1 ||
      limits.max_resources == 0U || limits.max_provenance_per_resource == 0U ||
      limits.max_total_provenance_records == 0U ||
      limits.max_string_bytes == 0U || limits.max_payload_bytes == 0U ||
      limits.max_total_payload_bytes == 0U) {
    fail(FormatKind::level_package,
         "LevelPackageV1 limits must provide non-zero core bounds");
  }
}

class ByteWriter final {
public:
  ByteWriter(const std::uint64_t maximum_bytes, const FormatKind kind)
      : maximum_bytes_(maximum_bytes), kind_(kind) {
    if (maximum_bytes_ > static_cast<std::uint64_t>(bytes_.max_size())) {
      maximum_bytes_ = bytes_.max_size();
    }
  }

  void append_u8(const std::uint8_t value) {
    append(std::span<const std::byte>(
        reinterpret_cast<const std::byte *>(&value), 1U));
  }

  void append_u32(const std::uint32_t value) {
    std::array<std::byte, 4U> encoded{};
    for (std::size_t index = 0U; index < encoded.size(); ++index) {
      encoded[index] =
          std::byte{static_cast<std::uint8_t>(value >> (index * 8U))};
    }
    append(encoded);
  }

  void append_i32(const std::int32_t value) {
    append_u32(static_cast<std::uint32_t>(value));
  }

  void append_u64(const std::uint64_t value) {
    std::array<std::byte, 8U> encoded{};
    for (std::size_t index = 0U; index < encoded.size(); ++index) {
      encoded[index] =
          std::byte{static_cast<std::uint8_t>(value >> (index * 8U))};
    }
    append(encoded);
  }

  void append_digest(const PreparedContentDigestV1 &digest) { append(digest); }

  void append_string(const std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
      fail(kind_, "A serialized string exceeds the 32-bit length field");
    }
    append_u32(static_cast<std::uint32_t>(value.size()));
    append(std::span<const std::byte>(
        reinterpret_cast<const std::byte *>(value.data()), value.size()));
  }

  void append(const std::span<const std::byte> bytes) {
    if (bytes.size() > maximum_bytes_ - bytes_.size()) {
      fail(kind_, "Canonical serialization exceeds its byte limit");
    }
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  [[nodiscard]] const std::vector<std::byte> &bytes() const noexcept {
    return bytes_;
  }

  [[nodiscard]] std::vector<std::byte> finish() && { return std::move(bytes_); }

private:
  std::uint64_t maximum_bytes_ = 0U;
  FormatKind kind_ = FormatKind::level_package;
  std::vector<std::byte> bytes_;
};

class ByteReader final {
public:
  ByteReader(const std::span<const std::byte> bytes, const FormatKind kind)
      : bytes_(bytes), kind_(kind) {}

  [[nodiscard]] std::uint8_t read_u8() {
    require(1U, "an 8-bit field");
    return byte_value(bytes_[position_++]);
  }

  [[nodiscard]] std::uint32_t read_u32() {
    require(4U, "a 32-bit field");
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
      value |= static_cast<std::uint32_t>(byte_value(bytes_[position_ + index]))
               << (index * 8U);
    }
    position_ += 4U;
    return value;
  }

  [[nodiscard]] std::int32_t read_i32() {
    return std::bit_cast<std::int32_t>(read_u32());
  }

  [[nodiscard]] std::uint64_t read_u64() {
    require(8U, "a 64-bit field");
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
      value |= static_cast<std::uint64_t>(byte_value(bytes_[position_ + index]))
               << (index * 8U);
    }
    position_ += 8U;
    return value;
  }

  [[nodiscard]] PreparedContentDigestV1 read_digest() {
    require(kPreparedContentDigestBytesV1, "a SHA-256 digest");
    PreparedContentDigestV1 result{};
    std::copy_n(bytes_.data() + position_, result.size(), result.data());
    position_ += result.size();
    return result;
  }

  [[nodiscard]] std::string read_string(const std::uint32_t maximum_bytes,
                                        const char *const description) {
    const auto size = read_u32();
    if (size > maximum_bytes) {
      fail(kind_, std::string(description) + " exceeds its string limit");
    }
    require(size, description);
    std::string result(
        reinterpret_cast<const char *>(bytes_.data() + position_), size);
    position_ += size;
    return result;
  }

  [[nodiscard]] std::vector<std::byte>
  read_owned_bytes(const std::uint64_t size, const char *const description) {
    if (size > std::numeric_limits<std::size_t>::max()) {
      fail(kind_, std::string(description) + " exceeds host size_t");
    }
    const auto host_size = static_cast<std::size_t>(size);
    require(host_size, description);
    std::vector<std::byte> result(host_size);
    std::copy_n(bytes_.data() + position_, host_size, result.data());
    position_ += host_size;
    return result;
  }

  [[nodiscard]] bool finished() const noexcept {
    return position_ == bytes_.size();
  }

private:
  void require(const std::size_t size, const char *const description) const {
    if (size > bytes_.size() - position_) {
      fail(kind_, std::string("Input ends inside ") + description);
    }
  }

  std::span<const std::byte> bytes_;
  FormatKind kind_ = FormatKind::level_package;
  std::size_t position_ = 0U;
};

struct ContainerHeader {
  std::uint32_t primary_count = 0U;
  std::uint32_t secondary_count = 0U;
  PreparedContentDigestV1 body_sha256{};
  std::span<const std::byte> body;
};

[[nodiscard]] ContainerHeader
parse_container_header(const std::span<const std::byte> bytes,
                       const std::span<const std::byte, 8U> expected_magic,
                       const std::uint32_t expected_version,
                       const std::uint32_t expected_header_bytes,
                       const std::uint64_t maximum_input_bytes,
                       const FormatKind kind) {
  if (bytes.size() > maximum_input_bytes) {
    fail(kind, "Container input exceeds the caller's byte limit");
  }
  if (bytes.size() < expected_header_bytes) {
    fail(kind, "Container input is smaller than its fixed header");
  }
  if (!std::equal(expected_magic.begin(), expected_magic.end(),
                  bytes.begin())) {
    fail(kind, "Container magic does not match the expected format");
  }

  ByteReader reader(bytes.subspan(8U, expected_header_bytes - 8U), kind);
  const auto version = reader.read_u32();
  const auto header_bytes = reader.read_u32();
  const auto total_bytes = reader.read_u64();
  ContainerHeader result;
  result.primary_count = reader.read_u32();
  result.secondary_count = reader.read_u32();
  result.body_sha256 = reader.read_digest();
  if (!reader.finished() || version != expected_version ||
      header_bytes != expected_header_bytes || total_bytes != bytes.size()) {
    fail(kind, "Container fixed header is inconsistent");
  }

  result.body = bytes.subspan(expected_header_bytes);
  if (prepared_content_sha256_v1(result.body) != result.body_sha256) {
    fail(kind, "Container body SHA-256 does not match its header");
  }
  return result;
}

[[nodiscard]] std::vector<std::byte> encode_container(
    const std::span<const std::byte, 8U> magic, const std::uint32_t version,
    const std::uint32_t header_bytes, const std::uint32_t primary_count,
    const std::uint32_t secondary_count, const std::span<const std::byte> body,
    const std::uint64_t maximum_input_bytes, const FormatKind kind) {
  const auto total_bytes = checked_add(header_bytes, body.size(), kind,
                                       "The serialized container size");
  if (total_bytes > maximum_input_bytes) {
    fail(kind, "Canonical container exceeds the caller's byte limit");
  }

  ByteWriter writer(maximum_input_bytes, kind);
  writer.append(magic);
  writer.append_u32(version);
  writer.append_u32(header_bytes);
  writer.append_u64(total_bytes);
  writer.append_u32(primary_count);
  writer.append_u32(secondary_count);
  writer.append_digest(prepared_content_sha256_v1(body));
  writer.append(body);
  return std::move(writer).finish();
}

[[nodiscard]] bool
level_reference_less(const PreparedGameLevelReferenceV2 *const left,
                     const PreparedGameLevelReferenceV2 *const right) noexcept {
  return left->level_id < right->level_id;
}

[[nodiscard]] bool overlay_reference_less(
    const PreparedGameOverlayReferenceV2 *const left,
    const PreparedGameOverlayReferenceV2 *const right) noexcept {
  return std::tie(left->priority, left->overlay_id) <
         std::tie(right->priority, right->overlay_id);
}

struct PreparedGameCanonicalView {
  std::vector<const PreparedGameLevelReferenceV2 *> levels;
  std::vector<const PreparedGameOverlayReferenceV2 *> overlays;
};

[[nodiscard]] PreparedGameCanonicalView
validate_prepared_game(const PreparedGameV2 &game,
                       const PreparedGameV2Limits &limits,
                       const bool require_canonical_order) {
  validate_prepared_game_limits(limits);
  if (game.content_api_version == 0U) {
    fail(FormatKind::prepared_game,
         "PreparedGameV2 content API version must be non-zero");
  }
  validate_visible_ascii(game.provenance.game_id, limits.max_string_bytes,
                         FormatKind::prepared_game, "PreparedGameV2 game ID");
  validate_visible_ascii(game.provenance.build_id, limits.max_string_bytes,
                         FormatKind::prepared_game, "PreparedGameV2 build ID");
  validate_visible_ascii(game.provenance.compiler_id, limits.max_string_bytes,
                         FormatKind::prepared_game,
                         "PreparedGameV2 compiler ID");
  validate_visible_ascii(game.provenance.compiler_version,
                         limits.max_string_bytes, FormatKind::prepared_game,
                         "PreparedGameV2 compiler version");
  if (game.provenance.source_image_bytes == 0U ||
      is_zero_prepared_digest_v1(game.provenance.source_image_sha256)) {
    fail(FormatKind::prepared_game,
         "PreparedGameV2 source-image provenance is incomplete");
  }
  if (game.provenance.prepared_game_v1_manifest_sha256 &&
      is_zero_prepared_digest_v1(
          *game.provenance.prepared_game_v1_manifest_sha256)) {
    fail(FormatKind::prepared_game,
         "PreparedGameV2 has a zero V1 manifest digest");
  }
  if (game.levels.empty() || game.levels.size() > limits.max_levels ||
      game.levels.size() > std::numeric_limits<std::uint32_t>::max()) {
    fail(FormatKind::prepared_game,
         "PreparedGameV2 has an invalid level-reference count");
  }
  if (game.overlays.size() > limits.max_overlays ||
      game.overlays.size() > std::numeric_limits<std::uint32_t>::max()) {
    fail(FormatKind::prepared_game,
         "PreparedGameV2 has too many overlay references");
  }

  PreparedGameCanonicalView result;
  result.levels.reserve(game.levels.size());
  std::set<std::uint32_t> level_ids;
  std::set<std::string> referenced_paths;
  std::uint64_t total_package_bytes = 0U;
  if (game.shared_package) {
    const auto &shared = *game.shared_package;
    validate_identifier(shared.package_path, limits.max_string_bytes,
                        FormatKind::prepared_game,
                        "PreparedGameV2 shared-package path");
    if (shared.package_bytes == 0U ||
        is_zero_prepared_digest_v1(shared.package_sha256) ||
        shared.package_bytes > limits.max_total_referenced_package_bytes) {
      fail(FormatKind::prepared_game,
           "PreparedGameV2 has an invalid or oversized shared reference");
    }
    referenced_paths.insert(shared.package_path);
    total_package_bytes = shared.package_bytes;
  }
  for (const auto &level : game.levels) {
    validate_identifier(level.package_path, limits.max_string_bytes,
                        FormatKind::prepared_game,
                        "PreparedGameV2 level-package path");
    if (level.level_id == kPreparedGameSharedPackageIdV2 ||
        level.package_bytes == 0U ||
        is_zero_prepared_digest_v1(level.package_sha256) ||
        !level_ids.insert(level.level_id).second ||
        !referenced_paths.insert(level.package_path).second) {
      fail(FormatKind::prepared_game,
           "PreparedGameV2 has an invalid or duplicate level reference");
    }
    total_package_bytes = checked_add(
        total_package_bytes, level.package_bytes, FormatKind::prepared_game,
        "PreparedGameV2 referenced package bytes");
    if (total_package_bytes > limits.max_total_referenced_package_bytes) {
      fail(FormatKind::prepared_game,
           "PreparedGameV2 referenced packages exceed their aggregate limit");
    }
    result.levels.push_back(&level);
  }

  result.overlays.reserve(game.overlays.size());
  std::set<std::string> overlay_ids;
  std::optional<PreparedContentDigestV1> overlay_base_sha256;
  std::uint64_t total_overlay_bytes = 0U;
  for (const auto &overlay : game.overlays) {
    validate_identifier(overlay.overlay_id, limits.max_string_bytes,
                        FormatKind::prepared_game, "PreparedGameV2 overlay ID");
    validate_identifier(overlay.manifest_path, limits.max_string_bytes,
                        FormatKind::prepared_game,
                        "PreparedGameV2 overlay-manifest path");
    if (overlay.overlay_id == "base" || overlay.priority < 0 ||
        overlay.content_api_version != game.content_api_version ||
        overlay.manifest_bytes == 0U ||
        is_zero_prepared_digest_v1(overlay.manifest_sha256) ||
        is_zero_prepared_digest_v1(overlay.required_base_game_sha256) ||
        !overlay_ids.insert(overlay.overlay_id).second ||
        !referenced_paths.insert(overlay.manifest_path).second) {
      fail(FormatKind::prepared_game,
           "PreparedGameV2 has an invalid or duplicate overlay reference");
    }
    if (!overlay_base_sha256) {
      overlay_base_sha256 = overlay.required_base_game_sha256;
    } else if (*overlay_base_sha256 != overlay.required_base_game_sha256) {
      fail(FormatKind::prepared_game,
           "PreparedGameV2 overlays target different base manifests");
    }
    total_overlay_bytes = checked_add(
        total_overlay_bytes, overlay.manifest_bytes, FormatKind::prepared_game,
        "PreparedGameV2 overlay-manifest bytes");
    if (total_overlay_bytes > limits.max_total_overlay_manifest_bytes) {
      fail(FormatKind::prepared_game,
           "PreparedGameV2 overlay manifests exceed their aggregate limit");
    }
    result.overlays.push_back(&overlay);
  }

  const auto levels_are_canonical = std::is_sorted(
      result.levels.begin(), result.levels.end(), level_reference_less);
  const auto overlays_are_canonical = std::is_sorted(
      result.overlays.begin(), result.overlays.end(), overlay_reference_less);
  if (require_canonical_order &&
      (!levels_are_canonical || !overlays_are_canonical)) {
    fail(FormatKind::prepared_game,
         "PreparedGameV2 references are not in canonical order");
  }
  if (!require_canonical_order) {
    std::sort(result.levels.begin(), result.levels.end(), level_reference_less);
    std::sort(result.overlays.begin(), result.overlays.end(),
              overlay_reference_less);
  }
  return result;
}

[[nodiscard]] bool
provenance_less(const LevelPackageProvenanceV1 *const left,
                const LevelPackageProvenanceV1 *const right) noexcept {
  if (std::tie(left->kind, left->source_locator, left->source_offset,
               left->source_bytes) !=
      std::tie(right->kind, right->source_locator, right->source_offset,
               right->source_bytes)) {
    return std::tie(left->kind, left->source_locator, left->source_offset,
                    left->source_bytes) <
           std::tie(right->kind, right->source_locator, right->source_offset,
                    right->source_bytes);
  }
  return digest_less(left->source_sha256, right->source_sha256);
}

[[nodiscard]] bool
resource_less(const LevelPackageResourceV1 *const left,
              const LevelPackageResourceV1 *const right) noexcept {
  return left->resource_id < right->resource_id;
}

struct LevelPackageCanonicalResource {
  const LevelPackageResourceV1 *resource = nullptr;
  std::vector<const LevelPackageProvenanceV1 *> provenance;
  PreparedContentDigestV1 payload_sha256{};
};

struct LevelPackageCanonicalView {
  std::vector<LevelPackageCanonicalResource> resources;
  std::uint32_t total_provenance_records = 0U;
};

[[nodiscard]] bool
known_provenance_kind(const LevelPackageProvenanceKindV1 kind) noexcept {
  switch (kind) {
  case LevelPackageProvenanceKindV1::iso_range:
  case LevelPackageProvenanceKindV1::prepared_resource:
  case LevelPackageProvenanceKindV1::generated:
  case LevelPackageProvenanceKindV1::mod_resource:
    return true;
  }
  return false;
}

[[nodiscard]] bool known_resource_operation(
    const LevelPackageResourceOperationV1 operation) noexcept {
  switch (operation) {
  case LevelPackageResourceOperationV1::upsert:
  case LevelPackageResourceOperationV1::remove:
    return true;
  }
  return false;
}

[[nodiscard]] LevelPackageCanonicalView
validate_level_package(const LevelPackageV1 &package,
                       const LevelPackageV1Limits &limits,
                       const bool require_canonical_order) {
  validate_level_package_limits(limits);
  if (package.content_api_version == 0U) {
    fail(FormatKind::level_package,
         "LevelPackageV1 content API version must be non-zero");
  }
  validate_visible_ascii(package.build_id, limits.max_string_bytes,
                         FormatKind::level_package, "LevelPackageV1 build ID");
  validate_identifier(package.layer_id, limits.max_string_bytes,
                      FormatKind::level_package, "LevelPackageV1 layer ID");

  switch (package.layer_kind) {
  case LevelPackageLayerKindV1::base:
    if (package.layer_id != "base" || package.priority != 0 ||
        !is_zero_prepared_digest_v1(package.required_base_package_sha256)) {
      fail(FormatKind::level_package,
           "A base LevelPackageV1 has invalid layer metadata");
    }
    break;
  case LevelPackageLayerKindV1::overlay:
    if (package.layer_id == "base" || package.priority < 0 ||
        is_zero_prepared_digest_v1(package.required_base_package_sha256)) {
      fail(FormatKind::level_package,
           "An overlay LevelPackageV1 has invalid layer metadata");
    }
    break;
  default:
    fail(FormatKind::level_package, "LevelPackageV1 has an unknown layer kind");
  }

  if (package.resources.empty() ||
      package.resources.size() > limits.max_resources ||
      package.resources.size() > std::numeric_limits<std::uint32_t>::max()) {
    fail(FormatKind::level_package,
         "LevelPackageV1 has an invalid resource count");
  }

  LevelPackageCanonicalView result;
  result.resources.reserve(package.resources.size());
  std::set<std::string> resource_ids;
  std::uint64_t total_payload_bytes = 0U;
  std::uint64_t total_provenance = 0U;
  for (const auto &resource : package.resources) {
    validate_identifier(resource.resource_id, limits.max_string_bytes,
                        FormatKind::level_package,
                        "LevelPackageV1 resource ID");
    validate_identifier(resource.type_id, limits.max_string_bytes,
                        FormatKind::level_package,
                        "LevelPackageV1 resource type ID");
    if (!resource_ids.insert(resource.resource_id).second ||
        resource.schema_version == 0U ||
        !known_resource_operation(resource.operation) ||
        (resource.flags & ~kLevelPackageKnownResourceFlagsV1) != 0U) {
      fail(FormatKind::level_package,
           "LevelPackageV1 has invalid or duplicate resource metadata");
    }
    if (package.layer_kind == LevelPackageLayerKindV1::base &&
        resource.operation != LevelPackageResourceOperationV1::upsert) {
      fail(FormatKind::level_package,
           "A base LevelPackageV1 contains a remove operation");
    }
    if (resource.operation == LevelPackageResourceOperationV1::remove &&
        (!resource.payload.empty() || resource.flags != 0U)) {
      fail(FormatKind::level_package,
           "A LevelPackageV1 remove operation carries payload or flags");
    }
    if (resource.payload.size() > limits.max_payload_bytes) {
      fail(FormatKind::level_package,
           "A LevelPackageV1 resource exceeds its payload limit");
    }
    total_payload_bytes = checked_add(
        total_payload_bytes, resource.payload.size(), FormatKind::level_package,
        "LevelPackageV1 aggregate payload bytes");
    if (total_payload_bytes > limits.max_total_payload_bytes) {
      fail(FormatKind::level_package,
           "LevelPackageV1 payloads exceed their aggregate limit");
    }
    if (resource.provenance.empty() ||
        resource.provenance.size() > limits.max_provenance_per_resource ||
        resource.provenance.size() >
            std::numeric_limits<std::uint32_t>::max()) {
      fail(FormatKind::level_package,
           "A LevelPackageV1 resource has invalid provenance count");
    }
    total_provenance = checked_add(total_provenance, resource.provenance.size(),
                                   FormatKind::level_package,
                                   "LevelPackageV1 provenance count");
    if (total_provenance > limits.max_total_provenance_records ||
        total_provenance > std::numeric_limits<std::uint32_t>::max()) {
      fail(FormatKind::level_package,
           "LevelPackageV1 provenance exceeds its aggregate limit");
    }

    LevelPackageCanonicalResource canonical;
    canonical.resource = &resource;
    canonical.payload_sha256 = prepared_content_sha256_v1(resource.payload);
    if (!is_zero_prepared_digest_v1(resource.payload_sha256) &&
        resource.payload_sha256 != canonical.payload_sha256) {
      fail(FormatKind::level_package,
           "A LevelPackageV1 resource payload digest is stale");
    }
    canonical.provenance.reserve(resource.provenance.size());
    bool has_mod_provenance = false;
    for (const auto &provenance : resource.provenance) {
      if (!known_provenance_kind(provenance.kind)) {
        fail(FormatKind::level_package,
             "A LevelPackageV1 provenance kind is unknown");
      }
      validate_visible_ascii(provenance.source_locator, limits.max_string_bytes,
                             FormatKind::level_package,
                             "LevelPackageV1 provenance locator");
      static_cast<void>(checked_add(
          provenance.source_offset, provenance.source_bytes,
          FormatKind::level_package, "A LevelPackageV1 provenance range"));
      if (provenance.kind == LevelPackageProvenanceKindV1::generated) {
        if (provenance.source_bytes == 0U &&
            (provenance.source_offset != 0U ||
             !is_zero_prepared_digest_v1(provenance.source_sha256))) {
          fail(FormatKind::level_package,
               "Generated provenance has an inconsistent empty range");
        }
        if (provenance.source_bytes != 0U &&
            is_zero_prepared_digest_v1(provenance.source_sha256)) {
          fail(FormatKind::level_package,
               "Generated provenance with bytes lacks a digest");
        }
      } else if (provenance.source_bytes == 0U ||
                 is_zero_prepared_digest_v1(provenance.source_sha256)) {
        fail(FormatKind::level_package,
             "Direct LevelPackageV1 provenance is incomplete");
      }
      has_mod_provenance |=
          provenance.kind == LevelPackageProvenanceKindV1::mod_resource;
      canonical.provenance.push_back(&provenance);
    }
    if (package.layer_kind == LevelPackageLayerKindV1::overlay &&
        !has_mod_provenance) {
      fail(FormatKind::level_package,
           "An overlay resource lacks mod-resource provenance");
    }

    const auto provenance_are_canonical =
        std::is_sorted(canonical.provenance.begin(), canonical.provenance.end(),
                       provenance_less);
    if (require_canonical_order && !provenance_are_canonical) {
      fail(FormatKind::level_package,
           "LevelPackageV1 provenance is not in canonical order");
    }
    if (!require_canonical_order) {
      std::sort(canonical.provenance.begin(), canonical.provenance.end(),
                provenance_less);
    }
    for (std::size_t index = 1U; index < canonical.provenance.size(); ++index) {
      const auto *const previous = canonical.provenance[index - 1U];
      const auto *const current = canonical.provenance[index];
      if (!provenance_less(previous, current) &&
          !provenance_less(current, previous)) {
        fail(FormatKind::level_package,
             "A LevelPackageV1 resource repeats provenance");
      }
    }
    result.resources.push_back(std::move(canonical));
  }

  const auto resources_are_canonical =
      std::is_sorted(result.resources.begin(), result.resources.end(),
                     [](const LevelPackageCanonicalResource &left,
                        const LevelPackageCanonicalResource &right) {
                       return resource_less(left.resource, right.resource);
                     });
  if (require_canonical_order && !resources_are_canonical) {
    fail(FormatKind::level_package,
         "LevelPackageV1 resources are not in canonical order");
  }
  if (!require_canonical_order) {
    std::sort(result.resources.begin(), result.resources.end(),
              [](const LevelPackageCanonicalResource &left,
                 const LevelPackageCanonicalResource &right) {
                return resource_less(left.resource, right.resource);
              });
  }
  result.total_provenance_records =
      static_cast<std::uint32_t>(total_provenance);
  return result;
}

void append_zero_bytes(ByteWriter &writer, std::size_t count) {
  std::array<std::byte, 8U> zeros{};
  while (count > 0U) {
    const auto chunk = std::min(count, zeros.size());
    writer.append(std::span<const std::byte>(zeros).first(chunk));
    count -= chunk;
  }
}

void require_zero_bytes(ByteReader &reader, const std::size_t count,
                        const FormatKind kind, const char *const description) {
  for (std::size_t index = 0U; index < count; ++index) {
    if (reader.read_u8() != 0U) {
      fail(kind, std::string(description) + " is non-zero");
    }
  }
}

[[nodiscard]] LevelPackageResourceV1
normalized_resource(const LevelPackageResourceV1 &source) {
  auto result = source;
  result.payload_sha256 = prepared_content_sha256_v1(result.payload);
  return result;
}

} // namespace

PreparedContentDigestV1
prepared_content_sha256_v1(const std::span<const std::byte> bytes) {
  Sha256 hash;
  hash.update(bytes);
  return hash.finish();
}

bool is_zero_prepared_digest_v1(
    const PreparedContentDigestV1 &digest) noexcept {
  return std::all_of(digest.begin(), digest.end(), [](const std::byte value) {
    return value == std::byte{0};
  });
}

std::vector<std::byte>
encode_prepared_game_v2(const PreparedGameV2 &game,
                        const PreparedGameV2Limits limits) {
  const auto canonical = validate_prepared_game(game, limits, false);
  ByteWriter body(limits.max_input_bytes - kPreparedGameHeaderBytesV2,
                  FormatKind::prepared_game);
  body.append_u32(game.content_api_version);
  body.append_u32(game.shared_package ? kPreparedGameHasSharedPackageV2 : 0U);
  body.append_string(game.provenance.game_id);
  body.append_string(game.provenance.build_id);
  body.append_string(game.provenance.compiler_id);
  body.append_string(game.provenance.compiler_version);
  body.append_u64(game.provenance.source_image_bytes);
  body.append_digest(game.provenance.source_image_sha256);
  body.append_u8(game.provenance.prepared_game_v1_manifest_sha256 ? 1U : 0U);
  append_zero_bytes(body, 7U);
  body.append_digest(game.provenance.prepared_game_v1_manifest_sha256.value_or(
      PreparedContentDigestV1{}));

  if (game.shared_package) {
    body.append_u64(game.shared_package->package_bytes);
    body.append_digest(game.shared_package->package_sha256);
    body.append_string(game.shared_package->package_path);
  }

  for (const auto *const level : canonical.levels) {
    body.append_u32(level->level_id);
    body.append_u32(0U);
    body.append_string(level->package_path);
    body.append_u64(level->package_bytes);
    body.append_digest(level->package_sha256);
  }
  for (const auto *const overlay : canonical.overlays) {
    body.append_i32(overlay->priority);
    body.append_u32(overlay->content_api_version);
    body.append_string(overlay->overlay_id);
    body.append_string(overlay->manifest_path);
    body.append_u64(overlay->manifest_bytes);
    body.append_digest(overlay->manifest_sha256);
    body.append_digest(overlay->required_base_game_sha256);
  }

  return encode_container(kPreparedGameMagicV2, kPreparedGameFormatVersionV2,
                          kPreparedGameHeaderBytesV2,
                          static_cast<std::uint32_t>(canonical.levels.size()),
                          static_cast<std::uint32_t>(canonical.overlays.size()),
                          body.bytes(), limits.max_input_bytes,
                          FormatKind::prepared_game);
}

PreparedGameV2 parse_prepared_game_v2(const std::span<const std::byte> bytes,
                                      const PreparedGameV2Limits limits) {
  validate_prepared_game_limits(limits);
  const auto header = parse_container_header(
      bytes, kPreparedGameMagicV2, kPreparedGameFormatVersionV2,
      kPreparedGameHeaderBytesV2, limits.max_input_bytes,
      FormatKind::prepared_game);
  if (header.primary_count == 0U || header.primary_count > limits.max_levels ||
      header.secondary_count > limits.max_overlays) {
    fail(FormatKind::prepared_game,
         "PreparedGameV2 header counts exceed caller limits");
  }

  ByteReader body(header.body, FormatKind::prepared_game);
  PreparedGameV2 result;
  result.content_api_version = body.read_u32();
  const auto features = body.read_u32();
  if ((features & ~kPreparedGameHasSharedPackageV2) != 0U) {
    fail(FormatKind::prepared_game,
         "PreparedGameV2 has unknown required features");
  }
  result.provenance.game_id =
      body.read_string(limits.max_string_bytes, "PreparedGameV2 game ID");
  result.provenance.build_id =
      body.read_string(limits.max_string_bytes, "PreparedGameV2 build ID");
  result.provenance.compiler_id =
      body.read_string(limits.max_string_bytes, "PreparedGameV2 compiler ID");
  result.provenance.compiler_version = body.read_string(
      limits.max_string_bytes, "PreparedGameV2 compiler version");
  result.provenance.source_image_bytes = body.read_u64();
  result.provenance.source_image_sha256 = body.read_digest();
  const auto has_v1_manifest = body.read_u8();
  if (has_v1_manifest > 1U) {
    fail(FormatKind::prepared_game,
         "PreparedGameV2 V1-manifest presence flag is invalid");
  }
  require_zero_bytes(body, 7U, FormatKind::prepared_game,
                     "PreparedGameV2 V1-manifest reserved bytes");
  const auto v1_manifest_sha256 = body.read_digest();
  if (has_v1_manifest != 0U) {
    result.provenance.prepared_game_v1_manifest_sha256 = v1_manifest_sha256;
  } else if (!is_zero_prepared_digest_v1(v1_manifest_sha256)) {
    fail(FormatKind::prepared_game,
         "PreparedGameV2 absent V1 manifest has a non-zero digest");
  }

  if ((features & kPreparedGameHasSharedPackageV2) != 0U) {
    PreparedGameSharedReferenceV2 shared;
    shared.package_bytes = body.read_u64();
    shared.package_sha256 = body.read_digest();
    shared.package_path = body.read_string(
        limits.max_string_bytes, "PreparedGameV2 shared-package path");
    result.shared_package = std::move(shared);
  }

  result.levels.reserve(header.primary_count);
  for (std::uint32_t index = 0U; index < header.primary_count; ++index) {
    PreparedGameLevelReferenceV2 level;
    level.level_id = body.read_u32();
    if (body.read_u32() != 0U) {
      fail(FormatKind::prepared_game,
           "PreparedGameV2 level reserved word is non-zero");
    }
    level.package_path = body.read_string(limits.max_string_bytes,
                                          "PreparedGameV2 level-package path");
    level.package_bytes = body.read_u64();
    level.package_sha256 = body.read_digest();
    result.levels.push_back(std::move(level));
  }

  result.overlays.reserve(header.secondary_count);
  for (std::uint32_t index = 0U; index < header.secondary_count; ++index) {
    PreparedGameOverlayReferenceV2 overlay;
    overlay.priority = body.read_i32();
    overlay.content_api_version = body.read_u32();
    overlay.overlay_id =
        body.read_string(limits.max_string_bytes, "PreparedGameV2 overlay ID");
    overlay.manifest_path = body.read_string(
        limits.max_string_bytes, "PreparedGameV2 overlay-manifest path");
    overlay.manifest_bytes = body.read_u64();
    overlay.manifest_sha256 = body.read_digest();
    overlay.required_base_game_sha256 = body.read_digest();
    result.overlays.push_back(std::move(overlay));
  }
  if (!body.finished()) {
    fail(FormatKind::prepared_game,
         "PreparedGameV2 contains trailing body bytes");
  }
  static_cast<void>(validate_prepared_game(result, limits, true));
  return result;
}

std::vector<std::byte>
encode_level_package_v1(const LevelPackageV1 &package,
                        const LevelPackageV1Limits limits) {
  const auto canonical = validate_level_package(package, limits, false);
  ByteWriter body(limits.max_input_bytes - kLevelPackageHeaderBytesV1,
                  FormatKind::level_package);
  body.append_u32(package.level_id);
  body.append_u32(package.content_api_version);
  body.append_u32(static_cast<std::uint32_t>(package.layer_kind));
  body.append_i32(package.priority);
  body.append_u32(0U);
  body.append_string(package.build_id);
  body.append_string(package.layer_id);
  body.append_digest(package.required_base_package_sha256);

  for (const auto &canonical_resource : canonical.resources) {
    const auto &resource = *canonical_resource.resource;
    body.append_string(resource.resource_id);
    body.append_string(resource.type_id);
    body.append_u32(resource.schema_version);
    body.append_u32(static_cast<std::uint32_t>(resource.operation));
    body.append_u32(resource.flags);
    body.append_u32(
        static_cast<std::uint32_t>(canonical_resource.provenance.size()));
    body.append_u64(resource.payload.size());
    body.append_digest(canonical_resource.payload_sha256);
    for (const auto *const provenance : canonical_resource.provenance) {
      body.append_u32(static_cast<std::uint32_t>(provenance->kind));
      body.append_u32(0U);
      body.append_string(provenance->source_locator);
      body.append_u64(provenance->source_offset);
      body.append_u64(provenance->source_bytes);
      body.append_digest(provenance->source_sha256);
    }
    body.append(resource.payload);
  }

  return encode_container(
      kLevelPackageMagicV1, kLevelPackageFormatVersionV1,
      kLevelPackageHeaderBytesV1,
      static_cast<std::uint32_t>(canonical.resources.size()),
      canonical.total_provenance_records, body.bytes(), limits.max_input_bytes,
      FormatKind::level_package);
}

LevelPackageV1 parse_level_package_v1(const std::span<const std::byte> bytes,
                                      const LevelPackageV1Limits limits) {
  validate_level_package_limits(limits);
  const auto header = parse_container_header(
      bytes, kLevelPackageMagicV1, kLevelPackageFormatVersionV1,
      kLevelPackageHeaderBytesV1, limits.max_input_bytes,
      FormatKind::level_package);
  if (header.primary_count == 0U ||
      header.primary_count > limits.max_resources ||
      header.secondary_count > limits.max_total_provenance_records) {
    fail(FormatKind::level_package,
         "LevelPackageV1 header counts exceed caller limits");
  }

  ByteReader body(header.body, FormatKind::level_package);
  LevelPackageV1 result;
  result.level_id = body.read_u32();
  result.content_api_version = body.read_u32();
  result.layer_kind = static_cast<LevelPackageLayerKindV1>(body.read_u32());
  result.priority = body.read_i32();
  if (body.read_u32() != 0U) {
    fail(FormatKind::level_package,
         "LevelPackageV1 body reserved word is non-zero");
  }
  result.build_id =
      body.read_string(limits.max_string_bytes, "LevelPackageV1 build ID");
  result.layer_id =
      body.read_string(limits.max_string_bytes, "LevelPackageV1 layer ID");
  result.required_base_package_sha256 = body.read_digest();

  result.resources.reserve(header.primary_count);
  std::uint64_t total_payload_bytes = 0U;
  std::uint64_t total_provenance = 0U;
  for (std::uint32_t resource_index = 0U; resource_index < header.primary_count;
       ++resource_index) {
    LevelPackageResourceV1 resource;
    resource.resource_id =
        body.read_string(limits.max_string_bytes, "LevelPackageV1 resource ID");
    resource.type_id = body.read_string(limits.max_string_bytes,
                                        "LevelPackageV1 resource type ID");
    resource.schema_version = body.read_u32();
    resource.operation =
        static_cast<LevelPackageResourceOperationV1>(body.read_u32());
    resource.flags = body.read_u32();
    const auto provenance_count = body.read_u32();
    const auto payload_bytes = body.read_u64();
    const auto stored_payload_sha256 = body.read_digest();
    if (provenance_count == 0U ||
        provenance_count > limits.max_provenance_per_resource) {
      fail(FormatKind::level_package,
           "A LevelPackageV1 resource provenance count exceeds its limit");
    }
    total_provenance = checked_add(total_provenance, provenance_count,
                                   FormatKind::level_package,
                                   "LevelPackageV1 parsed provenance count");
    if (total_provenance > limits.max_total_provenance_records ||
        total_provenance > header.secondary_count) {
      fail(FormatKind::level_package,
           "LevelPackageV1 parsed provenance exceeds its aggregate count");
    }
    if (payload_bytes > limits.max_payload_bytes) {
      fail(FormatKind::level_package,
           "A LevelPackageV1 parsed payload exceeds its limit");
    }
    total_payload_bytes = checked_add(total_payload_bytes, payload_bytes,
                                      FormatKind::level_package,
                                      "LevelPackageV1 parsed payload bytes");
    if (total_payload_bytes > limits.max_total_payload_bytes) {
      fail(FormatKind::level_package,
           "LevelPackageV1 parsed payloads exceed their aggregate limit");
    }

    resource.provenance.reserve(provenance_count);
    for (std::uint32_t provenance_index = 0U;
         provenance_index < provenance_count; ++provenance_index) {
      LevelPackageProvenanceV1 provenance;
      provenance.kind =
          static_cast<LevelPackageProvenanceKindV1>(body.read_u32());
      if (body.read_u32() != 0U) {
        fail(FormatKind::level_package,
             "LevelPackageV1 provenance reserved word is non-zero");
      }
      provenance.source_locator = body.read_string(
          limits.max_string_bytes, "LevelPackageV1 provenance locator");
      provenance.source_offset = body.read_u64();
      provenance.source_bytes = body.read_u64();
      provenance.source_sha256 = body.read_digest();
      resource.provenance.push_back(std::move(provenance));
    }
    resource.payload =
        body.read_owned_bytes(payload_bytes, "a LevelPackageV1 payload");
    resource.payload_sha256 = prepared_content_sha256_v1(resource.payload);
    if (resource.payload_sha256 != stored_payload_sha256) {
      fail(FormatKind::level_package,
           "A LevelPackageV1 payload SHA-256 does not match");
    }
    result.resources.push_back(std::move(resource));
  }
  if (total_provenance != header.secondary_count || !body.finished()) {
    fail(FormatKind::level_package,
         "LevelPackageV1 body counts or trailing bytes are inconsistent");
  }
  static_cast<void>(validate_level_package(result, limits, true));
  return result;
}

PreparedContentDigestV1
level_package_sha256_v1(const LevelPackageV1 &package,
                        const LevelPackageV1Limits limits) {
  const auto bytes = encode_level_package_v1(package, limits);
  return prepared_content_sha256_v1(bytes);
}

const PreparedGameLevelReferenceV2 *
find_prepared_game_level_v2(const PreparedGameV2 &game,
                            const std::uint32_t level_id) noexcept {
  const auto iterator =
      std::find_if(game.levels.begin(), game.levels.end(),
                   [level_id](const PreparedGameLevelReferenceV2 &level) {
                     return level.level_id == level_id;
                   });
  return iterator == game.levels.end() ? nullptr : &*iterator;
}

LevelPackageV1 parse_prepared_game_level_package_v1(
    const PreparedGameV2 &game, const std::uint32_t level_id,
    const std::span<const std::byte> package_bytes,
    const LevelPackageV1Limits limits) {
  const auto *const reference = find_prepared_game_level_v2(game, level_id);
  if (reference == nullptr) {
    throw PreparedGameV2Error(
        "PreparedGameV2 does not reference the requested level");
  }
  if (reference->package_bytes != package_bytes.size() ||
      reference->package_sha256 != prepared_content_sha256_v1(package_bytes)) {
    throw PreparedGameV2Error(
        "LevelPackageV1 bytes do not match their PreparedGameV2 reference");
  }

  LevelPackageV1 package;
  try {
    package = parse_level_package_v1(package_bytes, limits);
  } catch (const LevelPackageV1Error &error) {
    throw PreparedGameV2Error("Referenced LevelPackageV1 is invalid: " +
                              std::string(error.what()));
  }
  if (package.layer_kind != LevelPackageLayerKindV1::base ||
      package.level_id != level_id ||
      package.content_api_version != game.content_api_version ||
      package.build_id != game.provenance.build_id) {
    throw PreparedGameV2Error(
        "Referenced LevelPackageV1 identity disagrees with PreparedGameV2");
  }
  return package;
}

LevelPackageV1 parse_prepared_game_shared_package_v1(
    const PreparedGameV2 &game, const std::span<const std::byte> package_bytes,
    const LevelPackageV1Limits limits) {
  if (!game.shared_package) {
    throw PreparedGameV2Error("PreparedGameV2 does not reference a shared package");
  }
  const auto &reference = *game.shared_package;
  if (reference.package_bytes != package_bytes.size() ||
      reference.package_sha256 != prepared_content_sha256_v1(package_bytes)) {
    throw PreparedGameV2Error(
        "Shared LevelPackageV1 bytes do not match their PreparedGameV2 reference");
  }
  LevelPackageV1 package;
  try {
    package = parse_level_package_v1(package_bytes, limits);
  } catch (const LevelPackageV1Error &error) {
    throw PreparedGameV2Error("Referenced shared LevelPackageV1 is invalid: " +
                              std::string(error.what()));
  }
  if (package.layer_kind != LevelPackageLayerKindV1::base ||
      package.level_id != kPreparedGameSharedPackageIdV2 ||
      package.content_api_version != game.content_api_version ||
      package.build_id != game.provenance.build_id) {
    throw PreparedGameV2Error(
        "Referenced shared LevelPackageV1 identity disagrees with PreparedGameV2");
  }
  return package;
}

ResolvedLevelPackageV1
resolve_level_package_v1(const LevelPackageV1 &base,
                         const std::span<const LevelPackageV1> overlays,
                         const LevelPackageV1Limits limits) {
  validate_level_package_limits(limits);
  if (overlays.size() > limits.max_overlays_to_resolve) {
    fail(FormatKind::level_package,
         "LevelPackageV1 overlay count exceeds its resolution limit");
  }
  const auto base_view = validate_level_package(base, limits, false);
  if (base.layer_kind != LevelPackageLayerKindV1::base) {
    fail(FormatKind::level_package,
         "LevelPackageV1 overlay resolution requires a base package");
  }
  const auto base_sha256 = level_package_sha256_v1(base, limits);

  std::vector<const LevelPackageV1 *> ordered_overlays;
  ordered_overlays.reserve(overlays.size());
  std::set<std::string> layer_ids;
  for (const auto &overlay : overlays) {
    static_cast<void>(validate_level_package(overlay, limits, false));
    if (overlay.layer_kind != LevelPackageLayerKindV1::overlay ||
        overlay.level_id != base.level_id ||
        overlay.content_api_version != base.content_api_version ||
        overlay.build_id != base.build_id ||
        overlay.required_base_package_sha256 != base_sha256 ||
        !layer_ids.insert(overlay.layer_id).second) {
      fail(FormatKind::level_package,
           "A LevelPackageV1 overlay is incompatible with its base");
    }
    ordered_overlays.push_back(&overlay);
  }
  std::sort(
      ordered_overlays.begin(), ordered_overlays.end(),
      [](const LevelPackageV1 *const left, const LevelPackageV1 *const right) {
        return std::tie(left->priority, left->layer_id) <
               std::tie(right->priority, right->layer_id);
      });

  std::map<std::string, LevelPackageResourceV1> visible_resources;
  for (const auto &resource : base_view.resources) {
    visible_resources.emplace(resource.resource->resource_id,
                              normalized_resource(*resource.resource));
  }

  ResolvedLevelPackageV1 result;
  result.level_id = base.level_id;
  result.content_api_version = base.content_api_version;
  result.build_id = base.build_id;
  result.base_package_sha256 = base_sha256;
  result.applied_overlays.reserve(ordered_overlays.size());
  for (const auto *const overlay : ordered_overlays) {
    const auto overlay_view = validate_level_package(*overlay, limits, false);
    const auto overlay_sha256 = level_package_sha256_v1(*overlay, limits);
    for (const auto &canonical_resource : overlay_view.resources) {
      const auto &operation = *canonical_resource.resource;
      const auto existing = visible_resources.find(operation.resource_id);
      if (operation.operation == LevelPackageResourceOperationV1::remove) {
        if (existing == visible_resources.end() ||
            (existing->second.flags &
             kLevelPackageResourceOverlayRemovableV1) == 0U ||
            existing->second.type_id != operation.type_id ||
            existing->second.schema_version != operation.schema_version) {
          fail(FormatKind::level_package,
               "A LevelPackageV1 overlay cannot remove its target");
        }
        visible_resources.erase(existing);
        continue;
      }

      if (existing != visible_resources.end()) {
        if ((existing->second.flags &
             kLevelPackageResourceOverlayReplaceableV1) == 0U ||
            existing->second.type_id != operation.type_id ||
            existing->second.schema_version != operation.schema_version) {
          fail(FormatKind::level_package,
               "A LevelPackageV1 overlay cannot replace its target");
        }
        existing->second = normalized_resource(operation);
      } else {
        if (visible_resources.size() >= limits.max_resources) {
          fail(FormatKind::level_package,
               "Resolved LevelPackageV1 exceeds its resource limit");
        }
        visible_resources.emplace(operation.resource_id,
                                  normalized_resource(operation));
      }
    }
    result.applied_overlays.push_back(AppliedLevelPackageLayerV1{
        overlay->layer_id,
        overlay->priority,
        overlay_sha256,
    });
  }

  std::uint64_t total_payload_bytes = 0U;
  std::uint64_t total_provenance = 0U;
  result.resources.reserve(visible_resources.size());
  for (auto &[unused_id, resource] : visible_resources) {
    total_payload_bytes = checked_add(
        total_payload_bytes, resource.payload.size(), FormatKind::level_package,
        "Resolved LevelPackageV1 payload bytes");
    if (total_payload_bytes > limits.max_total_payload_bytes) {
      fail(FormatKind::level_package,
           "Resolved LevelPackageV1 payloads exceed their aggregate limit");
    }
    total_provenance = checked_add(total_provenance, resource.provenance.size(),
                                   FormatKind::level_package,
                                   "Resolved LevelPackageV1 provenance count");
    if (total_provenance > limits.max_total_provenance_records) {
      fail(FormatKind::level_package,
           "Resolved LevelPackageV1 provenance exceeds its aggregate limit");
    }
    result.resources.push_back(std::move(resource));
  }
  return result;
}

} // namespace openrc
