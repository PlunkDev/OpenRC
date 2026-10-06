#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace openrc {

// A prepared control graph. Voice indices name independently admitted neutral
// voice descriptions. The graph does not decode samples or own an audio device.
inline constexpr std::uint32_t audio_program_end_v1 = UINT32_MAX;
inline constexpr std::size_t audio_program_scalar_count_v1 = 8;
struct AudioProgramNoopV1 {};
struct AudioProgramVoiceV1 { std::uint32_t voice_index = 0; };
struct AudioProgramReleaseV1 {};
struct AudioProgramWaitOwnedV1 {};
// Normal execution ends the graph here. An explicit stop enters this node's
// successor instead; any release operation must be present in that path.
struct AudioProgramStopSectionV1 {};
struct AudioProgramRandomWaitV1 { std::uint32_t inclusive_max = 0; };
struct AudioProgramSetV1 { std::uint32_t scalar = 0; std::int32_t value = 0; };
struct AudioProgramAddV1 {
    std::uint32_t scalar = 0;
    std::int32_t value = 0, minimum = 0, maximum = 0;
};
// Draw one unsigned word, reduce modulo modulus, multiply/divide, add bias,
// then multiply/divide again. Divisions truncate toward zero. This explicit
// arithmetic also represents bounded uniform integer assignment.
struct AudioProgramRandomSetV1 {
    std::uint32_t scalar = 0, modulus = 1;
    std::int32_t multiplier = 1, divisor = 1, bias = 0;
    std::int32_t scale = 1, scale_divisor = 1;
};
enum class AudioProgramComparisonV1 : std::uint32_t { less, equal, greater };
struct AudioProgramCompareV1 {
    std::uint32_t scalar = 0;
    AudioProgramComparisonV1 comparison = AudioProgramComparisonV1::equal;
    std::int32_t value = 0;
    std::uint32_t true_next = audio_program_end_v1;
};
struct AudioProgramRandomBranchV1 {
    std::vector<std::uint32_t> successors;
    bool avoid_previous = false;
    std::uint32_t initial_previous = 0;
    bool shared_previous = false;
};
// Table values are the final scalar values, prepared by the compiler. Sampling
// precedes phase advance; both initial activation and each modulation tick
// sample once. Reconfiguration keeps no prior phase unless explicitly supplied.
struct AudioProgramOscillatorV1 {
    std::uint32_t slot = 0, scalar = 0;
    std::uint32_t phase_denominator = 1, phase_increment = 0;
    std::uint32_t initial_phase = 0, random_phase_mask = 0;
    bool random_initial_phase = false;
    std::vector<std::int32_t> values;
};
using AudioProgramActionV1 = std::variant<AudioProgramNoopV1, AudioProgramVoiceV1,
    AudioProgramReleaseV1, AudioProgramRandomWaitV1, AudioProgramSetV1,
    AudioProgramAddV1, AudioProgramRandomSetV1, AudioProgramCompareV1,
    AudioProgramRandomBranchV1, AudioProgramOscillatorV1,
    AudioProgramWaitOwnedV1, AudioProgramStopSectionV1>;
struct AudioProgramNodeV1 {
    std::uint32_t delay_ticks = 0, next = audio_program_end_v1;
    AudioProgramActionV1 action;
};
struct AudioProgramV1 {
    std::uint32_t key = 0, voice_count = 0, entry = 0;
    std::array<std::int32_t, audio_program_scalar_count_v1> initial_scalars{};
    std::vector<AudioProgramNodeV1> nodes;
};
struct AudioProgramRandomV1 {
    std::vector<std::uint16_t> words;
    std::uint32_t index = 0, forward_tap = 0;
};
struct AudioProgramBankV1 {
    std::uint32_t ticks_per_second = 0, modulation_tick_divisor = 0;
    AudioProgramRandomV1 random;
    std::vector<AudioProgramV1> programs;
};
struct AudioProgramLimitsV1 {
    std::uint64_t max_bytes = 16U * 1024U * 1024U;
    std::uint32_t max_programs = 64, max_nodes_per_program = 4096;
    std::uint32_t max_instances = 64, max_events = 256;
    std::uint32_t max_actions_per_tick = 4096, max_table_values = 65536;
};
class AudioProgramError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
void validate_audio_program_bank_v1(const AudioProgramBankV1&, AudioProgramLimitsV1 = {});
[[nodiscard]] std::vector<std::byte> encode_audio_program_bank_v1(
    const AudioProgramBankV1&, AudioProgramLimitsV1 = {});
[[nodiscard]] AudioProgramBankV1 decode_audio_program_bank_v1(
    std::span<const std::byte>, AudioProgramLimitsV1 = {});

enum class AudioProgramEventKindV1 { voice, release_owned };
struct AudioProgramEventV1 {
    AudioProgramEventKindV1 kind = AudioProgramEventKindV1::voice;
    std::uint64_t tick = 0, instance = 0;
    std::uint32_t program_key = 0, voice_index = 0;
    std::array<std::int32_t, audio_program_scalar_count_v1> scalars{};
    bool operator==(const AudioProgramEventV1&) const = default;
};
struct AudioProgramInstanceStateV1 {
    std::uint64_t instance = 0;
    std::uint32_t program_index = 0, node = audio_program_end_v1, delay_ticks = 0;
    std::array<std::int32_t, audio_program_scalar_count_v1> scalars{};
    std::uint32_t owned_count = 0;
    bool stop_requested = false;
    bool operator==(const AudioProgramInstanceStateV1&) const = default;
};

// One shared random owner and one integer clock. Programs execute in admission
// order, then oscillators update on divisible ticks. A finished graph can still
// own voices: release/completion is a separate audio owner's responsibility.
class AudioProgramSchedulerV1 final {
public:
    explicit AudioProgramSchedulerV1(AudioProgramBankV1, AudioProgramLimitsV1 = {});
    AudioProgramSchedulerV1(const AudioProgramSchedulerV1&) = delete;
    AudioProgramSchedulerV1& operator=(const AudioProgramSchedulerV1&) = delete;
    AudioProgramSchedulerV1(AudioProgramSchedulerV1&&) = delete;
    AudioProgramSchedulerV1& operator=(AudioProgramSchedulerV1&&) = delete;
    [[nodiscard]] std::uint64_t admit(std::uint32_t program_index);
    void advance_tick();
    // Advance an empty, event-free owner without iterating idle ticks. This
    // retains the random state and the absolute modulation-clock phase.
    void advance_idle_to(std::uint64_t absolute_tick);
    [[nodiscard]] std::uint64_t tick() const noexcept { return tick_; }
    [[nodiscard]] std::uint64_t random_draws() const noexcept { return random_draws_; }
    [[nodiscard]] const AudioProgramRandomV1& random_state() const noexcept { return random_; }
    [[nodiscard]] std::vector<AudioProgramEventV1> take_events();
    [[nodiscard]] AudioProgramInstanceStateV1 instance_state(std::uint64_t instance) const;
    // The host reports logical completion only for a still-attached voice of
    // this exact instance token. Detached release tails cannot complete it.
    void complete_owned(std::uint64_t instance, std::uint32_t count = 1);
    // Runs the prepared stop entry once. It does not implicitly release or
    // complete voices. Modulation persists if the tail retains owned voices.
    void stop_instance(std::uint64_t instance);
    // Requires ended graph and zero logical ownership; physical tails belong
    // to the host. Frees capacity, preserving order and never reusing a token.
    void retire(std::uint64_t instance);
    // Stops future graph/modulation work. It deliberately does not report that
    // voices or device buffers have completed; the host must retire those.
    void stop() noexcept;
private:
    struct OscillatorState {
        const AudioProgramOscillatorV1* definition = nullptr;
        std::uint32_t phase = 0;
    };
    struct Instance {
        AudioProgramInstanceStateV1 state;
        std::vector<std::uint32_t> previous_branches;
        std::array<OscillatorState, 4> oscillators{};
    };
    [[nodiscard]] std::uint16_t draw();
    void execute(Instance&);
    void oscillate(Instance&, OscillatorState&);
    [[nodiscard]] Instance& find_instance(std::uint64_t);
    AudioProgramBankV1 bank_;
    AudioProgramLimitsV1 limits_;
    AudioProgramRandomV1 random_;
    std::vector<Instance> instances_;
    std::vector<std::vector<std::uint32_t>> shared_previous_;
    std::vector<AudioProgramEventV1> events_;
    std::uint64_t tick_ = 0, next_instance_ = 1, random_draws_ = 0;
    bool stopped_ = false;
};

} // namespace openrc
