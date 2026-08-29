#include "openrc/wad_payload_structure.hpp"

#include "openrc/hash.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);

enum class WordShapeV1 : std::uint8_t {
    zero = 0U,
    all_ones = 1U,
    printable_fourcc = 2U,
    repeated_byte = 3U,
    aligned_offset_16_forward = 4U,
    aligned_offset_16_backward = 5U,
    aligned_offset_4_forward = 6U,
    aligned_offset_4_backward = 7U,
    small_unsigned_8 = 8U,
    small_unsigned_16 = 9U,
    small_negative = 10U,
    finite_float = 11U,
    ee_address_like = 12U,
    sparse_bits = 13U,
    other = 14U,
    missing = 255U,
};

[[nodiscard]] std::uint8_t byte_value(const std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t read_le32(
    const std::span<const std::byte> bytes,
    const std::size_t offset) {
    return static_cast<std::uint32_t>(byte_value(bytes[offset])) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 1U])) << 8U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 2U])) << 16U) |
        (static_cast<std::uint32_t>(byte_value(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] bool is_printable_ascii(const std::uint8_t value) {
    return value >= 0x20U && value <= 0x7eU;
}

[[nodiscard]] bool is_printable_fourcc(const std::uint32_t word) {
    return is_printable_ascii(static_cast<std::uint8_t>(word & 0xffU)) &&
        is_printable_ascii(static_cast<std::uint8_t>((word >> 8U) & 0xffU)) &&
        is_printable_ascii(static_cast<std::uint8_t>((word >> 16U) & 0xffU)) &&
        is_printable_ascii(static_cast<std::uint8_t>((word >> 24U) & 0xffU));
}

[[nodiscard]] WordShapeV1 classify_word(
    const std::uint32_t word,
    const std::uint64_t input_bytes,
    const std::uint64_t word_offset) {
    if (word == 0U) {
        return WordShapeV1::zero;
    }
    if (word == std::numeric_limits<std::uint32_t>::max()) {
        return WordShapeV1::all_ones;
    }
    if (is_printable_fourcc(word)) {
        return WordShapeV1::printable_fourcc;
    }

    const auto low = static_cast<std::uint8_t>(word & 0xffU);
    if (static_cast<std::uint8_t>((word >> 8U) & 0xffU) == low &&
        static_cast<std::uint8_t>((word >> 16U) & 0xffU) == low &&
        static_cast<std::uint8_t>((word >> 24U) & 0xffU) == low) {
        return WordShapeV1::repeated_byte;
    }

    const auto word64 = static_cast<std::uint64_t>(word);
    if (word64 >= 0x10U && word64 < input_bytes) {
        if ((word & 0x0fU) == 0U) {
            return word64 >= word_offset
                ? WordShapeV1::aligned_offset_16_forward
                : WordShapeV1::aligned_offset_16_backward;
        }
        if ((word & 0x03U) == 0U) {
            return word64 >= word_offset
                ? WordShapeV1::aligned_offset_4_forward
                : WordShapeV1::aligned_offset_4_backward;
        }
    }
    if (word <= 0xffU) {
        return WordShapeV1::small_unsigned_8;
    }
    if (word <= 0xffffU) {
        return WordShapeV1::small_unsigned_16;
    }
    const auto signed_word = static_cast<std::int32_t>(word);
    if (signed_word < 0 && signed_word >= -65536) {
        return WordShapeV1::small_negative;
    }

    const auto exponent = static_cast<std::uint8_t>((word >> 23U) & 0xffU);
    if (exponent != 0U && exponent != 0xffU) {
        const auto value = std::bit_cast<float>(word);
        const auto magnitude = value < 0.0F ? -value : value;
        if (magnitude >= 1.0e-12F && magnitude <= 1.0e12F) {
            return WordShapeV1::finite_float;
        }
    }
    if (word >= 0x00100000U && word < 0x02000000U &&
        (word & 0x03U) == 0U) {
        return WordShapeV1::ee_address_like;
    }
    if (std::popcount(word) <= 4) {
        return WordShapeV1::sparse_bits;
    }
    return WordShapeV1::other;
}

[[nodiscard]] std::uint8_t logarithmic_size_bucket(
    const std::uint64_t size) {
    if (size == 0U) {
        return 0U;
    }
    return static_cast<std::uint8_t>(
        1U + (63U - std::countl_zero(size)));
}

[[nodiscard]] std::uint8_t size_alignment_bucket(
    const std::uint64_t size) {
    if (size == 0U) {
        return 0xffU;
    }
    return static_cast<std::uint8_t>(
        std::min<unsigned int>(std::countr_zero(size), 31U));
}

[[nodiscard]] std::uint16_t scaled_permille(
    const std::uint64_t part,
    const std::uint64_t whole) {
    if (whole == 0U || part == 0U) {
        return 0U;
    }
    if (part > whole) {
        throw WadPayloadStructureError(
            "A WadV1 structure byte count exceeds its input size");
    }
    if (part <= std::numeric_limits<std::uint64_t>::max() / 1000U) {
        return static_cast<std::uint16_t>((part * 1000U) / whole);
    }

    std::uint64_t remainder = 0U;
    std::uint16_t result = 0U;
    for (std::uint16_t iteration = 0U; iteration < 1000U; ++iteration) {
        if (part >= whole - remainder) {
            remainder = part - (whole - remainder);
            ++result;
        } else {
            remainder += part;
        }
    }
    return result;
}

void append_u32_le(
    std::vector<std::byte>& output,
    const std::uint32_t value) {
    output.push_back(static_cast<std::byte>(value & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

[[nodiscard]] std::string prefix_hex(
    const std::span<const std::byte> bytes) {
    constexpr char kHex[] = "0123456789abcdef";
    const auto prefix_bytes = std::min(
        bytes.size(),
        kWadPayloadStructurePrefixBytesV1);
    std::string result;
    result.reserve(prefix_bytes * 2U);
    for (std::size_t index = 0U; index < prefix_bytes; ++index) {
        const auto value = byte_value(bytes[index]);
        result.push_back(kHex[value >> 4U]);
        result.push_back(kHex[value & 0x0fU]);
    }
    return result;
}

} // namespace

WadPayloadStructureFingerprintV1 fingerprint_wad_payload_structure_v1(
    const std::span<const std::byte> decoded_bytes,
    const WadPayloadStructureLimitsV1 limits) {
    if (limits.max_input_bytes == 0U || limits.max_sampled_bytes == 0U) {
        throw WadPayloadStructureError(
            "The WadV1 structure input and sampling limits must be non-zero");
    }
    if (decoded_bytes.size() > limits.max_input_bytes) {
        throw WadPayloadStructureError(
            "The decoded WadV1 payload exceeds the structure input limit");
    }

    const auto input_bytes = static_cast<std::uint64_t>(decoded_bytes.size());
    const auto size_bucket = logarithmic_size_bucket(input_bytes);
    const auto alignment_bucket = size_alignment_bucket(input_bytes);
    const auto available_words = std::min<std::size_t>(
        decoded_bytes.size() / sizeof(std::uint32_t),
        kWadPayloadStructureHeaderWordsV1);

    const auto sampled_bytes = std::min(
        decoded_bytes.size(),
        static_cast<std::size_t>(limits.max_sampled_bytes));
    std::uint64_t zero_bytes = 0U;
    std::uint64_t ff_bytes = 0U;
    std::uint64_t printable_bytes = 0U;
    std::uint64_t high_bit_bytes = 0U;
    const auto evenly_spaced =
        sampled_bytes > 1U && sampled_bytes != decoded_bytes.size();
    const auto sample_divisor = evenly_spaced ? sampled_bytes - 1U : 1U;
    const auto sample_span = evenly_spaced ? decoded_bytes.size() - 1U : 0U;
    const auto whole_sample_step = sample_span / sample_divisor;
    const auto remainder_sample_step = sample_span % sample_divisor;
    std::size_t remainder_accumulator = 0U;
    std::size_t byte_index = 0U;
    for (std::size_t sample = 0U; sample < sampled_bytes; ++sample) {
        if (!evenly_spaced) {
            byte_index = sample;
        }
        const auto value = byte_value(decoded_bytes[byte_index]);
        zero_bytes += value == 0U ? 1U : 0U;
        ff_bytes += value == 0xffU ? 1U : 0U;
        printable_bytes += is_printable_ascii(value) ? 1U : 0U;
        high_bit_bytes += (value & 0x80U) != 0U ? 1U : 0U;
        if (evenly_spaced && sample + 1U < sampled_bytes) {
            byte_index += whole_sample_step;
            if (remainder_sample_step >=
                sample_divisor - remainder_accumulator) {
                remainder_accumulator = remainder_sample_step -
                    (sample_divisor - remainder_accumulator);
                ++byte_index;
            } else {
                remainder_accumulator += remainder_sample_step;
            }
        }
    }

    std::vector<std::byte> features;
    features.reserve(16U + kWadPayloadStructureHeaderWordsV1);
    constexpr std::array<std::uint8_t, 8> kDomain{
        'O', 'R', 'C', 'W', 'F', 'A', 'M', '1'};
    for (const auto byte : kDomain) {
        features.push_back(static_cast<std::byte>(byte));
    }
    features.push_back(static_cast<std::byte>(size_bucket));
    features.push_back(static_cast<std::byte>(
        alignment_bucket == 0xffU
            ? 0xffU
            : std::min<std::uint8_t>(alignment_bucket, std::uint8_t{4U})));

    const auto has_printable_signature = decoded_bytes.size() >= 4U &&
        std::all_of(
            decoded_bytes.begin(),
            decoded_bytes.begin() + 4,
            [](const std::byte value) {
                return is_printable_ascii(byte_value(value));
            });
    features.push_back(
        static_cast<std::byte>(has_printable_signature ? 1U : 0U));
    if (has_printable_signature) {
        features.insert(
            features.end(),
            decoded_bytes.begin(),
            decoded_bytes.begin() + 4);
    } else {
        append_u32_le(features, 0U);
    }

    for (std::size_t word_index = 0U;
         word_index < kWadPayloadStructureHeaderWordsV1;
         ++word_index) {
        const auto shape = word_index < available_words
            ? classify_word(
                  read_le32(decoded_bytes, word_index * sizeof(std::uint32_t)),
                  input_bytes,
                  word_index * sizeof(std::uint32_t))
            : WordShapeV1::missing;
        features.push_back(static_cast<std::byte>(shape));
    }
    if (features.size() != kWadPayloadStructureCanonicalKeyBytesV1) {
        throw WadPayloadStructureError(
            "The WadV1 structural fingerprint has an invalid key length");
    }

    Sha256 hash;
    hash.update(features);
    const auto shape_sha256 = hex_digest(hash.finish());
    return WadPayloadStructureFingerprintV1{
        kWadPayloadStructureFingerprintVersionV1,
        shape_sha256,
        std::move(features),
        input_bytes,
        prefix_hex(decoded_bytes),
        size_bucket,
        alignment_bucket,
        static_cast<std::uint16_t>(available_words),
        static_cast<std::uint32_t>(sampled_bytes),
        scaled_permille(zero_bytes, sampled_bytes),
        scaled_permille(ff_bytes, sampled_bytes),
        scaled_permille(printable_bytes, sampled_bytes),
        scaled_permille(high_bit_bytes, sampled_bytes),
    };
}

} // namespace openrc
