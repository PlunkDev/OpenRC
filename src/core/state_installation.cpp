#include "openrc/state_installation.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace openrc {
namespace {

constexpr std::array<std::byte, 8U> kMagic{
    std::byte{'O'}, std::byte{'R'}, std::byte{'S'}, std::byte{'T'},
    std::byte{'I'}, std::byte{'N'}, std::byte{'0'}, std::byte{'1'}};
constexpr std::uint64_t kHeaderBytes = 56U;
constexpr std::uint64_t kBodyPrefixBytes = 48U;
constexpr std::uint64_t kWritePrefixBytes = 24U;

[[noreturn]] void fail(const char *message) {
  throw StateInstallationError(message);
}

void validate_limits(const StateInstallationLimitsV1 limits) {
  if (!limits.max_bytes || !limits.max_writes || !limits.max_key_bytes)
    fail("State installation limits must be positive");
}

void validate_key(const std::string_view key,
                  const StateInstallationLimitsV1 limits) {
  if (key.empty() || key.size() > limits.max_key_bytes ||
      std::ranges::any_of(key, [](unsigned char c) {
        return c < 0x21U || c > 0x7eU;
      }))
    fail("State installation key is empty, invalid or exceeds its limit");
}

void validate_value(const SessionStateValueTypeV1 type,
                    const std::uint32_t value) {
  switch (type) {
  case SessionStateValueTypeV1::u8:
    if (value > 0xffU) fail("State installation value exceeds its u8 type");
    return;
  case SessionStateValueTypeV1::u16:
    if (value > 0xffffU) fail("State installation value exceeds its u16 type");
    return;
  case SessionStateValueTypeV1::u32:
    return;
  }
  fail("State installation has an unknown value type");
}

void add_size(std::uint64_t &size, const std::uint64_t bytes,
              const std::uint64_t limit) {
  if (size > limit || bytes > limit - size)
    fail("State installation exceeds its byte limit");
  size += bytes;
}

std::uint64_t validate_shape(const StateInstallationV1 &installation,
                             const StateInstallationLimitsV1 limits) {
  validate_limits(limits);
  if (installation.schema_version != kStateInstallationSchemaVersionV1 ||
      is_zero_prepared_digest_v1(installation.state_schema_sha256))
    fail("State installation version or schema digest is invalid");
  if (installation.writes.empty() ||
      installation.writes.size() > limits.max_writes)
    fail("State installation write count is empty or exceeds its limit");
  std::uint64_t size = 0U;
  add_size(size, kHeaderBytes + kBodyPrefixBytes, limits.max_bytes);
  for (const auto &write : installation.writes) {
    validate_key(write.view_key, limits);
    validate_value(write.value_type, write.value_bits);
    add_size(size, kWritePrefixBytes, limits.max_bytes);
    add_size(size, write.view_key.size(), limits.max_bytes);
  }
  if (size > std::vector<std::byte>{}.max_size())
    fail("State installation exceeds addressable encoding storage");
  return size;
}

void put(std::vector<std::byte> &out, std::uint64_t value,
         const unsigned width = 4U) {
  for (unsigned i = 0; i < width; ++i) {
    out.push_back(static_cast<std::byte>(value & 0xffU));
    value >>= 8U;
  }
}

struct Reader {
  std::span<const std::byte> bytes;
  std::size_t at = 0U;

  std::span<const std::byte> take(const std::size_t count) {
    if (count > bytes.size() - at) fail("Truncated state installation");
    const auto part = bytes.subspan(at, count);
    at += count;
    return part;
  }
  std::uint64_t get(const unsigned width = 4U) {
    const auto part = take(width);
    std::uint64_t value = 0U;
    for (unsigned i = 0; i < width; ++i)
      value |= std::uint64_t(std::to_integer<unsigned>(part[i])) << (8U * i);
    return value;
  }
};

struct WriteView {
  std::string_view key;
  std::uint64_t index;
  SessionStateValueTypeV1 type;
  std::uint32_t value;
};

WriteView read_write(Reader &reader, const StateInstallationLimitsV1 limits) {
  const auto key_bytes = reader.get();
  if (!key_bytes || key_bytes > limits.max_key_bytes)
    fail("State installation key length exceeds its limit");
  const auto type = static_cast<SessionStateValueTypeV1>(reader.get());
  const auto index = reader.get(8U);
  const auto value = static_cast<std::uint32_t>(reader.get());
  if (reader.get() != 0U) fail("State installation write reserved bits are set");
  validate_value(type, value);
  const auto bytes = reader.take(static_cast<std::size_t>(key_bytes));
  const std::string_view key(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  validate_key(key, limits);
  return {key, index, type, value};
}

} // namespace

void validate_state_installation_v1(const StateInstallationV1 &installation,
                                   const StateInstallationLimitsV1 limits) {
  static_cast<void>(validate_shape(installation, limits));
}

std::vector<std::byte>
encode_state_installation_v1(const StateInstallationV1 &installation,
                             const StateInstallationLimitsV1 limits) {
  const auto size = validate_shape(installation, limits);
  std::vector<std::byte> out;
  out.reserve(static_cast<std::size_t>(size));
  out.insert(out.end(), kMagic.begin(), kMagic.end());
  put(out, installation.schema_version);
  put(out, 0U);
  put(out, size - kHeaderBytes, 8U);
  out.resize(static_cast<std::size_t>(kHeaderBytes), std::byte{});
  put(out, installation.level_id);
  put(out, 0U);
  out.insert(out.end(), installation.state_schema_sha256.begin(),
             installation.state_schema_sha256.end());
  put(out, installation.writes.size(), 8U);
  for (const auto &write : installation.writes) {
    put(out, write.view_key.size());
    put(out, static_cast<std::uint32_t>(write.value_type));
    put(out, write.element_index, 8U);
    put(out, write.value_bits);
    put(out, 0U);
    for (const unsigned char c : write.view_key)
      out.push_back(static_cast<std::byte>(c));
  }
  const auto digest = prepared_content_sha256_v1(
      std::span<const std::byte>(out).subspan(kHeaderBytes));
  std::copy(digest.begin(), digest.end(), out.begin() + 24U);
  return out;
}

StateInstallationV1
decode_state_installation_v1(const std::span<const std::byte> bytes,
                             const StateInstallationLimitsV1 limits) {
  validate_limits(limits);
  if (bytes.size() > limits.max_bytes ||
      bytes.size() < kHeaderBytes + kBodyPrefixBytes)
    fail("State installation input length exceeds its bounds");
  Reader reader{bytes};
  const auto magic = reader.take(kMagic.size());
  if (!std::equal(magic.begin(), magic.end(), kMagic.begin()) ||
      reader.get() != kStateInstallationSchemaVersionV1 || reader.get() != 0U)
    fail("State installation header is invalid");
  if (reader.get(8U) != bytes.size() - kHeaderBytes)
    fail("State installation body length differs from the input");
  const auto encoded_digest = reader.take(32U);
  const auto digest = prepared_content_sha256_v1(bytes.subspan(kHeaderBytes));
  if (!std::equal(encoded_digest.begin(), encoded_digest.end(), digest.begin()))
    fail("State installation body digest differs");
  StateInstallationV1 result;
  result.level_id = static_cast<std::uint32_t>(reader.get());
  if (reader.get() != 0U) fail("State installation body reserved bits are set");
  const auto schema = reader.take(result.state_schema_sha256.size());
  std::copy(schema.begin(), schema.end(), result.state_schema_sha256.begin());
  if (is_zero_prepared_digest_v1(result.state_schema_sha256))
    fail("State installation has no schema digest");
  const auto count = reader.get(8U);
  if (!count || count > limits.max_writes || count > result.writes.max_size() ||
      count > (bytes.size() - reader.at) / (kWritePrefixBytes + 1U))
    fail("State installation write count exceeds its limit or input length");
  // Validate every record and exact EOF before allocating keys or records.
  const auto records_begin = reader.at;
  for (std::uint64_t i = 0; i < count; ++i)
    static_cast<void>(read_write(reader, limits));
  if (reader.at != bytes.size()) fail("Trailing state installation bytes");
  reader.at = records_begin;
  result.writes.reserve(static_cast<std::size_t>(count));
  for (std::uint64_t i = 0; i < count; ++i) {
    const auto write = read_write(reader, limits);
    result.writes.push_back({std::string(write.key), write.index, write.type, write.value});
  }
  return result;
}

void execute_state_installation_v1(const StateInstallationV1 &installation,
                                  const std::uint32_t expected_level,
                                  game::SessionStateV1 &state,
                                  const std::uint64_t expected_revision,
                                  const StateInstallationLimitsV1 limits) {
  validate_state_installation_v1(installation, limits);
  if (installation.level_id != expected_level)
    fail("State installation level differs from the requested level");
  if (installation.state_schema_sha256 != state.schema_sha256())
    fail("State installation schema differs from the live session");
  try {
    state.apply_batch(installation.writes, expected_revision);
  } catch (const SessionStateError &error) {
    throw StateInstallationError("Cannot install session state: " + std::string(error.what()));
  }
}

} // namespace openrc
