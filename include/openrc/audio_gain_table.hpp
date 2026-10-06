#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Prepared finite-state transfer table. Coordinates are explicit integer
// indices; conversion from live control scalars belongs to its caller.
struct AudioGainTableCellV1 {
    std::array<std::int32_t, 2> gains{};
    std::uint32_t next_state = 0;
    bool operator==(const AudioGainTableCellV1&) const = default;
};
struct AudioGainTableV1 {
    std::uint32_t state_count = 0, level_count = 0, pan_count = 0;
    std::uint32_t initial_state = 0, gain_denominator = 0;
    // Dense order: ((state * level_count + level) * pan_count + pan).
    std::vector<AudioGainTableCellV1> cells;
    bool operator==(const AudioGainTableV1&) const = default;
};
struct AudioGainTableLimitsV1 {
    std::uint64_t max_bytes = 16U * 1024U * 1024U, max_cells = 1048576;
    std::uint32_t max_states = 256, max_levels = 65536, max_pans = 65536;
};
class AudioGainTableError final : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
void validate_audio_gain_table_v1(const AudioGainTableV1&, AudioGainTableLimitsV1 = {});
[[nodiscard]] std::vector<std::byte> encode_audio_gain_table_v1(
    const AudioGainTableV1&, AudioGainTableLimitsV1 = {});
[[nodiscard]] AudioGainTableV1 decode_audio_gain_table_v1(
    std::span<const std::byte>, AudioGainTableLimitsV1 = {});

// Admits once and shares an immutable definition across independent players.
// Copies, including transfers into players, do not copy the table cells.
class AudioGainTableResourceV1 final {
public:
    explicit AudioGainTableResourceV1(AudioGainTableV1, AudioGainTableLimitsV1 = {});
    AudioGainTableResourceV1(const AudioGainTableResourceV1&) = default;
    AudioGainTableResourceV1& operator=(const AudioGainTableResourceV1&) = default;
    [[nodiscard]] const AudioGainTableV1& definition() const noexcept { return *table_; }
private:
    std::shared_ptr<const AudioGainTableV1> table_;
};
class AudioGainTablePlayerV1 final {
public:
    explicit AudioGainTablePlayerV1(AudioGainTableResourceV1);
    [[nodiscard]] std::uint32_t state() const noexcept { return state_; }
    [[nodiscard]] std::uint32_t gain_denominator() const noexcept;
    void reset() noexcept;
    // No allocation. Both coordinates are checked before state mutation.
    [[nodiscard]] std::array<std::int32_t, 2> step(
        std::uint32_t level_index, std::uint32_t pan_index);
private:
    AudioGainTableResourceV1 resource_;
    std::uint32_t state_ = 0;
};

} // namespace openrc
