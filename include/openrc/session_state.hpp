#pragma once

#include "openrc/prepared_game_v2.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kSessionStateSchemaVersionV1 = 1U;

enum class SessionStateValueTypeV1 : std::uint32_t {
  u8 = 0U,
  u16 = 1U,
  u32 = 2U,
};

struct SessionStateBufferV1 {
  std::string key;
  std::uint64_t byte_count = 0U;
  [[nodiscard]] bool operator==(const SessionStateBufferV1 &) const = default;
};

// Little-endian unsigned views share their buffer's canonical bytes. Views
// may overlap, be unaligned, and have a stride smaller than their width.
// Keys are bounded opaque non-whitespace printable ASCII; no source semantics.
struct SessionStateViewV1 {
  std::string key;
  std::string buffer_key;
  SessionStateValueTypeV1 value_type = SessionStateValueTypeV1::u8;
  std::uint64_t byte_offset = 0U;
  std::uint64_t element_count = 0U;
  std::uint64_t byte_stride = 0U;
  [[nodiscard]] bool operator==(const SessionStateViewV1 &) const = default;
};

struct SessionStateSchemaV1 {
  std::string identity_key;
  std::vector<SessionStateBufferV1> buffers;
  std::vector<SessionStateViewV1> views;
  [[nodiscard]] bool operator==(const SessionStateSchemaV1 &) const = default;
};

struct SessionStateBufferBytesV1 {
  std::string buffer_key;
  std::vector<std::byte> bytes;
  [[nodiscard]] bool
  operator==(const SessionStateBufferBytesV1 &) const = default;
};

// Exactly one complete image for every declared buffer, including bytes not
// covered by any view. Absence/short images never imply zero initialization.
struct SessionStateInitialV1 {
  SessionStateSchemaV1 schema;
  std::vector<SessionStateBufferBytesV1> buffers;
  [[nodiscard]] bool operator==(const SessionStateInitialV1 &) const = default;
};

// All limits are explicit, positive and checked before internal copies.
struct SessionStateLimitsV1 {
  std::uint32_t max_buffers = 0U;
  std::uint32_t max_views = 0U;
  std::uint32_t max_key_bytes = 0U;
  // Schema identity/declaration/reference keys plus each image label when
  // initial or snapshot images are supplied; repeated labels count again.
  std::uint64_t max_total_key_bytes = 0U;
  std::uint64_t max_buffer_bytes = 0U;
  std::uint64_t max_total_buffer_bytes = 0U;
  std::uint64_t max_view_elements = 0U;
  std::uint64_t max_total_view_elements = 0U;
  std::uint64_t max_batch_writes = 0U;
};

class SessionStateError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Canonical order is ascending exact key bytes for buffers/views/images;
// caller input is never mutated. Duplicates are errors, not merged.
[[nodiscard]] SessionStateSchemaV1
canonicalize_session_state_schema_v1(const SessionStateSchemaV1 &schema,
                                     const SessionStateLimitsV1 &limits);
[[nodiscard]] SessionStateInitialV1
canonicalize_session_state_initial_v1(const SessionStateInitialV1 &initial,
                                      const SessionStateLimitsV1 &limits);
[[nodiscard]] PreparedContentDigestV1
hash_session_state_schema_v1(const SessionStateSchemaV1 &schema,
                             const SessionStateLimitsV1 &limits);
[[nodiscard]] PreparedContentDigestV1
hash_session_state_initial_v1(const SessionStateInitialV1 &initial,
                              const SessionStateLimitsV1 &limits);

namespace game {

struct SessionStateSnapshotV1 {
  std::string identity_key;
  PreparedContentDigestV1 schema_sha256{};
  std::uint64_t revision = 0U;
  // Strict canonical order and complete schema-matching images required.
  std::vector<SessionStateBufferBytesV1> buffers;
  [[nodiscard]] bool operator==(const SessionStateSnapshotV1 &) const = default;
};

struct SessionStateWriteV1 {
  std::string view_key;
  std::uint64_t element_index = 0U;
  SessionStateValueTypeV1 value_type = SessionStateValueTypeV1::u8;
  // Type must match the view, and unused high bits must be zero.
  std::uint32_t value_bits = 0U;
};

class SessionStateV1 final {
public:
  explicit SessionStateV1(const SessionStateInitialV1 &initial,
                          const SessionStateLimitsV1 &limits);
  // Restore requires an externally trusted schema, never infers it from
  // snapshot bytes, and never replays initial values over restored state.
  SessionStateV1(const SessionStateSchemaV1 &schema,
                 const SessionStateSnapshotV1 &snapshot,
                 const SessionStateLimitsV1 &limits);

  [[nodiscard]] const SessionStateSchemaV1 &schema() const noexcept;
  [[nodiscard]] const PreparedContentDigestV1 &schema_sha256() const noexcept;
  [[nodiscard]] std::uint64_t revision() const noexcept;
  [[nodiscard]] std::uint8_t read_u8(std::string_view view_key,
                                     std::uint64_t element_index) const;
  [[nodiscard]] std::uint16_t read_u16(std::string_view view_key,
                                       std::uint64_t element_index) const;
  [[nodiscard]] std::uint32_t read_u32(std::string_view view_key,
                                       std::uint64_t element_index) const;
  // Read-only canonical storage; never a separate decoded view cache.
  [[nodiscard]] std::span<const std::byte>
  buffer_bytes(std::string_view buffer_key) const;

  // Validate the entire batch before committing, then apply in caller order.
  // Overlapping writes are last-write-wins; subsequent reads use the same
  // bytes. A successful nonempty batch increments revision once. Empty
  // batches still check expected_revision but leave bytes/revision unchanged.
  // Values are explicit; this API provides no callback, VM or implicit RMW.
  void apply_batch(std::span<const SessionStateWriteV1> writes,
                   std::uint64_t expected_revision);
  [[nodiscard]] SessionStateSnapshotV1 snapshot() const;
  void restore_snapshot(const SessionStateSnapshotV1 &snapshot);
  [[nodiscard]] PreparedContentDigestV1 state_sha256() const;

private:
  struct ResolvedViewV1 {
    std::size_t buffer_index = 0U;
    std::uint32_t width = 0U;
  };
  void bind_views();
  [[nodiscard]] std::size_t find_view(std::string_view key) const;
  [[nodiscard]] std::uint32_t read_value(std::string_view key,
                                         std::uint64_t element_index,
                                         SessionStateValueTypeV1 type) const;

  SessionStateSchemaV1 schema_;
  SessionStateLimitsV1 limits_;
  PreparedContentDigestV1 schema_sha256_{};
  std::vector<SessionStateBufferBytesV1> buffers_;
  std::vector<ResolvedViewV1> resolved_views_;
  std::uint64_t revision_ = 0U;
};

} // namespace game
} // namespace openrc
