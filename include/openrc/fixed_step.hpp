#pragma once

#include <cstdint>
#include <stdexcept>

namespace openrc::game {

inline constexpr std::uint64_t kFixedStepTimeDenominatorV1 = 1'000'000'000U;

struct FixedStepConfigV1 {
  std::uint32_t ticks_per_second = 60U;
  std::uint32_t max_steps_per_advance = 8U;
  std::uint64_t max_elapsed_nanoseconds = 250'000'000U;

  [[nodiscard]] bool operator==(const FixedStepConfigV1 &) const = default;
};

struct FixedStepAdvanceV1 {
  // Callers execute [first_tick_index, first_tick_index + step_count) in
  // ascending order and obtain one recorded input command per tick.
  std::uint64_t first_tick_index = 0U;
  std::uint32_t step_count = 0U;

  // Whole simulation steps beyond the catch-up cap are deliberately dropped
  // to prevent an unbounded spiral. The fractional phase is retained.
  std::uint64_t dropped_step_count = 0U;
  std::uint64_t discarded_elapsed_nanoseconds = 0U;

  // Fraction of the next fixed tick. The denominator is always
  // kFixedStepTimeDenominatorV1; presentation may convert it to float.
  std::uint64_t interpolation_numerator = 0U;

  [[nodiscard]] bool operator==(const FixedStepAdvanceV1 &) const = default;
};

class FixedStepError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Uses integer nanoseconds multiplied by an integer tick rate. A one-second
// input therefore schedules exactly 50 or 60 ticks without a rounded
// 16,666,667-nanosecond step leaking drift into replay tick numbering.
class FixedStepAccumulatorV1 final {
public:
  explicit FixedStepAccumulatorV1(FixedStepConfigV1 config = {});

  [[nodiscard]] FixedStepAdvanceV1 advance(std::uint64_t elapsed_nanoseconds);

  void reset(std::uint64_t next_tick_index = 0U,
             std::uint64_t interpolation_numerator = 0U);

  [[nodiscard]] const FixedStepConfigV1 &config() const noexcept;
  [[nodiscard]] std::uint64_t next_tick_index() const noexcept;
  [[nodiscard]] std::uint64_t interpolation_numerator() const noexcept;
  [[nodiscard]] std::uint64_t total_dropped_step_count() const noexcept;
  [[nodiscard]] std::uint64_t
  total_discarded_elapsed_nanoseconds() const noexcept;

private:
  FixedStepConfigV1 config_;
  std::uint64_t next_tick_index_ = 0U;
  std::uint64_t interpolation_numerator_ = 0U;
  std::uint64_t total_dropped_step_count_ = 0U;
  std::uint64_t total_discarded_elapsed_nanoseconds_ = 0U;
};

} // namespace openrc::game
