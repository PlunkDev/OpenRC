#include "openrc/dvp_vu.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint32_t kUpperReservedMask = 0x06000000U;
constexpr std::uint32_t kDestinationMask = 0x01e00000U;
constexpr std::uint32_t kTargetRegisterMask = 0x001f0000U;
constexpr std::uint32_t kSourceRegisterMask = 0x0000f800U;
constexpr std::uint32_t kOpcode4Mask = 0x0000003cU;
constexpr std::uint32_t kOpcode6Mask = 0x0000003fU;
constexpr std::uint32_t kOpcode9Mask = 0x000007fcU;
constexpr std::uint32_t kOpcode11Mask = 0x000007ffU;
constexpr std::uint32_t kLowerOpcode7Mask = 0xfe000000U;
constexpr std::uint32_t kLowerBit24Mask = 0x01000000U;
constexpr std::uint32_t kLowerImmediate12UnusedMask = 0x01c00000U;
constexpr std::uint32_t kUpperImmediateFlag = 0x80000000U;
constexpr std::uint32_t kUpperEndFlag = 0x40000000U;
constexpr std::uint32_t kUpperMFlag = 0x20000000U;
constexpr std::uint32_t kUpperDFlag = 0x10000000U;
constexpr std::uint32_t kUpperTFlag = 0x08000000U;
constexpr std::uint64_t kInvalidIndex =
    std::numeric_limits<std::uint64_t>::max();

struct PendingChunk {
    DvpVuCodeChunkV1 chunk;
    std::uint32_t virtual_end = 0;
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

[[nodiscard]] bool matches(
    const std::uint32_t word,
    const std::uint32_t mask,
    const std::uint32_t value) {
    return (word & mask) == value;
}

[[nodiscard]] DvpVuComponent component(const std::uint8_t raw) {
    switch (raw & 0x03U) {
    case 0U:
        return DvpVuComponent::x;
    case 1U:
        return DvpVuComponent::y;
    case 2U:
        return DvpVuComponent::z;
    case 3U:
        return DvpVuComponent::w;
    default:
        return DvpVuComponent::none;
    }
}

[[nodiscard]] std::int32_t sign_extend(
    const std::uint32_t value,
    const std::uint32_t bits) {
    const auto sign_bit = std::uint32_t{1U} << (bits - 1U);
    const auto mask = (std::uint32_t{1U} << bits) - 1U;
    const auto bounded = value & mask;
    return static_cast<std::int32_t>((bounded ^ sign_bit) - sign_bit);
}

[[nodiscard]] DvpVuUpperInstructionV1 decode_upper(
    const std::uint32_t word) {
    DvpVuUpperInstructionV1 result;
    result.raw_flags = static_cast<std::uint8_t>((word >> 27U) & 0x1fU);
    result.destination_mask =
        static_cast<std::uint8_t>((word >> 21U) & 0x0fU);
    result.ft = static_cast<std::uint8_t>((word >> 16U) & 0x1fU);
    result.fs = static_cast<std::uint8_t>((word >> 11U) & 0x1fU);
    result.fd = static_cast<std::uint8_t>((word >> 6U) & 0x1fU);
    result.immediate = (word & kUpperImmediateFlag) != 0U;
    result.end = (word & kUpperEndFlag) != 0U;
    result.m = (word & kUpperMFlag) != 0U;
    result.d = (word & kUpperDFlag) != 0U;
    result.t = (word & kUpperTFlag) != 0U;

    const auto set = [&result](
                         const DvpVuUpperOpcode opcode,
                         const DvpVuUpperOperandMode mode) {
        result.opcode = opcode;
        result.operand_mode = mode;
    };
    const auto set_broadcast = [&result, word](
                                   const DvpVuUpperOpcode opcode) {
        result.opcode = opcode;
        result.operand_mode = DvpVuUpperOperandMode::broadcast;
        result.broadcast_component =
            component(static_cast<std::uint8_t>(word & 0x03U));
    };

    const auto opcode11_mask = kUpperReservedMask + kOpcode11Mask;
    const auto scalar_mask =
        kUpperReservedMask + kTargetRegisterMask + kOpcode6Mask;

    if (matches(word, 0x07ffffffU, 0x000002ffU)) {
        set(DvpVuUpperOpcode::nop, DvpVuUpperOperandMode::none);
    } else if (matches(word, opcode11_mask, 0x000001fdU)) {
        set(DvpVuUpperOpcode::abs, DvpVuUpperOperandMode::vector);
    } else if (matches(word, opcode11_mask, 0x0000017cU)) {
        set(DvpVuUpperOpcode::ftoi0, DvpVuUpperOperandMode::vector);
    } else if (matches(word, opcode11_mask, 0x0000017dU)) {
        set(DvpVuUpperOpcode::ftoi4, DvpVuUpperOperandMode::vector);
    } else if (matches(word, opcode11_mask, 0x0000017eU)) {
        set(DvpVuUpperOpcode::ftoi12, DvpVuUpperOperandMode::vector);
    } else if (matches(word, opcode11_mask, 0x0000017fU)) {
        set(DvpVuUpperOpcode::ftoi15, DvpVuUpperOperandMode::vector);
    } else if (matches(word, opcode11_mask, 0x0000013cU)) {
        set(DvpVuUpperOpcode::itof0, DvpVuUpperOperandMode::vector);
    } else if (matches(word, opcode11_mask, 0x0000013dU)) {
        set(DvpVuUpperOpcode::itof4, DvpVuUpperOperandMode::vector);
    } else if (matches(word, opcode11_mask, 0x0000013eU)) {
        set(DvpVuUpperOpcode::itof12, DvpVuUpperOperandMode::vector);
    } else if (matches(word, opcode11_mask, 0x0000013fU)) {
        set(DvpVuUpperOpcode::itof15, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word,
                   kUpperReservedMask + kDestinationMask + kOpcode11Mask,
                   0x01c001ffU)) {
        set(DvpVuUpperOpcode::clip, DvpVuUpperOperandMode::broadcast);
        result.broadcast_component = DvpVuComponent::w;
    } else if (matches(word, opcode11_mask, 0x000002bcU)) {
        set(DvpVuUpperOpcode::adda, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word,
                   kUpperReservedMask + kTargetRegisterMask + kOpcode11Mask,
                   0x0000023eU)) {
        set(DvpVuUpperOpcode::addai, DvpVuUpperOperandMode::scalar_i);
    } else if (matches(
                   word,
                   kUpperReservedMask + kTargetRegisterMask + kOpcode11Mask,
                   0x0000023cU)) {
        set(DvpVuUpperOpcode::addaq, DvpVuUpperOperandMode::scalar_q);
    } else if (matches(word, opcode11_mask, 0x000002bdU)) {
        set(DvpVuUpperOpcode::madda, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word,
                   kUpperReservedMask + kTargetRegisterMask + kOpcode11Mask,
                   0x0000023fU)) {
        set(DvpVuUpperOpcode::maddai, DvpVuUpperOperandMode::scalar_i);
    } else if (matches(
                   word,
                   kUpperReservedMask + kTargetRegisterMask + kOpcode11Mask,
                   0x0000023dU)) {
        set(DvpVuUpperOpcode::maddaq, DvpVuUpperOperandMode::scalar_q);
    } else if (matches(word, opcode11_mask, 0x000002fdU)) {
        set(DvpVuUpperOpcode::msuba, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word,
                   kUpperReservedMask + kTargetRegisterMask + kOpcode11Mask,
                   0x0000027fU)) {
        set(DvpVuUpperOpcode::msubai, DvpVuUpperOperandMode::scalar_i);
    } else if (matches(
                   word,
                   kUpperReservedMask + kTargetRegisterMask + kOpcode11Mask,
                   0x0000027dU)) {
        set(DvpVuUpperOpcode::msubaq, DvpVuUpperOperandMode::scalar_q);
    } else if (matches(word, opcode11_mask, 0x000002beU)) {
        set(DvpVuUpperOpcode::mula, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word,
                   kUpperReservedMask + kTargetRegisterMask + kOpcode11Mask,
                   0x000001feU)) {
        set(DvpVuUpperOpcode::mulai, DvpVuUpperOperandMode::scalar_i);
    } else if (matches(
                   word,
                   kUpperReservedMask + kTargetRegisterMask + kOpcode11Mask,
                   0x000001fcU)) {
        set(DvpVuUpperOpcode::mulaq, DvpVuUpperOperandMode::scalar_q);
    } else if (matches(word, opcode11_mask, 0x000002feU)) {
        set(DvpVuUpperOpcode::opmula, DvpVuUpperOperandMode::vector);
    } else if (matches(word, opcode11_mask, 0x000002fcU)) {
        set(DvpVuUpperOpcode::suba, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word,
                   kUpperReservedMask + kTargetRegisterMask + kOpcode11Mask,
                   0x0000027eU)) {
        set(DvpVuUpperOpcode::subai, DvpVuUpperOperandMode::scalar_i);
    } else if (matches(
                   word,
                   kUpperReservedMask + kTargetRegisterMask + kOpcode11Mask,
                   0x0000027cU)) {
        set(DvpVuUpperOpcode::subaq, DvpVuUpperOperandMode::scalar_q);
    } else if (matches(word, scalar_mask, 0x00000022U)) {
        set(DvpVuUpperOpcode::addi, DvpVuUpperOperandMode::scalar_i);
    } else if (matches(word, scalar_mask, 0x00000020U)) {
        set(DvpVuUpperOpcode::addq, DvpVuUpperOperandMode::scalar_q);
    } else if (matches(word, scalar_mask, 0x00000023U)) {
        set(DvpVuUpperOpcode::maddi, DvpVuUpperOperandMode::scalar_i);
    } else if (matches(word, scalar_mask, 0x00000021U)) {
        set(DvpVuUpperOpcode::maddq, DvpVuUpperOperandMode::scalar_q);
    } else if (matches(word, scalar_mask, 0x0000001dU)) {
        set(DvpVuUpperOpcode::maxi, DvpVuUpperOperandMode::scalar_i);
    } else if (matches(word, scalar_mask, 0x0000001fU)) {
        set(DvpVuUpperOpcode::minii, DvpVuUpperOperandMode::scalar_i);
    } else if (matches(word, scalar_mask, 0x00000027U)) {
        set(DvpVuUpperOpcode::msubi, DvpVuUpperOperandMode::scalar_i);
    } else if (matches(word, scalar_mask, 0x00000025U)) {
        set(DvpVuUpperOpcode::msubq, DvpVuUpperOperandMode::scalar_q);
    } else if (matches(word, scalar_mask, 0x0000001eU)) {
        set(DvpVuUpperOpcode::muli, DvpVuUpperOperandMode::scalar_i);
    } else if (matches(word, scalar_mask, 0x0000001cU)) {
        set(DvpVuUpperOpcode::mulq, DvpVuUpperOperandMode::scalar_q);
    } else if (matches(word, scalar_mask, 0x00000026U)) {
        set(DvpVuUpperOpcode::subi, DvpVuUpperOperandMode::scalar_i);
    } else if (matches(word, scalar_mask, 0x00000024U)) {
        set(DvpVuUpperOpcode::subq, DvpVuUpperOperandMode::scalar_q);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode6Mask, 0x00000028U)) {
        set(DvpVuUpperOpcode::add, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode6Mask, 0x00000029U)) {
        set(DvpVuUpperOpcode::madd, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode6Mask, 0x0000002bU)) {
        set(DvpVuUpperOpcode::max, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode6Mask, 0x0000002fU)) {
        set(DvpVuUpperOpcode::mini, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode6Mask, 0x0000002dU)) {
        set(DvpVuUpperOpcode::msub, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode6Mask, 0x0000002aU)) {
        set(DvpVuUpperOpcode::mul, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode6Mask, 0x0000002eU)) {
        set(DvpVuUpperOpcode::opmsub, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode6Mask, 0x0000002cU)) {
        set(DvpVuUpperOpcode::sub, DvpVuUpperOperandMode::vector);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode9Mask, 0x0000003cU)) {
        set_broadcast(DvpVuUpperOpcode::adda);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode9Mask, 0x000000bcU)) {
        set_broadcast(DvpVuUpperOpcode::madda);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode9Mask, 0x000000fcU)) {
        set_broadcast(DvpVuUpperOpcode::msuba);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode9Mask, 0x000001bcU)) {
        set_broadcast(DvpVuUpperOpcode::mula);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode9Mask, 0x0000007cU)) {
        set_broadcast(DvpVuUpperOpcode::suba);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode4Mask, 0x00000000U)) {
        set_broadcast(DvpVuUpperOpcode::add);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode4Mask, 0x00000008U)) {
        set_broadcast(DvpVuUpperOpcode::madd);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode4Mask, 0x00000010U)) {
        set_broadcast(DvpVuUpperOpcode::max);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode4Mask, 0x00000014U)) {
        set_broadcast(DvpVuUpperOpcode::mini);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode4Mask, 0x0000000cU)) {
        set_broadcast(DvpVuUpperOpcode::msub);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode4Mask, 0x00000018U)) {
        set_broadcast(DvpVuUpperOpcode::mul);
    } else if (matches(
                   word, kUpperReservedMask + kOpcode4Mask, 0x00000004U)) {
        set_broadcast(DvpVuUpperOpcode::sub);
    }

    return result;
}

[[nodiscard]] DvpVuLowerInstructionV1 decode_lower(
    const std::uint32_t word,
    const bool immediate_literal) {
    DvpVuLowerInstructionV1 result;
    result.destination_mask =
        static_cast<std::uint8_t>((word >> 21U) & 0x0fU);
    result.it = static_cast<std::uint8_t>((word >> 16U) & 0x1fU);
    result.is = static_cast<std::uint8_t>((word >> 11U) & 0x1fU);
    result.id = static_cast<std::uint8_t>((word >> 6U) & 0x1fU);
    result.ft_component =
        component(static_cast<std::uint8_t>((word >> 23U) & 0x03U));
    result.fs_component =
        component(static_cast<std::uint8_t>((word >> 21U) & 0x03U));

    if (immediate_literal) {
        result.kind = DvpVuLowerKind::immediate_literal;
        result.unsigned_immediate = word;
        return result;
    }

    const auto opcode_and_11 = kLowerOpcode7Mask + kOpcode11Mask;
    if (word == 0x8000033cU) {
        result.opcode = DvpVuLowerOpcode::nop;
    } else if (matches(word, 0xfffff800U, 0x40000000U)) {
        result.opcode = DvpVuLowerOpcode::b;
        result.signed_immediate = sign_extend(word, 11U);
    } else if (matches(word, 0xffe0f800U, 0x42000000U)) {
        result.opcode = DvpVuLowerOpcode::bal;
        result.signed_immediate = sign_extend(word, 11U);
    } else if (matches(word, opcode_and_11, 0x800003bcU)) {
        result.opcode = DvpVuLowerOpcode::div;
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kLowerBit24Mask,
                   0x24000000U)) {
        result.opcode = DvpVuLowerOpcode::fcand;
        result.unsigned_immediate = word & 0x00ffffffU;
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kLowerBit24Mask,
                   0x20000000U)) {
        result.opcode = DvpVuLowerOpcode::fceq;
        result.unsigned_immediate = word & 0x00ffffffU;
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kLowerBit24Mask,
                   0x26000000U)) {
        result.opcode = DvpVuLowerOpcode::fcor;
        result.unsigned_immediate = word & 0x00ffffffU;
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kLowerBit24Mask,
                   0x22000000U)) {
        result.opcode = DvpVuLowerOpcode::fcset;
        result.unsigned_immediate = word & 0x00ffffffU;
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kLowerImmediate12UnusedMask +
                       kSourceRegisterMask,
                   0x2c000000U)) {
        result.opcode = DvpVuLowerOpcode::fsand;
        result.unsigned_immediate =
            (((word >> 21U) & 1U) << 11U) | (word & 0x7ffU);
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kLowerImmediate12UnusedMask +
                       kSourceRegisterMask,
                   0x28000000U)) {
        result.opcode = DvpVuLowerOpcode::fseq;
        result.unsigned_immediate =
            (((word >> 21U) & 1U) << 11U) | (word & 0x7ffU);
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kLowerImmediate12UnusedMask +
                       kSourceRegisterMask,
                   0x2e000000U)) {
        result.opcode = DvpVuLowerOpcode::fsor;
        result.unsigned_immediate =
            (((word >> 21U) & 1U) << 11U) | (word & 0x7ffU);
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kLowerImmediate12UnusedMask +
                       kTargetRegisterMask + kSourceRegisterMask +
                       kOpcode6Mask,
                   0x2a000000U)) {
        result.opcode = DvpVuLowerOpcode::fsset;
        result.unsigned_immediate =
            (((word >> 21U) & 1U) << 11U) | (word & 0x7ffU);
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kDestinationMask + kOpcode6Mask,
                   0x80000030U)) {
        result.opcode = DvpVuLowerOpcode::iadd;
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kDestinationMask + kOpcode6Mask,
                   0x80000032U)) {
        result.opcode = DvpVuLowerOpcode::iaddi;
        result.signed_immediate = sign_extend(word >> 6U, 5U);
    } else if (matches(word, kLowerOpcode7Mask, 0x10000000U)) {
        result.opcode = DvpVuLowerOpcode::iaddiu;
        result.unsigned_immediate =
            (((word >> 21U) & 0x0fU) << 11U) | (word & 0x7ffU);
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kDestinationMask + kOpcode6Mask,
                   0x80000034U)) {
        result.opcode = DvpVuLowerOpcode::iand;
    } else if (matches(word, 0xffe00000U, 0x50000000U)) {
        result.opcode = DvpVuLowerOpcode::ibeq;
        result.signed_immediate = sign_extend(word, 11U);
    } else if (matches(word, 0xffff0000U, 0x5e000000U)) {
        result.opcode = DvpVuLowerOpcode::ibgez;
        result.signed_immediate = sign_extend(word, 11U);
    } else if (matches(word, 0xffff0000U, 0x5a000000U)) {
        result.opcode = DvpVuLowerOpcode::ibgtz;
        result.signed_immediate = sign_extend(word, 11U);
    } else if (matches(word, 0xffff0000U, 0x5c000000U)) {
        result.opcode = DvpVuLowerOpcode::iblez;
        result.signed_immediate = sign_extend(word, 11U);
    } else if (matches(word, 0xffff0000U, 0x58000000U)) {
        result.opcode = DvpVuLowerOpcode::ibltz;
        result.signed_immediate = sign_extend(word, 11U);
    } else if (matches(word, 0xffe00000U, 0x52000000U)) {
        result.opcode = DvpVuLowerOpcode::ibne;
        result.signed_immediate = sign_extend(word, 11U);
    } else if (matches(word, kLowerOpcode7Mask, 0x08000000U)) {
        result.opcode = DvpVuLowerOpcode::ilw;
        result.signed_immediate = sign_extend(word, 11U);
    } else if (matches(word, opcode_and_11, 0x800003feU)) {
        result.opcode = DvpVuLowerOpcode::ilwr;
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kDestinationMask + kOpcode6Mask,
                   0x80000035U)) {
        result.opcode = DvpVuLowerOpcode::ior;
    } else if (matches(
                   word,
                   kLowerOpcode7Mask + kDestinationMask + kOpcode6Mask,
                   0x80000031U)) {
        result.opcode = DvpVuLowerOpcode::isub;
    } else if (matches(word, kLowerOpcode7Mask, 0x12000000U)) {
        result.opcode = DvpVuLowerOpcode::isubiu;
        result.unsigned_immediate =
            (((word >> 21U) & 0x0fU) << 11U) | (word & 0x7ffU);
    } else if (matches(word, kLowerOpcode7Mask, 0x0a000000U)) {
        result.opcode = DvpVuLowerOpcode::isw;
        result.signed_immediate = sign_extend(word, 11U);
    } else if (matches(word, opcode_and_11, 0x800003ffU)) {
        result.opcode = DvpVuLowerOpcode::iswr;
    } else if (matches(word, 0xffff07ffU, 0x48000000U)) {
        result.opcode = DvpVuLowerOpcode::jr;
    } else if (matches(word, 0xffe007ffU, 0x4a000000U)) {
        result.opcode = DvpVuLowerOpcode::jalr;
    } else if (matches(word, kLowerOpcode7Mask, 0x00000000U)) {
        result.opcode = DvpVuLowerOpcode::lq;
        result.signed_immediate = sign_extend(word, 11U);
    } else if (matches(word, opcode_and_11, 0x8000037eU)) {
        result.opcode = DvpVuLowerOpcode::lqd;
    } else if (matches(word, opcode_and_11, 0x8000037cU)) {
        result.opcode = DvpVuLowerOpcode::lqi;
    } else if (matches(word, opcode_and_11, 0x800003fdU)) {
        result.opcode = DvpVuLowerOpcode::mfir;
    } else if (matches(word, opcode_and_11, 0x8000033cU)) {
        result.opcode = DvpVuLowerOpcode::move;
    } else if (matches(word, opcode_and_11, 0x8000033dU)) {
        result.opcode = DvpVuLowerOpcode::mr32;
    } else if (matches(word, 0xff8007ffU, 0x800003fcU)) {
        result.opcode = DvpVuLowerOpcode::mtir;
    } else if (matches(word, kLowerOpcode7Mask, 0x02000000U)) {
        result.opcode = DvpVuLowerOpcode::sq;
        result.signed_immediate = sign_extend(word, 11U);
    } else if (matches(word, opcode_and_11, 0x8000037fU)) {
        result.opcode = DvpVuLowerOpcode::sqd;
    } else if (matches(word, opcode_and_11, 0x8000037dU)) {
        result.opcode = DvpVuLowerOpcode::sqi;
    } else if (matches(word, 0xffff07ffU, 0x800006fcU)) {
        result.opcode = DvpVuLowerOpcode::xgkick;
    } else if (matches(word, 0xffe0ffffU, 0x800006bdU)) {
        result.opcode = DvpVuLowerOpcode::xitop;
    } else if (matches(word, 0xffe0ffffU, 0x800006bcU)) {
        result.opcode = DvpVuLowerOpcode::xtop;
    }

    return result;
}

[[nodiscard]] std::uint16_t wrapped_address(
    const std::int32_t address) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(address) &
        (static_cast<std::uint32_t>(kDvpVu1InstructionCount) - 1U));
}

void require_limits(const DvpVuLimits& limits) {
    if (limits.max_input_bytes == 0U ||
        limits.max_overlay_chunks == 0U ||
        limits.max_code_bytes == 0U || limits.max_entrypoints == 0U ||
        limits.max_control_transfers == 0U || limits.max_cfg_edges == 0U) {
        throw DvpVuError("DVP VU decoder limits must all be non-zero");
    }
}

void append_memory_access(
    DvpVuProgramV1& result,
    const std::uint64_t instruction_index) {
    const auto& lower = result.instructions[
        static_cast<std::size_t>(instruction_index)].lower;
    DvpVuMemoryAccessV1 access;
    access.instruction_index = instruction_index;
    access.component_mask = lower.destination_mask;

    switch (lower.opcode) {
    case DvpVuLowerOpcode::lq:
        access.kind = DvpVuMemoryAccessKind::vector_load;
        access.address_mode = DvpVuMemoryAddressMode::base_offset;
        access.base_vi = lower.is & 0x0fU;
        access.value_register = lower.it;
        access.qword_offset =
            static_cast<std::int16_t>(lower.signed_immediate);
        break;
    case DvpVuLowerOpcode::lqd:
        access.kind = DvpVuMemoryAccessKind::vector_load;
        access.address_mode = DvpVuMemoryAddressMode::pre_decrement;
        access.base_vi = lower.is & 0x0fU;
        access.value_register = lower.it;
        access.qword_offset = -1;
        break;
    case DvpVuLowerOpcode::lqi:
        access.kind = DvpVuMemoryAccessKind::vector_load;
        access.address_mode = DvpVuMemoryAddressMode::post_increment;
        access.base_vi = lower.is & 0x0fU;
        access.value_register = lower.it;
        break;
    case DvpVuLowerOpcode::sq:
        access.kind = DvpVuMemoryAccessKind::vector_store;
        access.address_mode = DvpVuMemoryAddressMode::base_offset;
        access.base_vi = lower.it & 0x0fU;
        access.value_register = lower.is;
        access.qword_offset =
            static_cast<std::int16_t>(lower.signed_immediate);
        break;
    case DvpVuLowerOpcode::sqd:
        access.kind = DvpVuMemoryAccessKind::vector_store;
        access.address_mode = DvpVuMemoryAddressMode::pre_decrement;
        access.base_vi = lower.it & 0x0fU;
        access.value_register = lower.is;
        access.qword_offset = -1;
        break;
    case DvpVuLowerOpcode::sqi:
        access.kind = DvpVuMemoryAccessKind::vector_store;
        access.address_mode = DvpVuMemoryAddressMode::post_increment;
        access.base_vi = lower.it & 0x0fU;
        access.value_register = lower.is;
        break;
    case DvpVuLowerOpcode::ilw:
        access.kind = DvpVuMemoryAccessKind::integer_load;
        access.address_mode = DvpVuMemoryAddressMode::base_offset;
        access.base_vi = lower.is & 0x0fU;
        access.value_register = lower.it & 0x0fU;
        access.qword_offset =
            static_cast<std::int16_t>(lower.signed_immediate);
        break;
    case DvpVuLowerOpcode::ilwr:
        access.kind = DvpVuMemoryAccessKind::integer_load;
        access.address_mode = DvpVuMemoryAddressMode::base;
        access.base_vi = lower.is & 0x0fU;
        access.value_register = lower.it & 0x0fU;
        break;
    case DvpVuLowerOpcode::isw:
        access.kind = DvpVuMemoryAccessKind::integer_store;
        access.address_mode = DvpVuMemoryAddressMode::base_offset;
        access.base_vi = lower.is & 0x0fU;
        access.value_register = lower.it & 0x0fU;
        access.qword_offset =
            static_cast<std::int16_t>(lower.signed_immediate);
        break;
    case DvpVuLowerOpcode::iswr:
        access.kind = DvpVuMemoryAccessKind::integer_store;
        access.address_mode = DvpVuMemoryAddressMode::base;
        access.base_vi = lower.is & 0x0fU;
        access.value_register = lower.it & 0x0fU;
        break;
    default:
        return;
    }

    result.memory_accesses.push_back(access);
}

void append_control_transfer(
    DvpVuProgramV1& result,
    const std::array<std::uint64_t, kDvpVu1InstructionCount>&
        address_to_instruction,
    const std::uint64_t instruction_index,
    const DvpVuControlTransferKind kind,
    const DvpVuBranchCondition condition,
    const std::optional<std::uint16_t> direct_target,
    const std::optional<std::uint8_t> target_vi,
    const std::optional<std::uint8_t> link_vi,
    const bool has_continuation,
    const DvpVuLimits& limits) {
    if (result.control_transfers.size() >= limits.max_control_transfers) {
        throw DvpVuError("DVP VU control-transfer limit exceeded");
    }

    const auto address = result.instructions[
        static_cast<std::size_t>(instruction_index)].instruction_address;
    const auto delay_address = wrapped_address(
        static_cast<std::int32_t>(address) + 1);
    const auto continuation_address = wrapped_address(
        static_cast<std::int32_t>(address) + 2);

    DvpVuControlTransferV1 transfer;
    transfer.instruction_index = instruction_index;
    transfer.kind = kind;
    transfer.condition = condition;
    transfer.direct_target_address = direct_target;
    transfer.delay_slot_address = delay_address;
    if (has_continuation) {
        transfer.continuation_address = continuation_address;
    }
    transfer.target_vi = target_vi;
    transfer.link_vi = link_vi;
    transfer.delay_slot_decoded =
        address_to_instruction[delay_address] != kInvalidIndex;
    if (direct_target) {
        transfer.direct_target_decoded =
            address_to_instruction[*direct_target] != kInvalidIndex;
        if (!transfer.direct_target_decoded) {
            ++result.unresolved_direct_target_count;
        }
    }
    if (!transfer.delay_slot_decoded) {
        ++result.missing_delay_slot_count;
    }
    if (kind == DvpVuControlTransferKind::indirect_jump ||
        kind == DvpVuControlTransferKind::indirect_call) {
        ++result.indirect_control_transfer_count;
    }
    result.control_transfers.push_back(std::move(transfer));
}

void inventory_instruction_semantics(
    DvpVuProgramV1& result,
    const std::array<std::uint64_t, kDvpVu1InstructionCount>&
        address_to_instruction,
    const std::uint64_t instruction_index,
    const DvpVuLimits& limits) {
    const auto& instruction =
        result.instructions[static_cast<std::size_t>(instruction_index)];
    const auto& lower = instruction.lower;
    append_memory_access(result, instruction_index);

    const auto direct_target = [&instruction, &lower]() {
        return wrapped_address(
            static_cast<std::int32_t>(instruction.instruction_address) + 1 +
            lower.signed_immediate);
    };
    const auto semantic_vi = [](const std::uint8_t raw) {
        return static_cast<std::uint8_t>(raw & 0x0fU);
    };

    switch (lower.opcode) {
    case DvpVuLowerOpcode::b:
        append_control_transfer(
            result, address_to_instruction, instruction_index,
            DvpVuControlTransferKind::unconditional_branch,
            DvpVuBranchCondition::always, direct_target(), std::nullopt,
            std::nullopt, true, limits);
        break;
    case DvpVuLowerOpcode::bal:
        append_control_transfer(
            result, address_to_instruction, instruction_index,
            DvpVuControlTransferKind::direct_call,
            DvpVuBranchCondition::always, direct_target(), std::nullopt,
            semantic_vi(lower.it), true, limits);
        break;
    case DvpVuLowerOpcode::ibeq:
    case DvpVuLowerOpcode::ibne:
    case DvpVuLowerOpcode::ibgez:
    case DvpVuLowerOpcode::ibgtz:
    case DvpVuLowerOpcode::iblez:
    case DvpVuLowerOpcode::ibltz: {
        DvpVuBranchCondition condition = DvpVuBranchCondition::equal;
        switch (lower.opcode) {
        case DvpVuLowerOpcode::ibeq:
            condition = DvpVuBranchCondition::equal;
            break;
        case DvpVuLowerOpcode::ibne:
            condition = DvpVuBranchCondition::not_equal;
            break;
        case DvpVuLowerOpcode::ibgez:
            condition = DvpVuBranchCondition::greater_equal_zero;
            break;
        case DvpVuLowerOpcode::ibgtz:
            condition = DvpVuBranchCondition::greater_zero;
            break;
        case DvpVuLowerOpcode::iblez:
            condition = DvpVuBranchCondition::less_equal_zero;
            break;
        case DvpVuLowerOpcode::ibltz:
            condition = DvpVuBranchCondition::less_zero;
            break;
        default:
            break;
        }
        append_control_transfer(
            result, address_to_instruction, instruction_index,
            DvpVuControlTransferKind::conditional_branch, condition,
            direct_target(), std::nullopt, std::nullopt, true, limits);
        break;
    }
    case DvpVuLowerOpcode::jr:
        append_control_transfer(
            result, address_to_instruction, instruction_index,
            DvpVuControlTransferKind::indirect_jump,
            DvpVuBranchCondition::always, std::nullopt,
            semantic_vi(lower.is), std::nullopt, true, limits);
        break;
    case DvpVuLowerOpcode::jalr:
        append_control_transfer(
            result, address_to_instruction, instruction_index,
            DvpVuControlTransferKind::indirect_call,
            DvpVuBranchCondition::always, std::nullopt,
            semantic_vi(lower.is), semantic_vi(lower.it), true, limits);
        break;
    default:
        break;
    }

    if (instruction.upper.end) {
        append_control_transfer(
            result, address_to_instruction, instruction_index,
            DvpVuControlTransferKind::end_after_delay_slot,
            DvpVuBranchCondition::always, std::nullopt, std::nullopt,
            std::nullopt, false, limits);
    }
}

void append_edge(
    DvpVuProgramV1& result,
    DvpVuCfgEdgeV1 edge,
    const DvpVuLimits& limits) {
    if (result.cfg_edges.size() >= limits.max_cfg_edges) {
        throw DvpVuError("DVP VU CFG edge limit exceeded");
    }
    result.cfg_edges.push_back(std::move(edge));
}

void validate_cfg_shape(
    const DvpVuProgramV1& result,
    const std::array<bool, kDvpVu1InstructionCount>& entrypoint_addresses) {
    std::array<bool, kDvpVu1InstructionCount> delay_slot_addresses{};
    std::array<bool, kDvpVu1InstructionCount> control_addresses{};
    std::array<bool, kDvpVu1InstructionCount> run_start_addresses{};

    for (const auto& run : result.instruction_runs) {
        run_start_addresses[run.first_instruction_address] = true;
    }

    for (const auto& transfer : result.control_transfers) {
        const auto address = result.instructions[
            static_cast<std::size_t>(transfer.instruction_index)]
                                 .instruction_address;
        if (control_addresses[address]) {
            throw DvpVuError(
                "DVP VU CFG does not support multiple control transfers "
                "in one instruction pair");
        }
        control_addresses[address] = true;
        if (transfer.delay_slot_address && transfer.delay_slot_decoded) {
            delay_slot_addresses[*transfer.delay_slot_address] = true;
        }
    }

    for (std::size_t address = 0U; address < delay_slot_addresses.size();
         ++address) {
        if (!delay_slot_addresses[address]) {
            continue;
        }
        if (control_addresses[address]) {
            throw DvpVuError(
                "DVP VU CFG does not support a control transfer in a "
                "delay slot");
        }
        if (entrypoint_addresses[address]) {
            throw DvpVuError(
                "DVP VU CFG does not support an entrypoint in a delay "
                "slot");
        }
        if (run_start_addresses[address]) {
            throw DvpVuError(
                "DVP VU CFG does not support an instruction-run start in "
                "a delay slot");
        }
    }

    for (const auto& transfer : result.control_transfers) {
        if (transfer.direct_target_address &&
            delay_slot_addresses[*transfer.direct_target_address]) {
            throw DvpVuError(
                "DVP VU CFG does not support a direct target in a delay "
                "slot");
        }
    }
}

void build_cfg(
    DvpVuProgramV1& result,
    const std::array<std::uint64_t, kDvpVu1InstructionCount>&
        address_to_instruction,
    const std::array<bool, kDvpVu1InstructionCount>& entrypoint_addresses,
    const DvpVuLimits& limits) {
    const auto instruction_count = result.instructions.size();
    if (instruction_count == 0U) {
        return;
    }

    std::vector<bool> starts_block(instruction_count, false);
    std::vector<bool> ends_block(instruction_count, false);
    for (const auto& run : result.instruction_runs) {
        starts_block[static_cast<std::size_t>(run.first_instruction_index)] =
            true;
    }
    for (std::size_t address = 0U;
         address < entrypoint_addresses.size(); ++address) {
        if (entrypoint_addresses[address] &&
            address_to_instruction[address] != kInvalidIndex) {
            starts_block[static_cast<std::size_t>(
                address_to_instruction[address])] = true;
        }
    }
    for (const auto& transfer : result.control_transfers) {
        if (transfer.direct_target_address &&
            transfer.direct_target_decoded) {
            starts_block[static_cast<std::size_t>(address_to_instruction[
                *transfer.direct_target_address])] = true;
        }
        if (transfer.continuation_address &&
            address_to_instruction[*transfer.continuation_address] !=
                kInvalidIndex) {
            starts_block[static_cast<std::size_t>(address_to_instruction[
                *transfer.continuation_address])] = true;
        }
        if (transfer.delay_slot_address && transfer.delay_slot_decoded) {
            ends_block[static_cast<std::size_t>(address_to_instruction[
                *transfer.delay_slot_address])] = true;
        } else {
            ends_block[static_cast<std::size_t>(
                transfer.instruction_index)] = true;
        }
    }

    std::vector<std::uint64_t> instruction_to_block(
        instruction_count, kInvalidIndex);
    std::size_t first = 0U;
    while (first < instruction_count) {
        auto last = first;
        while (last + 1U < instruction_count) {
            if (ends_block[last] || starts_block[last + 1U]) {
                break;
            }
            const auto address = result.instructions[last].instruction_address;
            const auto next_address =
                result.instructions[last + 1U].instruction_address;
            if (static_cast<std::uint32_t>(address) + 1U != next_address) {
                break;
            }
            ++last;
        }

        DvpVuBasicBlockV1 block;
        block.first_instruction_index = first;
        block.first_instruction_address =
            result.instructions[first].instruction_address;
        block.instruction_count = static_cast<std::uint16_t>(last - first + 1U);
        block.starts_at_entrypoint =
            entrypoint_addresses[block.first_instruction_address];
        const auto block_index =
            static_cast<std::uint64_t>(result.basic_blocks.size());
        for (auto index = first; index <= last; ++index) {
            instruction_to_block[index] = block_index;
        }
        result.basic_blocks.push_back(block);
        first = last + 1U;
    }

    std::vector<std::vector<std::uint64_t>> controls_by_block(
        result.basic_blocks.size());
    for (std::size_t index = 0U; index < result.control_transfers.size();
         ++index) {
        const auto instruction_index = result.control_transfers[index].instruction_index;
        const auto block_index = instruction_to_block[
            static_cast<std::size_t>(instruction_index)];
        controls_by_block[static_cast<std::size_t>(block_index)].push_back(
            index);
    }

    const auto target_block = [&address_to_instruction,
                               &instruction_to_block](
                                  const std::uint16_t address)
        -> std::optional<std::uint64_t> {
        const auto instruction_index = address_to_instruction[address];
        if (instruction_index == kInvalidIndex) {
            return std::nullopt;
        }
        return instruction_to_block[
            static_cast<std::size_t>(instruction_index)];
    };

    for (std::size_t block_index = 0U;
         block_index < result.basic_blocks.size(); ++block_index) {
        auto& block = result.basic_blocks[block_index];
        block.first_edge_index = result.cfg_edges.size();
        const auto& control_indices = controls_by_block[block_index];
        if (control_indices.empty()) {
            const auto last_instruction_index =
                static_cast<std::size_t>(block.first_instruction_index) +
                block.instruction_count - 1U;
            const auto next_address = wrapped_address(
                static_cast<std::int32_t>(result.instructions[
                    last_instruction_index].instruction_address) + 1);
            DvpVuCfgEdgeV1 edge;
            edge.source_block_index = block_index;
            edge.target_instruction_address = next_address;
            edge.target_block_index = target_block(next_address);
            edge.kind = edge.target_block_index
                ? DvpVuCfgEdgeKind::fallthrough
                : DvpVuCfgEdgeKind::unresolved_fallthrough;
            append_edge(result, std::move(edge), limits);
        } else {
            for (const auto control_index : control_indices) {
                const auto& transfer = result.control_transfers[
                    static_cast<std::size_t>(control_index)];
                DvpVuCfgEdgeV1 edge;
                edge.source_block_index = block_index;
                edge.control_transfer_index = control_index;

                switch (transfer.kind) {
                case DvpVuControlTransferKind::unconditional_branch:
                case DvpVuControlTransferKind::direct_call:
                    edge.target_instruction_address =
                        transfer.direct_target_address;
                    if (transfer.direct_target_address) {
                        edge.target_block_index =
                            target_block(*transfer.direct_target_address);
                    }
                    if (!edge.target_block_index) {
                        edge.kind = DvpVuCfgEdgeKind::unresolved_direct;
                    } else if (transfer.kind ==
                               DvpVuControlTransferKind::direct_call) {
                        edge.kind = DvpVuCfgEdgeKind::direct_call;
                    } else {
                        edge.kind = DvpVuCfgEdgeKind::branch_taken;
                    }
                    append_edge(result, std::move(edge), limits);
                    break;
                case DvpVuControlTransferKind::conditional_branch: {
                    edge.target_instruction_address =
                        transfer.direct_target_address;
                    if (transfer.direct_target_address) {
                        edge.target_block_index =
                            target_block(*transfer.direct_target_address);
                    }
                    edge.kind = edge.target_block_index
                        ? DvpVuCfgEdgeKind::branch_taken
                        : DvpVuCfgEdgeKind::unresolved_direct;
                    append_edge(result, std::move(edge), limits);

                    DvpVuCfgEdgeV1 not_taken;
                    not_taken.source_block_index = block_index;
                    not_taken.control_transfer_index = control_index;
                    not_taken.target_instruction_address =
                        transfer.continuation_address;
                    if (transfer.continuation_address) {
                        not_taken.target_block_index =
                            target_block(*transfer.continuation_address);
                    }
                    not_taken.kind = not_taken.target_block_index
                        ? DvpVuCfgEdgeKind::branch_not_taken
                        : DvpVuCfgEdgeKind::unresolved_fallthrough;
                    append_edge(result, std::move(not_taken), limits);
                    break;
                }
                case DvpVuControlTransferKind::indirect_jump:
                    edge.kind = DvpVuCfgEdgeKind::indirect_jump;
                    append_edge(result, std::move(edge), limits);
                    break;
                case DvpVuControlTransferKind::indirect_call:
                    edge.kind = DvpVuCfgEdgeKind::indirect_call;
                    append_edge(result, std::move(edge), limits);
                    break;
                case DvpVuControlTransferKind::end_after_delay_slot:
                    edge.kind = DvpVuCfgEdgeKind::program_end;
                    append_edge(result, std::move(edge), limits);
                    break;
                }
            }
        }
        const auto edge_count = result.cfg_edges.size() -
            static_cast<std::size_t>(block.first_edge_index);
        block.edge_count = static_cast<std::uint32_t>(edge_count);
    }

    for (const auto& entrypoint : result.entrypoints) {
        if (entrypoint.instruction_index >= instruction_to_block.size()) {
            throw DvpVuError("Internal DVP VU entrypoint index is invalid");
        }
    }
    for (auto& entrypoint : result.entrypoints) {
        entrypoint.basic_block_index = instruction_to_block[
            static_cast<std::size_t>(entrypoint.instruction_index)];
    }
}

} // namespace

DvpVuProgramV1 decode_dvp_vu_program_v1(
    const std::span<const std::byte> elf_bytes,
    const std::span<const ElfDvpOverlay> overlay_chunks,
    const std::span<const std::uint16_t> entrypoints,
    const DvpVuLimits limits) {
    require_limits(limits);
    const auto input_bytes = static_cast<std::uint64_t>(elf_bytes.size());
    if (input_bytes > limits.max_input_bytes) {
        throw DvpVuError("DVP VU input-byte limit exceeded");
    }
    if (overlay_chunks.empty()) {
        throw DvpVuError("DVP VU program has no overlay chunks");
    }
    if (overlay_chunks.size() > limits.max_overlay_chunks) {
        throw DvpVuError("DVP VU overlay-chunk limit exceeded");
    }
    if (entrypoints.size() > limits.max_entrypoints) {
        throw DvpVuError("DVP VU entrypoint limit exceeded");
    }

    std::vector<PendingChunk> pending;
    pending.reserve(overlay_chunks.size());
    std::uint64_t total_code_bytes = 0U;
    for (std::size_t index = 0U; index < overlay_chunks.size(); ++index) {
        const auto& overlay = overlay_chunks[index];
        if (overlay.size == 0U) {
            throw DvpVuError("DVP VU overlay chunk is empty");
        }
        if ((overlay.virtual_memory_address % kDvpVuInstructionBytes) != 0U ||
            (overlay.size % kDvpVuInstructionBytes) != 0U) {
            throw DvpVuError(
                "DVP VU overlay VMA and size must be instruction-aligned");
        }
        if (overlay.virtual_memory_address > kDvpVu1MicroMemoryBytes ||
            overlay.size > kDvpVu1MicroMemoryBytes -
                overlay.virtual_memory_address) {
            throw DvpVuError(
                "DVP VU overlay exceeds VU1 micro memory");
        }
        if (overlay.code_file_offset > input_bytes ||
            overlay.size > input_bytes - overlay.code_file_offset) {
            throw DvpVuError(
                "DVP VU overlay code range is outside the input");
        }
        if (overlay.size > limits.max_code_bytes -
                std::min(total_code_bytes, limits.max_code_bytes)) {
            throw DvpVuError("DVP VU code-byte limit exceeded");
        }
        total_code_bytes += overlay.size;
        if (total_code_bytes > limits.max_code_bytes) {
            throw DvpVuError("DVP VU code-byte limit exceeded");
        }

        PendingChunk item;
        item.chunk.input_overlay_index = index;
        item.chunk.overlay_section_index = overlay.overlay_section_index;
        item.chunk.code_section_index = overlay.code_section_index;
        item.chunk.source_range =
            DvpVuByteRangeV1{overlay.code_file_offset, overlay.size};
        item.chunk.virtual_byte_address = overlay.virtual_memory_address;
        item.chunk.instruction_count = static_cast<std::uint16_t>(
            overlay.size / kDvpVuInstructionBytes);
        item.virtual_end = overlay.virtual_memory_address + overlay.size;
        pending.push_back(item);
    }

    std::sort(
        pending.begin(), pending.end(),
        [](const PendingChunk& left, const PendingChunk& right) {
            if (left.chunk.virtual_byte_address !=
                right.chunk.virtual_byte_address) {
                return left.chunk.virtual_byte_address <
                    right.chunk.virtual_byte_address;
            }
            return left.chunk.input_overlay_index <
                right.chunk.input_overlay_index;
        });
    for (std::size_t index = 1U; index < pending.size(); ++index) {
        if (pending[index].chunk.virtual_byte_address <
            pending[index - 1U].virtual_end) {
            throw DvpVuError("DVP VU overlay chunks overlap in micro memory");
        }
    }

    DvpVuProgramV1 result;
    result.input_bytes = input_bytes;
    result.total_code_bytes = total_code_bytes;
    result.code_chunks.reserve(pending.size());
    result.instructions.reserve(static_cast<std::size_t>(
        total_code_bytes / kDvpVuInstructionBytes));
    std::array<std::uint64_t, kDvpVu1InstructionCount>
        address_to_instruction;
    address_to_instruction.fill(kInvalidIndex);

    for (const auto& pending_chunk : pending) {
        auto chunk = pending_chunk.chunk;
        chunk.first_instruction_index = result.instructions.size();
        const auto output_chunk_index = result.code_chunks.size();
        result.code_chunks.push_back(chunk);
        for (std::uint32_t local_offset = 0U;
             local_offset < chunk.source_range.size;
             local_offset += kDvpVuInstructionBytes) {
            const auto source_offset = chunk.source_range.offset + local_offset;
            const auto byte_address = chunk.virtual_byte_address + local_offset;
            const auto instruction_address = static_cast<std::uint16_t>(
                byte_address / kDvpVuInstructionBytes);
            DvpVuInstructionPairV1 instruction;
            instruction.instruction_address = instruction_address;
            instruction.code_chunk_index = output_chunk_index;
            instruction.source_range = DvpVuByteRangeV1{
                source_offset, kDvpVuInstructionBytes};
            instruction.raw_lower = read_le32(
                elf_bytes, static_cast<std::size_t>(source_offset));
            instruction.raw_upper = read_le32(
                elf_bytes,
                static_cast<std::size_t>(source_offset + 4U));
            instruction.upper = decode_upper(instruction.raw_upper);
            instruction.lower = decode_lower(
                instruction.raw_lower, instruction.upper.immediate);
            if (instruction.upper.opcode == DvpVuUpperOpcode::unknown) {
                ++result.unknown_upper_count;
            }
            if (instruction.lower.kind == DvpVuLowerKind::instruction &&
                instruction.lower.opcode == DvpVuLowerOpcode::unknown) {
                ++result.unknown_lower_count;
            }
            const auto instruction_index = result.instructions.size();
            address_to_instruction[instruction_address] = instruction_index;
            result.instructions.push_back(instruction);
        }
    }

    for (std::size_t index = 0U; index < result.instructions.size(); ++index) {
        if (index == 0U ||
            static_cast<std::uint32_t>(
                result.instructions[index - 1U].instruction_address) + 1U !=
                result.instructions[index].instruction_address) {
            result.instruction_runs.push_back(DvpVuInstructionRunV1{
                result.instructions[index].instruction_address,
                1U,
                index,
            });
        } else {
            ++result.instruction_runs.back().instruction_count;
        }
    }

    std::array<bool, kDvpVu1InstructionCount> entrypoint_addresses{};
    result.entrypoints.reserve(entrypoints.size());
    for (const auto address : entrypoints) {
        if (address >= kDvpVu1InstructionCount) {
            throw DvpVuError("DVP VU entrypoint is outside VU1 micro memory");
        }
        if (entrypoint_addresses[address]) {
            throw DvpVuError("DVP VU entrypoint is duplicated");
        }
        const auto instruction_index = address_to_instruction[address];
        if (instruction_index == kInvalidIndex) {
            throw DvpVuError("DVP VU entrypoint points into unloaded code");
        }
        entrypoint_addresses[address] = true;
        result.entrypoints.push_back(DvpVuEntrypointV1{
            address,
            instruction_index,
            0U,
        });
    }

    result.memory_accesses.reserve(result.instructions.size());
    result.control_transfers.reserve(result.instructions.size());
    for (std::size_t index = 0U; index < result.instructions.size(); ++index) {
        inventory_instruction_semantics(
            result, address_to_instruction, index, limits);
    }
    validate_cfg_shape(result, entrypoint_addresses);
    build_cfg(result, address_to_instruction, entrypoint_addresses, limits);
    return result;
}

} // namespace openrc
