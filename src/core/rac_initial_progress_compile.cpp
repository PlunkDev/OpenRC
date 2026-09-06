#include "openrc/rac_initial_progress_compile.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint64_t kRowCount = kRacInitialProgressLevelRowsV1;
constexpr std::uint64_t kSelectorBytes = kRowCount * 16U;
constexpr std::uint64_t kPrimaryBytes = kRowCount * 256U;
constexpr std::uint64_t kRegistrationBase = kSelectorBytes + kPrimaryBytes;
constexpr std::uint64_t kRowStorageBytes = kRegistrationBase + kRowCount * 256U;

void append_le(std::vector<std::byte> &bytes, const std::uint32_t value,
               const unsigned width) {
  for (unsigned byte = 0U; byte < width; ++byte) {
    bytes.push_back(static_cast<std::byte>((value >> (byte * 8U)) & 0xffU));
  }
}

} // namespace

RacInitialProgressCompilationV1
compile_rac_initial_progress_v1(const RacInitialProgressTemplateV1 &source,
                                const SessionStateLimitsV1 &limits) {
  if (limits.max_buffers < 2U || limits.max_views < 1U + 5U * kRowCount ||
      limits.max_buffer_bytes < kRowStorageBytes ||
      limits.max_total_buffer_bytes < kRowStorageBytes + 4U) {
    throw SessionStateError(
        "RAC initial-progress compilation exceeds output shape limits");
  }
  RacInitialProgressCompilationV1 result;
  auto &schema = result.initial.schema;
  schema.identity_key = "rac1.progress/template-v1";
  const std::string level_buffer = "rac1.progress/encoded-level";
  const std::string rows_buffer = "rac1.progress/level-rows";
  schema.buffers = {{level_buffer, 4U}, {rows_buffer, kRowStorageBytes}};
  result.encoded_source_level = "rac1.progress/encoded-source-level";
  schema.views.push_back({result.encoded_source_level, level_buffer,
                          SessionStateValueTypeV1::u32, 0U, 1U, 4U});
  for (std::size_t row = 0U; row < source.rows.size(); ++row) {
    const auto prefix = "rac1.progress/level/" + std::to_string(row) + '/';
    auto &bindings = result.rows[row];
    bindings = {prefix + "selectors", prefix + "primary-words",
                prefix + "registration-keys", prefix + "registration-auxiliary",
                prefix + "registration-words"};
    schema.views.push_back({bindings.selector_bytes, rows_buffer,
                            SessionStateValueTypeV1::u8, row * 16U, 16U, 1U});
    schema.views.push_back({bindings.primary_bit_words, rows_buffer,
                            SessionStateValueTypeV1::u32,
                            kSelectorBytes + row * 256U, 64U, 4U});
    schema.views.push_back({bindings.registration_keys, rows_buffer,
                            SessionStateValueTypeV1::u16,
                            kRegistrationBase + row * 256U, 64U, 4U});
    schema.views.push_back({bindings.registration_auxiliary, rows_buffer,
                            SessionStateValueTypeV1::u16,
                            kRegistrationBase + row * 256U + 2U, 64U, 4U});
    schema.views.push_back({bindings.registration_words, rows_buffer,
                            SessionStateValueTypeV1::u32,
                            kRegistrationBase + row * 256U, 64U, 4U});
  }
  // Validate the complete typed shape before allocating its storage images.
  schema = canonicalize_session_state_schema_v1(schema, limits);
  SessionStateBufferBytesV1 level_bytes{level_buffer, {}};
  append_le(level_bytes.bytes,
            static_cast<std::uint32_t>(source.encoded_source_level), 4U);
  SessionStateBufferBytesV1 rows_bytes{rows_buffer, {}};
  rows_bytes.bytes.reserve(static_cast<std::size_t>(kRowStorageBytes));
  for (const auto &row : source.rows) {
    for (const auto byte : row.selector_bytes) {
      rows_bytes.bytes.push_back(static_cast<std::byte>(byte));
    }
  }
  for (const auto &row : source.rows) {
    for (const auto word : row.primary_bit_words) {
      append_le(rows_bytes.bytes, word, 4U);
    }
  }
  for (const auto &row : source.rows) {
    for (const auto slot : row.registration_slots) {
      append_le(rows_bytes.bytes, static_cast<std::uint16_t>(slot.key), 2U);
      append_le(rows_bytes.bytes, slot.auxiliary_bits, 2U);
    }
  }
  result.initial.buffers.push_back(std::move(level_bytes));
  result.initial.buffers.push_back(std::move(rows_bytes));
  result.initial =
      canonicalize_session_state_initial_v1(result.initial, limits);
  return result;
}

} // namespace openrc
