#include "openrc/rac_moby_fresh_constructor.hpp"
#include "openrc/rac_moby_sequence_set.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace openrc;
constexpr RacMobySequenceSetLimitsV1 kLimits{16U, 2048U, 2048U};

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Callable> void error(Callable &&callable) {
  bool failed = false;
  try {
    callable();
  } catch (const RacMobySequenceSetError &) {
    failed = true;
  }
  expect(failed, "unsupported sequence binding must fail, not invent a frame");
}

struct Fixture {
  std::array<std::vector<RacMobySequenceFrameBindingV1>, 5U> frames;
  std::vector<RacMobySequenceBindingV1> sequences;
  std::vector<RacMobyFrameFirstWordBindingV1> first_words;

  Fixture() {
    constexpr std::array keys{0U, 1U, 255U, 256U, 0x3fffffffU};
    constexpr std::array<std::uint8_t, 5U> counts{5U, 3U, 4U, 2U, 1U};
    for (std::uint32_t row = 0U; row < keys.size(); ++row) {
      for (std::uint32_t index = 0U; index < 256U; ++index) {
        const auto reference = 0x300000U + row * 0x2000U + index * 16U;
        frames[row].push_back({static_cast<std::uint8_t>(index), reference});
        first_words.push_back(
            {reference, 0x80000000U + row * 0x10000U + index});
      }
      sequences.push_back(
          {keys[row], counts[row], static_cast<std::uint8_t>(0x80U + row),
           static_cast<std::uint8_t>(0x70U + row), frames[row]});
    }
    for (std::uint32_t index = 0U; index < 256U; ++index)
      first_words.push_back({0x18f040U + index * 0x800U, 0xff000000U + index});
    std::sort(first_words.begin(), first_words.end(),
              [](const auto &a, const auto &b) {
                return a.frame_reference < b.frame_reference;
              });
  }

  RacMobySequenceBankV1 bank() const { return {sequences, first_words}; }
};

void test_complete_leaf_regular_and_snapshot() {
  Fixture f;
  const auto leaf = resolve_rac_moby_frame_references_v1({254U, 1U, 0U, 1U},
                                                         f.bank(), kLimits);
  expect(leaf == RacMobyFrameReferencesV1{0x300fe0U, 0x302010U, 0x80U, 0x70U},
         "leaf must use separate logical sequence/frame indices and previous "
         "sound/trigger");
  // This leaf never reads first-word contents; missing header input is legal.
  f.first_words.clear();
  const auto snapshot = resolve_rac_moby_frame_references_v1(
      {255U, 2U, 255U, 255U}, f.bank(), kLimits);
  expect(
      snapshot == RacMobyFrameReferencesV1{0x20e840U, 0x304020U, 0xffU, 0U},
      "previousFF snapshot must not replace currentFF ordinary table lookup");
  f.sequences.erase(f.sequences.begin() + 2);
  error([&] {
    (void)resolve_rac_moby_frame_references_v1({255U, 2U, 255U, 255U}, f.bank(),
                                               kLimits);
  });
}

void test_signed64_frame_arguments() {
  Fixture f;
  struct Case {
    std::uint64_t frame;
    std::uint8_t previous;
    std::uint8_t current;
  };
  const std::array cases{Case{0U, 0U, 1U},
                         Case{1U, 1U, 2U},
                         Case{2U, 2U, 2U},
                         Case{3U, 2U, 2U},
                         Case{0x00000000ffffffffULL, 2U, 2U},
                         Case{0xffffffffffffffffULL, 255U, 0U},
                         Case{0xfffffffffffffffeULL, 254U, 2U},
                         Case{0x8000000000000000ULL, 0U, 1U},
                         Case{0x0000000080000000ULL, 2U, 2U},
                         Case{0xffffffff80000000ULL, 0U, 1U},
                         Case{0xffffffff00000000ULL, 0U, 1U},
                         Case{0x0000000100000000ULL, 2U, 2U},
                         Case{0x8000000000000005ULL, 5U, 2U}};
  for (const auto &test : cases) {
    const auto r =
        set_rac_moby_sequence_v1(1U, test.frame, 0xffU, f.bank(), kLimits);
    expect(
        r.indices ==
                RacMobyFrameIndicesV1{test.previous, test.current, 1U, 1U} &&
            r.references.previous_frame_reference ==
                0x302000U + test.previous * 16U &&
            r.references.current_frame_reference ==
                0x302000U + test.current * 16U &&
            r.references.sound_byte == 0x81U &&
            r.references.trigger_byte == 0x71U && r.sequence_flags == 0xfdU &&
            r.animation_rate_bits == 0x80010000U + test.previous,
        "signed64 SLT/MOVN, word truncation, byte clamps or raw rate differs");
  }
}

void test_zero_count_and_byte_overflow() {
  Fixture f;
  f.sequences[1U].frame_count = 0U;
  for (auto frame : std::array<std::uint64_t, 5U>{
           0U, 255U, UINT64_MAX, UINT64_MAX - 1U, 0xffffffff00000000ULL}) {
    const auto expected_previous = frame == UINT64_MAX - 1U         ? 254U
                                   : frame == 0xffffffff00000000ULL ? 0U
                                                                    : 255U;
    const auto r = set_rac_moby_sequence_v1(1U, frame, 2U, f.bank(), kLimits);
    expect(r.indices.previous_frame == expected_previous &&
               r.indices.current_frame == 0U,
           "zero count still has literal source previous-frame read and "
           "next-index zero");
  }
  f.sequences[1U].frame_count = 255U;
  const auto last = set_rac_moby_sequence_v1(1U, 254U, 0U, f.bank(), kLimits);
  const auto negative =
      set_rac_moby_sequence_v1(1U, UINT64_MAX, 0U, f.bank(), kLimits);
  expect(last.indices.previous_frame == 254U &&
             last.indices.current_frame == 254U &&
             negative.indices.previous_frame == 255U &&
             negative.indices.current_frame == 0U,
         "byte overflow must occur at the original ADDIU/SB boundary");
}

void test_initial_word_index_vs_later_sequence_byte() {
  Fixture f;
  const auto high_index =
      set_rac_moby_sequence_v1(256U, 9U, 0U, f.bank(), kLimits);
  expect(high_index.indices == RacMobyFrameIndicesV1{1U, 1U, 0U, 0U} &&
             high_index.references.previous_frame_reference == 0x300010U &&
             high_index.animation_rate_bits == 0x80000001U,
         "initial256 count2 differs from leaf0 count5; do not truncate "
         "sequence too early");
  const auto reference =
      set_rac_moby_sequence_v1(1U, 1U, 0xabU, f.bank(), kLimits);
  for (auto argument :
       std::array<std::uint64_t, 5U>{0x40000001U, 0x80000001U, 0xc0000001U,
                                     0x100000001ULL, 0xffffffff00000001ULL})
    expect(
        set_rac_moby_sequence_v1(argument, 1U, 0xabU, f.bank(), kLimits) ==
            reference,
        "SLL2 drops high two low-word bits and all upper32 before addressing");
  const auto snapshot =
      set_rac_moby_sequence_v1(UINT64_MAX, 100U, 0xffU, f.bank(), kLimits);
  expect(snapshot.indices == RacMobyFrameIndicesV1{0U, 0U, 255U, 255U} &&
             snapshot.references.previous_frame_reference == 0x18f040U &&
             snapshot.references.current_frame_reference == 0x304000U &&
             snapshot.references.sound_byte == 0xffU &&
             snapshot.references.trigger_byte == 0U &&
             snapshot.animation_rate_bits == 0xff000000U,
         "wrapping initial3fffffff and laterFF snapshot/current table must "
         "remain distinct");
}

void test_raw_rate_and_flag_preservation() {
  Fixture f;
  const auto ref = f.frames[1U][0U].frame_reference;
  auto header =
      std::find_if(f.first_words.begin(), f.first_words.end(),
                   [ref](const auto &v) { return v.frame_reference == ref; });
  for (auto raw :
       std::array{0U, 0x80000000U, 0x7f800000U, 0x7fc12345U, 0xffffffffU}) {
    header->first_word_bits = raw;
    for (std::uint32_t flags = 0U; flags <= 255U; ++flags) {
      const auto result = set_rac_moby_sequence_v1(
          1U, 0U, static_cast<std::uint8_t>(flags), f.bank(), kLimits);
      expect(result.animation_rate_bits == raw &&
                 result.sequence_flags == (flags & 0xfdU),
             "raw LWC1/SWC1 rate and all unaffected flag bits must survive");
    }
  }
}

void test_fresh_leaf_specialization_agreement() {
  Fixture f;
  for (std::uint32_t sound = 0U; sound <= 255U; ++sound) {
    f.sequences[0U].sound_byte = static_cast<std::uint8_t>(sound);
    f.sequences[0U].trigger_byte = static_cast<std::uint8_t>(255U - sound);
    RacMobyFreshModelV1 model;
    model.reference = 0x400000U;
    model.sequence0 =
        RacMobyFreshSequenceV1{0x300000U, 5U, static_cast<std::uint8_t>(sound),
                               static_cast<std::uint8_t>(255U - sound)};
    const auto fresh = construct_rac_moby_fresh_v1({1U, 0U, 2U, 0U, model});
    const auto leaf =
        resolve_rac_moby_frame_references_v1({}, f.bank(), kLimits);
    expect(fresh.previous_frame_reference == leaf.previous_frame_reference &&
               fresh.current_frame_reference == leaf.current_frame_reference &&
               fresh.sound_byte == leaf.sound_byte &&
               fresh.trigger_byte == leaf.trigger_byte,
           "general leaf differs from unchanged source-valid fresh "
           "specialization");
  }
}

void test_missing_ownership_and_limits() {
  Fixture f;
  auto bank = f.bank();
  error([&] {
    (void)set_rac_moby_sequence_v1(1U, 0U, 0U, bank, {0U, 2048U, 2048U});
  });
  error([&] {
    (void)set_rac_moby_sequence_v1(1U, 0U, 0U, bank, {4U, 2048U, 2048U});
  });
  error([&] {
    (void)set_rac_moby_sequence_v1(1U, 0U, 0U, bank, {16U, 1279U, 2048U});
  });
  error([&] {
    (void)set_rac_moby_sequence_v1(1U, 0U, 0U, bank, {16U, 2048U, 1535U});
  });
  error([&] { (void)set_rac_moby_sequence_v1(2U, 0U, 0U, bank, kLimits); });
  bank.frame_first_words = {};
  error([&] { (void)set_rac_moby_sequence_v1(1U, 0U, 0U, bank, kLimits); });
  error([&] {
    (void)set_rac_moby_sequence_v1(255U, 0U, 0U, bank, kLimits);
  }); // snapshot contents not invented
  expect(resolve_rac_moby_frame_references_v1({}, bank, kLimits)
                 .previous_frame_reference == 0x300000U,
         "leaf without header read must not require first-word contents");
  f.sequences[1U].frames = std::span{f.frames[1U]}.subspan(1U);
  error([&] { (void)set_rac_moby_sequence_v1(1U, 0U, 0U, f.bank(), kLimits); });
  f.sequences[1U].frames = f.frames[1U];
  f.sequences[1U].table_word_index = 0U;
  error([&] {
    (void)resolve_rac_moby_frame_references_v1({}, f.bank(), kLimits);
  });
  f.sequences[1U].table_word_index = 0x40000000U;
  error([&] {
    (void)resolve_rac_moby_frame_references_v1({}, f.bank(), kLimits);
  });
  f.sequences[1U].table_word_index = 1U;
  f.frames[1U][1U].logical_frame_index = 0U;
  error([&] {
    (void)resolve_rac_moby_frame_references_v1({}, f.bank(), kLimits);
  });
  f.frames[1U][1U].logical_frame_index = 1U;
  f.first_words[0U].frame_reference += 1U;
  error([&] {
    (void)resolve_rac_moby_frame_references_v1({}, f.bank(), kLimits);
  });
  f.first_words[0U].frame_reference -= 1U;
  f.first_words[1U].frame_reference = f.first_words[0U].frame_reference;
  error([&] {
    (void)resolve_rac_moby_frame_references_v1({}, f.bank(), kLimits);
  });
  const RacMobySequenceBankV1 empty;
  error([&] {
    (void)resolve_rac_moby_frame_references_v1({0U, 0U, 255U, 0U}, empty,
                                               kLimits);
  });
}

} // namespace

int main() {
  try {
    test_complete_leaf_regular_and_snapshot();
    test_signed64_frame_arguments();
    test_zero_count_and_byte_overflow();
    test_initial_word_index_vs_later_sequence_byte();
    test_raw_rate_and_flag_preservation();
    test_fresh_leaf_specialization_agreement();
    test_missing_ownership_and_limits();
    std::cout << "rac_moby_sequence_set_tests: PASS\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rac_moby_sequence_set_tests: " << error.what() << '\n';
    return 1;
  }
}
