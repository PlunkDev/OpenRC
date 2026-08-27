#include "openrc/dvp_vu_execute.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint32_t kFullKnown = 0xffffffffU;
constexpr std::uint32_t kViMask = 0x0000ffffU;
constexpr std::uint32_t kStatusMask = 0x00000fffU;
constexpr std::uint32_t kClipMask = 0x00ffffffU;
constexpr std::size_t kInvalidIndex = std::numeric_limits<std::size_t>::max();

[[nodiscard]] constexpr DvpVuWordV1 known_word(const std::uint32_t bits) {
    return DvpVuWordV1{bits, kFullKnown};
}

[[nodiscard]] constexpr DvpVuWordV1 known_vi(const std::uint16_t bits) {
    return DvpVuWordV1{bits, kViMask};
}

[[nodiscard]] constexpr bool
fully_known(const DvpVuWordV1 value, const std::uint32_t mask = kFullKnown) {
    return (value.known_mask & mask) == mask;
}

[[nodiscard]] constexpr DvpVuWordV1 masked_word(const DvpVuWordV1 value,
                                                const std::uint32_t mask) {
    return DvpVuWordV1{value.bits & mask, value.known_mask & mask};
}

[[nodiscard]] constexpr DvpVuWordV1 bit_and(const DvpVuWordV1 left,
                                            const DvpVuWordV1 right) {
    const auto known_zero =
        (left.known_mask & ~left.bits) | (right.known_mask & ~right.bits);
    const auto known_one =
        left.known_mask & left.bits & right.known_mask & right.bits;
    return DvpVuWordV1{known_one, known_zero | known_one};
}

[[nodiscard]] constexpr DvpVuWordV1 bit_or(const DvpVuWordV1 left,
                                           const DvpVuWordV1 right) {
    const auto known_one =
        (left.known_mask & left.bits) | (right.known_mask & right.bits);
    const auto known_zero =
        left.known_mask & ~left.bits & right.known_mask & ~right.bits;
    return DvpVuWordV1{known_one, known_zero | known_one};
}

void replace_bits(DvpVuWordV1& destination,
                  const DvpVuWordV1 source,
                  const std::uint32_t mask) {
    destination.bits = (destination.bits & ~mask) | (source.bits & mask);
    destination.known_mask =
        (destination.known_mask & ~mask) | (source.known_mask & mask);
}

void add_warning(DvpVuExecutionResultV1& result,
                 const DvpVuExecutionWarningV1 warning) {
    if (std::find(result.warnings.begin(), result.warnings.end(), warning) ==
        result.warnings.end()) {
        result.warnings.push_back(warning);
    }
}

[[nodiscard]] constexpr std::uint8_t lane_bit(const std::size_t lane) {
    return static_cast<std::uint8_t>(0x08U >> lane);
}

[[nodiscard]] std::optional<std::size_t>
component_lane(const DvpVuComponent component) {
    switch (component) {
    case DvpVuComponent::x:
        return 0U;
    case DvpVuComponent::y:
        return 1U;
    case DvpVuComponent::z:
        return 2U;
    case DvpVuComponent::w:
        return 3U;
    case DvpVuComponent::none:
        return std::nullopt;
    }
    return std::nullopt;
}

void enforce_architectural_constants(DvpVuExecutionStateV1& state) {
    state.vi[0U] = known_vi(0U);
    state.vf[0U].lanes[0U] = known_word(0U);
    state.vf[0U].lanes[1U] = known_word(0U);
    state.vf[0U].lanes[2U] = known_word(0U);
    state.vf[0U].lanes[3U] = known_word(0x3f800000U);
    state.mac_flags = masked_word(state.mac_flags, 0x0000ffffU);
    state.status_flags = masked_word(state.status_flags, kStatusMask);
    state.clip_flags = masked_word(state.clip_flags, kClipMask);
}

[[nodiscard]] constexpr std::uint32_t
normalize_vu_float_bits(const std::uint32_t bits) {
    const auto sign = bits & 0x80000000U;
    const auto exponent = bits & 0x7f800000U;
    if (exponent == 0U) {
        return sign;
    }
    if (exponent == 0x7f800000U) {
        return sign | 0x7f7fffffU;
    }
    return bits;
}

struct FloatOutcome {
    DvpVuWordV1 value;
    bool flags_known = false;
    bool zero = false;
    bool sign = false;
    bool underflow = false;
    bool overflow = false;
};

enum class FloatBinaryOperation : std::uint8_t {
    add = 0,
    subtract,
    multiply,
};

[[nodiscard]] FloatOutcome float_binary(const DvpVuWordV1 left,
                                        const DvpVuWordV1 right,
                                        const FloatBinaryOperation operation) {
    if (!fully_known(left) || !fully_known(right)) {
        return {};
    }

    const auto left_bits = normalize_vu_float_bits(left.bits);
    const auto right_bits = normalize_vu_float_bits(right.bits);
    const auto left_float = std::bit_cast<float>(left_bits);
    const auto right_float = std::bit_cast<float>(right_bits);
    float raw_result = 0.0F;
    switch (operation) {
    case FloatBinaryOperation::add:
        raw_result = left_float + right_float;
        break;
    case FloatBinaryOperation::subtract:
        raw_result = left_float - right_float;
        break;
    case FloatBinaryOperation::multiply:
        raw_result = left_float * right_float;
        break;
    }

    auto result_bits = std::bit_cast<std::uint32_t>(raw_result);
    const auto raw_exponent = result_bits & 0x7f800000U;
    const auto raw_mantissa = result_bits & 0x007fffffU;
    const bool overflow = raw_exponent == 0x7f800000U;
    const bool underflow = raw_exponent == 0U && raw_mantissa != 0U;
    result_bits = normalize_vu_float_bits(result_bits);

    FloatOutcome outcome;
    outcome.value = known_word(result_bits);
    outcome.flags_known = true;
    outcome.zero = (result_bits & 0x7fffffffU) == 0U;
    outcome.sign = (result_bits & 0x80000000U) != 0U;
    outcome.underflow = underflow;
    outcome.overflow = overflow;
    return outcome;
}

[[nodiscard]] FloatOutcome float_madd(const DvpVuWordV1 accumulator,
                                      const DvpVuWordV1 left,
                                      const DvpVuWordV1 right,
                                      const bool subtract) {
    const auto product =
        float_binary(left, right, FloatBinaryOperation::multiply);
    if (!fully_known(product.value)) {
        return {};
    }
    return float_binary(accumulator,
                        product.value,
                        subtract ? FloatBinaryOperation::subtract
                                 : FloatBinaryOperation::add);
}

[[nodiscard]] DvpVuWordV1 float_minmax(const DvpVuWordV1 left,
                                       const DvpVuWordV1 right,
                                       const bool maximum) {
    if (!fully_known(left) || !fully_known(right)) {
        return {};
    }
    const auto left_bits = normalize_vu_float_bits(left.bits);
    const auto right_bits = normalize_vu_float_bits(right.bits);
    const auto left_sign = (left_bits >> 31U) != 0U;
    const auto right_sign = (right_bits >> 31U) != 0U;

    bool left_less = false;
    if (left_sign != right_sign) {
        left_less = left_sign;
    } else if (!left_sign) {
        left_less = left_bits < right_bits;
    } else {
        left_less = left_bits > right_bits;
    }
    const auto selected = maximum ? (left_less ? right_bits : left_bits)
                                  : (left_less ? left_bits : right_bits);
    return known_word(selected);
}

[[nodiscard]] DvpVuWordV1 convert_ftoi(const DvpVuWordV1 source,
                                       const int fractional_bits) {
    if (!fully_known(source)) {
        return {};
    }
    const auto source_bits = normalize_vu_float_bits(source.bits);
    const auto value = static_cast<double>(std::bit_cast<float>(source_bits));
    const auto scaled = std::ldexp(value, fractional_bits);
    std::int32_t result = 0;
    if (scaled >=
        static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        result = std::numeric_limits<std::int32_t>::max();
    } else if (scaled <=
               static_cast<double>(std::numeric_limits<std::int32_t>::min())) {
        result = std::numeric_limits<std::int32_t>::min();
    } else {
        result = static_cast<std::int32_t>(std::trunc(scaled));
    }
    return known_word(static_cast<std::uint32_t>(result));
}

[[nodiscard]] DvpVuWordV1 convert_itof(const DvpVuWordV1 source,
                                       const int fractional_bits) {
    if (!fully_known(source)) {
        return {};
    }
    const auto integer = static_cast<std::int32_t>(source.bits);
    const auto value =
        std::ldexp(static_cast<float>(integer), -fractional_bits);
    return known_word(
        normalize_vu_float_bits(std::bit_cast<std::uint32_t>(value)));
}

struct RegisterSnapshot {
    std::array<DvpVuVectorV1, kDvpVuVectorRegisterCount> vf;
    std::array<DvpVuWordV1, kDvpVuIntegerRegisterCount> vi;
    DvpVuVectorV1 accumulator;
    DvpVuWordV1 scalar_i;
    DvpVuWordV1 scalar_q;
    DvpVuWordV1 mac_flags;
    DvpVuWordV1 status_flags;
    DvpVuWordV1 clip_flags;
    DvpVuWordV1 xtop_qword;
};

[[nodiscard]] RegisterSnapshot
snapshot_registers(const DvpVuExecutionStateV1& state) {
    return RegisterSnapshot{
        state.vf,
        state.vi,
        state.accumulator,
        state.scalar_i,
        state.scalar_q,
        state.mac_flags,
        state.status_flags,
        state.clip_flags,
        state.xtop_qword,
    };
}

struct PendingStatus {
    std::uint64_t ready_cycle = 0U;
    DvpVuWordV1 mac;
    DvpVuWordV1 current;
};

struct PendingClip {
    std::uint64_t ready_cycle = 0U;
    DvpVuWordV1 value;
};

struct PendingQ {
    std::uint64_t ready_cycle = 0U;
    DvpVuWordV1 value;
    DvpVuWordV1 divide_current;
};

struct PendingStore {
    std::uint64_t ready_cycle = 0U;
    std::uint16_t address = 0U;
    std::uint8_t destination_mask = 0U;
    DvpVuVectorV1 value;
};

struct Pipelines {
    std::vector<PendingStatus> status;
    std::vector<PendingClip> clip;
    std::vector<PendingQ> q;
    std::vector<PendingStore> stores;
};

void commit_status(DvpVuExecutionStateV1& state, const PendingStatus& pending) {
    state.mac_flags = masked_word(pending.mac, 0x0000ffffU);
    replace_bits(state.status_flags, pending.current, 0x0fU);
    for (std::uint32_t bit = 0U; bit < 4U; ++bit) {
        const auto current = masked_word(pending.current, 1U << bit);
        DvpVuWordV1 shifted{
            current.bits << 6U,
            current.known_mask << 6U,
        };
        const auto sticky_mask = 1U << (bit + 6U);
        const auto old_sticky = masked_word(state.status_flags, sticky_mask);
        replace_bits(state.status_flags,
                     bit_or(old_sticky, shifted),
                     sticky_mask);
    }
}

void commit_q(DvpVuExecutionStateV1& state, const PendingQ& pending) {
    state.scalar_q = pending.value;
    replace_bits(state.status_flags, pending.divide_current, 0x30U);
    for (std::uint32_t bit = 4U; bit < 6U; ++bit) {
        const auto current = masked_word(pending.divide_current, 1U << bit);
        const DvpVuWordV1 shifted{
            current.bits << 6U,
            current.known_mask << 6U,
        };
        const auto sticky_mask = 1U << (bit + 6U);
        replace_bits(
            state.status_flags,
            bit_or(masked_word(state.status_flags, sticky_mask), shifted),
            sticky_mask);
    }
}

void commit_store(DvpVuExecutionStateV1& state, const PendingStore& pending) {
    auto& destination = state.data_memory[pending.address];
    for (std::size_t lane = 0U; lane < kDvpVuLaneCount; ++lane) {
        if ((pending.destination_mask & lane_bit(lane)) != 0U) {
            destination.lanes[lane] = pending.value.lanes[lane];
        }
    }
}

template <typename Pending, typename Commit>
void commit_ready_queue(std::vector<Pending>& queue,
                        const std::uint64_t cycle,
                        Commit&& commit) {
    std::size_t committed = 0U;
    while (committed < queue.size() && queue[committed].ready_cycle <= cycle) {
        commit(queue[committed]);
        ++committed;
    }
    if (committed != 0U) {
        queue.erase(queue.begin(),
                    queue.begin() + static_cast<std::ptrdiff_t>(committed));
    }
}

void commit_ready(DvpVuExecutionStateV1& state,
                  Pipelines& pipelines,
                  const std::uint64_t cycle) {
    commit_ready_queue(pipelines.status,
                       cycle,
                       [&state](const PendingStatus& pending) {
                           commit_status(state, pending);
                       });
    commit_ready_queue(pipelines.clip,
                       cycle,
                       [&state](const PendingClip& pending) {
                           state.clip_flags =
                               masked_word(pending.value, kClipMask);
                       });
    commit_ready_queue(pipelines.q, cycle, [&state](const PendingQ& pending) {
        commit_q(state, pending);
    });
    commit_ready_queue(pipelines.stores,
                       cycle,
                       [&state](const PendingStore& pending) {
                           commit_store(state, pending);
                       });
}

void commit_all(DvpVuExecutionStateV1& state, Pipelines& pipelines) {
    for (const auto& pending : pipelines.status) {
        commit_status(state, pending);
    }
    for (const auto& pending : pipelines.clip) {
        state.clip_flags = masked_word(pending.value, kClipMask);
    }
    for (const auto& pending : pipelines.q) {
        commit_q(state, pending);
    }
    for (const auto& pending : pipelines.stores) {
        commit_store(state, pending);
    }
    pipelines = {};
}

void commit_all_stores(DvpVuExecutionStateV1& state, Pipelines& pipelines) {
    for (const auto& pending : pipelines.stores) {
        commit_store(state, pending);
    }
    pipelines.stores.clear();
}

[[nodiscard]] DvpVuWordV1 aggregate_flag(const DvpVuWordV1 mac,
                                         const std::uint32_t mask) {
    const auto known_one = mac.bits & mac.known_mask & mask;
    if (known_one != 0U) {
        return DvpVuWordV1{1U, 1U};
    }
    if ((mac.known_mask & mask) == mask) {
        return DvpVuWordV1{0U, 1U};
    }
    return {};
}

[[nodiscard]] PendingStatus
make_pending_status(const std::uint64_t ready_cycle,
                    const std::array<FloatOutcome, kDvpVuLaneCount>& outcomes,
                    const std::uint8_t destination_mask) {
    DvpVuWordV1 mac{0U, 0U};
    for (std::size_t lane = 0U; lane < kDvpVuLaneCount; ++lane) {
        const auto lane_position = static_cast<std::uint32_t>(3U - lane);
        const std::array<std::uint32_t, 4U> bits{
            1U << lane_position,
            1U << (lane_position + 4U),
            1U << (lane_position + 8U),
            1U << (lane_position + 12U),
        };
        const bool active = (destination_mask & lane_bit(lane)) != 0U;
        if (!active) {
            for (const auto bit : bits) {
                mac.known_mask |= bit;
            }
            continue;
        }
        if (!outcomes[lane].flags_known) {
            continue;
        }
        const std::array<bool, 4U> values{
            outcomes[lane].zero,
            outcomes[lane].sign,
            outcomes[lane].underflow,
            outcomes[lane].overflow,
        };
        for (std::size_t flag = 0U; flag < values.size(); ++flag) {
            mac.known_mask |= bits[flag];
            if (values[flag]) {
                mac.bits |= bits[flag];
            }
        }
    }

    DvpVuWordV1 current{0U, 0U};
    const std::array<std::uint32_t, 4U> groups{
        0x000fU,
        0x00f0U,
        0x0f00U,
        0xf000U,
    };
    for (std::uint32_t flag = 0U; flag < groups.size(); ++flag) {
        const auto aggregate = aggregate_flag(mac, groups[flag]);
        if ((aggregate.known_mask & 1U) != 0U) {
            current.known_mask |= 1U << flag;
            current.bits |= (aggregate.bits & 1U) << flag;
        }
    }
    return PendingStatus{ready_cycle, mac, current};
}

[[nodiscard]] DvpVuWordV1 upper_operand(const DvpVuUpperInstructionV1& upper,
                                        const DvpVuInstructionPairV1& pair,
                                        const RegisterSnapshot& before,
                                        const std::size_t lane) {
    switch (upper.operand_mode) {
    case DvpVuUpperOperandMode::vector:
        return before.vf[upper.ft].lanes[lane];
    case DvpVuUpperOperandMode::broadcast: {
        const auto selected = component_lane(upper.broadcast_component);
        return selected ? before.vf[upper.ft].lanes[*selected] : DvpVuWordV1{};
    }
    case DvpVuUpperOperandMode::scalar_i:
        return upper.immediate ? known_word(pair.raw_lower) : before.scalar_i;
    case DvpVuUpperOperandMode::scalar_q:
        return before.scalar_q;
    case DvpVuUpperOperandMode::none:
        return {};
    }
    return {};
}

void write_masked_vector(DvpVuVectorV1& destination,
                         const DvpVuVectorV1& source,
                         const std::uint8_t mask) {
    for (std::size_t lane = 0U; lane < kDvpVuLaneCount; ++lane) {
        if ((mask & lane_bit(lane)) != 0U) {
            destination.lanes[lane] = source.lanes[lane];
        }
    }
}

[[nodiscard]] bool is_arithmetic_upper(const DvpVuUpperOpcode opcode) {
    switch (opcode) {
    case DvpVuUpperOpcode::add:
    case DvpVuUpperOpcode::addi:
    case DvpVuUpperOpcode::addq:
    case DvpVuUpperOpcode::adda:
    case DvpVuUpperOpcode::addai:
    case DvpVuUpperOpcode::addaq:
    case DvpVuUpperOpcode::madd:
    case DvpVuUpperOpcode::maddi:
    case DvpVuUpperOpcode::maddq:
    case DvpVuUpperOpcode::madda:
    case DvpVuUpperOpcode::maddai:
    case DvpVuUpperOpcode::maddaq:
    case DvpVuUpperOpcode::msub:
    case DvpVuUpperOpcode::msubi:
    case DvpVuUpperOpcode::msubq:
    case DvpVuUpperOpcode::msuba:
    case DvpVuUpperOpcode::msubai:
    case DvpVuUpperOpcode::msubaq:
    case DvpVuUpperOpcode::mul:
    case DvpVuUpperOpcode::muli:
    case DvpVuUpperOpcode::mulq:
    case DvpVuUpperOpcode::mula:
    case DvpVuUpperOpcode::mulai:
    case DvpVuUpperOpcode::mulaq:
    case DvpVuUpperOpcode::sub:
    case DvpVuUpperOpcode::subi:
    case DvpVuUpperOpcode::subq:
    case DvpVuUpperOpcode::suba:
    case DvpVuUpperOpcode::subai:
    case DvpVuUpperOpcode::subaq:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool supported_upper(const DvpVuUpperOpcode opcode) {
    switch (opcode) {
    case DvpVuUpperOpcode::nop:
    case DvpVuUpperOpcode::abs:
    case DvpVuUpperOpcode::add:
    case DvpVuUpperOpcode::addi:
    case DvpVuUpperOpcode::addq:
    case DvpVuUpperOpcode::adda:
    case DvpVuUpperOpcode::addai:
    case DvpVuUpperOpcode::addaq:
    case DvpVuUpperOpcode::clip:
    case DvpVuUpperOpcode::ftoi0:
    case DvpVuUpperOpcode::ftoi4:
    case DvpVuUpperOpcode::ftoi12:
    case DvpVuUpperOpcode::ftoi15:
    case DvpVuUpperOpcode::itof0:
    case DvpVuUpperOpcode::itof4:
    case DvpVuUpperOpcode::itof12:
    case DvpVuUpperOpcode::itof15:
    case DvpVuUpperOpcode::madd:
    case DvpVuUpperOpcode::maddi:
    case DvpVuUpperOpcode::maddq:
    case DvpVuUpperOpcode::madda:
    case DvpVuUpperOpcode::maddai:
    case DvpVuUpperOpcode::maddaq:
    case DvpVuUpperOpcode::max:
    case DvpVuUpperOpcode::maxi:
    case DvpVuUpperOpcode::mini:
    case DvpVuUpperOpcode::minii:
    case DvpVuUpperOpcode::msub:
    case DvpVuUpperOpcode::msubi:
    case DvpVuUpperOpcode::msubq:
    case DvpVuUpperOpcode::msuba:
    case DvpVuUpperOpcode::msubai:
    case DvpVuUpperOpcode::msubaq:
    case DvpVuUpperOpcode::mul:
    case DvpVuUpperOpcode::muli:
    case DvpVuUpperOpcode::mulq:
    case DvpVuUpperOpcode::mula:
    case DvpVuUpperOpcode::mulai:
    case DvpVuUpperOpcode::mulaq:
    case DvpVuUpperOpcode::sub:
    case DvpVuUpperOpcode::subi:
    case DvpVuUpperOpcode::subq:
    case DvpVuUpperOpcode::suba:
    case DvpVuUpperOpcode::subai:
    case DvpVuUpperOpcode::subaq:
        return true;
    case DvpVuUpperOpcode::unknown:
    case DvpVuUpperOpcode::opmula:
    case DvpVuUpperOpcode::opmsub:
        return false;
    }
    return false;
}

[[nodiscard]] DvpVuWordV1
clip_flags_after(const RegisterSnapshot& before,
                 const DvpVuUpperInstructionV1& upper) {
    DvpVuWordV1 result{
        (before.clip_flags.bits << 6U) & kClipMask,
        (before.clip_flags.known_mask << 6U) & kClipMask,
    };
    const auto bound_word = before.vf[upper.ft].lanes[3U];
    for (std::size_t lane = 0U; lane < 3U; ++lane) {
        const auto positive_bit = 1U << (lane * 2U);
        const auto negative_bit = positive_bit << 1U;
        const auto source_word = before.vf[upper.fs].lanes[lane];
        if (!fully_known(source_word) || !fully_known(bound_word)) {
            continue;
        }
        const auto source =
            std::bit_cast<float>(normalize_vu_float_bits(source_word.bits));
        const auto bound_bits =
            normalize_vu_float_bits(bound_word.bits) & 0x7fffffffU;
        const auto bound = std::bit_cast<float>(bound_bits);
        result.known_mask |= positive_bit | negative_bit;
        if (source > bound) {
            result.bits |= positive_bit;
        }
        if (source < -bound) {
            result.bits |= negative_bit;
        }
    }
    return result;
}

[[nodiscard]] int conversion_fractional_bits(const DvpVuUpperOpcode opcode) {
    switch (opcode) {
    case DvpVuUpperOpcode::ftoi4:
    case DvpVuUpperOpcode::itof4:
        return 4;
    case DvpVuUpperOpcode::ftoi12:
    case DvpVuUpperOpcode::itof12:
        return 12;
    case DvpVuUpperOpcode::ftoi15:
    case DvpVuUpperOpcode::itof15:
        return 15;
    default:
        return 0;
    }
}

[[nodiscard]] bool is_ftoi(const DvpVuUpperOpcode opcode) {
    return opcode == DvpVuUpperOpcode::ftoi0 ||
           opcode == DvpVuUpperOpcode::ftoi4 ||
           opcode == DvpVuUpperOpcode::ftoi12 ||
           opcode == DvpVuUpperOpcode::ftoi15;
}

[[nodiscard]] bool is_itof(const DvpVuUpperOpcode opcode) {
    return opcode == DvpVuUpperOpcode::itof0 ||
           opcode == DvpVuUpperOpcode::itof4 ||
           opcode == DvpVuUpperOpcode::itof12 ||
           opcode == DvpVuUpperOpcode::itof15;
}

[[nodiscard]] bool is_accumulator_destination(const DvpVuUpperOpcode opcode) {
    switch (opcode) {
    case DvpVuUpperOpcode::adda:
    case DvpVuUpperOpcode::addai:
    case DvpVuUpperOpcode::addaq:
    case DvpVuUpperOpcode::madda:
    case DvpVuUpperOpcode::maddai:
    case DvpVuUpperOpcode::maddaq:
    case DvpVuUpperOpcode::msuba:
    case DvpVuUpperOpcode::msubai:
    case DvpVuUpperOpcode::msubaq:
    case DvpVuUpperOpcode::mula:
    case DvpVuUpperOpcode::mulai:
    case DvpVuUpperOpcode::mulaq:
    case DvpVuUpperOpcode::suba:
    case DvpVuUpperOpcode::subai:
    case DvpVuUpperOpcode::subaq:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_madd_family(const DvpVuUpperOpcode opcode) {
    switch (opcode) {
    case DvpVuUpperOpcode::madd:
    case DvpVuUpperOpcode::maddi:
    case DvpVuUpperOpcode::maddq:
    case DvpVuUpperOpcode::madda:
    case DvpVuUpperOpcode::maddai:
    case DvpVuUpperOpcode::maddaq:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_msub_family(const DvpVuUpperOpcode opcode) {
    switch (opcode) {
    case DvpVuUpperOpcode::msub:
    case DvpVuUpperOpcode::msubi:
    case DvpVuUpperOpcode::msubq:
    case DvpVuUpperOpcode::msuba:
    case DvpVuUpperOpcode::msubai:
    case DvpVuUpperOpcode::msubaq:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_multiply_family(const DvpVuUpperOpcode opcode) {
    switch (opcode) {
    case DvpVuUpperOpcode::mul:
    case DvpVuUpperOpcode::muli:
    case DvpVuUpperOpcode::mulq:
    case DvpVuUpperOpcode::mula:
    case DvpVuUpperOpcode::mulai:
    case DvpVuUpperOpcode::mulaq:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool is_add_family(const DvpVuUpperOpcode opcode) {
    switch (opcode) {
    case DvpVuUpperOpcode::add:
    case DvpVuUpperOpcode::addi:
    case DvpVuUpperOpcode::addq:
    case DvpVuUpperOpcode::adda:
    case DvpVuUpperOpcode::addai:
    case DvpVuUpperOpcode::addaq:
        return true;
    default:
        return false;
    }
}

void execute_upper(const DvpVuInstructionPairV1& pair,
                   const RegisterSnapshot& before,
                   DvpVuExecutionStateV1& state,
                   Pipelines& pipelines,
                   DvpVuExecutionResultV1& result,
                   const std::uint64_t cycle) {
    const auto& upper = pair.upper;
    if (upper.opcode == DvpVuUpperOpcode::nop) {
        return;
    }

    if (upper.opcode == DvpVuUpperOpcode::clip) {
        pipelines.clip.push_back(PendingClip{
            cycle + 4U,
            clip_flags_after(before, upper),
        });
        add_warning(result, DvpVuExecutionWarningV1::host_float_approximation);
        return;
    }

    if (upper.opcode == DvpVuUpperOpcode::abs) {
        DvpVuVectorV1 output;
        for (std::size_t lane = 0U; lane < kDvpVuLaneCount; ++lane) {
            const auto source = before.vf[upper.fs].lanes[lane];
            if (fully_known(source)) {
                output.lanes[lane] = known_word(source.bits & 0x7fffffffU);
            }
        }
        if (upper.ft != 0U) {
            write_masked_vector(state.vf[upper.ft],
                                output,
                                upper.destination_mask);
        }
        return;
    }

    if (is_ftoi(upper.opcode) || is_itof(upper.opcode)) {
        DvpVuVectorV1 output;
        const auto fractional_bits = conversion_fractional_bits(upper.opcode);
        for (std::size_t lane = 0U; lane < kDvpVuLaneCount; ++lane) {
            const auto source = before.vf[upper.fs].lanes[lane];
            output.lanes[lane] = is_ftoi(upper.opcode)
                                     ? convert_ftoi(source, fractional_bits)
                                     : convert_itof(source, fractional_bits);
        }
        if (upper.ft != 0U) {
            write_masked_vector(state.vf[upper.ft],
                                output,
                                upper.destination_mask);
        }
        add_warning(result, DvpVuExecutionWarningV1::host_float_approximation);
        return;
    }

    if (upper.opcode == DvpVuUpperOpcode::max ||
        upper.opcode == DvpVuUpperOpcode::maxi ||
        upper.opcode == DvpVuUpperOpcode::mini ||
        upper.opcode == DvpVuUpperOpcode::minii) {
        DvpVuVectorV1 output;
        const bool maximum = upper.opcode == DvpVuUpperOpcode::max ||
                             upper.opcode == DvpVuUpperOpcode::maxi;
        for (std::size_t lane = 0U; lane < kDvpVuLaneCount; ++lane) {
            output.lanes[lane] =
                float_minmax(before.vf[upper.fs].lanes[lane],
                             upper_operand(upper, pair, before, lane),
                             maximum);
        }
        if (upper.fd != 0U) {
            write_masked_vector(state.vf[upper.fd],
                                output,
                                upper.destination_mask);
        }
        add_warning(result, DvpVuExecutionWarningV1::host_float_approximation);
        return;
    }

    DvpVuVectorV1 output;
    std::array<FloatOutcome, kDvpVuLaneCount> outcomes{};
    for (std::size_t lane = 0U; lane < kDvpVuLaneCount; ++lane) {
        const auto left = before.vf[upper.fs].lanes[lane];
        const auto right = upper_operand(upper, pair, before, lane);
        if (is_add_family(upper.opcode)) {
            outcomes[lane] =
                float_binary(left, right, FloatBinaryOperation::add);
        } else if (is_multiply_family(upper.opcode)) {
            outcomes[lane] =
                float_binary(left, right, FloatBinaryOperation::multiply);
        } else if (is_madd_family(upper.opcode)) {
            outcomes[lane] =
                float_madd(before.accumulator.lanes[lane], left, right, false);
        } else if (is_msub_family(upper.opcode)) {
            outcomes[lane] =
                float_madd(before.accumulator.lanes[lane], left, right, true);
        } else {
            outcomes[lane] =
                float_binary(left, right, FloatBinaryOperation::subtract);
        }
        output.lanes[lane] = outcomes[lane].value;
    }

    if (is_accumulator_destination(upper.opcode)) {
        write_masked_vector(state.accumulator, output, upper.destination_mask);
    } else if (upper.fd != 0U) {
        write_masked_vector(state.vf[upper.fd], output, upper.destination_mask);
    }
    if (is_arithmetic_upper(upper.opcode)) {
        pipelines.status.push_back(
            make_pending_status(cycle + 4U, outcomes, upper.destination_mask));
    }
    if (upper.operand_mode == DvpVuUpperOperandMode::scalar_q &&
        !pipelines.q.empty() && pipelines.q.front().ready_cycle > cycle) {
        add_warning(result, DvpVuExecutionWarningV1::q_read_before_ready);
    }
    add_warning(result, DvpVuExecutionWarningV1::host_float_approximation);
}

[[nodiscard]] bool supported_lower(const DvpVuLowerInstructionV1& lower) {
    if (lower.kind == DvpVuLowerKind::immediate_literal) {
        return true;
    }
    switch (lower.opcode) {
    case DvpVuLowerOpcode::nop:
    case DvpVuLowerOpcode::b:
    case DvpVuLowerOpcode::bal:
    case DvpVuLowerOpcode::div:
    case DvpVuLowerOpcode::fcand:
    case DvpVuLowerOpcode::fcset:
    case DvpVuLowerOpcode::fsand:
    case DvpVuLowerOpcode::iadd:
    case DvpVuLowerOpcode::iaddi:
    case DvpVuLowerOpcode::iaddiu:
    case DvpVuLowerOpcode::iand:
    case DvpVuLowerOpcode::ibeq:
    case DvpVuLowerOpcode::ibgez:
    case DvpVuLowerOpcode::ibgtz:
    case DvpVuLowerOpcode::iblez:
    case DvpVuLowerOpcode::ibltz:
    case DvpVuLowerOpcode::ibne:
    case DvpVuLowerOpcode::ilw:
    case DvpVuLowerOpcode::ilwr:
    case DvpVuLowerOpcode::ior:
    case DvpVuLowerOpcode::isub:
    case DvpVuLowerOpcode::isubiu:
    case DvpVuLowerOpcode::isw:
    case DvpVuLowerOpcode::iswr:
    case DvpVuLowerOpcode::jalr:
    case DvpVuLowerOpcode::jr:
    case DvpVuLowerOpcode::lq:
    case DvpVuLowerOpcode::lqd:
    case DvpVuLowerOpcode::lqi:
    case DvpVuLowerOpcode::mfir:
    case DvpVuLowerOpcode::move:
    case DvpVuLowerOpcode::mr32:
    case DvpVuLowerOpcode::mtir:
    case DvpVuLowerOpcode::sq:
    case DvpVuLowerOpcode::sqd:
    case DvpVuLowerOpcode::sqi:
    case DvpVuLowerOpcode::xgkick:
    case DvpVuLowerOpcode::xtop:
        return true;
    case DvpVuLowerOpcode::unknown:
    case DvpVuLowerOpcode::fceq:
    case DvpVuLowerOpcode::fcor:
    case DvpVuLowerOpcode::fseq:
    case DvpVuLowerOpcode::fsor:
    case DvpVuLowerOpcode::fsset:
    case DvpVuLowerOpcode::xitop:
        return false;
    }
    return false;
}

[[nodiscard]] bool is_memory_operation(const DvpVuLowerOpcode opcode) {
    switch (opcode) {
    case DvpVuLowerOpcode::ilw:
    case DvpVuLowerOpcode::ilwr:
    case DvpVuLowerOpcode::isw:
    case DvpVuLowerOpcode::iswr:
    case DvpVuLowerOpcode::lq:
    case DvpVuLowerOpcode::lqd:
    case DvpVuLowerOpcode::lqi:
    case DvpVuLowerOpcode::sq:
    case DvpVuLowerOpcode::sqd:
    case DvpVuLowerOpcode::sqi:
        return true;
    default:
        return false;
    }
}

void write_vi(DvpVuExecutionStateV1& state,
              const std::uint8_t raw_register,
              const DvpVuWordV1 value) {
    const auto destination = static_cast<std::uint8_t>(raw_register & 0x0fU);
    if (destination == 0U) {
        return;
    }
    state.vi[destination] =
        DvpVuWordV1{value.bits & kViMask, value.known_mask & kViMask};
}

[[nodiscard]] DvpVuWordV1 vi_binary(const DvpVuWordV1 left,
                                    const DvpVuWordV1 right,
                                    const DvpVuLowerOpcode opcode) {
    if (opcode == DvpVuLowerOpcode::iand) {
        return masked_word(bit_and(left, right), kViMask);
    }
    if (opcode == DvpVuLowerOpcode::ior) {
        return masked_word(bit_or(left, right), kViMask);
    }
    if (!fully_known(left, kViMask) || !fully_known(right, kViMask)) {
        return {};
    }
    const auto left_value = static_cast<std::uint16_t>(left.bits);
    const auto right_value = static_cast<std::uint16_t>(right.bits);
    const auto result =
        opcode == DvpVuLowerOpcode::isub
            ? static_cast<std::uint16_t>(left_value - right_value)
            : static_cast<std::uint16_t>(left_value + right_value);
    return known_vi(result);
}

[[nodiscard]] std::optional<std::uint16_t>
memory_address(const DvpVuWordV1 base, const std::int32_t offset) {
    if (!fully_known(base, 0x03ffU)) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(base.bits)) +
         static_cast<std::uint32_t>(offset)) &
        0x03ffU);
}

[[nodiscard]] DvpVuWordV1 increment_vi(const DvpVuWordV1 value,
                                       const std::int32_t delta) {
    const auto addend = static_cast<std::uint16_t>(delta);
    DvpVuWordV1 result{0U, 0U};
    bool carry_can_be_zero = true;
    bool carry_can_be_one = false;
    for (std::uint32_t bit = 0U; bit < 16U; ++bit) {
        const auto mask = 1U << bit;
        const bool input_known = (value.known_mask & mask) != 0U;
        const bool input_can_be_zero =
            !input_known || (value.bits & mask) == 0U;
        const bool input_can_be_one = !input_known || (value.bits & mask) != 0U;
        const bool addend_bit = (addend & mask) != 0U;
        bool result_can_be_zero = false;
        bool result_can_be_one = false;
        bool next_carry_can_be_zero = false;
        bool next_carry_can_be_one = false;
        for (std::uint32_t input = 0U; input < 2U; ++input) {
            if ((input == 0U && !input_can_be_zero) ||
                (input == 1U && !input_can_be_one)) {
                continue;
            }
            for (std::uint32_t carry = 0U; carry < 2U; ++carry) {
                if ((carry == 0U && !carry_can_be_zero) ||
                    (carry == 1U && !carry_can_be_one)) {
                    continue;
                }
                const auto sum = input + (addend_bit ? 1U : 0U) + carry;
                result_can_be_zero |= (sum & 1U) == 0U;
                result_can_be_one |= (sum & 1U) != 0U;
                next_carry_can_be_zero |= sum < 2U;
                next_carry_can_be_one |= sum >= 2U;
            }
        }
        if (result_can_be_zero != result_can_be_one) {
            result.known_mask |= mask;
            if (result_can_be_one) {
                result.bits |= mask;
            }
        }
        carry_can_be_zero = next_carry_can_be_zero;
        carry_can_be_one = next_carry_can_be_one;
    }
    return result;
}

[[nodiscard]] std::optional<bool> branch_equal(const DvpVuWordV1 left,
                                               const DvpVuWordV1 right) {
    const auto common_known = left.known_mask & right.known_mask & kViMask;
    if (((left.bits ^ right.bits) & common_known) != 0U) {
        return false;
    }
    if (fully_known(left, kViMask) && fully_known(right, kViMask)) {
        return true;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<bool>
branch_signed_zero(const DvpVuWordV1 value, const DvpVuLowerOpcode opcode) {
    const bool sign_known = (value.known_mask & 0x8000U) != 0U;
    const bool negative = (value.bits & 0x8000U) != 0U;
    if (opcode == DvpVuLowerOpcode::ibltz && sign_known) {
        return negative;
    }
    if (opcode == DvpVuLowerOpcode::ibgez && sign_known) {
        return !negative;
    }
    if (opcode == DvpVuLowerOpcode::iblez && sign_known && negative) {
        return true;
    }
    if (opcode == DvpVuLowerOpcode::ibgtz && sign_known && negative) {
        return false;
    }
    if (!fully_known(value, kViMask)) {
        return std::nullopt;
    }
    const auto signed_value =
        static_cast<std::int16_t>(static_cast<std::uint16_t>(value.bits));
    switch (opcode) {
    case DvpVuLowerOpcode::ibgez:
        return signed_value >= 0;
    case DvpVuLowerOpcode::ibgtz:
        return signed_value > 0;
    case DvpVuLowerOpcode::iblez:
        return signed_value <= 0;
    case DvpVuLowerOpcode::ibltz:
        return signed_value < 0;
    default:
        return std::nullopt;
    }
}

struct DivideOutcome {
    DvpVuWordV1 value;
    DvpVuWordV1 current;
};

[[nodiscard]] DivideOutcome divide_vu(const DvpVuWordV1 numerator,
                                      const DvpVuWordV1 denominator) {
    if (!fully_known(numerator) || !fully_known(denominator)) {
        return {};
    }
    const auto numerator_bits = normalize_vu_float_bits(numerator.bits);
    const auto denominator_bits = normalize_vu_float_bits(denominator.bits);
    const bool numerator_zero = (numerator_bits & 0x7fffffffU) == 0U;
    const bool denominator_zero = (denominator_bits & 0x7fffffffU) == 0U;
    const auto quotient_sign =
        (numerator_bits ^ denominator_bits) & 0x80000000U;

    DivideOutcome result;
    result.current = DvpVuWordV1{0U, 0x30U};
    if (denominator_zero) {
        result.value = known_word(quotient_sign | 0x7f7fffffU);
        result.current.bits = numerator_zero ? 0x10U : 0x20U;
        return result;
    }

    const auto left = std::bit_cast<float>(numerator_bits);
    const auto right = std::bit_cast<float>(denominator_bits);
    const auto quotient = left / right;
    result.value = known_word(
        normalize_vu_float_bits(std::bit_cast<std::uint32_t>(quotient)));
    return result;
}

[[nodiscard]] std::optional<DvpVuTerminationV1>
capture_xgkick_packet(const DvpVuExecutionStateV1& state,
                      const std::uint16_t base,
                      const DvpVuExecutionLimitsV1& limits,
                      DvpVuXgkickEventV1& event) {
    auto cursor = static_cast<std::uint16_t>(base & 0x03ffU);
    const auto advance = [&cursor, &event]() {
        if (cursor == 0x03ffU) {
            event.wrapped_memory = true;
        }
        cursor = static_cast<std::uint16_t>((cursor + 1U) & 0x03ffU);
    };
    const auto append_qword = [&state, &limits, &event, &cursor, &advance]()
        -> std::optional<DvpVuTerminationV1> {
        if (event.packet_qwords.size() >= limits.max_xgkick_qwords_per_event) {
            return DvpVuTerminationV1::xgkick_qword_limit;
        }
        event.packet_qwords.push_back(state.data_memory[cursor]);
        advance();
        return std::nullopt;
    };

    for (;;) {
        if (event.tags.size() >= limits.max_xgkick_tags_per_event) {
            return DvpVuTerminationV1::xgkick_tag_limit;
        }
        const auto tag_address = cursor;
        if (const auto stop = append_qword()) {
            return stop;
        }
        const auto& tag_qword = event.packet_qwords.back();
        if (!fully_known(tag_qword.lanes[0U]) ||
            !fully_known(tag_qword.lanes[1U])) {
            event.encountered_indeterminate_tag = true;
            return std::nullopt;
        }

        const auto low64 =
            static_cast<std::uint64_t>(tag_qword.lanes[0U].bits) |
            (static_cast<std::uint64_t>(tag_qword.lanes[1U].bits) << 32U);
        DvpVuGifTagV1 tag;
        tag.nloop = static_cast<std::uint16_t>(low64 & 0x7fffU);
        tag.eop = ((low64 >> 15U) & 1U) != 0U;
        tag.pre = ((low64 >> 46U) & 1U) != 0U;
        tag.prim = static_cast<std::uint16_t>((low64 >> 47U) & 0x07ffU);
        tag.format = static_cast<std::uint8_t>((low64 >> 58U) & 0x03U);
        const auto raw_register_count =
            static_cast<std::uint8_t>((low64 >> 60U) & 0x0fU);
        tag.register_count =
            raw_register_count == 0U ? 16U : raw_register_count;
        if (fully_known(tag_qword.lanes[2U]) &&
            fully_known(tag_qword.lanes[3U])) {
            const auto high64 =
                static_cast<std::uint64_t>(tag_qword.lanes[2U].bits) |
                (static_cast<std::uint64_t>(tag_qword.lanes[3U].bits)
                 << 32U);
            for (std::size_t index = 0U; index < tag.registers.size();
                 ++index) {
                tag.registers[index] = static_cast<std::uint8_t>(
                    (high64 >> (index * 4U)) & 0x0fU);
            }
            tag.registers_known = true;
        }

        const auto register_items =
            static_cast<std::uint64_t>(tag.nloop) *
            static_cast<std::uint64_t>(tag.register_count);
        switch (tag.format) {
        case 0U:
            tag.payload_qword_count = register_items;
            break;
        case 1U:
            tag.payload_qword_count = (register_items + 1U) / 2U;
            break;
        case 2U:
        case 3U:
            tag.payload_qword_count = tag.nloop;
            break;
        default:
            return DvpVuTerminationV1::xgkick_qword_limit;
        }
        event.tags.push_back(DvpVuXgkickTagV1{
            tag_address,
            tag,
            static_cast<std::uint64_t>(event.packet_qwords.size() - 1U),
        });

        for (std::uint64_t index = 0U; index < tag.payload_qword_count;
             ++index) {
            if (const auto stop = append_qword()) {
                return stop;
            }
        }
        if (tag.eop) {
            event.packet_complete = true;
            return std::nullopt;
        }
    }
}

struct PendingControl {
    bool end = false;
    bool target_known = false;
    std::uint16_t target = 0U;
};

struct LowerOutcome {
    std::optional<PendingControl> control;
    std::optional<DvpVuTerminationV1> stop;
};

[[nodiscard]] std::uint16_t
direct_branch_target(const DvpVuInstructionPairV1& pair) {
    return static_cast<std::uint16_t>(
        (static_cast<std::int32_t>(pair.instruction_address) + 1 +
         pair.lower.signed_immediate) &
        0x07ff);
}

void execute_vector_load(const DvpVuLowerInstructionV1& lower,
                         const RegisterSnapshot& before,
                         DvpVuExecutionStateV1& state,
                         const std::uint16_t address) {
    if (lower.it == 0U) {
        return;
    }
    write_masked_vector(state.vf[lower.it],
                        state.data_memory[address],
                        lower.destination_mask);
    (void)before;
}

[[nodiscard]] LowerOutcome execute_lower(const DvpVuInstructionPairV1& pair,
                                         const RegisterSnapshot& before,
                                         DvpVuExecutionStateV1& state,
                                         Pipelines& pipelines,
                                         DvpVuExecutionResultV1& result,
                                         const DvpVuExecutionOptionsV1& options,
                                         const DvpVuExecutionLimitsV1& limits,
                                         const std::uint64_t cycle) {
    const auto& lower = pair.lower;
    if (lower.kind == DvpVuLowerKind::immediate_literal) {
        state.scalar_i = known_word(lower.unsigned_immediate);
        return {};
    }

    const auto source_is = static_cast<std::uint8_t>(lower.is & 0x0fU);
    const auto source_it = static_cast<std::uint8_t>(lower.it & 0x0fU);
    const auto destination_id = static_cast<std::uint8_t>(lower.id & 0x0fU);

    switch (lower.opcode) {
    case DvpVuLowerOpcode::nop:
        break;
    case DvpVuLowerOpcode::iadd:
    case DvpVuLowerOpcode::iand:
    case DvpVuLowerOpcode::ior:
    case DvpVuLowerOpcode::isub:
        write_vi(state,
                 destination_id,
                 vi_binary(before.vi[source_is],
                           before.vi[source_it],
                           lower.opcode));
        break;
    case DvpVuLowerOpcode::iaddi: {
        DvpVuWordV1 immediate =
            known_vi(static_cast<std::uint16_t>(lower.signed_immediate));
        write_vi(
            state,
            source_it,
            vi_binary(before.vi[source_is], immediate, DvpVuLowerOpcode::iadd));
        break;
    }
    case DvpVuLowerOpcode::iaddiu:
    case DvpVuLowerOpcode::isubiu: {
        DvpVuWordV1 immediate =
            known_vi(static_cast<std::uint16_t>(lower.unsigned_immediate));
        write_vi(state,
                 source_it,
                 vi_binary(before.vi[source_is],
                           immediate,
                           lower.opcode == DvpVuLowerOpcode::isubiu
                               ? DvpVuLowerOpcode::isub
                               : DvpVuLowerOpcode::iadd));
        break;
    }
    case DvpVuLowerOpcode::move:
        if (lower.it != 0U) {
            write_masked_vector(state.vf[lower.it],
                                before.vf[lower.is],
                                lower.destination_mask);
        }
        break;
    case DvpVuLowerOpcode::mr32:
        if (lower.it != 0U) {
            DvpVuVectorV1 rotated;
            rotated.lanes[0U] = before.vf[lower.is].lanes[1U];
            rotated.lanes[1U] = before.vf[lower.is].lanes[2U];
            rotated.lanes[2U] = before.vf[lower.is].lanes[3U];
            rotated.lanes[3U] = before.vf[lower.is].lanes[0U];
            write_masked_vector(state.vf[lower.it],
                                rotated,
                                lower.destination_mask);
        }
        break;
    case DvpVuLowerOpcode::mfir:
        if (lower.it != 0U) {
            DvpVuWordV1 expanded;
            const auto source = before.vi[source_is];
            if (fully_known(source, kViMask)) {
                const auto signed_value =
                    static_cast<std::int32_t>(static_cast<std::int16_t>(
                        static_cast<std::uint16_t>(source.bits)));
                expanded = known_word(static_cast<std::uint32_t>(signed_value));
            }
            DvpVuVectorV1 vector;
            vector.lanes.fill(expanded);
            write_masked_vector(state.vf[lower.it],
                                vector,
                                lower.destination_mask);
        }
        break;
    case DvpVuLowerOpcode::mtir: {
        const auto lane = component_lane(lower.fs_component);
        if (!lane) {
            return LowerOutcome{
                std::nullopt,
                DvpVuTerminationV1::unsupported_instruction,
            };
        }
        const auto source = before.vf[lower.is].lanes[*lane];
        write_vi(state,
                 source_it,
                 DvpVuWordV1{
                     source.bits & kViMask,
                     source.known_mask & kViMask,
                 });
        break;
    }
    case DvpVuLowerOpcode::xtop:
        write_vi(state,
                 source_it,
                 DvpVuWordV1{
                     before.xtop_qword.bits & 0x03ffU,
                     (before.xtop_qword.known_mask & 0x03ffU) | 0xfc00U,
                 });
        break;
    case DvpVuLowerOpcode::fsand:
        write_vi(state,
                 source_it,
                 masked_word(bit_and(before.status_flags,
                                     known_word(lower.unsigned_immediate)),
                             kViMask));
        break;
    case DvpVuLowerOpcode::fcand: {
        const auto mask = lower.unsigned_immediate & kClipMask;
        const auto known_one =
            before.clip_flags.bits & before.clip_flags.known_mask & mask;
        DvpVuWordV1 value;
        if (known_one != 0U) {
            value = known_vi(1U);
        } else if ((before.clip_flags.known_mask & mask) == mask) {
            value = known_vi(0U);
        }
        write_vi(state, 1U, value);
        break;
    }
    case DvpVuLowerOpcode::fcset:
        pipelines.clip.push_back(PendingClip{
            cycle + 4U,
            DvpVuWordV1{
                lower.unsigned_immediate & kClipMask,
                kClipMask,
            },
        });
        break;
    case DvpVuLowerOpcode::div: {
        const auto numerator_lane = component_lane(lower.fs_component);
        const auto denominator_lane = component_lane(lower.ft_component);
        if (!numerator_lane || !denominator_lane) {
            return LowerOutcome{
                std::nullopt,
                DvpVuTerminationV1::unsupported_instruction,
            };
        }
        const auto divided =
            divide_vu(before.vf[lower.is].lanes[*numerator_lane],
                      before.vf[lower.it].lanes[*denominator_lane]);
        pipelines.q.push_back(PendingQ{
            cycle + 7U,
            divided.value,
            divided.current,
        });
        add_warning(result, DvpVuExecutionWarningV1::host_float_approximation);
        break;
    }
    case DvpVuLowerOpcode::lq:
    case DvpVuLowerOpcode::lqd:
    case DvpVuLowerOpcode::lqi: {
        const bool pre_decrement = lower.opcode == DvpVuLowerOpcode::lqd;
        const bool post_increment = lower.opcode == DvpVuLowerOpcode::lqi;
        const auto base = before.vi[source_is];
        const auto offset = lower.opcode == DvpVuLowerOpcode::lq
                                ? lower.signed_immediate
                                : (pre_decrement ? -1 : 0);
        const auto address = memory_address(base, offset);
        if (!address) {
            return LowerOutcome{
                std::nullopt,
                DvpVuTerminationV1::indeterminate_memory_address,
            };
        }
        if (pre_decrement) {
            write_vi(state, source_is, increment_vi(base, -1));
        }
        execute_vector_load(lower, before, state, *address);
        if (post_increment) {
            write_vi(state, source_is, increment_vi(base, 1));
        }
        break;
    }
    case DvpVuLowerOpcode::sq:
    case DvpVuLowerOpcode::sqd:
    case DvpVuLowerOpcode::sqi: {
        const auto base_register = source_it;
        const auto base = before.vi[base_register];
        const bool pre_decrement = lower.opcode == DvpVuLowerOpcode::sqd;
        const bool post_increment = lower.opcode == DvpVuLowerOpcode::sqi;
        const auto offset = lower.opcode == DvpVuLowerOpcode::sq
                                ? lower.signed_immediate
                                : (pre_decrement ? -1 : 0);
        const auto address = memory_address(base, offset);
        if (!address) {
            return LowerOutcome{
                std::nullopt,
                DvpVuTerminationV1::indeterminate_memory_address,
            };
        }
        pipelines.stores.push_back(PendingStore{
            cycle + 4U,
            *address,
            lower.destination_mask,
            before.vf[lower.is],
        });
        if (pre_decrement) {
            write_vi(state, base_register, increment_vi(base, -1));
        } else if (post_increment) {
            write_vi(state, base_register, increment_vi(base, 1));
        }
        break;
    }
    case DvpVuLowerOpcode::ilw:
    case DvpVuLowerOpcode::ilwr: {
        if (std::popcount(lower.destination_mask) != 1) {
            return LowerOutcome{
                std::nullopt,
                DvpVuTerminationV1::unsupported_instruction,
            };
        }
        const auto address = memory_address(
            before.vi[source_is],
            lower.opcode == DvpVuLowerOpcode::ilw ? lower.signed_immediate : 0);
        if (!address) {
            return LowerOutcome{
                std::nullopt,
                DvpVuTerminationV1::indeterminate_memory_address,
            };
        }
        for (std::size_t lane = 0U; lane < kDvpVuLaneCount; ++lane) {
            if ((lower.destination_mask & lane_bit(lane)) != 0U) {
                const auto value = state.data_memory[*address].lanes[lane];
                write_vi(state,
                         source_it,
                         DvpVuWordV1{
                             value.bits & kViMask,
                             value.known_mask & kViMask,
                         });
            }
        }
        break;
    }
    case DvpVuLowerOpcode::isw:
    case DvpVuLowerOpcode::iswr: {
        const auto address = memory_address(
            before.vi[source_is],
            lower.opcode == DvpVuLowerOpcode::isw ? lower.signed_immediate : 0);
        if (!address) {
            return LowerOutcome{
                std::nullopt,
                DvpVuTerminationV1::indeterminate_memory_address,
            };
        }
        DvpVuVectorV1 stored;
        const auto value = before.vi[source_it];
        const DvpVuWordV1 widened{
            value.bits & kViMask,
            (value.known_mask & kViMask) | 0xffff0000U,
        };
        stored.lanes.fill(widened);
        pipelines.stores.push_back(PendingStore{
            cycle + 4U,
            *address,
            lower.destination_mask,
            stored,
        });
        break;
    }
    case DvpVuLowerOpcode::b:
    case DvpVuLowerOpcode::bal: {
        if (lower.opcode == DvpVuLowerOpcode::bal) {
            write_vi(state,
                     source_it,
                     known_vi(static_cast<std::uint16_t>(
                         (pair.instruction_address + 2U) & 0x07ffU)));
        }
        return LowerOutcome{
            PendingControl{
                false,
                true,
                direct_branch_target(pair),
            },
            std::nullopt,
        };
    }
    case DvpVuLowerOpcode::ibeq:
    case DvpVuLowerOpcode::ibne:
    case DvpVuLowerOpcode::ibgez:
    case DvpVuLowerOpcode::ibgtz:
    case DvpVuLowerOpcode::iblez:
    case DvpVuLowerOpcode::ibltz: {
        std::optional<bool> taken;
        if (lower.opcode == DvpVuLowerOpcode::ibeq ||
            lower.opcode == DvpVuLowerOpcode::ibne) {
            taken = branch_equal(before.vi[source_is], before.vi[source_it]);
            if (taken && lower.opcode == DvpVuLowerOpcode::ibne) {
                *taken = !*taken;
            }
        } else {
            taken = branch_signed_zero(before.vi[source_is], lower.opcode);
        }
        PendingControl control;
        control.target_known = taken.has_value();
        if (taken) {
            control.target =
                *taken ? direct_branch_target(pair)
                       : static_cast<std::uint16_t>(
                             (pair.instruction_address + 2U) & 0x07ffU);
        }
        return LowerOutcome{control, std::nullopt};
    }
    case DvpVuLowerOpcode::jr:
    case DvpVuLowerOpcode::jalr: {
        PendingControl control;
        const auto target = before.vi[source_is];
        control.target_known = fully_known(target, 0x07ffU);
        control.target = static_cast<std::uint16_t>(target.bits & 0x07ffU);
        if (lower.opcode == DvpVuLowerOpcode::jalr) {
            write_vi(state,
                     source_it,
                     known_vi(static_cast<std::uint16_t>(
                         (pair.instruction_address + 2U) & 0x07ffU)));
        }
        return LowerOutcome{control, std::nullopt};
    }
    case DvpVuLowerOpcode::xgkick: {
        if (result.xgkick_events.size() >= limits.max_xgkick_events) {
            return LowerOutcome{
                std::nullopt,
                DvpVuTerminationV1::xgkick_event_limit,
            };
        }
        if (!pipelines.stores.empty()) {
            add_warning(result,
                        DvpVuExecutionWarningV1::
                            forced_store_commit_for_xgkick_snapshot);
            commit_all_stores(state, pipelines);
        }
        DvpVuXgkickEventV1 event;
        event.instruction_address = pair.instruction_address;
        event.base_qword = DvpVuWordV1{
            before.vi[source_is].bits & 0x03ffU,
            (before.vi[source_is].known_mask & 0x03ffU) | 0xfffffc00U,
        };
        if (!fully_known(before.vi[source_is], 0x03ffU)) {
            result.xgkick_events.push_back(std::move(event));
            return LowerOutcome{
                std::nullopt,
                DvpVuTerminationV1::indeterminate_memory_address,
            };
        }
        const auto base =
            static_cast<std::uint16_t>(before.vi[source_is].bits & 0x03ffU);
        const auto capture_stop =
            capture_xgkick_packet(state, base, limits, event);
        result.xgkick_events.push_back(std::move(event));
        if (capture_stop) {
            return LowerOutcome{std::nullopt, capture_stop};
        }
        if (options.stop_after_first_xgkick) {
            return LowerOutcome{
                std::nullopt,
                DvpVuTerminationV1::stopped_after_xgkick,
            };
        }
        break;
    }
    case DvpVuLowerOpcode::unknown:
    case DvpVuLowerOpcode::fceq:
    case DvpVuLowerOpcode::fcor:
    case DvpVuLowerOpcode::fseq:
    case DvpVuLowerOpcode::fsor:
    case DvpVuLowerOpcode::fsset:
    case DvpVuLowerOpcode::xitop:
        return LowerOutcome{
            std::nullopt,
            DvpVuTerminationV1::unsupported_instruction,
        };
    }
    return {};
}

[[nodiscard]] bool is_control_lower(const DvpVuLowerOpcode opcode) {
    switch (opcode) {
    case DvpVuLowerOpcode::b:
    case DvpVuLowerOpcode::bal:
    case DvpVuLowerOpcode::ibeq:
    case DvpVuLowerOpcode::ibgez:
    case DvpVuLowerOpcode::ibgtz:
    case DvpVuLowerOpcode::iblez:
    case DvpVuLowerOpcode::ibltz:
    case DvpVuLowerOpcode::ibne:
    case DvpVuLowerOpcode::jalr:
    case DvpVuLowerOpcode::jr:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool valid_entrypoint(const DvpVuProgramV1& program,
                                    const std::uint16_t address) {
    return std::any_of(program.entrypoints.begin(),
                       program.entrypoints.end(),
                       [address](const DvpVuEntrypointV1& entrypoint) {
                           return entrypoint.instruction_address == address;
                       });
}

void validate_instruction_fields(const DvpVuInstructionPairV1& instruction) {
    const auto& upper = instruction.upper;
    const auto& lower = instruction.lower;
    if (upper.raw_flags > 0x1fU || upper.destination_mask > 0x0fU ||
        upper.ft >= kDvpVuVectorRegisterCount ||
        upper.fs >= kDvpVuVectorRegisterCount ||
        upper.fd >= kDvpVuVectorRegisterCount ||
        static_cast<std::uint8_t>(upper.opcode) >
            static_cast<std::uint8_t>(DvpVuUpperOpcode::subaq) ||
        static_cast<std::uint8_t>(upper.operand_mode) >
            static_cast<std::uint8_t>(DvpVuUpperOperandMode::scalar_q) ||
        static_cast<std::uint8_t>(upper.broadcast_component) >
            static_cast<std::uint8_t>(DvpVuComponent::w) ||
        lower.destination_mask > 0x0fU ||
        lower.it >= kDvpVuVectorRegisterCount ||
        lower.is >= kDvpVuVectorRegisterCount ||
        lower.id >= kDvpVuVectorRegisterCount ||
        static_cast<std::uint8_t>(lower.kind) >
            static_cast<std::uint8_t>(DvpVuLowerKind::immediate_literal) ||
        static_cast<std::uint8_t>(lower.opcode) >
            static_cast<std::uint8_t>(DvpVuLowerOpcode::xtop) ||
        static_cast<std::uint8_t>(lower.ft_component) >
            static_cast<std::uint8_t>(DvpVuComponent::w) ||
        static_cast<std::uint8_t>(lower.fs_component) >
            static_cast<std::uint8_t>(DvpVuComponent::w)) {
        throw DvpVuExecutionError(
            "Decoded DVP VU program contains an invalid instruction field");
    }
}

} // namespace

DvpVuExecutionStateV1 make_dvp_vu_execution_state_v1() {
    DvpVuExecutionStateV1 state;
    enforce_architectural_constants(state);
    return state;
}

void apply_scene_block_dvp_vu_writes_v1(
    DvpVuExecutionStateV1& state,
    const SceneBlockVuSnapshotV1& snapshot,
    const std::uint64_t first_write_index,
    const std::uint64_t write_count,
    const SceneBlockDvpVuBridgeLimitsV1 limits) {
    if (limits.max_replayed_writes == 0U) {
        throw DvpVuExecutionError(
            "SceneBlock DVP VU bridge replay limit must be non-zero");
    }

    const auto available_writes =
        static_cast<std::uint64_t>(snapshot.writes.size());
    if (first_write_index > available_writes ||
        write_count > available_writes - first_write_index) {
        throw DvpVuExecutionError(
            "SceneBlock DVP VU write range exceeds the snapshot");
    }
    if (write_count > limits.max_replayed_writes) {
        throw DvpVuExecutionError(
            "SceneBlock DVP VU bridge replay limit exceeded");
    }

    for (std::uint64_t offset = 0U; offset < write_count; ++offset) {
        const auto& write = snapshot.writes[static_cast<std::size_t>(
            first_write_index + offset)];
        if (write.destination_qword >= kDvpVuDataMemoryQwordCount) {
            throw DvpVuExecutionError(
                "SceneBlock snapshot contains an invalid VU1 address");
        }
        for (const auto& lane : write.lanes) {
            switch (lane.written_value.state) {
            case SceneBlockVuValueState::indeterminate:
            case SceneBlockVuValueState::known:
                break;
            default:
                throw DvpVuExecutionError(
                    "SceneBlock snapshot contains an invalid lane state");
            }
        }
    }

    for (std::uint64_t offset = 0U; offset < write_count; ++offset) {
        const auto& write = snapshot.writes[static_cast<std::size_t>(
            first_write_index + offset)];
        auto& destination = state.data_memory[write.destination_qword];
        for (std::size_t lane = 0U; lane < kDvpVuLaneCount; ++lane) {
            const auto& source = write.lanes[lane].written_value;
            destination.lanes[lane] =
                source.state == SceneBlockVuValueState::known
                    ? known_word(source.bits)
                    : DvpVuWordV1{};
        }
    }
}

SceneBlockDvpVuInvocationV1 make_scene_block_dvp_vu_invocation_v1(
    const SceneBlockVuSnapshotV1& snapshot,
    const DvpVuProgramV1& program,
    const SceneBlockDvpVuInvocationOptionsV1 options,
    const SceneBlockDvpVuBridgeLimitsV1 limits) {
    if (options.write_prefix_count > snapshot.writes.size()) {
        throw DvpVuExecutionError(
            "SceneBlock DVP VU write prefix exceeds the snapshot");
    }
    if (options.write_prefix_count > limits.max_replayed_writes) {
        throw DvpVuExecutionError(
            "SceneBlock DVP VU bridge replay limit exceeded");
    }
    if (!valid_entrypoint(program, options.entrypoint_address)) {
        throw DvpVuExecutionError(
            "SceneBlock DVP VU entrypoint is not in the decoded program");
    }
    if ((options.xtop_qword.bits & options.xtop_qword.known_mask & ~0x03ffU) !=
        0U) {
        throw DvpVuExecutionError(
            "SceneBlock DVP VU TOP is outside VU1 data memory");
    }

    SceneBlockDvpVuInvocationV1 invocation;
    invocation.initial_state = make_dvp_vu_execution_state_v1();
    invocation.initial_state.pc = options.entrypoint_address;
    invocation.initial_state.xtop_qword = DvpVuWordV1{
        options.xtop_qword.bits & 0x03ffU,
        (options.xtop_qword.known_mask & 0x03ffU) | 0xfffffc00U,
    };
    apply_scene_block_dvp_vu_writes_v1(invocation.initial_state,
                                        snapshot,
                                        0U,
                                        options.write_prefix_count,
                                        limits);
    invocation.applied_write_count = options.write_prefix_count;
    invocation.entrypoint_address = options.entrypoint_address;
    invocation.unpack_tops_qword = snapshot.final_state.tops_qword;
    return invocation;
}

DvpVuExecutionResultV1
execute_dvp_vu_program_v1(const DvpVuProgramV1& program,
                          DvpVuExecutionStateV1 initial_state,
                          const DvpVuExecutionOptionsV1 options,
                          const DvpVuExecutionLimitsV1 limits) {
    if (limits.max_instruction_pairs == 0U || limits.max_xgkick_events == 0U ||
        limits.max_xgkick_tags_per_event == 0U ||
        limits.max_xgkick_qwords_per_event == 0U) {
        throw DvpVuExecutionError(
            "DVP VU execution limits must all be non-zero");
    }
    if (options.entrypoint_address >= kDvpVu1InstructionCount ||
        !valid_entrypoint(program, options.entrypoint_address)) {
        throw DvpVuExecutionError(
            "DVP VU execution entrypoint is not in the decoded program");
    }

    std::array<std::size_t, kDvpVu1InstructionCount> address_to_instruction{};
    address_to_instruction.fill(kInvalidIndex);
    for (std::size_t index = 0U; index < program.instructions.size(); ++index) {
        validate_instruction_fields(program.instructions[index]);
        const auto address = program.instructions[index].instruction_address;
        if (address >= kDvpVu1InstructionCount) {
            throw DvpVuExecutionError(
                "Decoded DVP VU instruction address is out of range");
        }
        if (address_to_instruction[address] != kInvalidIndex) {
            throw DvpVuExecutionError(
                "Decoded DVP VU program contains duplicate addresses");
        }
        address_to_instruction[address] = index;
    }
    if (address_to_instruction[options.entrypoint_address] == kInvalidIndex) {
        throw DvpVuExecutionError("DVP VU execution entrypoint is not loaded");
    }

    DvpVuExecutionResultV1 result;
    result.termination = DvpVuTerminationV1::instruction_limit;
    result.instruction_trace.reserve(static_cast<std::size_t>(
        std::min<std::uint64_t>(limits.max_instruction_pairs,
                                program.instructions.size())));
    initial_state.pc = options.entrypoint_address;
    enforce_architectural_constants(initial_state);
    Pipelines pipelines;
    std::optional<PendingControl> pending_control;

    while (result.executed_instruction_pairs < limits.max_instruction_pairs) {
        const auto cycle = result.executed_instruction_pairs;
        commit_ready(initial_state, pipelines, cycle);
        enforce_architectural_constants(initial_state);

        const auto pc = initial_state.pc;
        const auto instruction_index = address_to_instruction[pc];
        if (instruction_index == kInvalidIndex) {
            result.termination = DvpVuTerminationV1::unmapped_instruction;
            result.stopped_instruction_address = pc;
            break;
        }
        const auto& pair = program.instructions[instruction_index];
        const bool executing_delay_slot = pending_control.has_value();

        if (!supported_upper(pair.upper.opcode) ||
            !supported_lower(pair.lower) || pair.upper.m || pair.upper.d ||
            pair.upper.t ||
            (executing_delay_slot &&
             (is_control_lower(pair.lower.opcode) || pair.upper.end)) ||
            (pair.upper.end && is_control_lower(pair.lower.opcode))) {
            result.termination = DvpVuTerminationV1::unsupported_instruction;
            result.stopped_instruction_address = pc;
            break;
        }

        const auto before = snapshot_registers(initial_state);
        execute_upper(pair, before, initial_state, pipelines, result, cycle);
        const auto lower = execute_lower(pair,
                                         before,
                                         initial_state,
                                         pipelines,
                                         result,
                                         options,
                                         limits,
                                         cycle);

        result.instruction_trace.push_back(pc);
        ++result.executed_instruction_pairs;
        initial_state.pc = static_cast<std::uint16_t>((pc + 1U) & 0x07ffU);
        enforce_architectural_constants(initial_state);

        if (executing_delay_slot && pending_control->end &&
            is_memory_operation(pair.lower.opcode)) {
            add_warning(
                result,
                DvpVuExecutionWarningV1::documented_undefined_e_delay_memory);
        }

        if (lower.stop) {
            result.termination = *lower.stop;
            result.stopped_instruction_address = pc;
            break;
        }

        if (executing_delay_slot) {
            const auto completed_control = *pending_control;
            pending_control.reset();
            if (completed_control.end) {
                commit_all(initial_state, pipelines);
                enforce_architectural_constants(initial_state);
                result.termination = DvpVuTerminationV1::program_end;
                result.stopped_instruction_address = pc;
                break;
            }
            if (!completed_control.target_known) {
                result.termination = DvpVuTerminationV1::indeterminate_control;
                result.stopped_instruction_address = pc;
                break;
            }
            initial_state.pc = completed_control.target;
            continue;
        }

        if (lower.control) {
            pending_control = lower.control;
        } else if (pair.upper.end) {
            pending_control = PendingControl{true, true, 0U};
        }
    }

    result.final_state = std::move(initial_state);
    return result;
}

} // namespace openrc
