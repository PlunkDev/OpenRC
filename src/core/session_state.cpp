#include "openrc/session_state.hpp"

#include "openrc/hash.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw SessionStateError("SessionStateV1 " + message);
}

void validate_limits(const SessionStateLimitsV1 &limits) {
  if (limits.max_buffers == 0U || limits.max_views == 0U ||
      limits.max_key_bytes == 0U || limits.max_total_key_bytes == 0U ||
      limits.max_buffer_bytes == 0U || limits.max_total_buffer_bytes == 0U ||
      limits.max_view_elements == 0U || limits.max_total_view_elements == 0U ||
      limits.max_batch_writes == 0U) {
    fail("limits must all be positive");
  }
}

void accumulate(std::uint64_t &total, const std::uint64_t count,
                const std::uint64_t maximum, const char *description) {
  if (total > maximum || count > maximum - total) {
    fail(std::string(description) + " exceeds the aggregate limit");
  }
  total += count;
}

void validate_key(const std::string_view key,
                  const SessionStateLimitsV1 &limits,
                  std::uint64_t &key_bytes) {
  if (key.empty() || key.size() > limits.max_key_bytes) {
    fail("key is empty or exceeds its byte limit");
  }
  for (const char character : key) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x21U || byte > 0x7eU) {
      fail("key must contain only non-whitespace printable ASCII bytes");
    }
  }
  accumulate(key_bytes, key.size(), limits.max_total_key_bytes, "key bytes");
}

[[nodiscard]] std::uint32_t value_width(const SessionStateValueTypeV1 type) {
  switch (type) {
  case SessionStateValueTypeV1::u8:
    return 1U;
  case SessionStateValueTypeV1::u16:
    return 2U;
  case SessionStateValueTypeV1::u32:
    return 4U;
  }
  fail("view has an unknown unsigned value type");
}

[[nodiscard]] std::uint64_t
preflight_schema(const SessionStateSchemaV1 &schema,
                 const SessionStateLimitsV1 &limits) {
  validate_limits(limits);
  if (schema.buffers.empty() || schema.buffers.size() > limits.max_buffers ||
      schema.views.size() > limits.max_views) {
    fail("buffer/view count is empty or exceeds its limit");
  }
  std::uint64_t key_bytes = 0U;
  validate_key(schema.identity_key, limits, key_bytes);
  std::uint64_t buffer_bytes = 0U;
  for (const auto &buffer : schema.buffers) {
    validate_key(buffer.key, limits, key_bytes);
    if (buffer.byte_count == 0U ||
        buffer.byte_count > limits.max_buffer_bytes ||
        buffer.byte_count > static_cast<std::uint64_t>(
                                std::numeric_limits<std::ptrdiff_t>::max())) {
      fail("buffer shape is empty or exceeds its byte limit");
    }
    accumulate(buffer_bytes, buffer.byte_count, limits.max_total_buffer_bytes,
               "buffer bytes");
  }
  std::uint64_t elements = 0U;
  for (const auto &view : schema.views) {
    validate_key(view.key, limits, key_bytes);
    validate_key(view.buffer_key, limits, key_bytes);
    static_cast<void>(value_width(view.value_type));
    if (view.element_count == 0U ||
        view.element_count > limits.max_view_elements ||
        view.byte_stride == 0U) {
      fail("view shape has zero count/stride or exceeds its element limit");
    }
    accumulate(elements, view.element_count, limits.max_total_view_elements,
               "view elements");
  }

  // Count, shape and aggregate preflight above precede metadata containers
  // and every copy of caller-owned strings or payload bytes.
  std::map<std::string_view, std::uint64_t, std::less<>> buffers;
  for (const auto &buffer : schema.buffers) {
    if (!buffers.emplace(buffer.key, buffer.byte_count).second) {
      fail("schema has a duplicate buffer key");
    }
  }
  std::set<std::string_view, std::less<>> views;
  for (const auto &view : schema.views) {
    if (!views.insert(view.key).second) {
      fail("schema has a duplicate view key");
    }
    const auto buffer = buffers.find(view.buffer_key);
    if (buffer == buffers.end()) {
      fail("view references an unknown buffer key");
    }
    const auto bytes = buffer->second;
    const auto width = value_width(view.value_type);
    if (view.byte_offset > bytes || width > bytes - view.byte_offset ||
        view.element_count - 1U >
            (bytes - view.byte_offset - width) / view.byte_stride) {
      fail("view extent leaves its canonical buffer or overflows");
    }
  }
  return key_bytes;
}

void preflight_images(const SessionStateSchemaV1 &schema,
                      const std::vector<SessionStateBufferBytesV1> &images,
                      const SessionStateLimitsV1 &limits,
                      std::uint64_t schema_key_bytes,
                      const bool require_canonical) {
  if (images.size() != schema.buffers.size()) {
    fail("images must contain exactly one complete image per buffer");
  }
  std::uint64_t payload_bytes = 0U;
  std::string_view previous;
  for (const auto &image : images) {
    validate_key(image.buffer_key, limits, schema_key_bytes);
    if (image.bytes.size() > limits.max_buffer_bytes) {
      fail("image exceeds its buffer byte limit");
    }
    accumulate(payload_bytes, image.bytes.size(), limits.max_total_buffer_bytes,
               "image bytes");
    if (require_canonical && !previous.empty() &&
        previous >= image.buffer_key) {
      fail("snapshot images are not in strict canonical order");
    }
    previous = image.buffer_key;
  }
  std::map<std::string_view, std::uint64_t, std::less<>> sizes;
  for (const auto &image : images) {
    if (!sizes.emplace(image.buffer_key, image.bytes.size()).second) {
      fail("images have a duplicate buffer key");
    }
  }
  for (const auto &buffer : schema.buffers) {
    const auto image = sizes.find(buffer.key);
    if (image == sizes.end()) {
      fail("images have a missing or unknown buffer key");
    }
    if (image->second != buffer.byte_count) {
      fail("image size must exactly match its buffer shape; no implicit fill");
    }
  }
}

class DigestWriter final {
public:
  void u32(const std::uint32_t value) {
    std::array<std::byte, 4U> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
      bytes[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
    hash_.update(bytes);
  }
  void u64(const std::uint64_t value) {
    std::array<std::byte, 8U> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
      bytes[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
    hash_.update(bytes);
  }
  void raw(const std::span<const std::byte> bytes) { hash_.update(bytes); }
  void key(const std::string_view value) {
    u64(value.size());
    raw(std::as_bytes(std::span(value.data(), value.size())));
  }
  [[nodiscard]] PreparedContentDigestV1 finish() { return hash_.finish(); }

private:
  Sha256 hash_;
};

[[nodiscard]] PreparedContentDigestV1
hash_schema_canonical(const SessionStateSchemaV1 &schema) {
  DigestWriter writer;
  writer.key("openrc.session-state.schema.v1");
  writer.u32(kSessionStateSchemaVersionV1);
  writer.key(schema.identity_key);
  writer.u64(schema.buffers.size());
  for (const auto &buffer : schema.buffers) {
    writer.key(buffer.key);
    writer.u64(buffer.byte_count);
  }
  writer.u64(schema.views.size());
  for (const auto &view : schema.views) {
    writer.key(view.key);
    writer.key(view.buffer_key);
    writer.u32(static_cast<std::uint32_t>(view.value_type));
    writer.u64(view.byte_offset);
    writer.u64(view.element_count);
    writer.u64(view.byte_stride);
  }
  return writer.finish();
}

void hash_images(DigestWriter &writer,
                 const std::vector<SessionStateBufferBytesV1> &images) {
  writer.u64(images.size());
  for (const auto &image : images) {
    writer.key(image.buffer_key);
    writer.u64(image.bytes.size());
    writer.raw(image.bytes);
  }
}

void sort_schema(SessionStateSchemaV1 &schema) {
  std::ranges::sort(schema.buffers, {}, &SessionStateBufferV1::key);
  std::ranges::sort(schema.views, {}, &SessionStateViewV1::key);
}

} // namespace

SessionStateSchemaV1
canonicalize_session_state_schema_v1(const SessionStateSchemaV1 &schema,
                                     const SessionStateLimitsV1 &limits) {
  static_cast<void>(preflight_schema(schema, limits));
  auto result = schema;
  sort_schema(result);
  return result;
}

SessionStateInitialV1
canonicalize_session_state_initial_v1(const SessionStateInitialV1 &initial,
                                      const SessionStateLimitsV1 &limits) {
  const auto key_bytes = preflight_schema(initial.schema, limits);
  preflight_images(initial.schema, initial.buffers, limits, key_bytes, false);
  auto result = initial;
  sort_schema(result.schema);
  std::ranges::sort(result.buffers, {}, &SessionStateBufferBytesV1::buffer_key);
  return result;
}

PreparedContentDigestV1
hash_session_state_schema_v1(const SessionStateSchemaV1 &schema,
                             const SessionStateLimitsV1 &limits) {
  return hash_schema_canonical(
      canonicalize_session_state_schema_v1(schema, limits));
}

PreparedContentDigestV1
hash_session_state_initial_v1(const SessionStateInitialV1 &initial,
                              const SessionStateLimitsV1 &limits) {
  const auto canonical = canonicalize_session_state_initial_v1(initial, limits);
  DigestWriter writer;
  writer.key("openrc.session-state.initial.v1");
  writer.raw(hash_schema_canonical(canonical.schema));
  hash_images(writer, canonical.buffers);
  return writer.finish();
}

namespace game {

SessionStateV1::SessionStateV1(const SessionStateInitialV1 &initial,
                               const SessionStateLimitsV1 &limits)
    : limits_(limits) {
  auto canonical = canonicalize_session_state_initial_v1(initial, limits);
  schema_ = std::move(canonical.schema);
  buffers_ = std::move(canonical.buffers);
  schema_sha256_ = hash_schema_canonical(schema_);
  bind_views();
}

SessionStateV1::SessionStateV1(const SessionStateSchemaV1 &schema,
                               const SessionStateSnapshotV1 &snapshot,
                               const SessionStateLimitsV1 &limits)
    : schema_(canonicalize_session_state_schema_v1(schema, limits)),
      limits_(limits), schema_sha256_(hash_schema_canonical(schema_)) {
  bind_views();
  restore_snapshot(snapshot);
}

void SessionStateV1::bind_views() {
  resolved_views_.reserve(schema_.views.size());
  for (const auto &view : schema_.views) {
    const auto buffer = std::lower_bound(
        schema_.buffers.begin(), schema_.buffers.end(), view.buffer_key,
        [](const SessionStateBufferV1 &candidate, const std::string_view key) {
          return candidate.key < key;
        });
    resolved_views_.push_back(
        {static_cast<std::size_t>(buffer - schema_.buffers.begin()),
         value_width(view.value_type)});
  }
}

const SessionStateSchemaV1 &SessionStateV1::schema() const noexcept {
  return schema_;
}
const PreparedContentDigestV1 &SessionStateV1::schema_sha256() const noexcept {
  return schema_sha256_;
}
std::uint64_t SessionStateV1::revision() const noexcept { return revision_; }

std::size_t SessionStateV1::find_view(const std::string_view key) const {
  if (key.size() > limits_.max_key_bytes) {
    fail("view lookup key exceeds its limit");
  }
  const auto found = std::lower_bound(
      schema_.views.begin(), schema_.views.end(), key,
      [](const SessionStateViewV1 &view, const std::string_view wanted) {
        return view.key < wanted;
      });
  if (found == schema_.views.end() || found->key != key) {
    fail("unknown view key");
  }
  return static_cast<std::size_t>(found - schema_.views.begin());
}

std::uint32_t
SessionStateV1::read_value(const std::string_view key,
                           const std::uint64_t element_index,
                           const SessionStateValueTypeV1 type) const {
  const auto index = find_view(key);
  const auto &view = schema_.views[index];
  const auto &resolved = resolved_views_[index];
  if (view.value_type != type) {
    fail("read type does not match its view");
  }
  if (element_index >= view.element_count) {
    fail("read element index is out of range");
  }
  const auto offset = static_cast<std::size_t>(
      view.byte_offset + element_index * view.byte_stride);
  const auto &bytes = buffers_[resolved.buffer_index].bytes;
  std::uint32_t value = 0U;
  for (std::uint32_t lane = 0U; lane < resolved.width; ++lane) {
    value |= std::to_integer<std::uint32_t>(bytes[offset + lane])
             << (lane * 8U);
  }
  return value;
}

std::uint8_t SessionStateV1::read_u8(const std::string_view key,
                                     const std::uint64_t index) const {
  return static_cast<std::uint8_t>(
      read_value(key, index, SessionStateValueTypeV1::u8));
}
std::uint16_t SessionStateV1::read_u16(const std::string_view key,
                                       const std::uint64_t index) const {
  return static_cast<std::uint16_t>(
      read_value(key, index, SessionStateValueTypeV1::u16));
}
std::uint32_t SessionStateV1::read_u32(const std::string_view key,
                                       const std::uint64_t index) const {
  return read_value(key, index, SessionStateValueTypeV1::u32);
}

std::span<const std::byte>
SessionStateV1::buffer_bytes(const std::string_view key) const {
  if (key.size() > limits_.max_key_bytes) {
    fail("buffer lookup key exceeds its limit");
  }
  const auto found = std::lower_bound(
      buffers_.begin(), buffers_.end(), key,
      [](const SessionStateBufferBytesV1 &buffer,
         const std::string_view wanted) { return buffer.buffer_key < wanted; });
  if (found == buffers_.end() || found->buffer_key != key) {
    fail("unknown buffer key");
  }
  return found->bytes;
}

void SessionStateV1::apply_batch(
    const std::span<const SessionStateWriteV1> writes,
    const std::uint64_t expected_revision) {
  if (expected_revision != revision_) {
    fail("write batch has a stale revision");
  }
  if (writes.size() > limits_.max_batch_writes) {
    fail("write batch exceeds its count limit");
  }
  if (writes.empty()) {
    return;
  }
  if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
    fail("write revision is exhausted");
  }
  struct PendingWrite {
    std::size_t buffer_index;
    std::size_t offset;
    std::uint32_t width;
    std::uint32_t value;
  };
  std::vector<PendingWrite> pending;
  pending.reserve(writes.size());
  for (const auto &write : writes) {
    const auto index = find_view(write.view_key);
    const auto &view = schema_.views[index];
    const auto &resolved = resolved_views_[index];
    if (write.value_type != view.value_type) {
      fail("write type does not match its view");
    }
    if (write.element_index >= view.element_count) {
      fail("write element index is out of range");
    }
    if (resolved.width < 4U &&
        (write.value_bits >> (resolved.width * 8U)) != 0U) {
      fail("write value has nonzero bits outside its unsigned type");
    }
    pending.push_back(
        {resolved.buffer_index,
         static_cast<std::size_t>(view.byte_offset +
                                  write.element_index * view.byte_stride),
         resolved.width, write.value_bits});
  }
  // The commit phase cannot allocate or throw. All aliases address these
  // same canonical bytes; caller order, not key order, resolves overlap.
  for (const auto &write : pending) {
    auto &bytes = buffers_[write.buffer_index].bytes;
    for (std::uint32_t lane = 0U; lane < write.width; ++lane) {
      bytes[write.offset + lane] =
          static_cast<std::byte>((write.value >> (lane * 8U)) & 0xffU);
    }
  }
  ++revision_;
}

SessionStateSnapshotV1 SessionStateV1::snapshot() const {
  return {schema_.identity_key, schema_sha256_, revision_, buffers_};
}

void SessionStateV1::restore_snapshot(const SessionStateSnapshotV1 &snapshot) {
  if (snapshot.identity_key != schema_.identity_key) {
    fail("snapshot identity does not match");
  }
  if (snapshot.schema_sha256 != schema_sha256_) {
    fail("snapshot schema digest does not match");
  }
  const auto key_bytes = preflight_schema(schema_, limits_);
  preflight_images(schema_, snapshot.buffers, limits_, key_bytes, true);
  auto staged = snapshot.buffers;
  buffers_.swap(staged);
  revision_ = snapshot.revision;
}

PreparedContentDigestV1 SessionStateV1::state_sha256() const {
  DigestWriter writer;
  writer.key("openrc.session-state.snapshot.v1");
  writer.key(schema_.identity_key);
  writer.raw(schema_sha256_);
  writer.u64(revision_);
  hash_images(writer, buffers_);
  return writer.finish();
}

} // namespace game
} // namespace openrc
