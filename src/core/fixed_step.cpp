#include "openrc/fixed_step.hpp"

#include <algorithm>
#include <limits>

namespace openrc::game {
namespace {

void checked_accumulate(std::uint64_t &total, const std::uint64_t addition,
                        const char *const description) {
  if (addition > std::numeric_limits<std::uint64_t>::max() - total) {
    throw FixedStepError(description);
  }
  total += addition;
}

} // namespace

FixedStepAccumulatorV1::FixedStepAccumulatorV1(const FixedStepConfigV1 config)
    : config_(config) {
  if (config_.ticks_per_second == 0U || config_.max_steps_per_advance == 0U ||
      config_.max_elapsed_nanoseconds == 0U) {
    throw FixedStepError("Fixed-step limits must all be non-zero");
  }
  const auto maximum_safe_elapsed = (std::numeric_limits<std::uint64_t>::max() -
                                     (kFixedStepTimeDenominatorV1 - 1U)) /
                                    config_.ticks_per_second;
  if (config_.max_elapsed_nanoseconds > maximum_safe_elapsed) {
    throw FixedStepError(
        "The fixed-step elapsed-time limit exceeds its integer domain");
  }
}

FixedStepAdvanceV1
FixedStepAccumulatorV1::advance(const std::uint64_t elapsed_nanoseconds) {
  const auto accepted_elapsed =
      std::min(elapsed_nanoseconds, config_.max_elapsed_nanoseconds);
  const auto discarded_elapsed = elapsed_nanoseconds - accepted_elapsed;
  const auto scaled_elapsed = accepted_elapsed * config_.ticks_per_second;
  const auto scaled_total = interpolation_numerator_ + scaled_elapsed;
  const auto available_steps = scaled_total / kFixedStepTimeDenominatorV1;
  const auto emitted_steps =
      std::min<std::uint64_t>(available_steps, config_.max_steps_per_advance);
  const auto dropped_steps = available_steps - emitted_steps;

  if (emitted_steps >
      std::numeric_limits<std::uint64_t>::max() - next_tick_index_) {
    throw FixedStepError("The fixed-step tick sequence is exhausted");
  }
  checked_accumulate(total_dropped_step_count_, dropped_steps,
                     "The fixed-step dropped-tick counter overflowed");
  checked_accumulate(total_discarded_elapsed_nanoseconds_, discarded_elapsed,
                     "The fixed-step discarded-time counter overflowed");

  FixedStepAdvanceV1 result{
      next_tick_index_,
      static_cast<std::uint32_t>(emitted_steps),
      dropped_steps,
      discarded_elapsed,
      scaled_total % kFixedStepTimeDenominatorV1,
  };
  next_tick_index_ += emitted_steps;
  interpolation_numerator_ = result.interpolation_numerator;
  return result;
}

void FixedStepAccumulatorV1::reset(
    const std::uint64_t next_tick_index,
    const std::uint64_t interpolation_numerator) {
  if (interpolation_numerator >= kFixedStepTimeDenominatorV1) {
    throw FixedStepError(
        "A fixed-step interpolation phase must be less than one tick");
  }
  next_tick_index_ = next_tick_index;
  interpolation_numerator_ = interpolation_numerator;
  total_dropped_step_count_ = 0U;
  total_discarded_elapsed_nanoseconds_ = 0U;
}

const FixedStepConfigV1 &FixedStepAccumulatorV1::config() const noexcept {
  return config_;
}

std::uint64_t FixedStepAccumulatorV1::next_tick_index() const noexcept {
  return next_tick_index_;
}

std::uint64_t FixedStepAccumulatorV1::interpolation_numerator() const noexcept {
  return interpolation_numerator_;
}

std::uint64_t
FixedStepAccumulatorV1::total_dropped_step_count() const noexcept {
  return total_dropped_step_count_;
}

std::uint64_t
FixedStepAccumulatorV1::total_discarded_elapsed_nanoseconds() const noexcept {
  return total_discarded_elapsed_nanoseconds_;
}

} // namespace openrc::game
