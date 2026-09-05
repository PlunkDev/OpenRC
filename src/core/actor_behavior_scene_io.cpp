#include "openrc/actor_behavior_scene_io.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::array<std::byte, 8U> kMagic{
    std::byte{'O'}, std::byte{'R'}, std::byte{'A'}, std::byte{'B'},
    std::byte{'H'}, std::byte{'V'}, std::byte{'R'}, std::byte{'1'},
};

constexpr std::uint64_t kFormatVersionOffset = 0x08U;
constexpr std::uint64_t kHeaderBytesOffset = 0x0cU;
constexpr std::uint64_t kTotalBytesOffset = 0x10U;
constexpr std::uint64_t kPayloadTypeOffset = 0x18U;
constexpr std::uint64_t kSchemaVersionOffset = 0x1cU;
constexpr std::uint64_t kLevelIdOffset = 0x20U;
constexpr std::uint64_t kHeaderFlagsOffset = 0x24U;
constexpr std::uint64_t kProgramCountOffset = 0x28U;
constexpr std::uint64_t kFieldCountOffset = 0x2cU;
constexpr std::uint64_t kAnimationImportCountOffset = 0x30U;
constexpr std::uint64_t kRandomImportCountOffset = 0x34U;
constexpr std::uint64_t kRandomStreamCountOffset = 0x38U;
constexpr std::uint64_t kInstanceCountOffset = 0x3cU;
constexpr std::uint64_t kInitialValueCountOffset = 0x40U;
constexpr std::uint64_t kRandomWordCountOffset = 0x44U;
constexpr std::uint64_t kInitialAnimationCountOffset = 0x48U;
constexpr std::uint64_t kHeaderReserved32Offset = 0x4cU;
constexpr std::uint64_t kStringBytesOffset = 0x50U;
constexpr std::uint64_t kProgramRecordBytesOffset = 0x58U;
constexpr std::uint64_t kFieldRecordBytesOffset = 0x5cU;
constexpr std::uint64_t kAnimationImportRecordBytesOffset = 0x60U;
constexpr std::uint64_t kRandomImportRecordBytesOffset = 0x64U;
constexpr std::uint64_t kRandomStreamRecordBytesOffset = 0x68U;
constexpr std::uint64_t kInstanceRecordBytesOffset = 0x6cU;
constexpr std::uint64_t kInitialValueRecordBytesOffset = 0x70U;
constexpr std::uint64_t kRandomWordRecordBytesOffset = 0x74U;
constexpr std::uint64_t kInitialAnimationRecordBytesOffset = 0x78U;
constexpr std::uint64_t kHeaderReservedRecordOffset = 0x7cU;
constexpr std::uint64_t kProgramTableOffset = 0x80U;
constexpr std::uint64_t kFieldTableOffset = 0x88U;
constexpr std::uint64_t kAnimationImportTableOffset = 0x90U;
constexpr std::uint64_t kRandomImportTableOffset = 0x98U;
constexpr std::uint64_t kRandomStreamTableOffset = 0xa0U;
constexpr std::uint64_t kInstanceTableOffset = 0xa8U;
constexpr std::uint64_t kInitialValueTableOffset = 0xb0U;
constexpr std::uint64_t kRandomWordTableOffset = 0xb8U;
constexpr std::uint64_t kInitialAnimationTableOffset = 0xc0U;
constexpr std::uint64_t kStringDataOffset = 0xc8U;
constexpr std::uint64_t kHeaderReservedOffset = 0xd0U;

constexpr std::uint64_t kProgramIdOffset = 0x00U;
constexpr std::uint64_t kProgramAbiOffset = 0x04U;
constexpr std::uint64_t kProgramStateCountOffset = 0x08U;
constexpr std::uint64_t kProgramSourceUpdatesPerSecondOffset = 0x0cU;
constexpr std::uint64_t kProgramAnimationChannelCountOffset = 0x10U;
constexpr std::uint64_t kProgramFlagsOffset = 0x14U;
constexpr std::uint64_t kProgramSemanticKeyBytesOffset = 0x18U;
constexpr std::uint64_t kProgramImplementationKeyBytesOffset = 0x1cU;
constexpr std::uint64_t kProgramRigKeyBytesOffset = 0x20U;
constexpr std::uint64_t kProgramReservedOffset = 0x24U;
constexpr std::uint64_t kProgramSemanticKeyOffsetOffset = 0x28U;
constexpr std::uint64_t kProgramImplementationKeyOffsetOffset = 0x30U;
constexpr std::uint64_t kProgramRigKeyOffsetOffset = 0x38U;
constexpr std::uint64_t kProgramRigDigestOffset = 0x40U;
constexpr std::uint64_t kProgramLayoutDigestOffset = 0x60U;
constexpr std::uint64_t kProgramFirstFieldOffset = 0x80U;
constexpr std::uint64_t kProgramFieldCountOffset = 0x84U;
constexpr std::uint64_t kProgramFirstAnimationImportOffset = 0x88U;
constexpr std::uint64_t kProgramAnimationImportCountOffset = 0x8cU;
constexpr std::uint64_t kProgramFirstRandomImportOffset = 0x90U;
constexpr std::uint64_t kProgramRandomImportCountOffset = 0x94U;
constexpr std::uint64_t kProgramFieldElementCountOffset = 0x98U;
constexpr std::uint64_t kProgramInstanceRandomWordCountOffset = 0x9cU;
constexpr std::uint64_t kProgramModelKeyBytesOffset = 0xa0U;
constexpr std::uint64_t kProgramModelReservedOffset = 0xa4U;
constexpr std::uint64_t kProgramModelKeyOffsetOffset = 0xa8U;
constexpr std::uint64_t kProgramModelDigestOffset = 0xb0U;

constexpr std::uint64_t kFieldProgramIdOffset = 0x00U;
constexpr std::uint64_t kFieldIdOffset = 0x04U;
constexpr std::uint64_t kFieldTypeOffset = 0x08U;
constexpr std::uint64_t kFieldFlagsOffset = 0x0cU;
constexpr std::uint64_t kFieldElementCountOffset = 0x10U;
constexpr std::uint64_t kFieldKeyBytesOffset = 0x14U;
constexpr std::uint64_t kFieldKeyOffsetOffset = 0x18U;

constexpr std::uint64_t kAnimationProgramIdOffset = 0x00U;
constexpr std::uint64_t kAnimationIdOffset = 0x04U;
constexpr std::uint64_t kAnimationFlagsOffset = 0x08U;
constexpr std::uint64_t kAnimationBindingKeyBytesOffset = 0x0cU;
constexpr std::uint64_t kAnimationClipKeyBytesOffset = 0x10U;
constexpr std::uint64_t kAnimationFrameCountOffset = 0x14U;
constexpr std::uint64_t kAnimationBindingKeyOffsetOffset = 0x18U;
constexpr std::uint64_t kAnimationClipKeyOffsetOffset = 0x20U;
constexpr std::uint64_t kAnimationReserved64Offset = 0x28U;
constexpr std::uint64_t kAnimationClipDigestOffset = 0x30U;

constexpr std::uint64_t kRandomImportProgramIdOffset = 0x00U;
constexpr std::uint64_t kRandomImportIdOffset = 0x04U;
constexpr std::uint64_t kRandomImportScopeOffset = 0x08U;
constexpr std::uint64_t kRandomImportFlagsOffset = 0x0cU;
constexpr std::uint64_t kRandomImportStateWordCountOffset = 0x10U;
constexpr std::uint64_t kRandomImportAlgorithmAbiOffset = 0x14U;
constexpr std::uint64_t kRandomImportBindingKeyBytesOffset = 0x18U;
constexpr std::uint64_t kRandomImportStreamKeyBytesOffset = 0x1cU;
constexpr std::uint64_t kRandomImportAlgorithmKeyBytesOffset = 0x20U;
constexpr std::uint64_t kRandomImportReservedOffset = 0x24U;
constexpr std::uint64_t kRandomImportBindingKeyOffsetOffset = 0x28U;
constexpr std::uint64_t kRandomImportStreamKeyOffsetOffset = 0x30U;
constexpr std::uint64_t kRandomImportAlgorithmKeyOffsetOffset = 0x38U;

constexpr std::uint64_t kRandomStreamIdOffset = 0x00U;
constexpr std::uint64_t kRandomStreamScopeOffset = 0x04U;
constexpr std::uint64_t kRandomStreamFlagsOffset = 0x08U;
constexpr std::uint64_t kRandomStreamAlgorithmAbiOffset = 0x0cU;
constexpr std::uint64_t kRandomStreamFirstWordOffset = 0x10U;
constexpr std::uint64_t kRandomStreamWordCountOffset = 0x14U;
constexpr std::uint64_t kRandomStreamSemanticKeyBytesOffset = 0x18U;
constexpr std::uint64_t kRandomStreamAlgorithmKeyBytesOffset = 0x1cU;
constexpr std::uint64_t kRandomStreamSemanticKeyOffsetOffset = 0x20U;
constexpr std::uint64_t kRandomStreamAlgorithmKeyOffsetOffset = 0x28U;
constexpr std::uint64_t kRandomStreamReservedOffset = 0x30U;

constexpr std::uint64_t kInstanceAuthoredIdOffset = 0x00U;
constexpr std::uint64_t kInstanceProgramIdOffset = 0x04U;
constexpr std::uint64_t kInstanceInitialStateOffset = 0x08U;
constexpr std::uint64_t kInstanceFlagsOffset = 0x0cU;
constexpr std::uint64_t kInstanceFirstValueOffset = 0x10U;
constexpr std::uint64_t kInstanceValueCountOffset = 0x14U;
constexpr std::uint64_t kInstanceFirstRandomWordOffset = 0x18U;
constexpr std::uint64_t kInstanceRandomWordCountOffset = 0x1cU;
constexpr std::uint64_t kInstanceFirstAnimationOffset = 0x20U;
constexpr std::uint64_t kInstanceAnimationCountOffset = 0x24U;
constexpr std::uint64_t kInstanceReservedOffset = 0x28U;

constexpr std::uint64_t kValueTypeOffset = 0x00U;
constexpr std::uint64_t kValueFlagsOffset = 0x04U;
constexpr std::uint64_t kValuePayloadOffset = 0x08U;

constexpr std::uint64_t kInitialAnimationChannelIdOffset = 0x00U;
constexpr std::uint64_t kInitialAnimationImportIdOffset = 0x04U;
constexpr std::uint64_t kInitialAnimationFirstFrameOffset = 0x08U;
constexpr std::uint64_t kInitialAnimationFlagsOffset = 0x0cU;

static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<float>::radix == 2);
static_assert(std::numeric_limits<float>::digits == 24);

[[noreturn]] void fail(const std::string &message) {
  throw ActorBehaviorSceneIoError(message);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(const std::uint64_t left,
                                             const std::uint64_t right,
                                             const char *const description) {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left * right;
}

[[nodiscard]] std::uint64_t
random_word_limit(const ActorBehaviorSceneLimitsV1 &limits) {
  return checked_add(limits.max_total_random_state_words,
                     limits.max_total_initial_random_words,
                     "ActorBehaviorSceneV1 total random-word limit");
}

void validate_limits(const ActorBehaviorSceneIoLimitsV1 &limits) {
  if (limits.max_encoded_bytes < kActorBehaviorSceneIoHeaderBytesV1) {
    fail("ActorBehaviorSceneV1 maximum encoded size is too small");
  }
  try {
    validate_actor_behavior_scene_v1(ActorBehaviorSceneV1{}, limits.scene);
  } catch (const ActorBehaviorSceneError &error) {
    fail("ActorBehaviorSceneV1 I/O limits are invalid: " +
         std::string(error.what()));
  }
}

[[nodiscard]] std::size_t host_size(const std::uint64_t value,
                                    const std::size_t maximum,
                                    const char *const description) {
  if (value > maximum || value > static_cast<std::uint64_t>(
                                     std::numeric_limits<std::size_t>::max())) {
    fail(std::string(description) + " exceeds the host container domain");
  }
  return static_cast<std::size_t>(value);
}

[[nodiscard]] std::uint32_t format_count(const std::uint64_t value,
                                         const char *const description) {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    fail(std::string(description) + " exceeds the format count width");
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_u32(const std::span<const std::byte> bytes,
                                     const std::uint64_t offset) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  return static_cast<std::uint32_t>(byte_value(bytes[begin])) |
         (static_cast<std::uint32_t>(byte_value(bytes[begin + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[begin + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[begin + 3U])) << 24U);
}

[[nodiscard]] std::uint64_t read_u64(const std::span<const std::byte> bytes,
                                     const std::uint64_t offset) noexcept {
  std::uint64_t result = 0U;
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < sizeof(result); ++index) {
    result |= static_cast<std::uint64_t>(byte_value(bytes[begin + index]))
              << (index * 8U);
  }
  return result;
}

void write_u32(std::vector<std::byte> &bytes, const std::uint64_t offset,
               const std::uint32_t value) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes[begin + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT32_C(0xff));
  }
}

void write_u64(std::vector<std::byte> &bytes, const std::uint64_t offset,
               const std::uint64_t value) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes[begin + index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
  }
}

void write_f32(std::vector<std::byte> &bytes, const std::uint64_t offset,
               const float value) noexcept {
  write_u32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] bool all_zero(const std::span<const std::byte> bytes,
                            const std::uint64_t begin,
                            const std::uint64_t end) noexcept {
  for (auto offset = begin; offset < end; ++offset) {
    if (bytes[static_cast<std::size_t>(offset)] != std::byte{0U}) {
      return false;
    }
  }
  return true;
}

void write_digest(std::vector<std::byte> &bytes, const std::uint64_t offset,
                  const PreparedContentDigestV1 &digest) noexcept {
  std::copy(digest.begin(), digest.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

[[nodiscard]] PreparedContentDigestV1
read_digest(const std::span<const std::byte> bytes,
            const std::uint64_t offset) noexcept {
  PreparedContentDigestV1 result{};
  std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
              result.size(), result.begin());
  return result;
}

void write_string(std::vector<std::byte> &bytes, const std::uint64_t offset,
                  const std::string &value) noexcept {
  const auto begin = static_cast<std::size_t>(offset);
  for (std::size_t index = 0U; index < value.size(); ++index) {
    bytes[begin + index] =
        static_cast<std::byte>(static_cast<unsigned char>(value[index]));
  }
}

[[nodiscard]] std::string read_string(const std::span<const std::byte> bytes,
                                      const std::uint64_t offset,
                                      const std::uint32_t length,
                                      const std::uint32_t maximum_length,
                                      const char *const description) {
  if (length == 0U || length > maximum_length) {
    fail(std::string(description) + " has an invalid byte length");
  }
  std::string result;
  result.reserve(host_size(length, result.max_size(), description));
  const auto begin = static_cast<std::size_t>(offset);
  for (std::uint32_t index = 0U; index < length; ++index) {
    result.push_back(static_cast<char>(byte_value(bytes[begin + index])));
  }
  return result;
}

void require_partition(const std::uint64_t declared_offset,
                       const std::uint64_t length,
                       std::uint64_t &expected_offset, const std::uint64_t end,
                       const char *const description) {
  if (expected_offset > end || declared_offset != expected_offset ||
      length > end - expected_offset) {
    fail(std::string(description) +
         " is not part of its exact canonical partition");
  }
  expected_offset += length;
}

struct Counts {
  std::uint32_t programs = 0U;
  std::uint32_t fields = 0U;
  std::uint32_t animations = 0U;
  std::uint32_t random_imports = 0U;
  std::uint32_t random_streams = 0U;
  std::uint32_t instances = 0U;
  std::uint32_t values = 0U;
  std::uint32_t random_words = 0U;
  std::uint32_t initial_animations = 0U;
  std::uint64_t strings = 0U;
};

void add_string_count(std::uint64_t &total, const std::string &value) {
  total = checked_add(total, value.size(), "ActorBehaviorSceneV1 string bytes");
}

[[nodiscard]] Counts scene_counts(const ActorBehaviorSceneV1 &scene) {
  Counts result;
  result.programs = format_count(scene.programs.size(), "Program count");
  result.random_streams =
      format_count(scene.random_streams.size(), "Random stream count");
  result.instances = format_count(scene.instances.size(), "Instance count");
  std::uint64_t fields = 0U;
  std::uint64_t animations = 0U;
  std::uint64_t random_imports = 0U;
  std::uint64_t values = 0U;
  std::uint64_t random_words = 0U;
  std::uint64_t initial_animations = 0U;
  for (const auto &program : scene.programs) {
    fields = checked_add(fields, program.fields.size(), "Field count");
    animations = checked_add(animations, program.animation_imports.size(),
                             "Animation import count");
    random_imports = checked_add(random_imports, program.random_imports.size(),
                                 "Random import count");
    add_string_count(result.strings, program.semantic_key);
    add_string_count(result.strings, program.implementation_key);
    add_string_count(result.strings, program.required_rig_key);
    add_string_count(result.strings, program.required_model_key);
    for (const auto &field : program.fields) {
      add_string_count(result.strings, field.semantic_key);
    }
    for (const auto &animation : program.animation_imports) {
      add_string_count(result.strings, animation.binding_key);
      add_string_count(result.strings, animation.clip_key);
    }
    for (const auto &random : program.random_imports) {
      add_string_count(result.strings, random.binding_key);
      add_string_count(result.strings, random.stream_key);
      add_string_count(result.strings, random.algorithm_key);
    }
  }
  for (const auto &stream : scene.random_streams) {
    random_words = checked_add(random_words, stream.initial_state_words.size(),
                               "Random-state word count");
    add_string_count(result.strings, stream.semantic_key);
    add_string_count(result.strings, stream.algorithm_key);
  }
  for (const auto &instance : scene.instances) {
    values = checked_add(values, instance.initial_values.size(),
                         "Initial value count");
    random_words =
        checked_add(random_words, instance.initial_random_words.size(),
                    "Random-state word count");
    initial_animations =
        checked_add(initial_animations, instance.initial_animations.size(),
                    "Initial animation count");
  }
  result.fields = format_count(fields, "Field count");
  result.animations = format_count(animations, "Animation import count");
  result.random_imports = format_count(random_imports, "Random import count");
  result.values = format_count(values, "Initial value count");
  result.random_words = format_count(random_words, "Random-state word count");
  result.initial_animations =
      format_count(initial_animations, "Initial animation count");
  return result;
}

struct Layout {
  std::uint64_t programs = 0U;
  std::uint64_t fields = 0U;
  std::uint64_t animations = 0U;
  std::uint64_t random_imports = 0U;
  std::uint64_t random_streams = 0U;
  std::uint64_t instances = 0U;
  std::uint64_t values = 0U;
  std::uint64_t random_words = 0U;
  std::uint64_t initial_animations = 0U;
  std::uint64_t strings = 0U;
  std::uint64_t total = 0U;
};

[[nodiscard]] Layout canonical_layout(const Counts &counts) {
  Layout result;
  result.programs = kActorBehaviorSceneIoHeaderBytesV1;
  result.fields = checked_add(
      result.programs,
      checked_multiply(counts.programs, kActorBehaviorSceneIoProgramBytesV1,
                       "ActorBehaviorSceneV1 program table"),
      "ActorBehaviorSceneV1 program table");
  result.animations = checked_add(
      result.fields,
      checked_multiply(counts.fields, kActorBehaviorSceneIoFieldBytesV1,
                       "ActorBehaviorSceneV1 field table"),
      "ActorBehaviorSceneV1 field table");
  result.random_imports = checked_add(
      result.animations,
      checked_multiply(counts.animations,
                       kActorBehaviorSceneIoAnimationImportBytesV1,
                       "ActorBehaviorSceneV1 animation-import table"),
      "ActorBehaviorSceneV1 animation-import table");
  result.random_streams =
      checked_add(result.random_imports,
                  checked_multiply(counts.random_imports,
                                   kActorBehaviorSceneIoRandomImportBytesV1,
                                   "ActorBehaviorSceneV1 random-import table"),
                  "ActorBehaviorSceneV1 random-import table");
  result.instances =
      checked_add(result.random_streams,
                  checked_multiply(counts.random_streams,
                                   kActorBehaviorSceneIoRandomStreamBytesV1,
                                   "ActorBehaviorSceneV1 random-stream table"),
                  "ActorBehaviorSceneV1 random-stream table");
  result.values = checked_add(
      result.instances,
      checked_multiply(counts.instances, kActorBehaviorSceneIoInstanceBytesV1,
                       "ActorBehaviorSceneV1 instance table"),
      "ActorBehaviorSceneV1 instance table");
  result.random_words = checked_add(
      result.values,
      checked_multiply(counts.values, kActorBehaviorSceneIoInitialValueBytesV1,
                       "ActorBehaviorSceneV1 initial-value table"),
      "ActorBehaviorSceneV1 initial-value table");
  result.initial_animations =
      checked_add(result.random_words,
                  checked_multiply(counts.random_words,
                                   kActorBehaviorSceneIoRandomStateWordBytesV1,
                                   "ActorBehaviorSceneV1 random-word table"),
                  "ActorBehaviorSceneV1 random-word table");
  result.strings = checked_add(
      result.initial_animations,
      checked_multiply(counts.initial_animations,
                       kActorBehaviorSceneIoInitialAnimationBytesV1,
                       "ActorBehaviorSceneV1 initial-animation table"),
      "ActorBehaviorSceneV1 initial-animation table");
  result.total = checked_add(result.strings, counts.strings,
                             "ActorBehaviorSceneV1 string data");
  return result;
}

[[nodiscard]] ActorBehaviorSceneV1
canonical_scene(const ActorBehaviorSceneV1 &scene,
                const ActorBehaviorSceneLimitsV1 limits) {
  try {
    return canonicalize_actor_behavior_scene_v1(scene, limits);
  } catch (const ActorBehaviorSceneError &error) {
    fail("Cannot encode ActorBehaviorSceneV1: " + std::string(error.what()));
  }
}

[[nodiscard]] std::uint32_t
program_value_count(const ActorBehaviorProgramV1 &program) {
  std::uint64_t result = 0U;
  for (const auto &field : program.fields) {
    result = checked_add(result, field.element_count,
                         "ActorBehaviorSceneV1 program field elements");
  }
  return format_count(result, "Per-program field-element count");
}

[[nodiscard]] std::uint32_t
program_instance_random_word_count(const ActorBehaviorProgramV1 &program) {
  std::uint64_t result = 0U;
  for (const auto &random : program.random_imports) {
    if (random.scope == ActorBehaviorRandomScopeV1::instance) {
      result = checked_add(result, random.state_word_count,
                           "ActorBehaviorSceneV1 instance random words");
    }
  }
  return format_count(result, "Per-program instance random-word count");
}

void write_value(std::vector<std::byte> &bytes, const std::uint64_t offset,
                 const ActorBehaviorInitialValueV1 &value) noexcept {
  const auto type = actor_behavior_initial_value_type_v1(value);
  write_u32(bytes, offset + kValueTypeOffset, static_cast<std::uint32_t>(type));
  if (const auto boolean = std::get_if<bool>(&value)) {
    write_u32(bytes, offset + kValuePayloadOffset, *boolean ? 1U : 0U);
  } else if (const auto signed_value = std::get_if<std::int32_t>(&value)) {
    write_u32(bytes, offset + kValuePayloadOffset,
              std::bit_cast<std::uint32_t>(*signed_value));
  } else if (const auto unsigned_value = std::get_if<std::uint32_t>(&value)) {
    write_u32(bytes, offset + kValuePayloadOffset, *unsigned_value);
  } else if (const auto scalar = std::get_if<float>(&value)) {
    write_f32(bytes, offset + kValuePayloadOffset, *scalar);
  } else if (const auto vector = std::get_if<std::array<float, 3U>>(&value)) {
    for (std::size_t axis = 0U; axis < vector->size(); ++axis) {
      write_f32(bytes, offset + kValuePayloadOffset + axis * 4U,
                (*vector)[axis]);
    }
  } else {
    const auto &reference = std::get<ActorBehaviorEntityReferenceV1>(value);
    write_u32(bytes, offset + kValuePayloadOffset,
              reference.authored_id.has_value() ? 1U : 0U);
    if (reference.authored_id) {
      write_u32(bytes, offset + kValuePayloadOffset + 4U,
                *reference.authored_id);
    }
  }
}

[[nodiscard]] ActorBehaviorInitialValueV1
read_value(const std::span<const std::byte> bytes, const std::uint64_t offset) {
  if (read_u32(bytes, offset + kValueFlagsOffset) != 0U) {
    fail("ActorBehaviorSceneV1 initial value has unknown flags");
  }
  const auto type = static_cast<ActorBehaviorValueTypeV1>(
      read_u32(bytes, offset + kValueTypeOffset));
  const auto payload = offset + kValuePayloadOffset;
  switch (type) {
  case ActorBehaviorValueTypeV1::boolean: {
    const auto value = read_u32(bytes, payload);
    if (value > 1U || !all_zero(bytes, payload + 4U, payload + 16U)) {
      fail("ActorBehaviorSceneV1 boolean payload is not canonical");
    }
    return value != 0U;
  }
  case ActorBehaviorValueTypeV1::signed_integer: {
    if (!all_zero(bytes, payload + 4U, payload + 16U)) {
      fail("ActorBehaviorSceneV1 signed-integer payload is not canonical");
    }
    return std::bit_cast<std::int32_t>(read_u32(bytes, payload));
  }
  case ActorBehaviorValueTypeV1::unsigned_integer: {
    if (!all_zero(bytes, payload + 4U, payload + 16U)) {
      fail("ActorBehaviorSceneV1 unsigned-integer payload is not canonical");
    }
    return read_u32(bytes, payload);
  }
  case ActorBehaviorValueTypeV1::scalar_f32: {
    if (!all_zero(bytes, payload + 4U, payload + 16U)) {
      fail("ActorBehaviorSceneV1 scalar payload is not canonical");
    }
    return std::bit_cast<float>(read_u32(bytes, payload));
  }
  case ActorBehaviorValueTypeV1::vector3_f32: {
    if (!all_zero(bytes, payload + 12U, payload + 16U)) {
      fail("ActorBehaviorSceneV1 vector payload is not canonical");
    }
    return std::array{std::bit_cast<float>(read_u32(bytes, payload)),
                      std::bit_cast<float>(read_u32(bytes, payload + 4U)),
                      std::bit_cast<float>(read_u32(bytes, payload + 8U))};
  }
  case ActorBehaviorValueTypeV1::entity_reference: {
    const auto present = read_u32(bytes, payload);
    const auto authored_id = read_u32(bytes, payload + 4U);
    if (present > 1U || (present == 0U && authored_id != 0U) ||
        !all_zero(bytes, payload + 8U, payload + 16U)) {
      fail("ActorBehaviorSceneV1 entity-reference payload is not canonical");
    }
    ActorBehaviorEntityReferenceV1 result;
    if (present != 0U) {
      result.authored_id = authored_id;
    }
    return result;
  }
  }
  fail("ActorBehaviorSceneV1 initial value type is unknown");
}

} // namespace

std::vector<std::byte>
encode_actor_behavior_scene_v1(const ActorBehaviorSceneV1 &scene,
                               const ActorBehaviorSceneIoLimitsV1 limits) {
  validate_limits(limits);
  const auto canonical = canonical_scene(scene, limits.scene);
  const auto counts = scene_counts(canonical);
  const auto layout = canonical_layout(counts);
  if (layout.total > limits.max_encoded_bytes) {
    fail("ActorBehaviorSceneV1 encoded bytes exceed the caller limit");
  }

  std::vector<std::byte> result(host_size(layout.total,
                                          std::vector<std::byte>{}.max_size(),
                                          "ActorBehaviorSceneV1 encoded size"),
                                std::byte{0U});
  std::copy(kMagic.begin(), kMagic.end(), result.begin());
  write_u32(result, kFormatVersionOffset, kActorBehaviorSceneIoFormatVersionV1);
  write_u32(result, kHeaderBytesOffset, kActorBehaviorSceneIoHeaderBytesV1);
  write_u64(result, kTotalBytesOffset, layout.total);
  write_u32(result, kPayloadTypeOffset, kActorBehaviorSceneIoPayloadTypeV1);
  write_u32(result, kSchemaVersionOffset, canonical.schema_version);
  write_u32(result, kLevelIdOffset, canonical.level_id);
  write_u32(result, kProgramCountOffset, counts.programs);
  write_u32(result, kFieldCountOffset, counts.fields);
  write_u32(result, kAnimationImportCountOffset, counts.animations);
  write_u32(result, kRandomImportCountOffset, counts.random_imports);
  write_u32(result, kRandomStreamCountOffset, counts.random_streams);
  write_u32(result, kInstanceCountOffset, counts.instances);
  write_u32(result, kInitialValueCountOffset, counts.values);
  write_u32(result, kRandomWordCountOffset, counts.random_words);
  write_u32(result, kInitialAnimationCountOffset, counts.initial_animations);
  write_u64(result, kStringBytesOffset, counts.strings);
  write_u32(result, kProgramRecordBytesOffset,
            kActorBehaviorSceneIoProgramBytesV1);
  write_u32(result, kFieldRecordBytesOffset, kActorBehaviorSceneIoFieldBytesV1);
  write_u32(result, kAnimationImportRecordBytesOffset,
            kActorBehaviorSceneIoAnimationImportBytesV1);
  write_u32(result, kRandomImportRecordBytesOffset,
            kActorBehaviorSceneIoRandomImportBytesV1);
  write_u32(result, kRandomStreamRecordBytesOffset,
            kActorBehaviorSceneIoRandomStreamBytesV1);
  write_u32(result, kInstanceRecordBytesOffset,
            kActorBehaviorSceneIoInstanceBytesV1);
  write_u32(result, kInitialValueRecordBytesOffset,
            kActorBehaviorSceneIoInitialValueBytesV1);
  write_u32(result, kRandomWordRecordBytesOffset,
            kActorBehaviorSceneIoRandomStateWordBytesV1);
  write_u32(result, kInitialAnimationRecordBytesOffset,
            kActorBehaviorSceneIoInitialAnimationBytesV1);
  write_u64(result, kProgramTableOffset, layout.programs);
  write_u64(result, kFieldTableOffset, layout.fields);
  write_u64(result, kAnimationImportTableOffset, layout.animations);
  write_u64(result, kRandomImportTableOffset, layout.random_imports);
  write_u64(result, kRandomStreamTableOffset, layout.random_streams);
  write_u64(result, kInstanceTableOffset, layout.instances);
  write_u64(result, kInitialValueTableOffset, layout.values);
  write_u64(result, kRandomWordTableOffset, layout.random_words);
  write_u64(result, kInitialAnimationTableOffset, layout.initial_animations);
  write_u64(result, kStringDataOffset, layout.strings);

  std::uint32_t first_field = 0U;
  std::uint32_t first_animation = 0U;
  std::uint32_t first_random_import = 0U;
  auto string_offset = layout.strings;
  for (std::size_t program_index = 0U;
       program_index < canonical.programs.size(); ++program_index) {
    const auto record =
        layout.programs + program_index * kActorBehaviorSceneIoProgramBytesV1;
    const auto &program = canonical.programs[program_index];
    const auto field_count =
        format_count(program.fields.size(), "Per-program field count");
    const auto animation_count = format_count(program.animation_imports.size(),
                                              "Per-program animation count");
    const auto random_count = format_count(program.random_imports.size(),
                                           "Per-program random-import count");
    write_u32(result, record + kProgramIdOffset, program.id);
    write_u32(result, record + kProgramAbiOffset,
              program.implementation_abi_version);
    write_u32(result, record + kProgramStateCountOffset, program.state_count);
    write_u32(result, record + kProgramSourceUpdatesPerSecondOffset,
              program.source_updates_per_second);
    write_u32(result, record + kProgramAnimationChannelCountOffset,
              program.animation_channel_count);
    write_u32(result, record + kProgramFlagsOffset, program.flags);
    write_u32(result, record + kProgramSemanticKeyBytesOffset,
              format_count(program.semantic_key.size(), "Program key length"));
    write_u32(result, record + kProgramImplementationKeyBytesOffset,
              format_count(program.implementation_key.size(),
                           "Implementation key length"));
    write_u32(result, record + kProgramRigKeyBytesOffset,
              format_count(program.required_rig_key.size(), "Rig key length"));
    write_u32(
        result, record + kProgramModelKeyBytesOffset,
        format_count(program.required_model_key.size(), "Model key length"));
    write_u64(result, record + kProgramSemanticKeyOffsetOffset, string_offset);
    write_string(result, string_offset, program.semantic_key);
    string_offset = checked_add(string_offset, program.semantic_key.size(),
                                "ActorBehaviorSceneV1 string partition");
    write_u64(result, record + kProgramImplementationKeyOffsetOffset,
              string_offset);
    write_string(result, string_offset, program.implementation_key);
    string_offset =
        checked_add(string_offset, program.implementation_key.size(),
                    "ActorBehaviorSceneV1 string partition");
    write_u64(result, record + kProgramRigKeyOffsetOffset, string_offset);
    write_string(result, string_offset, program.required_rig_key);
    string_offset = checked_add(string_offset, program.required_rig_key.size(),
                                "ActorBehaviorSceneV1 string partition");
    write_u64(result, record + kProgramModelKeyOffsetOffset, string_offset);
    write_string(result, string_offset, program.required_model_key);
    string_offset =
        checked_add(string_offset, program.required_model_key.size(),
                    "ActorBehaviorSceneV1 string partition");
    write_digest(result, record + kProgramRigDigestOffset,
                 program.required_rig_sha256);
    write_digest(result, record + kProgramModelDigestOffset,
                 program.required_model_sha256);
    write_digest(result, record + kProgramLayoutDigestOffset,
                 program.state_layout_sha256);
    write_u32(result, record + kProgramFirstFieldOffset, first_field);
    write_u32(result, record + kProgramFieldCountOffset, field_count);
    write_u32(result, record + kProgramFirstAnimationImportOffset,
              first_animation);
    write_u32(result, record + kProgramAnimationImportCountOffset,
              animation_count);
    write_u32(result, record + kProgramFirstRandomImportOffset,
              first_random_import);
    write_u32(result, record + kProgramRandomImportCountOffset, random_count);
    write_u32(result, record + kProgramFieldElementCountOffset,
              program_value_count(program));
    write_u32(result, record + kProgramInstanceRandomWordCountOffset,
              program_instance_random_word_count(program));

    for (std::size_t local = 0U; local < program.fields.size(); ++local) {
      const auto flat = static_cast<std::uint64_t>(first_field) + local;
      const auto field_record =
          layout.fields + flat * kActorBehaviorSceneIoFieldBytesV1;
      const auto &field = program.fields[local];
      write_u32(result, field_record + kFieldProgramIdOffset, program.id);
      write_u32(result, field_record + kFieldIdOffset, field.id);
      write_u32(result, field_record + kFieldTypeOffset,
                static_cast<std::uint32_t>(field.value_type));
      write_u32(result, field_record + kFieldFlagsOffset, field.flags);
      write_u32(result, field_record + kFieldElementCountOffset,
                field.element_count);
      write_u32(result, field_record + kFieldKeyBytesOffset,
                format_count(field.semantic_key.size(), "Field key length"));
      write_u64(result, field_record + kFieldKeyOffsetOffset, string_offset);
      write_string(result, string_offset, field.semantic_key);
      string_offset = checked_add(string_offset, field.semantic_key.size(),
                                  "ActorBehaviorSceneV1 string partition");
    }

    for (std::size_t local = 0U; local < program.animation_imports.size();
         ++local) {
      const auto flat = static_cast<std::uint64_t>(first_animation) + local;
      const auto animation_record =
          layout.animations +
          flat * kActorBehaviorSceneIoAnimationImportBytesV1;
      const auto &animation = program.animation_imports[local];
      write_u32(result, animation_record + kAnimationProgramIdOffset,
                program.id);
      write_u32(result, animation_record + kAnimationIdOffset, animation.id);
      write_u32(result, animation_record + kAnimationFlagsOffset,
                animation.flags);
      write_u32(result, animation_record + kAnimationBindingKeyBytesOffset,
                format_count(animation.binding_key.size(),
                             "Animation binding-key length"));
      write_u32(
          result, animation_record + kAnimationClipKeyBytesOffset,
          format_count(animation.clip_key.size(), "Animation clip-key length"));
      write_u64(result, animation_record + kAnimationBindingKeyOffsetOffset,
                string_offset);
      write_string(result, string_offset, animation.binding_key);
      string_offset = checked_add(string_offset, animation.binding_key.size(),
                                  "ActorBehaviorSceneV1 string partition");
      write_u64(result, animation_record + kAnimationClipKeyOffsetOffset,
                string_offset);
      write_string(result, string_offset, animation.clip_key);
      string_offset = checked_add(string_offset, animation.clip_key.size(),
                                  "ActorBehaviorSceneV1 string partition");
      write_digest(result, animation_record + kAnimationClipDigestOffset,
                   animation.required_clip_sha256);
      write_u32(result, animation_record + kAnimationFrameCountOffset,
                animation.required_frame_count);
    }

    for (std::size_t local = 0U; local < program.random_imports.size();
         ++local) {
      const auto flat = static_cast<std::uint64_t>(first_random_import) + local;
      const auto random_record =
          layout.random_imports +
          flat * kActorBehaviorSceneIoRandomImportBytesV1;
      const auto &random = program.random_imports[local];
      write_u32(result, random_record + kRandomImportProgramIdOffset,
                program.id);
      write_u32(result, random_record + kRandomImportIdOffset, random.id);
      write_u32(result, random_record + kRandomImportScopeOffset,
                static_cast<std::uint32_t>(random.scope));
      write_u32(result, random_record + kRandomImportFlagsOffset, random.flags);
      write_u32(result, random_record + kRandomImportStateWordCountOffset,
                random.state_word_count);
      write_u32(result, random_record + kRandomImportAlgorithmAbiOffset,
                random.algorithm_abi_version);
      write_u32(
          result, random_record + kRandomImportBindingKeyBytesOffset,
          format_count(random.binding_key.size(), "Random binding-key length"));
      write_u32(
          result, random_record + kRandomImportStreamKeyBytesOffset,
          format_count(random.stream_key.size(), "Random stream-key length"));
      write_u32(result, random_record + kRandomImportAlgorithmKeyBytesOffset,
                format_count(random.algorithm_key.size(),
                             "Random algorithm-key length"));
      write_u64(result, random_record + kRandomImportBindingKeyOffsetOffset,
                string_offset);
      write_string(result, string_offset, random.binding_key);
      string_offset = checked_add(string_offset, random.binding_key.size(),
                                  "ActorBehaviorSceneV1 string partition");
      write_u64(result, random_record + kRandomImportStreamKeyOffsetOffset,
                string_offset);
      write_string(result, string_offset, random.stream_key);
      string_offset = checked_add(string_offset, random.stream_key.size(),
                                  "ActorBehaviorSceneV1 string partition");
      write_u64(result, random_record + kRandomImportAlgorithmKeyOffsetOffset,
                string_offset);
      write_string(result, string_offset, random.algorithm_key);
      string_offset = checked_add(string_offset, random.algorithm_key.size(),
                                  "ActorBehaviorSceneV1 string partition");
    }
    first_field =
        format_count(static_cast<std::uint64_t>(first_field) + field_count,
                     "Flattened field count");
    first_animation = format_count(static_cast<std::uint64_t>(first_animation) +
                                       animation_count,
                                   "Flattened animation-import count");
    first_random_import = format_count(
        static_cast<std::uint64_t>(first_random_import) + random_count,
        "Flattened random-import count");
  }

  std::uint32_t first_random_word = 0U;
  for (std::size_t index = 0U; index < canonical.random_streams.size();
       ++index) {
    const auto record = layout.random_streams +
                        index * kActorBehaviorSceneIoRandomStreamBytesV1;
    const auto &stream = canonical.random_streams[index];
    const auto word_count = format_count(stream.initial_state_words.size(),
                                         "Random-stream word count");
    write_u32(result, record + kRandomStreamIdOffset, stream.id);
    write_u32(result, record + kRandomStreamScopeOffset,
              static_cast<std::uint32_t>(stream.scope));
    write_u32(result, record + kRandomStreamFlagsOffset, stream.flags);
    write_u32(result, record + kRandomStreamAlgorithmAbiOffset,
              stream.algorithm_abi_version);
    write_u32(result, record + kRandomStreamFirstWordOffset, first_random_word);
    write_u32(result, record + kRandomStreamWordCountOffset, word_count);
    write_u32(result, record + kRandomStreamSemanticKeyBytesOffset,
              format_count(stream.semantic_key.size(), "Stream key length"));
    write_u32(result, record + kRandomStreamAlgorithmKeyBytesOffset,
              format_count(stream.algorithm_key.size(),
                           "Stream algorithm-key length"));
    write_u64(result, record + kRandomStreamSemanticKeyOffsetOffset,
              string_offset);
    write_string(result, string_offset, stream.semantic_key);
    string_offset = checked_add(string_offset, stream.semantic_key.size(),
                                "ActorBehaviorSceneV1 string partition");
    write_u64(result, record + kRandomStreamAlgorithmKeyOffsetOffset,
              string_offset);
    write_string(result, string_offset, stream.algorithm_key);
    string_offset = checked_add(string_offset, stream.algorithm_key.size(),
                                "ActorBehaviorSceneV1 string partition");
    for (std::size_t local = 0U; local < stream.initial_state_words.size();
         ++local) {
      const auto flat = static_cast<std::uint64_t>(first_random_word) + local;
      write_u32(result,
                layout.random_words +
                    flat * kActorBehaviorSceneIoRandomStateWordBytesV1,
                stream.initial_state_words[local]);
    }
    first_random_word =
        format_count(static_cast<std::uint64_t>(first_random_word) + word_count,
                     "Flattened random-word count");
  }

  std::uint32_t first_value = 0U;
  std::uint32_t first_initial_animation = 0U;
  for (std::size_t index = 0U; index < canonical.instances.size(); ++index) {
    const auto record =
        layout.instances + index * kActorBehaviorSceneIoInstanceBytesV1;
    const auto &instance = canonical.instances[index];
    const auto value_count =
        format_count(instance.initial_values.size(), "Instance value count");
    const auto word_count = format_count(instance.initial_random_words.size(),
                                         "Instance random-word count");
    const auto initial_animation_count = format_count(
        instance.initial_animations.size(), "Instance initial-animation count");
    write_u32(result, record + kInstanceAuthoredIdOffset, instance.authored_id);
    write_u32(result, record + kInstanceProgramIdOffset, instance.program_id);
    write_u32(result, record + kInstanceInitialStateOffset,
              instance.initial_state_id);
    write_u32(result, record + kInstanceFlagsOffset, instance.flags);
    write_u32(result, record + kInstanceFirstValueOffset, first_value);
    write_u32(result, record + kInstanceValueCountOffset, value_count);
    write_u32(result, record + kInstanceFirstRandomWordOffset,
              first_random_word);
    write_u32(result, record + kInstanceRandomWordCountOffset, word_count);
    write_u32(result, record + kInstanceFirstAnimationOffset,
              first_initial_animation);
    write_u32(result, record + kInstanceAnimationCountOffset,
              initial_animation_count);
    for (std::size_t local = 0U; local < instance.initial_values.size();
         ++local) {
      const auto flat = static_cast<std::uint64_t>(first_value) + local;
      write_value(result,
                  layout.values +
                      flat * kActorBehaviorSceneIoInitialValueBytesV1,
                  instance.initial_values[local]);
    }
    for (std::size_t local = 0U; local < instance.initial_random_words.size();
         ++local) {
      const auto flat = static_cast<std::uint64_t>(first_random_word) + local;
      write_u32(result,
                layout.random_words +
                    flat * kActorBehaviorSceneIoRandomStateWordBytesV1,
                instance.initial_random_words[local]);
    }
    for (std::size_t local = 0U; local < instance.initial_animations.size();
         ++local) {
      const auto flat =
          static_cast<std::uint64_t>(first_initial_animation) + local;
      const auto animation_record =
          layout.initial_animations +
          flat * kActorBehaviorSceneIoInitialAnimationBytesV1;
      const auto &animation = instance.initial_animations[local];
      write_u32(result, animation_record + kInitialAnimationChannelIdOffset,
                animation.channel_id);
      write_u32(result, animation_record + kInitialAnimationImportIdOffset,
                animation.animation_import_id);
      write_u32(result, animation_record + kInitialAnimationFirstFrameOffset,
                animation.first_frame_index);
      write_u32(result, animation_record + kInitialAnimationFlagsOffset,
                animation.flags);
    }
    first_value =
        format_count(static_cast<std::uint64_t>(first_value) + value_count,
                     "Flattened initial-value count");
    first_random_word =
        format_count(static_cast<std::uint64_t>(first_random_word) + word_count,
                     "Flattened random-word count");
    first_initial_animation =
        format_count(static_cast<std::uint64_t>(first_initial_animation) +
                         initial_animation_count,
                     "Flattened initial-animation count");
  }
  if (first_field != counts.fields || first_animation != counts.animations ||
      first_random_import != counts.random_imports ||
      first_value != counts.values ||
      first_random_word != counts.random_words ||
      first_initial_animation != counts.initial_animations ||
      string_offset != layout.total) {
    fail("ActorBehaviorSceneV1 internal table partition is inconsistent");
  }
  return result;
}

ActorBehaviorSceneV1
decode_actor_behavior_scene_v1(const std::span<const std::byte> bytes,
                               const ActorBehaviorSceneIoLimitsV1 limits) {
  validate_limits(limits);
  if (bytes.size() > limits.max_encoded_bytes) {
    fail("ActorBehaviorSceneV1 encoded bytes exceed the caller limit");
  }
  if (bytes.size() < kActorBehaviorSceneIoHeaderBytesV1) {
    fail("ActorBehaviorSceneV1 header is truncated");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    fail("ActorBehaviorSceneV1 magic is invalid");
  }
  if (read_u32(bytes, kFormatVersionOffset) !=
          kActorBehaviorSceneIoFormatVersionV1 ||
      read_u32(bytes, kPayloadTypeOffset) !=
          kActorBehaviorSceneIoPayloadTypeV1) {
    fail("ActorBehaviorSceneV1 format version or payload type is unknown");
  }
  if (read_u32(bytes, kHeaderBytesOffset) !=
          kActorBehaviorSceneIoHeaderBytesV1 ||
      read_u32(bytes, kSchemaVersionOffset) !=
          kActorBehaviorSceneSchemaVersionV1 ||
      read_u32(bytes, kHeaderFlagsOffset) != 0U ||
      read_u32(bytes, kHeaderReserved32Offset) != 0U ||
      read_u32(bytes, kProgramRecordBytesOffset) !=
          kActorBehaviorSceneIoProgramBytesV1 ||
      read_u32(bytes, kFieldRecordBytesOffset) !=
          kActorBehaviorSceneIoFieldBytesV1 ||
      read_u32(bytes, kAnimationImportRecordBytesOffset) !=
          kActorBehaviorSceneIoAnimationImportBytesV1 ||
      read_u32(bytes, kRandomImportRecordBytesOffset) !=
          kActorBehaviorSceneIoRandomImportBytesV1 ||
      read_u32(bytes, kRandomStreamRecordBytesOffset) !=
          kActorBehaviorSceneIoRandomStreamBytesV1 ||
      read_u32(bytes, kInstanceRecordBytesOffset) !=
          kActorBehaviorSceneIoInstanceBytesV1 ||
      read_u32(bytes, kInitialValueRecordBytesOffset) !=
          kActorBehaviorSceneIoInitialValueBytesV1 ||
      read_u32(bytes, kRandomWordRecordBytesOffset) !=
          kActorBehaviorSceneIoRandomStateWordBytesV1 ||
      read_u32(bytes, kInitialAnimationRecordBytesOffset) !=
          kActorBehaviorSceneIoInitialAnimationBytesV1 ||
      read_u32(bytes, kHeaderReservedRecordOffset) != 0U ||
      !all_zero(bytes, kHeaderReservedOffset,
                kActorBehaviorSceneIoHeaderBytesV1)) {
    fail("ActorBehaviorSceneV1 header schema, record sizes, flags, or reserved "
         "data are invalid");
  }

  Counts counts;
  counts.programs = read_u32(bytes, kProgramCountOffset);
  counts.fields = read_u32(bytes, kFieldCountOffset);
  counts.animations = read_u32(bytes, kAnimationImportCountOffset);
  counts.random_imports = read_u32(bytes, kRandomImportCountOffset);
  counts.random_streams = read_u32(bytes, kRandomStreamCountOffset);
  counts.instances = read_u32(bytes, kInstanceCountOffset);
  counts.values = read_u32(bytes, kInitialValueCountOffset);
  counts.random_words = read_u32(bytes, kRandomWordCountOffset);
  counts.initial_animations = read_u32(bytes, kInitialAnimationCountOffset);
  counts.strings = read_u64(bytes, kStringBytesOffset);
  if (counts.programs > limits.scene.max_programs ||
      counts.fields > limits.scene.max_total_fields ||
      counts.animations > limits.scene.max_total_animation_imports ||
      counts.random_imports > limits.scene.max_total_random_imports ||
      counts.random_streams > limits.scene.max_random_streams ||
      counts.instances > limits.scene.max_instances ||
      counts.values > limits.scene.max_total_initial_values ||
      counts.random_words > random_word_limit(limits.scene) ||
      counts.initial_animations > limits.scene.max_total_initial_animations ||
      counts.strings > limits.scene.max_total_semantic_key_bytes) {
    fail("ActorBehaviorSceneV1 header exceeds a caller limit");
  }

  const auto layout = canonical_layout(counts);
  if (read_u64(bytes, kProgramTableOffset) != layout.programs ||
      read_u64(bytes, kFieldTableOffset) != layout.fields ||
      read_u64(bytes, kAnimationImportTableOffset) != layout.animations ||
      read_u64(bytes, kRandomImportTableOffset) != layout.random_imports ||
      read_u64(bytes, kRandomStreamTableOffset) != layout.random_streams ||
      read_u64(bytes, kInstanceTableOffset) != layout.instances ||
      read_u64(bytes, kInitialValueTableOffset) != layout.values ||
      read_u64(bytes, kRandomWordTableOffset) != layout.random_words ||
      read_u64(bytes, kInitialAnimationTableOffset) !=
          layout.initial_animations ||
      read_u64(bytes, kStringDataOffset) != layout.strings ||
      read_u64(bytes, kTotalBytesOffset) != layout.total ||
      layout.total != bytes.size()) {
    fail("ActorBehaviorSceneV1 table layout or exact byte size is invalid");
  }

  ActorBehaviorSceneV1 result;
  result.schema_version = read_u32(bytes, kSchemaVersionOffset);
  result.level_id = read_u32(bytes, kLevelIdOffset);
  result.programs.reserve(host_size(counts.programs, result.programs.max_size(),
                                    "Behavior program count"));
  auto expected_string_offset = layout.strings;
  std::uint64_t expected_field = 0U;
  std::uint64_t expected_animation = 0U;
  std::uint64_t expected_random_import = 0U;
  for (std::uint32_t program_index = 0U; program_index < counts.programs;
       ++program_index) {
    const auto record =
        layout.programs + static_cast<std::uint64_t>(program_index) *
                              kActorBehaviorSceneIoProgramBytesV1;
    if (read_u32(bytes, record + kProgramReservedOffset) != 0U ||
        read_u32(bytes, record + kProgramModelReservedOffset) != 0U) {
      fail("ActorBehaviorSceneV1 program reserved data is non-zero");
    }
    const auto first_field = read_u32(bytes, record + kProgramFirstFieldOffset);
    const auto field_count = read_u32(bytes, record + kProgramFieldCountOffset);
    const auto first_animation =
        read_u32(bytes, record + kProgramFirstAnimationImportOffset);
    const auto animation_count =
        read_u32(bytes, record + kProgramAnimationImportCountOffset);
    const auto first_random =
        read_u32(bytes, record + kProgramFirstRandomImportOffset);
    const auto random_count =
        read_u32(bytes, record + kProgramRandomImportCountOffset);
    if (first_field != expected_field ||
        first_animation != expected_animation ||
        first_random != expected_random_import ||
        expected_field > counts.fields ||
        expected_animation > counts.animations ||
        expected_random_import > counts.random_imports ||
        field_count > limits.scene.max_fields_per_program ||
        animation_count > limits.scene.max_animation_imports_per_program ||
        random_count > limits.scene.max_random_imports_per_program ||
        field_count > counts.fields - expected_field ||
        animation_count > counts.animations - expected_animation ||
        random_count > counts.random_imports - expected_random_import) {
      fail("ActorBehaviorSceneV1 program ranges are not a complete canonical "
           "partition");
    }

    ActorBehaviorProgramV1 program;
    program.id = read_u32(bytes, record + kProgramIdOffset);
    program.implementation_abi_version =
        read_u32(bytes, record + kProgramAbiOffset);
    program.state_count = read_u32(bytes, record + kProgramStateCountOffset);
    program.source_updates_per_second =
        read_u32(bytes, record + kProgramSourceUpdatesPerSecondOffset);
    if (program.source_updates_per_second == 0U ||
        program.source_updates_per_second >
            limits.scene.max_source_updates_per_second) {
      fail("ActorBehaviorSceneV1 program source cadence exceeds its contract "
           "or caller limit");
    }
    program.animation_channel_count =
        read_u32(bytes, record + kProgramAnimationChannelCountOffset);
    program.flags = read_u32(bytes, record + kProgramFlagsOffset);
    const auto semantic_length =
        read_u32(bytes, record + kProgramSemanticKeyBytesOffset);
    const auto implementation_length =
        read_u32(bytes, record + kProgramImplementationKeyBytesOffset);
    const auto rig_length = read_u32(bytes, record + kProgramRigKeyBytesOffset);
    const auto model_length =
        read_u32(bytes, record + kProgramModelKeyBytesOffset);
    const auto semantic_offset =
        read_u64(bytes, record + kProgramSemanticKeyOffsetOffset);
    const auto implementation_offset =
        read_u64(bytes, record + kProgramImplementationKeyOffsetOffset);
    const auto rig_offset =
        read_u64(bytes, record + kProgramRigKeyOffsetOffset);
    const auto model_offset =
        read_u64(bytes, record + kProgramModelKeyOffsetOffset);
    require_partition(semantic_offset, semantic_length, expected_string_offset,
                      layout.total, "A program semantic key");
    program.semantic_key = read_string(bytes, semantic_offset, semantic_length,
                                       limits.scene.max_semantic_key_bytes,
                                       "A program semantic key");
    require_partition(implementation_offset, implementation_length,
                      expected_string_offset, layout.total,
                      "A program implementation key");
    program.implementation_key = read_string(
        bytes, implementation_offset, implementation_length,
        limits.scene.max_semantic_key_bytes, "A program implementation key");
    require_partition(rig_offset, rig_length, expected_string_offset,
                      layout.total, "A required actor rig key");
    program.required_rig_key = read_string(bytes, rig_offset, rig_length,
                                           limits.scene.max_semantic_key_bytes,
                                           "A required actor rig key");
    require_partition(model_offset, model_length, expected_string_offset,
                      layout.total, "A required actor model key");
    program.required_model_key = read_string(
        bytes, model_offset, model_length, limits.scene.max_semantic_key_bytes,
        "A required actor model key");
    program.required_rig_sha256 =
        read_digest(bytes, record + kProgramRigDigestOffset);
    program.required_model_sha256 =
        read_digest(bytes, record + kProgramModelDigestOffset);
    program.state_layout_sha256 =
        read_digest(bytes, record + kProgramLayoutDigestOffset);
    program.fields.reserve(host_size(field_count, program.fields.max_size(),
                                     "Per-program field count"));
    program.animation_imports.reserve(
        host_size(animation_count, program.animation_imports.max_size(),
                  "Per-program animation-import count"));
    program.random_imports.reserve(
        host_size(random_count, program.random_imports.max_size(),
                  "Per-program random-import count"));

    std::uint64_t field_elements = 0U;
    for (std::uint32_t local = 0U; local < field_count; ++local) {
      const auto flat = expected_field + local;
      const auto field_record =
          layout.fields + flat * kActorBehaviorSceneIoFieldBytesV1;
      if (read_u32(bytes, field_record + kFieldProgramIdOffset) != program.id) {
        fail("ActorBehaviorSceneV1 field has a mismatched owner");
      }
      ActorBehaviorStateFieldV1 field;
      field.id = read_u32(bytes, field_record + kFieldIdOffset);
      field.value_type = static_cast<ActorBehaviorValueTypeV1>(
          read_u32(bytes, field_record + kFieldTypeOffset));
      field.flags = read_u32(bytes, field_record + kFieldFlagsOffset);
      field.element_count =
          read_u32(bytes, field_record + kFieldElementCountOffset);
      const auto key_length =
          read_u32(bytes, field_record + kFieldKeyBytesOffset);
      const auto key_offset =
          read_u64(bytes, field_record + kFieldKeyOffsetOffset);
      require_partition(key_offset, key_length, expected_string_offset,
                        layout.total, "A behavior field key");
      field.semantic_key = read_string(bytes, key_offset, key_length,
                                       limits.scene.max_semantic_key_bytes,
                                       "A behavior field key");
      field_elements = checked_add(field_elements, field.element_count,
                                   "Per-program field elements");
      program.fields.push_back(std::move(field));
    }
    if (field_elements !=
        read_u32(bytes, record + kProgramFieldElementCountOffset)) {
      fail("ActorBehaviorSceneV1 program field-element count is inconsistent");
    }

    for (std::uint32_t local = 0U; local < animation_count; ++local) {
      const auto flat = expected_animation + local;
      const auto animation_record =
          layout.animations +
          flat * kActorBehaviorSceneIoAnimationImportBytesV1;
      if (read_u32(bytes, animation_record + kAnimationProgramIdOffset) !=
              program.id ||
          read_u64(bytes, animation_record + kAnimationReserved64Offset) !=
              0U) {
        fail("ActorBehaviorSceneV1 animation import owner or reserved data is "
             "invalid");
      }
      ActorBehaviorAnimationImportV1 animation;
      animation.id = read_u32(bytes, animation_record + kAnimationIdOffset);
      animation.flags =
          read_u32(bytes, animation_record + kAnimationFlagsOffset);
      const auto binding_length =
          read_u32(bytes, animation_record + kAnimationBindingKeyBytesOffset);
      const auto clip_length =
          read_u32(bytes, animation_record + kAnimationClipKeyBytesOffset);
      const auto binding_offset =
          read_u64(bytes, animation_record + kAnimationBindingKeyOffsetOffset);
      const auto clip_offset =
          read_u64(bytes, animation_record + kAnimationClipKeyOffsetOffset);
      require_partition(binding_offset, binding_length, expected_string_offset,
                        layout.total, "An animation binding key");
      animation.binding_key = read_string(bytes, binding_offset, binding_length,
                                          limits.scene.max_semantic_key_bytes,
                                          "An animation binding key");
      require_partition(clip_offset, clip_length, expected_string_offset,
                        layout.total, "An animation clip key");
      animation.clip_key = read_string(bytes, clip_offset, clip_length,
                                       limits.scene.max_semantic_key_bytes,
                                       "An animation clip key");
      animation.required_clip_sha256 =
          read_digest(bytes, animation_record + kAnimationClipDigestOffset);
      animation.required_frame_count =
          read_u32(bytes, animation_record + kAnimationFrameCountOffset);
      program.animation_imports.push_back(std::move(animation));
    }

    std::uint64_t instance_random_words = 0U;
    for (std::uint32_t local = 0U; local < random_count; ++local) {
      const auto flat = expected_random_import + local;
      const auto random_record =
          layout.random_imports +
          flat * kActorBehaviorSceneIoRandomImportBytesV1;
      if (read_u32(bytes, random_record + kRandomImportProgramIdOffset) !=
              program.id ||
          read_u32(bytes, random_record + kRandomImportReservedOffset) != 0U) {
        fail("ActorBehaviorSceneV1 random import owner or reserved data is "
             "invalid");
      }
      ActorBehaviorRandomImportV1 random;
      random.id = read_u32(bytes, random_record + kRandomImportIdOffset);
      random.scope = static_cast<ActorBehaviorRandomScopeV1>(
          read_u32(bytes, random_record + kRandomImportScopeOffset));
      random.flags = read_u32(bytes, random_record + kRandomImportFlagsOffset);
      random.state_word_count =
          read_u32(bytes, random_record + kRandomImportStateWordCountOffset);
      random.algorithm_abi_version =
          read_u32(bytes, random_record + kRandomImportAlgorithmAbiOffset);
      const auto binding_length =
          read_u32(bytes, random_record + kRandomImportBindingKeyBytesOffset);
      const auto stream_length =
          read_u32(bytes, random_record + kRandomImportStreamKeyBytesOffset);
      const auto algorithm_length =
          read_u32(bytes, random_record + kRandomImportAlgorithmKeyBytesOffset);
      const auto binding_offset =
          read_u64(bytes, random_record + kRandomImportBindingKeyOffsetOffset);
      const auto stream_offset =
          read_u64(bytes, random_record + kRandomImportStreamKeyOffsetOffset);
      const auto algorithm_offset = read_u64(
          bytes, random_record + kRandomImportAlgorithmKeyOffsetOffset);
      require_partition(binding_offset, binding_length, expected_string_offset,
                        layout.total, "A random binding key");
      random.binding_key = read_string(bytes, binding_offset, binding_length,
                                       limits.scene.max_semantic_key_bytes,
                                       "A random binding key");
      require_partition(stream_offset, stream_length, expected_string_offset,
                        layout.total, "A random stream key");
      random.stream_key = read_string(bytes, stream_offset, stream_length,
                                      limits.scene.max_semantic_key_bytes,
                                      "A random stream key");
      require_partition(algorithm_offset, algorithm_length,
                        expected_string_offset, layout.total,
                        "A random algorithm key");
      random.algorithm_key = read_string(
          bytes, algorithm_offset, algorithm_length,
          limits.scene.max_semantic_key_bytes, "A random algorithm key");
      if (random.scope == ActorBehaviorRandomScopeV1::instance) {
        instance_random_words =
            checked_add(instance_random_words, random.state_word_count,
                        "Per-program instance random words");
      }
      program.random_imports.push_back(std::move(random));
    }
    if (instance_random_words !=
        read_u32(bytes, record + kProgramInstanceRandomWordCountOffset)) {
      fail("ActorBehaviorSceneV1 program instance random-word count is "
           "inconsistent");
    }
    expected_field += field_count;
    expected_animation += animation_count;
    expected_random_import += random_count;
    result.programs.push_back(std::move(program));
  }
  if (expected_field != counts.fields ||
      expected_animation != counts.animations ||
      expected_random_import != counts.random_imports) {
    fail("ActorBehaviorSceneV1 program tables are not fully consumed");
  }

  result.random_streams.reserve(host_size(counts.random_streams,
                                          result.random_streams.max_size(),
                                          "Random stream count"));
  std::uint64_t expected_random_word = 0U;
  for (std::uint32_t index = 0U; index < counts.random_streams; ++index) {
    const auto record =
        layout.random_streams + static_cast<std::uint64_t>(index) *
                                    kActorBehaviorSceneIoRandomStreamBytesV1;
    if (!all_zero(bytes, record + kRandomStreamReservedOffset,
                  record + kActorBehaviorSceneIoRandomStreamBytesV1)) {
      fail("ActorBehaviorSceneV1 random stream reserved data is non-zero");
    }
    const auto first_word =
        read_u32(bytes, record + kRandomStreamFirstWordOffset);
    const auto word_count =
        read_u32(bytes, record + kRandomStreamWordCountOffset);
    if (first_word != expected_random_word ||
        expected_random_word > counts.random_words ||
        word_count > limits.scene.max_random_state_words_per_stream ||
        word_count > counts.random_words - expected_random_word) {
      fail("ActorBehaviorSceneV1 random stream words are not a canonical "
           "partition");
    }
    ActorBehaviorRandomStreamV1 stream;
    stream.id = read_u32(bytes, record + kRandomStreamIdOffset);
    stream.scope = static_cast<ActorBehaviorRandomScopeV1>(
        read_u32(bytes, record + kRandomStreamScopeOffset));
    stream.flags = read_u32(bytes, record + kRandomStreamFlagsOffset);
    stream.algorithm_abi_version =
        read_u32(bytes, record + kRandomStreamAlgorithmAbiOffset);
    const auto semantic_length =
        read_u32(bytes, record + kRandomStreamSemanticKeyBytesOffset);
    const auto algorithm_length =
        read_u32(bytes, record + kRandomStreamAlgorithmKeyBytesOffset);
    const auto semantic_offset =
        read_u64(bytes, record + kRandomStreamSemanticKeyOffsetOffset);
    const auto algorithm_offset =
        read_u64(bytes, record + kRandomStreamAlgorithmKeyOffsetOffset);
    require_partition(semantic_offset, semantic_length, expected_string_offset,
                      layout.total, "A random stream key");
    stream.semantic_key =
        read_string(bytes, semantic_offset, semantic_length,
                    limits.scene.max_semantic_key_bytes, "A random stream key");
    require_partition(algorithm_offset, algorithm_length,
                      expected_string_offset, layout.total,
                      "A random algorithm key");
    stream.algorithm_key = read_string(
        bytes, algorithm_offset, algorithm_length,
        limits.scene.max_semantic_key_bytes, "A random algorithm key");
    stream.initial_state_words.reserve(
        host_size(word_count, stream.initial_state_words.max_size(),
                  "Random-stream state-word count"));
    for (std::uint32_t local = 0U; local < word_count; ++local) {
      const auto flat = expected_random_word + local;
      stream.initial_state_words.push_back(read_u32(
          bytes, layout.random_words +
                     flat * kActorBehaviorSceneIoRandomStateWordBytesV1));
    }
    expected_random_word += word_count;
    result.random_streams.push_back(std::move(stream));
  }

  result.instances.reserve(host_size(counts.instances,
                                     result.instances.max_size(),
                                     "Behavior instance count"));
  std::uint64_t expected_value = 0U;
  std::uint64_t expected_initial_animation = 0U;
  for (std::uint32_t index = 0U; index < counts.instances; ++index) {
    const auto record =
        layout.instances + static_cast<std::uint64_t>(index) *
                               kActorBehaviorSceneIoInstanceBytesV1;
    if (!all_zero(bytes, record + kInstanceReservedOffset,
                  record + kActorBehaviorSceneIoInstanceBytesV1)) {
      fail("ActorBehaviorSceneV1 instance reserved data is non-zero");
    }
    const auto first_value =
        read_u32(bytes, record + kInstanceFirstValueOffset);
    const auto value_count =
        read_u32(bytes, record + kInstanceValueCountOffset);
    const auto first_word =
        read_u32(bytes, record + kInstanceFirstRandomWordOffset);
    const auto word_count =
        read_u32(bytes, record + kInstanceRandomWordCountOffset);
    const auto first_animation =
        read_u32(bytes, record + kInstanceFirstAnimationOffset);
    const auto animation_count =
        read_u32(bytes, record + kInstanceAnimationCountOffset);
    if (first_value != expected_value || first_word != expected_random_word ||
        first_animation != expected_initial_animation ||
        expected_value > counts.values ||
        expected_random_word > counts.random_words ||
        expected_initial_animation > counts.initial_animations ||
        value_count > limits.scene.max_initial_values_per_instance ||
        word_count > limits.scene.max_initial_random_words_per_instance ||
        animation_count > limits.scene.max_initial_animations_per_instance ||
        value_count > counts.values - expected_value ||
        word_count > counts.random_words - expected_random_word ||
        animation_count >
            counts.initial_animations - expected_initial_animation) {
      fail("ActorBehaviorSceneV1 instance state is not a complete canonical "
           "partition");
    }
    ActorBehaviorInstanceV1 instance;
    instance.authored_id = read_u32(bytes, record + kInstanceAuthoredIdOffset);
    instance.program_id = read_u32(bytes, record + kInstanceProgramIdOffset);
    instance.initial_state_id =
        read_u32(bytes, record + kInstanceInitialStateOffset);
    instance.flags = read_u32(bytes, record + kInstanceFlagsOffset);
    instance.initial_values.reserve(
        host_size(value_count, instance.initial_values.max_size(),
                  "Per-instance initial-value count"));
    for (std::uint32_t local = 0U; local < value_count; ++local) {
      const auto flat = expected_value + local;
      instance.initial_values.push_back(read_value(
          bytes,
          layout.values + flat * kActorBehaviorSceneIoInitialValueBytesV1));
    }
    instance.initial_random_words.reserve(
        host_size(word_count, instance.initial_random_words.max_size(),
                  "Per-instance random-word count"));
    for (std::uint32_t local = 0U; local < word_count; ++local) {
      const auto flat = expected_random_word + local;
      instance.initial_random_words.push_back(read_u32(
          bytes, layout.random_words +
                     flat * kActorBehaviorSceneIoRandomStateWordBytesV1));
    }
    instance.initial_animations.reserve(
        host_size(animation_count, instance.initial_animations.max_size(),
                  "Per-instance initial-animation count"));
    for (std::uint32_t local = 0U; local < animation_count; ++local) {
      const auto flat = expected_initial_animation + local;
      const auto animation_record =
          layout.initial_animations +
          flat * kActorBehaviorSceneIoInitialAnimationBytesV1;
      instance.initial_animations.push_back(
          {read_u32(bytes, animation_record + kInitialAnimationChannelIdOffset),
           read_u32(bytes, animation_record + kInitialAnimationImportIdOffset),
           read_u32(bytes,
                    animation_record + kInitialAnimationFirstFrameOffset),
           read_u32(bytes, animation_record + kInitialAnimationFlagsOffset)});
    }
    expected_value += value_count;
    expected_random_word += word_count;
    expected_initial_animation += animation_count;
    result.instances.push_back(std::move(instance));
  }
  if (expected_value != counts.values ||
      expected_random_word != counts.random_words ||
      expected_initial_animation != counts.initial_animations ||
      expected_string_offset != layout.total) {
    fail("ActorBehaviorSceneV1 records do not consume their declared table "
         "partitions");
  }

  try {
    validate_actor_behavior_scene_v1(result, limits.scene);
  } catch (const ActorBehaviorSceneError &error) {
    fail("Decoded ActorBehaviorSceneV1 is invalid: " +
         std::string(error.what()));
  }
  return result;
}

} // namespace openrc
