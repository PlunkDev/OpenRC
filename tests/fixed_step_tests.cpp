#include "openrc/fixed_step.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_fixed_step_error(Callback &&callback, const std::string &message) {
  try {
    callback();
  } catch (const openrc::game::FixedStepError &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] std::uint64_t
run_partition(const std::uint32_t ticks_per_second,
              const std::vector<std::uint64_t> &frame_times) {
  openrc::game::FixedStepAccumulatorV1 accumulator({
      ticks_per_second,
      128U,
      1'000'000'000U,
  });
  std::uint64_t emitted = 0U;
  for (const auto elapsed : frame_times) {
    const auto result = accumulator.advance(elapsed);
    expect(result.first_tick_index == emitted &&
               result.dropped_step_count == 0U &&
               result.discarded_elapsed_nanoseconds == 0U,
           "an uncapped partition produced a discontinuous tick range");
    emitted += result.step_count;
  }
  expect(accumulator.interpolation_numerator() == 0U,
         "an exact one-second partition retained a fractional tick");
  return emitted;
}

void test_exact_rates_and_partition_independence() {
  const std::vector<std::uint64_t> one_frame{1'000'000'000U};
  const std::vector<std::uint64_t> four_frames{
      250'000'000U,
      250'000'000U,
      250'000'000U,
      250'000'000U,
  };
  const std::vector<std::uint64_t> uneven_frames{
      1U, 16'666'666U, 200'000'003U, 333'333'333U, 449'999'997U,
  };
  expect(run_partition(50U, one_frame) == 50U &&
             run_partition(50U, four_frames) == 50U &&
             run_partition(50U, uneven_frames) == 50U,
         "the 50 Hz accumulator depends on frame partitioning");
  expect(run_partition(60U, one_frame) == 60U &&
             run_partition(60U, four_frames) == 60U &&
             run_partition(60U, uneven_frames) == 60U,
         "the 60 Hz accumulator depends on frame partitioning");
}

void test_fractional_phase_and_tick_ranges() {
  using namespace openrc::game;

  FixedStepAccumulatorV1 accumulator({60U, 8U, 250'000'000U});
  const auto first = accumulator.advance(10'000'000U);
  expect(first == FixedStepAdvanceV1{0U, 0U, 0U, 0U, 600'000'000U},
         "the first fractional fixed-step phase is wrong");
  const auto second = accumulator.advance(10'000'000U);
  expect(second == FixedStepAdvanceV1{0U, 1U, 0U, 0U, 200'000'000U},
         "the fixed-step phase did not carry across frames");
  const auto third = accumulator.advance(30'000'000U);
  expect(third == FixedStepAdvanceV1{1U, 2U, 0U, 0U, 0U} &&
             accumulator.next_tick_index() == 3U,
         "a multi-step tick range is wrong");
}

void test_catch_up_and_elapsed_caps() {
  using namespace openrc::game;

  FixedStepAccumulatorV1 accumulator({60U, 4U, 100'000'000U});
  const auto result = accumulator.advance(250'000'000U);
  expect(result.first_tick_index == 0U && result.step_count == 4U &&
             result.dropped_step_count == 2U &&
             result.discarded_elapsed_nanoseconds == 150'000'000U &&
             result.interpolation_numerator == 0U,
         "the fixed-step catch-up or elapsed cap is wrong");
  expect(accumulator.total_dropped_step_count() == 2U &&
             accumulator.total_discarded_elapsed_nanoseconds() == 150'000'000U,
         "fixed-step overload totals are wrong");

  const auto next = accumulator.advance(0U);
  expect(next.first_tick_index == 4U && next.step_count == 0U,
         "dropped catch-up work leaked into a later frame");
}

void test_reset_and_validation() {
  using namespace openrc::game;

  expect_fixed_step_error([] { FixedStepAccumulatorV1 invalid({0U, 1U, 1U}); },
                          "a zero fixed-step rate was accepted");
  expect_fixed_step_error([] { FixedStepAccumulatorV1 invalid({60U, 0U, 1U}); },
                          "a zero fixed-step catch-up limit was accepted");
  expect_fixed_step_error(
      [] {
        FixedStepAccumulatorV1 invalid(
            {2U, 1U, std::numeric_limits<std::uint64_t>::max()});
      },
      "an overflowing fixed-step elapsed limit was accepted");

  FixedStepAccumulatorV1 accumulator;
  static_cast<void>(accumulator.advance(20'000'000U));
  accumulator.reset(90U, 250'000'000U);
  expect(accumulator.next_tick_index() == 90U &&
             accumulator.interpolation_numerator() == 250'000'000U &&
             accumulator.total_dropped_step_count() == 0U,
         "fixed-step reset did not restore explicit replay state");
  expect_fixed_step_error(
      [&] { accumulator.reset(0U, kFixedStepTimeDenominatorV1); },
      "a whole-tick interpolation phase was accepted");

  accumulator.reset(std::numeric_limits<std::uint64_t>::max());
  expect_fixed_step_error(
      [&] { static_cast<void>(accumulator.advance(20'000'000U)); },
      "an exhausted fixed-step tick sequence wrapped");
}

} // namespace

int main() {
  try {
    test_exact_rates_and_partition_independence();
    test_fractional_phase_and_tick_ranges();
    test_catch_up_and_elapsed_caps();
    test_reset_and_validation();
    std::cout << "fixed_step_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "fixed_step_tests: " << error.what() << '\n';
    return 1;
  }
}
