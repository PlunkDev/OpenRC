#include "openrc/audio_program.hpp"
#include "openrc/prepared_game_v2.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <type_traits>
#include <cstdlib>
#include <utility>
#include <bit>

namespace openrc {
namespace {
void require(bool value, const char* message) {
    if (!value) throw AudioProgramError(message);
}
std::int32_t transform(std::uint16_t word, const AudioProgramRandomSetV1& action) {
    auto value = std::int64_t{word % action.modulus} * action.multiplier / action.divisor;
    value = (value + action.bias) * action.scale / action.scale_divisor;
    require(value >= INT32_MIN && value <= INT32_MAX, "Audio program random result exceeds scalar range");
    return static_cast<std::int32_t>(value);
}
bool scalar_valid(std::uint32_t index) { return index < audio_program_scalar_count_v1; }
constexpr std::array<std::byte, 8> magic{std::byte{'O'}, std::byte{'R'}, std::byte{'A'}, std::byte{'U'},
    std::byte{'P'}, std::byte{'R'}, std::byte{'G'}, std::byte{'1'}};
void put(std::vector<std::byte>& bytes, std::uint64_t value, unsigned width = 4) {
    for (unsigned i = 0; i < width; ++i) { bytes.push_back(std::byte(value & 255U)); value >>= 8U; }
}
struct Reader {
    std::span<const std::byte> bytes;
    std::size_t at = 0;
    std::uint64_t get(unsigned width = 4) {
        require(at <= bytes.size() && width <= bytes.size() - at, "Truncated audio program resource");
        std::uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i) value |= std::uint64_t{std::to_integer<unsigned>(bytes[at++])} << (8U * i);
        return value;
    }
    std::uint32_t u32() { return static_cast<std::uint32_t>(get()); }
    std::int32_t i32() { return std::bit_cast<std::int32_t>(u32()); }
};
std::uint64_t encoded_size(const AudioProgramBankV1& bank) {
    std::uint64_t size = 64U + 32U + bank.random.words.size() * 2U;
    for (const auto& program : bank.programs) {
        size += 48U;
        for (const auto& node : program.nodes) {
            size += 16U;
            std::visit([&](const auto& action) {
                using T = std::decay_t<decltype(action)>;
                if constexpr (std::is_same_v<T, AudioProgramVoiceV1> || std::is_same_v<T, AudioProgramRandomWaitV1>) size += 4;
                else if constexpr (std::is_same_v<T, AudioProgramSetV1>) size += 8;
                else if constexpr (std::is_same_v<T, AudioProgramAddV1> || std::is_same_v<T, AudioProgramCompareV1>) size += 16;
                else if constexpr (std::is_same_v<T, AudioProgramRandomSetV1>) size += 28;
                else if constexpr (std::is_same_v<T, AudioProgramRandomBranchV1>) size += 12U + action.successors.size() * 4U;
                else if constexpr (std::is_same_v<T, AudioProgramOscillatorV1>) size += 32U + action.values.size() * 4U;
            }, node.action);
        }
    }
    return size;
}
} // namespace

void validate_audio_program_bank_v1(const AudioProgramBankV1& bank, AudioProgramLimitsV1 limits) {
    require(bank.ticks_per_second > 0 && bank.ticks_per_second <= 48000 &&
        bank.modulation_tick_divisor > 0 && bank.modulation_tick_divisor <= 48000 &&
        !bank.programs.empty() && bank.programs.size() <= limits.max_programs &&
        limits.max_instances && limits.max_events && limits.max_actions_per_tick,
        "Audio program clocks or limits are invalid");
    require(bank.random.words.size() >= 2 && bank.random.words.size() <= 65536 &&
        bank.random.index < bank.random.words.size() && bank.random.forward_tap > 0 &&
        bank.random.forward_tap < bank.random.words.size(), "Audio program random owner is invalid");
    std::set<std::uint32_t> keys;
    for (const auto& program : bank.programs) {
        require(keys.insert(program.key).second && !program.nodes.empty() &&
            program.nodes.size() <= limits.max_nodes_per_program && program.entry < program.nodes.size(),
            "Audio program graph owner is invalid");
        const auto successor = [&](std::uint32_t index) {
            require(index == audio_program_end_v1 || index < program.nodes.size(),
                "Audio program successor is outside its graph");
        };
        unsigned stop_sections = 0;
        for (const auto& node : program.nodes) {
            successor(node.next);
            if (std::holds_alternative<AudioProgramStopSectionV1>(node.action))
                require(++stop_sections <= 1, "Audio program has multiple stop entries");
            require(node.delay_ticks <= INT32_MAX, "Audio program delay is too large");
            std::visit([&](const auto& action) {
                using T = std::decay_t<decltype(action)>;
                if constexpr (std::is_same_v<T, AudioProgramVoiceV1>) {
                    require(action.voice_index < program.voice_count, "Audio program voice is absent");
                } else if constexpr (std::is_same_v<T, AudioProgramRandomWaitV1>) {
                    require(action.inclusive_max <= 65535, "Audio program random wait is too large");
                } else if constexpr (std::is_same_v<T, AudioProgramSetV1>) {
                    require(scalar_valid(action.scalar), "Audio program scalar is absent");
                } else if constexpr (std::is_same_v<T, AudioProgramAddV1>) {
                    require(scalar_valid(action.scalar) && action.minimum <= action.maximum,
                        "Audio program clamped addition is invalid");
                } else if constexpr (std::is_same_v<T, AudioProgramRandomSetV1>) {
                    require(scalar_valid(action.scalar) && action.modulus > 0 && action.modulus <= 65536 &&
                        action.divisor > 0 && action.scale_divisor > 0 &&
                        std::abs(std::int64_t{action.multiplier}) <= (1 << 20) &&
                        std::abs(std::int64_t{action.scale}) <= (1 << 20),
                        "Audio program random transform is invalid");
                    (void)transform(0, action);
                    (void)transform(static_cast<std::uint16_t>(action.modulus - 1), action);
                } else if constexpr (std::is_same_v<T, AudioProgramCompareV1>) {
                    require(scalar_valid(action.scalar) &&
                        static_cast<unsigned>(action.comparison) <= static_cast<unsigned>(AudioProgramComparisonV1::greater),
                        "Audio program comparison is invalid");
                    successor(action.true_next);
                } else if constexpr (std::is_same_v<T, AudioProgramRandomBranchV1>) {
                    require(!action.successors.empty() && action.successors.size() <= 65536 &&
                        action.initial_previous < action.successors.size() &&
                        (!action.avoid_previous || action.successors.size() > 1),
                        "Audio program random branch is invalid");
                    for (auto next : action.successors) successor(next);
                } else if constexpr (std::is_same_v<T, AudioProgramOscillatorV1>) {
                    const auto period = std::uint64_t{action.phase_denominator} * action.values.size();
                    require(action.slot < 4 && scalar_valid(action.scalar) && !action.values.empty() &&
                        action.values.size() <= limits.max_table_values && action.phase_denominator > 0 &&
                        period <= UINT32_MAX && action.phase_increment < period && action.initial_phase < period &&
                        (!action.random_initial_phase || action.random_phase_mask < action.values.size()),
                        "Audio program oscillator is invalid");
                }
            }, node.action);
        }
    }
    require(encoded_size(bank) <= limits.max_bytes, "Audio program encoded owner exceeds its byte limit");
}

std::vector<std::byte> encode_audio_program_bank_v1(const AudioProgramBankV1& bank, AudioProgramLimitsV1 limits) {
    validate_audio_program_bank_v1(bank, limits);
    std::vector<std::byte> body;
    body.reserve(static_cast<std::size_t>(encoded_size(bank) - 64U));
    put(body, bank.ticks_per_second); put(body, bank.modulation_tick_divisor);
    put(body, bank.random.words.size()); put(body, bank.random.index); put(body, bank.random.forward_tap);
    put(body, bank.programs.size()); put(body, 0, 8);
    for (const auto word : bank.random.words) put(body, word, 2);
    for (const auto& program : bank.programs) {
        put(body, program.key); put(body, program.voice_count); put(body, program.entry); put(body, program.nodes.size());
        for (const auto scalar : program.initial_scalars) put(body, static_cast<std::uint32_t>(scalar));
        for (const auto& node : program.nodes) {
            put(body, node.action.index()); put(body, node.delay_ticks); put(body, node.next);
            const auto length_at = body.size(); put(body, 0); const auto payload_begin = body.size();
            std::visit([&](const auto& action) {
                using T = std::decay_t<decltype(action)>;
                if constexpr (std::is_same_v<T, AudioProgramVoiceV1>) put(body, action.voice_index);
                else if constexpr (std::is_same_v<T, AudioProgramRandomWaitV1>) put(body, action.inclusive_max);
                else if constexpr (std::is_same_v<T, AudioProgramSetV1>) {
                    put(body, action.scalar); put(body, static_cast<std::uint32_t>(action.value));
                } else if constexpr (std::is_same_v<T, AudioProgramAddV1>) {
                    put(body, action.scalar); put(body, static_cast<std::uint32_t>(action.value));
                    put(body, static_cast<std::uint32_t>(action.minimum)); put(body, static_cast<std::uint32_t>(action.maximum));
                } else if constexpr (std::is_same_v<T, AudioProgramRandomSetV1>) {
                    put(body, action.scalar); put(body, action.modulus); put(body, static_cast<std::uint32_t>(action.multiplier));
                    put(body, action.divisor); put(body, static_cast<std::uint32_t>(action.bias));
                    put(body, static_cast<std::uint32_t>(action.scale)); put(body, action.scale_divisor);
                } else if constexpr (std::is_same_v<T, AudioProgramCompareV1>) {
                    put(body, action.scalar); put(body, static_cast<unsigned>(action.comparison));
                    put(body, static_cast<std::uint32_t>(action.value)); put(body, action.true_next);
                } else if constexpr (std::is_same_v<T, AudioProgramRandomBranchV1>) {
                    put(body, (action.avoid_previous ? 1U : 0U) | (action.shared_previous ? 2U : 0U));
                    put(body, action.initial_previous); put(body, action.successors.size());
                    for (const auto next : action.successors) put(body, next);
                } else if constexpr (std::is_same_v<T, AudioProgramOscillatorV1>) {
                    put(body, action.slot); put(body, action.scalar); put(body, action.phase_denominator); put(body, action.phase_increment);
                    put(body, action.initial_phase); put(body, action.random_phase_mask); put(body, action.random_initial_phase ? 1 : 0);
                    put(body, action.values.size());
                    for (const auto value : action.values) put(body, static_cast<std::uint32_t>(value));
                }
            }, node.action);
            const auto length = body.size() - payload_begin;
            for (unsigned i = 0; i < 4; ++i) body[length_at + i] = std::byte((length >> (8U * i)) & 255U);
        }
    }
    std::vector<std::byte> result(magic.begin(), magic.end());
    put(result, 1); put(result, 64); put(result, 64U + body.size(), 8); put(result, 0, 8);
    const auto digest = prepared_content_sha256_v1(body);
    result.insert(result.end(), digest.begin(), digest.end()); result.insert(result.end(), body.begin(), body.end());
    return result;
}

AudioProgramBankV1 decode_audio_program_bank_v1(std::span<const std::byte> bytes, AudioProgramLimitsV1 limits) {
    require(bytes.size() >= 96 && bytes.size() <= limits.max_bytes && std::equal(magic.begin(), magic.end(), bytes.begin()),
        "Invalid audio program envelope");
    Reader reader{bytes, 8};
    require(reader.get() == 1 && reader.get() == 64 && reader.get(8) == bytes.size() && reader.get(8) == 0,
        "Invalid audio program header");
    const auto digest = prepared_content_sha256_v1(bytes.subspan(64));
    require(std::equal(digest.begin(), digest.end(), bytes.begin() + 32), "Audio program digest differs");
    reader.at = 64;
    AudioProgramBankV1 bank;
    bank.ticks_per_second = reader.u32(); bank.modulation_tick_divisor = reader.u32();
    const auto words = reader.u32(); bank.random.index = reader.u32(); bank.random.forward_tap = reader.u32();
    const auto programs = reader.u32();
    require(reader.get(8) == 0 && words >= 2 && words <= 65536 && programs > 0 && programs <= limits.max_programs &&
        std::uint64_t{words} * 2U + std::uint64_t{programs} * 48U <= bytes.size() - reader.at,
        "Audio program counts exceed the byte owner");
    bank.random.words.reserve(words);
    for (unsigned i = 0; i < words; ++i) bank.random.words.push_back(static_cast<std::uint16_t>(reader.get(2)));
    bank.programs.reserve(programs);
    for (unsigned p = 0; p < programs; ++p) {
        AudioProgramV1 program;
        program.key = reader.u32(); program.voice_count = reader.u32(); program.entry = reader.u32();
        const auto nodes = reader.u32();
        require(nodes > 0 && nodes <= limits.max_nodes_per_program &&
            32U + std::uint64_t{nodes} * 16U <= bytes.size() - reader.at,
            "Audio program node count exceeds its byte owner");
        for (auto& scalar : program.initial_scalars) scalar = reader.i32();
        program.nodes.reserve(nodes);
        for (unsigned n = 0; n < nodes; ++n) {
            const auto kind = reader.u32();
            AudioProgramNodeV1 node; node.delay_ticks = reader.u32(); node.next = reader.u32();
            const auto length = reader.u32();
            require(length <= bytes.size() - reader.at, "Audio program action exceeds its byte owner");
            Reader action{bytes.subspan(reader.at, length)}; reader.at += length;
            switch (kind) {
            case 0: node.action = AudioProgramNoopV1{}; break;
            case 1: node.action = AudioProgramVoiceV1{action.u32()}; break;
            case 2: node.action = AudioProgramReleaseV1{}; break;
            case 3: node.action = AudioProgramRandomWaitV1{action.u32()}; break;
            case 4: node.action = AudioProgramSetV1{action.u32(), action.i32()}; break;
            case 5: node.action = AudioProgramAddV1{action.u32(), action.i32(), action.i32(), action.i32()}; break;
            case 6: node.action = AudioProgramRandomSetV1{action.u32(), action.u32(), action.i32(), action.i32(),
                action.i32(), action.i32(), action.i32()}; break;
            case 7: node.action = AudioProgramCompareV1{action.u32(), static_cast<AudioProgramComparisonV1>(action.u32()),
                action.i32(), action.u32()}; break;
            case 8: {
                AudioProgramRandomBranchV1 branch;
                const auto flags = action.u32(); branch.avoid_previous = (flags & 1U) != 0; branch.shared_previous = (flags & 2U) != 0;
                branch.initial_previous = action.u32(); const auto count = action.u32();
                require(flags <= 3 && count > 0 && count <= 65536 && std::uint64_t{count} * 4 == length - action.at,
                    "Audio program branch count or flags differ");
                branch.successors.reserve(count);
                for (unsigned i = 0; i < count; ++i) branch.successors.push_back(action.u32());
                node.action = std::move(branch); break;
            }
            case 9: {
                AudioProgramOscillatorV1 oscillator;
                oscillator.slot = action.u32(); oscillator.scalar = action.u32(); oscillator.phase_denominator = action.u32();
                oscillator.phase_increment = action.u32(); oscillator.initial_phase = action.u32(); oscillator.random_phase_mask = action.u32();
                const auto flags = action.u32(), count = action.u32(); oscillator.random_initial_phase = flags != 0;
                require(flags <= 1 && count > 0 && count <= limits.max_table_values && std::uint64_t{count} * 4 == length - action.at,
                    "Audio program oscillator count or flags differ");
                oscillator.values.reserve(count);
                for (unsigned i = 0; i < count; ++i) oscillator.values.push_back(action.i32());
                node.action = std::move(oscillator); break;
            }
            case 10: node.action = AudioProgramWaitOwnedV1{}; break;
            case 11: node.action = AudioProgramStopSectionV1{}; break;
            default: throw AudioProgramError("Unknown neutral audio program action");
            }
            require(action.at == action.bytes.size(), "Audio program action has trailing bytes");
            program.nodes.push_back(std::move(node));
        }
        bank.programs.push_back(std::move(program));
    }
    require(reader.at == bytes.size(), "Audio program has trailing bytes");
    validate_audio_program_bank_v1(bank, limits);
    return bank;
}

AudioProgramSchedulerV1::AudioProgramSchedulerV1(AudioProgramBankV1 bank, AudioProgramLimitsV1 limits)
    : bank_(std::move(bank)), limits_(limits), random_(bank_.random) {
    validate_audio_program_bank_v1(bank_, limits_);
    instances_.reserve(limits_.max_instances);
    events_.reserve(limits_.max_events);
    shared_previous_.resize(bank_.programs.size());
    for (std::size_t p = 0; p < bank_.programs.size(); ++p) {
        shared_previous_[p].resize(bank_.programs[p].nodes.size());
        for (std::size_t n = 0; n < bank_.programs[p].nodes.size(); ++n)
            if (const auto* branch = std::get_if<AudioProgramRandomBranchV1>(&bank_.programs[p].nodes[n].action))
                shared_previous_[p][n] = branch->initial_previous;
    }
}
std::uint16_t AudioProgramSchedulerV1::draw() {
    require(random_draws_ != UINT64_MAX, "Audio program random count overflow");
    auto& word = random_.words[random_.index];
    word ^= random_.words[(random_.index + random_.forward_tap) % random_.words.size()];
    const auto result = word;
    random_.index = (random_.index + 1U) % static_cast<std::uint32_t>(random_.words.size());
    ++random_draws_;
    return result;
}
void AudioProgramSchedulerV1::oscillate(Instance& instance, OscillatorState& oscillator) {
    const auto& definition = *oscillator.definition;
    instance.state.scalars[definition.scalar] = definition.values[oscillator.phase / definition.phase_denominator];
    oscillator.phase = static_cast<std::uint32_t>((std::uint64_t{oscillator.phase} + definition.phase_increment) %
        (std::uint64_t{definition.values.size()} * definition.phase_denominator));
}
void AudioProgramSchedulerV1::execute(Instance& instance) {
    auto& state = instance.state;
    const auto& program = bank_.programs[state.program_index];
    for (std::uint32_t actions = 0; state.node != audio_program_end_v1 && state.delay_ticks == 0; ++actions) {
        require(actions < limits_.max_actions_per_tick, "Audio program has no bounded yielding path");
        const auto node_index = state.node;
        const auto& node = program.nodes[node_index];
        if (std::holds_alternative<AudioProgramWaitOwnedV1>(node.action) && state.owned_count != 0) {
            state.delay_ticks = 1;
            return;
        }
        auto next = node.next;
        std::uint32_t extra_delay = 0;
        std::visit([&](const auto& action) {
            using T = std::decay_t<decltype(action)>;
            if constexpr (std::is_same_v<T, AudioProgramVoiceV1> || std::is_same_v<T, AudioProgramReleaseV1>) {
                require(events_.size() < limits_.max_events, "Audio program event queue is full");
                AudioProgramEventV1 event;
                event.tick = tick_; event.instance = state.instance; event.program_key = program.key;
                event.scalars = state.scalars;
                if constexpr (std::is_same_v<T, AudioProgramVoiceV1>) {
                    require(state.owned_count != UINT32_MAX, "Audio program logical voice count overflow");
                    event.voice_index = action.voice_index;
                } else event.kind = AudioProgramEventKindV1::release_owned;
                events_.push_back(event);
                if constexpr (std::is_same_v<T, AudioProgramVoiceV1>) ++state.owned_count;
                else state.owned_count = 0;
            } else if constexpr (std::is_same_v<T, AudioProgramStopSectionV1>) {
                next = audio_program_end_v1;
            } else if constexpr (std::is_same_v<T, AudioProgramRandomWaitV1>) {
                extra_delay = draw() % (action.inclusive_max + 1U);
            } else if constexpr (std::is_same_v<T, AudioProgramSetV1>) {
                state.scalars[action.scalar] = action.value;
            } else if constexpr (std::is_same_v<T, AudioProgramAddV1>) {
                state.scalars[action.scalar] = static_cast<std::int32_t>(std::clamp<std::int64_t>(
                    std::int64_t{state.scalars[action.scalar]} + action.value, action.minimum, action.maximum));
            } else if constexpr (std::is_same_v<T, AudioProgramRandomSetV1>) {
                state.scalars[action.scalar] = transform(draw(), action);
            } else if constexpr (std::is_same_v<T, AudioProgramCompareV1>) {
                const auto value = state.scalars[action.scalar];
                const bool matches = action.comparison == AudioProgramComparisonV1::less ? value < action.value :
                    action.comparison == AudioProgramComparisonV1::equal ? value == action.value : value > action.value;
                if (matches) next = action.true_next;
            } else if constexpr (std::is_same_v<T, AudioProgramRandomBranchV1>) {
                auto selected = draw() % static_cast<std::uint32_t>(action.successors.size());
                auto& previous = action.shared_previous ? shared_previous_[state.program_index][node_index] :
                    instance.previous_branches[node_index];
                if (action.avoid_previous && selected == previous)
                    selected = (selected + 1U) % static_cast<std::uint32_t>(action.successors.size());
                previous = selected;
                next = action.successors[selected];
            } else if constexpr (std::is_same_v<T, AudioProgramOscillatorV1>) {
                auto& oscillator = instance.oscillators[action.slot];
                oscillator.definition = &action;
                oscillator.phase = action.random_initial_phase ? (draw() & action.random_phase_mask) * action.phase_denominator :
                    action.initial_phase;
                oscillate(instance, oscillator);
            }
        }, node.action);
        state.node = next;
        if (next != audio_program_end_v1) {
            const auto delay = std::uint64_t{program.nodes[next].delay_ticks} + extra_delay;
            require(delay <= UINT32_MAX, "Audio program combined delay overflow");
            state.delay_ticks = static_cast<std::uint32_t>(delay);
        }
    }
    if (state.node == audio_program_end_v1 && state.owned_count == 0)
        for (auto& oscillator : instance.oscillators) oscillator.definition = nullptr;
}
std::uint64_t AudioProgramSchedulerV1::admit(std::uint32_t index) {
    require(!stopped_ && index < bank_.programs.size() && instances_.size() < limits_.max_instances &&
        next_instance_ != UINT64_MAX, "Audio program cannot admit another instance");
    const auto& program = bank_.programs[index];
    Instance instance;
    instance.state = {next_instance_, index, program.entry, program.nodes[program.entry].delay_ticks, program.initial_scalars};
    instance.previous_branches.resize(program.nodes.size());
    for (std::size_t i = 0; i < program.nodes.size(); ++i)
        if (const auto* branch = std::get_if<AudioProgramRandomBranchV1>(&program.nodes[i].action))
            instance.previous_branches[i] = branch->initial_previous;
    const auto old_random = random_; const auto old_previous = shared_previous_;
    const auto old_draws = random_draws_; const auto old_events = events_.size();
    try { execute(instance); }
    catch (...) { random_ = old_random; shared_previous_ = old_previous; random_draws_ = old_draws; events_.resize(old_events); throw; }
    instances_.push_back(std::move(instance));
    return next_instance_++;
}
void AudioProgramSchedulerV1::advance_idle_to(std::uint64_t target) {
    require(!stopped_ && instances_.empty() && events_.empty() && target >= tick_,
        "Audio program idle advance requires an empty forward-running owner");
    tick_ = target;
}
void AudioProgramSchedulerV1::advance_tick() {
    require(!stopped_ && tick_ != UINT64_MAX, "Audio program clock cannot advance");
    const auto old_instances = instances_; const auto old_random = random_;
    const auto old_previous = shared_previous_;
    const auto old_draws = random_draws_; const auto old_events = events_.size();
    ++tick_;
    try {
        for (auto& instance : instances_) {
            if (instance.state.delay_ticks > 0) --instance.state.delay_ticks;
            execute(instance);
        }
        if (tick_ % bank_.modulation_tick_divisor == 0)
            for (auto& instance : instances_)
                for (auto& oscillator : instance.oscillators)
                    if (oscillator.definition) oscillate(instance, oscillator);
    } catch (...) {
        instances_ = old_instances; random_ = old_random; random_draws_ = old_draws;
        shared_previous_ = old_previous;
        events_.resize(old_events); --tick_; throw;
    }
}
std::vector<AudioProgramEventV1> AudioProgramSchedulerV1::take_events() {
    std::vector<AudioProgramEventV1> result;
    result.reserve(limits_.max_events);
    result.swap(events_); return result;
}
AudioProgramInstanceStateV1 AudioProgramSchedulerV1::instance_state(std::uint64_t id) const {
    const auto found = std::find_if(instances_.begin(), instances_.end(),
        [&](const Instance& instance) { return instance.state.instance == id; });
    require(found != instances_.end(), "Audio program instance is absent");
    return found->state;
}
AudioProgramSchedulerV1::Instance& AudioProgramSchedulerV1::find_instance(std::uint64_t id) {
    const auto found = std::find_if(instances_.begin(), instances_.end(),
        [&](const Instance& instance) { return instance.state.instance == id; });
    require(found != instances_.end(), "Audio program instance is absent");
    return *found;
}
void AudioProgramSchedulerV1::complete_owned(std::uint64_t id, std::uint32_t count) {
    auto& instance = find_instance(id);
    require(count > 0 && count <= instance.state.owned_count, "Audio program completion exceeds attached ownership");
    instance.state.owned_count -= count;
    if (instance.state.node == audio_program_end_v1 && instance.state.owned_count == 0)
        for (auto& oscillator : instance.oscillators) oscillator.definition = nullptr;
}
void AudioProgramSchedulerV1::stop_instance(std::uint64_t id) {
    require(!stopped_, "Stopped audio program scheduler cannot execute a stop entry");
    auto& instance = find_instance(id);
    if (instance.state.stop_requested) return;
    const auto old_instance = instance; const auto old_random = random_; const auto old_previous = shared_previous_;
    const auto old_draws = random_draws_; const auto old_events = events_.size();
    const auto& program = bank_.programs[instance.state.program_index];
    auto entry = audio_program_end_v1;
    for (const auto& node : program.nodes)
        if (std::holds_alternative<AudioProgramStopSectionV1>(node.action)) entry = node.next;
    instance.state.stop_requested = true;
    instance.state.node = entry;
    instance.state.delay_ticks = entry == audio_program_end_v1 ? 0 : program.nodes[entry].delay_ticks;
    try { execute(instance); }
    catch (...) {
        instance = old_instance; random_ = old_random; shared_previous_ = old_previous;
        random_draws_ = old_draws; events_.resize(old_events); throw;
    }
}
void AudioProgramSchedulerV1::retire(std::uint64_t id) {
    const auto& instance = find_instance(id);
    require(instance.state.node == audio_program_end_v1 && instance.state.owned_count == 0,
        "Audio program still has graph work or attached voices");
    const auto index = static_cast<std::size_t>(&instance - instances_.data());
    instances_.erase(instances_.begin() + index);
}
void AudioProgramSchedulerV1::stop() noexcept {
    stopped_ = true;
    for (auto& instance : instances_) {
        instance.state.node = audio_program_end_v1;
        instance.state.delay_ticks = 0;
        for (auto& oscillator : instance.oscillators) oscillator.definition = nullptr;
    }
}

} // namespace openrc
