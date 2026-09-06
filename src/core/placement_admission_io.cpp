#include "openrc/placement_admission_io.hpp"

#include "openrc/hash.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>

namespace openrc {
namespace {

constexpr std::array<std::byte, 8U> kMagic{
    std::byte{'O'}, std::byte{'R'}, std::byte{'P'}, std::byte{'A'},
    std::byte{'D'}, std::byte{'M'}, std::byte{'I'}, std::byte{'T'}};
constexpr std::size_t kDigestOffset = 0x20U;
constexpr PreparedContentDigestV1 kZeroDigest{};

[[noreturn]] void fail(const std::string &message) {
  throw PlacementAdmissionIoError("Placement admission I/O " + message);
}

void validate_limits(const PlacementAdmissionIoLimitsV1 limits) {
  if (limits.max_input_bytes == 0U || limits.plan.max_key_bytes == 0U ||
      limits.plan.max_registration_slots == 0U) {
    fail("requires explicit positive limits");
  }
}

void validate_plan(const PlacementAdmissionPlanV1 &plan,
                   const PlacementAdmissionLimitsV1 limits) {
  try {
    validate_placement_admission_plan_v1(plan, limits);
  } catch (const PlacementAdmissionError &error) {
    fail(error.what());
  }
}

// The same field writer measures first, without copying or allocating, and
// then writes into one exactly sized allocation. No optional padding exists.
class Writer final {
public:
  explicit Writer(const std::uint64_t maximum) : maximum_(maximum) {}
  explicit Writer(const std::span<std::byte> output)
      : output_(output), maximum_(output.size()), measuring_(false) {}

  void raw(const std::span<const std::byte> value) {
    if (value.size() > maximum_ - position_) {
      fail("encoded output exceeds its byte limit");
    }
    if (!measuring_) {
      const auto target = output_.subspan(static_cast<std::size_t>(position_),
                                          value.size());
      std::copy(value.begin(), value.end(), target.begin());
    }
    position_ += value.size();
  }

  void u32(const std::uint32_t value) {
    std::array<std::byte, 4U> bytes{};
    for (unsigned i = 0U; i < 4U; ++i) {
      bytes[i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
    raw(bytes);
  }

  void u64(const std::uint64_t value) {
    std::array<std::byte, 8U> bytes{};
    for (unsigned i = 0U; i < 8U; ++i) {
      bytes[i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
    raw(bytes);
  }

  void presence(const bool value) { u32(value ? 1U : 0U); }

  void element(const std::optional<PlacementStateElementV1> &value) {
    presence(value.has_value());
    if (!value) {
      return;
    }
    u32(static_cast<std::uint32_t>(value->view_key.size()));
    u64(value->element_index);
    raw(std::as_bytes(std::span(value->view_key.data(), value->view_key.size())));
  }

  void bit(const PlacementBitReadV1 &value) {
    element(value.word);
    u32(value.bit_index);
  }

  void arm(const std::optional<PlacementCountAdjustmentV1> &value) {
    presence(value.has_value());
    if (value) {
      u32(value->maximum_bits);
      bit(value->adjust_when_set);
    }
  }

  [[nodiscard]] std::uint64_t position() const noexcept { return position_; }

private:
  std::span<std::byte> output_;
  std::uint64_t maximum_;
  std::uint64_t position_ = 0U;
  bool measuring_ = true;
};

void write_plan(Writer &writer, const PlacementAdmissionPlanV1 &plan,
                const std::uint64_t total_bytes) {
  writer.raw(kMagic);
  writer.u32(kPlacementAdmissionIoFormatVersionV1);
  writer.u32(kPlacementAdmissionIoHeaderBytesV1);
  writer.u64(total_bytes);
  writer.u32(plan.schema_version);
  writer.u32(0U);
  writer.raw(kZeroDigest);
  writer.raw(plan.state_schema_sha256);
  writer.u32(plan.initial_count_bits);
  writer.presence(plan.registration.has_value());
  if (plan.registration) {
    const auto &registration = *plan.registration;
    writer.element(registration.row);
    writer.u32(registration.slot_count);
    writer.presence(registration.exact_match.has_value());
    if (registration.exact_match) {
      writer.u32(*registration.exact_match);
    }
    writer.u32(registration.inserted_value);
  }
  writer.presence(plan.reject_when_nonzero.has_value());
  if (plan.reject_when_nonzero) {
    writer.element(plan.reject_when_nonzero->element);
  }
  if (const auto *clear =
          std::get_if<PlacementRequireBitClearV1>(&plan.condition)) {
    writer.u32(1U);
    writer.bit(clear->bit);
  } else if (const auto *selection =
                 std::get_if<PlacementCountSelectionV1>(&plan.condition)) {
    writer.u32(2U);
    writer.element(selection->selector.element);
    writer.u32(selection->equal_value);
    writer.arm(selection->when_equal);
    writer.arm(selection->otherwise);
  } else {
    writer.u32(0U);
  }
}

class Reader final {
public:
  Reader(const std::span<const std::byte> input,
          const PlacementAdmissionLimitsV1 limits)
      : input_(input), limits_(limits) {}

  [[nodiscard]] std::span<const std::byte> raw(const std::size_t count) {
    if (count > input_.size() - position_) {
      fail("has a truncated field");
    }
    const auto result = input_.subspan(position_, count);
    position_ += count;
    return result;
  }

  [[nodiscard]] std::uint32_t u32() {
    const auto bytes = raw(4U);
    std::uint32_t value = 0U;
    for (unsigned i = 0U; i < 4U; ++i) {
      value |= std::to_integer<std::uint32_t>(bytes[i]) << (i * 8U);
    }
    return value;
  }

  [[nodiscard]] std::uint64_t u64() {
    const auto bytes = raw(8U);
    std::uint64_t value = 0U;
    for (unsigned i = 0U; i < 8U; ++i) {
      value |= std::to_integer<std::uint64_t>(bytes[i]) << (i * 8U);
    }
    return value;
  }

  [[nodiscard]] bool presence() {
    const auto value = u32();
    if (value > 1U) {
      fail("has an unknown presence marker");
    }
    return value == 1U;
  }

  [[nodiscard]] std::uint32_t scalar(const std::uint32_t maximum) {
    const auto value = u32();
    if (value > maximum) {
      fail("has noncanonical high bits in a scalar field");
    }
    return value;
  }

  // A null destination is the allocation-free preflight. It still validates
  // every encoded field, including unavailable reads and rejected arms.
  [[nodiscard]] std::optional<std::uint64_t>
  element(std::optional<PlacementStateElementV1> *destination) {
    if (!presence()) {
      return std::nullopt;
    }
    const auto size = u32();
    if (size == 0U || size > limits_.max_key_bytes ||
        size > std::string{}.max_size()) {
      fail("element key exceeds its nonempty bounded shape");
    }
    const auto index = u64();
    const auto bytes = raw(size);
    for (const auto byte : bytes) {
      const auto ch = std::to_integer<unsigned>(byte);
      if (ch < 0x21U || ch > 0x7eU) {
        fail("element key is not printable non-whitespace ASCII");
      }
    }
    if (destination != nullptr) {
      destination->emplace(PlacementStateElementV1{
          std::string(reinterpret_cast<const char *>(bytes.data()), size), index});
    }
    return index;
  }

  void bit(PlacementBitReadV1 *destination) {
    (void)element(destination == nullptr ? nullptr : &destination->word);
    const auto index = scalar(31U);
    if (destination != nullptr) {
      destination->bit_index = static_cast<std::uint8_t>(index);
    }
  }

  void arm(std::optional<PlacementCountAdjustmentV1> *destination) {
    if (!presence()) {
      return;
    }
    const auto maximum = u32();
    if (destination == nullptr) {
      bit(nullptr);
    } else {
      auto &value = destination->emplace();
      value.maximum_bits = maximum;
      bit(&value.adjust_when_set);
    }
  }

  void finish() const {
    if (position_ != input_.size()) {
      fail("has trailing or alternate-layout bytes");
    }
  }

private:
  std::span<const std::byte> input_;
  PlacementAdmissionLimitsV1 limits_;
  std::size_t position_ = 0U;
};

void read_plan(const std::span<const std::byte> bytes,
               const PlacementAdmissionLimitsV1 limits,
               PlacementAdmissionPlanV1 *destination) {
  Reader reader(bytes, limits);
  const auto magic = reader.raw(kMagic.size());
  if (!std::equal(magic.begin(), magic.end(), kMagic.begin()) ||
      reader.u32() != kPlacementAdmissionIoFormatVersionV1 ||
      reader.u32() != kPlacementAdmissionIoHeaderBytesV1) {
    fail("has unknown magic, format version or header size");
  }
  if (reader.u64() != bytes.size()) {
    fail("does not have its exact declared size");
  }
  const auto schema_version = reader.u32();
  if (schema_version != kPlacementAdmissionSchemaVersionV1 || reader.u32() != 0U) {
    fail("has an unknown plan version or nonzero reserved field");
  }
  (void)reader.raw(kZeroDigest.size());
  const auto state_digest = reader.raw(kZeroDigest.size());
  if (std::all_of(state_digest.begin(), state_digest.end(),
                  [](const std::byte value) { return value == std::byte{0}; })) {
    fail("requires a nonzero state-schema digest");
  }
  const auto initial_count = reader.u32();
  if (destination != nullptr) {
    destination->schema_version = schema_version;
    std::copy(state_digest.begin(), state_digest.end(),
               destination->state_schema_sha256.begin());
    destination->initial_count_bits = initial_count;
  }
  if (reader.presence()) {
    auto *registration = destination == nullptr
                             ? nullptr
                             : &destination->registration.emplace();
    const auto first = reader.element(registration == nullptr
                                          ? nullptr
                                          : &registration->row);
    const auto slots = reader.u32();
    if (slots == 0U || slots > limits.max_registration_slots ||
        (first && slots - 1U >
                      std::numeric_limits<std::uint64_t>::max() - *first)) {
      fail("registration slot count or index extent is invalid");
    }
    const auto exact_match = reader.presence()
                                 ? std::optional<std::uint16_t>(
                                       static_cast<std::uint16_t>(reader.scalar(0xffffU)))
                                 : std::nullopt;
    const auto inserted = reader.scalar(0xffffU);
    if (registration != nullptr) {
      registration->slot_count = slots;
      registration->exact_match = exact_match;
      registration->inserted_value = static_cast<std::uint16_t>(inserted);
    }
  }
  if (reader.presence()) {
    auto *read = destination == nullptr
                     ? nullptr
                     : &destination->reject_when_nonzero.emplace();
    (void)reader.element(read == nullptr ? nullptr : &read->element);
  }
  const auto kind = reader.u32();
  if (kind == 0U) {
    // Default-constructed destination already contains monostate.
  } else if (kind == 1U) {
    auto *clear = destination == nullptr
                      ? nullptr
                      : &destination->condition.emplace<PlacementRequireBitClearV1>();
    reader.bit(clear == nullptr ? nullptr : &clear->bit);
  } else if (kind == 2U) {
    auto *selection = destination == nullptr
                          ? nullptr
                          : &destination->condition.emplace<PlacementCountSelectionV1>();
    (void)reader.element(selection == nullptr ? nullptr
                                              : &selection->selector.element);
    const auto equal = reader.scalar(0xffU);
    reader.arm(selection == nullptr ? nullptr : &selection->when_equal);
    reader.arm(selection == nullptr ? nullptr : &selection->otherwise);
    if (selection != nullptr) {
      selection->equal_value = static_cast<std::uint8_t>(equal);
    }
  } else {
    fail("has an unknown condition kind");
  }
  reader.finish();
}

PreparedContentDigestV1 wire_digest(const std::span<const std::byte> bytes) {
  Sha256 hash;
  hash.update(bytes.first(kDigestOffset));
  hash.update(kZeroDigest);
  hash.update(bytes.subspan(kPlacementAdmissionIoHeaderBytesV1));
  return hash.finish();
}

} // namespace

std::vector<std::byte> encode_placement_admission_plan_v1(
    const PlacementAdmissionPlanV1 &plan, const PlacementAdmissionIoLimitsV1 limits) {
  validate_limits(limits);
  validate_plan(plan, limits.plan);
  Writer measure(limits.max_input_bytes);
  write_plan(measure, plan, 0U);
  const auto total = measure.position();
  if (total > std::numeric_limits<std::size_t>::max() ||
      total > std::vector<std::byte>{}.max_size()) {
    fail("encoded output exceeds the host container limit");
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(total));
  Writer writer{std::span<std::byte>(bytes)};
  write_plan(writer, plan, total);
  const auto digest = wire_digest(bytes);
  std::copy(digest.begin(), digest.end(), bytes.begin() + kDigestOffset);
  return bytes;
}

PlacementAdmissionPlanV1 decode_placement_admission_plan_v1(
    const std::span<const std::byte> bytes, const PlacementAdmissionIoLimitsV1 limits) {
  validate_limits(limits);
  if (bytes.size() > limits.max_input_bytes) {
    fail("encoded input exceeds its byte limit");
  }
  read_plan(bytes, limits.plan, nullptr);
  const auto digest = wire_digest(bytes);
  if (!std::equal(digest.begin(), digest.end(), bytes.begin() + kDigestOffset)) {
    fail("whole-plan SHA-256 digest is stale");
  }
  PlacementAdmissionPlanV1 result;
  read_plan(bytes, limits.plan, &result);
  validate_plan(result, limits.plan);
  return result;
}

} // namespace openrc
