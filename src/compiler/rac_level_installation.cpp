#include "openrc/rac_level_installation.hpp"

#include "openrc/hash.hpp"
#include "openrc/state_installation.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>

namespace openrc {
namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

std::uint32_t word(std::span<const std::byte> bytes, std::size_t offset) {
  require(offset <= bytes.size() && bytes.size() - offset >= 4U,
          "Initial level installation source word leaves its owner");
  std::uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i)
    value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
  return value;
}

void qualify(std::span<const std::byte> bytes, const char *expected) {
  require(hex_digest(prepared_content_sha256_v1(bytes)) == expected,
          "Initial level installation source revision is not qualified");
}

std::vector<std::byte> read_range(const std::filesystem::path &image,
                                std::uint64_t offset, std::uint64_t count) {
  const auto image_bytes = std::filesystem::file_size(image);
  require(count != 0U && count <= 2U * 1024U * 1024U &&
              offset <= image_bytes && count <= image_bytes - offset,
          "Initial level installation ISO range leaves its owner");
  std::vector<std::byte> bytes(static_cast<std::size_t>(count));
  std::ifstream input(image, std::ios::binary);
  input.seekg(static_cast<std::streamoff>(offset));
  require(static_cast<bool>(input.read(reinterpret_cast<char *>(bytes.data()),
                                      static_cast<std::streamsize>(bytes.size()))),
          "Initial level installation ISO read failed");
  return bytes;
}

struct Region {
  std::uint32_t address;
  std::uint32_t bytes;
  const char *owner;
};

// These are Veldin phase bindings, not intersections with old boot addresses.
// 290188 writes target15f6a4, request15f6bc and exit15f650. Query1efff0
// and prepare2940e0 share control173f50 (base173f40+10).
constexpr std::array kRegions{
    Region{0x15f650U, 4U, "session/transition-requested"},
    Region{0x15f6a4U, 4U, "session/target-level"},
    Region{0x15f6bcU, 4U, "session/level-change-requested"},
    Region{0x15fd47U, 17U, "rac1.level/selector-cache"},
    Region{0x173f50U, 4U, "collision/query-flags"},
    Region{0x1ba860U, 256U, "rac1.level/alternate-bits"},
    Region{0x1bb5c0U, 3168U, "rac1.level/saved-state"},
};

void require_view(const SessionStateSchemaV1 &schema, const std::string &key,
                  const std::string &owner, SessionStateValueTypeV1 type,
                  std::uint64_t offset, std::uint64_t count,
                  std::uint64_t stride) {
  const auto view = std::find_if(schema.views.begin(), schema.views.end(),
                                [&](const auto &v) { return v.key == key; });
  require(view != schema.views.end() && view->buffer_key == owner &&
              view->value_type == type && view->byte_offset == offset &&
              view->element_count == count && view->byte_stride == stride,
          "Initial level installation canonical view binding differs");
}

} // namespace

LevelPackageResourceV1 compile_rac_initial_level_installation_v1(
    const std::filesystem::path &image, std::span<const std::byte> boot,
    const SessionStateSchemaV1 &schema, SessionStateLimitsV1 limits) {
  require(!boot.empty() && boot.size() <= 32U * 1024U * 1024U,
          "Initial level installation boot source exceeds its bound");
  qualify(boot, "17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b");
  const auto canonical = canonicalize_session_state_schema_v1(schema, limits);
  for (const auto &region : kRegions) {
    const auto buffer = std::find_if(canonical.buffers.begin(), canonical.buffers.end(),
                                    [&](const auto &b) { return b.key == region.owner; });
    require(buffer != canonical.buffers.end() && buffer->byte_count == region.bytes,
            "Initial level installation canonical owner differs");
    require_view(canonical, std::string(region.owner) + "/bytes", region.owner,
                 SessionStateValueTypeV1::u8, 0, region.bytes, 1);
  }
  require_view(canonical, "rac1.level/selector-cache/selectors", "rac1.level/selector-cache",
               SessionStateValueTypeV1::u8, 1, 16, 1);
  require_view(canonical, "rac1.level/alternate-bits/words", "rac1.level/alternate-bits",
               SessionStateValueTypeV1::u32, 0, 64, 4);
  require_view(canonical, "rac1.level/saved-state/suppression", "rac1.level/saved-state",
               SessionStateValueTypeV1::u8, 0x454U, 2047, 1);

  constexpr std::uint64_t sector_bytes = 2048U;
  const auto toc = read_range(image, 1500U * sector_bytes, 0x2960U);
  qualify(toc, "10b5950d0c5c4271f40640f5ae1ce7750bfcdc942459814a6f2865be7ba252c4");
  const auto local_lba = word(toc, 0x28c8U);
  require(local_lba == 1886014U && word(toc, 0x28ccU) == 0x4c0fU,
          "Initial level installation TOC selection differs");
  const auto local = read_range(image, std::uint64_t{local_lba} * sector_bytes, 0x28U);
  const auto primary_lba = word(local, 8U), primary_sectors = word(local, 12U);
  require(word(local, 0) == 0U && word(local, 4) == 0x2434U &&
              primary_lba == 1886019U && primary_sectors == 6569U,
          "Initial level installation primary extent differs");
  constexpr std::size_t header_bytes = 0x80U;
  constexpr std::size_t overlay_bytes = 0x1930e4U;
  constexpr std::size_t prefix_bytes = header_bytes + overlay_bytes + 16U;
  require(prefix_bytes <= std::uint64_t{primary_sectors} * sector_bytes,
          "Initial level installation prefix leaves its primary extent");
  const auto source_offset = std::uint64_t{primary_lba} * sector_bytes;
  const auto source = read_range(image, source_offset, prefix_bytes);
  const auto source_span = std::span<const std::byte>(source);
  qualify(source_span, "57782bad75d255647d7977766f67b8273c790a5f33329cb51b0c9093ebca1599");
  qualify(source_span.first(header_bytes),
          "725950527a3d0c64999f0f6affee77a9a7505d4d8193a080cafe7babc7622449");
  require(word(source_span, 0) == header_bytes && word(source_span, 4) == overlay_bytes,
          "Initial level installation section stream descriptor differs");
  const auto overlay = source_span.subspan(header_bytes, overlay_bytes);
  qualify(overlay, "922fd06e1ac6c717cc7832a3b8ac7be5aaf178ebe1b7982ae4cd294af1876c79");
  require(std::all_of(source_span.last(16U).begin(), source_span.last(16U).end(),
                      [](std::byte value) { return value == std::byte{0}; }),
          "Initial level installation actual terminating header differs");

  struct Section { std::uint32_t destination, bytes; };
  constexpr std::array sections{
      Section{0x15f000U, 0x2ec0U}, Section{0x161f00U, 0x41b0U},
      Section{0x166100U, 0x84530U}, Section{0x1ea680U, 0x504U},
      Section{0x1eac00U, 0xa0U}, Section{0x1ead00U, 0x18U},
      Section{0x1ead80U, 0x107518U}};
  StateInstallationV1 installation;
  installation.level_id = 0;
  installation.state_schema_sha256 = hash_session_state_schema_v1(canonical, limits);
  installation.writes.reserve(3457U);
  std::array<bool, kRegions.size()> emitted{};
  std::size_t cursor = 0;
  std::uint64_t copied_bytes = 0;
  for (const auto &expected : sections) {
    const auto destination = word(overlay, cursor), count = word(overlay, cursor + 4U);
    require(destination == expected.destination && count == expected.bytes &&
                word(overlay, cursor + 12U) == 0x2465f8U &&
                cursor <= overlay.size() && overlay.size() - cursor >= 16U &&
                count <= overlay.size() - cursor - 16U,
            "Initial level installation section record differs");
    // Original12da38 does not read header+8 or dispatch on section type.
    // Its complete payload includes the explicit type8 zero bytes.
    const auto payload = overlay.subspan(cursor + 16U, count);
    for (std::size_t r = 0; r < kRegions.size(); ++r) {
      const auto &region = kRegions[r];
      if (region.address < destination ||
          std::uint64_t{region.address} + region.bytes > std::uint64_t{destination} + count)
        continue;
      require(!emitted[r], "Initial level installation owner has duplicate source sections");
      const auto values = payload.subspan(region.address - destination, region.bytes);
      for (std::uint32_t i = 0; i < region.bytes; ++i)
        installation.writes.push_back({std::string(region.owner) + "/bytes", i,
            SessionStateValueTypeV1::u8, std::to_integer<std::uint32_t>(values[i])});
      emitted[r] = true;
    }
    copied_bytes += count;
    cursor += 16U + count;
  }
  require(cursor == overlay.size() && copied_bytes == 1650804U &&
              installation.writes.size() == 3457U &&
              std::all_of(emitted.begin(), emitted.end(), [](bool v) { return v; }),
          "Initial level installation projection is incomplete");
  // The saved frontend display selector is retired: physical16044c is now
  // padding after an overlay format string. Old menu pointers also change
  // owners. Neither is copied into its old semantic view. Persistent rows
  // remain unchanged, and cached selectors receive file defaults here, not
  // the later244ae0(1,0) row copy. No entry BSS or late2860d8 clear is hoisted.
  LevelPackageResourceV1 result;
  result.resource_id = "new-game/level-installation";
  result.type_id = "openrc.state-installation";
  result.schema_version = 1U;
  result.provenance = {
      {LevelPackageProvenanceKindV1::iso_range, "rac1/initial-level-overlay",
       source_offset, source.size(), prepared_content_sha256_v1(source)},
      {LevelPackageProvenanceKindV1::generated,
       "compiler/rac-initial-level-installation-v1", 0, 0, {}}};
  result.payload = encode_state_installation_v1(installation);
  result.payload_sha256 = prepared_content_sha256_v1(result.payload);
  return result;
}

} // namespace openrc
