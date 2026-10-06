#include "openrc/rac_startup.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <algorithm>

namespace {
using namespace openrc;
using Bytes = std::vector<std::byte>;
constexpr RacStartupLimitsV1 kLimits{4096U, 8192U};
constexpr auto kImageBytes = UINT64_C(1510) * 2048U;

void expect(bool condition, const char *description) {
  if (!condition) throw std::runtime_error(description);
}
template<class F> void error(F operation) {
  try { operation(); } catch (const RacStartupError &) { return; }
  throw std::runtime_error("Expected bounded startup rejection");
}
void write32(Bytes &bytes, std::size_t at, std::uint32_t value) {
  for (unsigned lane = 0U; lane < 4U; ++lane)
    bytes.at(at + lane) = static_cast<std::byte>((value >> (lane * 8U)) & 255U);
}
Bytes toc() {
  Bytes bytes(0x2960U);
  write32(bytes, 0U, 1U);
  write32(bytes, 4U, static_cast<std::uint32_t>(bytes.size()));
  write32(bytes, 0x17f8U, 1506U);
  write32(bytes, 0x17fcU, 2049U);
  write32(bytes, 0x1800U, 1508U);
  write32(bytes, 0x1804U, 101U);
  return bytes;
}
RacStartupCatalogV1 parse(const Bytes &bytes,
                          RacStartupLimitsV1 limits = kLimits) {
  return parse_rac_startup_catalog_v1(bytes, kImageBytes, std::byte{'P'}, limits);
}

void test_catalog_and_selection() {
  const auto bytes = toc();
  const auto catalog = parse(bytes);
  expect(catalog.initial_selector == 1U, "Original non-N boot byte selects one");
  expect(parse_rac_startup_catalog_v1(bytes, kImageBytes, std::byte{'N'}, kLimits)
             .initial_selector == 0U, "Original N boot byte selects zero");
  expect(parse_rac_startup_catalog_v1(bytes, kImageBytes, std::byte{0U}, kLimits)
             .initial_selector == 1U, "Boot comparison is exact, not a locale whitelist");
  const auto &first = select_rac_startup_movie_v1(catalog, 0U);
  expect(first.source_toc_offset == 0x17f8U && first.source_call_pc == 0x1e9c34U &&
         first.extent.byte_size == 2049U && first.source_byte_offset == 1506U * 2048U,
         "Movie record is byte sized and bound to its source branch");
  for (auto selector : {1U, 2U, 255U, UINT32_MAX}) {
    const auto &other = select_rac_startup_movie_v1(catalog, selector);
    expect(&other == &catalog.movies[1U] && other.source_call_pc == 0x1e9c70U &&
           other.extent.byte_size == 101U, "Every nonzero selector takes the second branch");
  }
  auto padded = bytes;
  padded.resize(6U * 2048U);
  expect(parse(padded).movies[1U].extent.lba == 1508U, "Accept the TOC sector envelope");
  (void)parse(bytes, {2049U, 2150U});
  error([&] { (void)parse(bytes, {2048U, 8192U}); });
  error([&] { (void)parse(bytes, {4096U, 2149U}); });
  error([&] { (void)parse(bytes, {0U, 8192U}); });
}

void test_catalog_ranges() {
  auto bytes = toc();
  for (std::size_t size : {0U, 4U, 0x1804U, 0x295fU, 0x2961U, 0x3001U}) {
    auto bad = bytes;
    bad.resize(size);
    error([&] { (void)parse(bad); });
  }
  for (const auto field : {0U, 4U, 0x17f8U, 0x17fcU, 0x1800U, 0x1804U}) {
    auto bad = bytes;
    write32(bad, field, 0U);
    error([&] { (void)parse(bad); });
  }
  for (auto lba : {1505U, 1507U, 1510U, UINT32_MAX}) {
    auto bad = bytes;
    write32(bad, 0x1800U, lba);
    error([&] { (void)parse(bad); });
  }
  error([&] { (void)parse_rac_startup_catalog_v1(
      bytes, kImageBytes - 1U, std::byte{'P'}, kLimits); });
  auto bad = bytes;
  write32(bad, 0x1804U, 3U);
  error([&] { (void)parse(bad); });
}

void test_prelude_order_and_boundaries() {
  RacStartupPreludeV1 empty;
  auto step = empty.step(0U, UINT32_MAX);
  expect(step.enter_intro && !step.fade_previous_image && empty.rendered_frames() == 0U,
         "A zero poll bypasses the warning loop without inventing a prior image");
  error([&] { (void)empty.step(0U, 0U); });
  RacStartupPreludeV1 prelude;
  error([&] { (void)prelude.step(3U, 0U); });
  expect(prelude.rendered_frames() == 0U && !prelude.entered_intro(),
         "Invalid poll leaves state unchanged");
  for (unsigned frame = 0U; frame < 11U; ++frame) {
    const auto poll = frame < 4U ? 1U : 2U;
    step = prelude.step(poll, 1U);
    expect(!step.enter_intro && step.reload_image == (frame == 0U || frame == 4U) &&
           step.image_row_family == poll, "Source reloads only on poll state change");
    expect(step.black_overlay_alpha == (frame < 8U ?
             std::optional<std::uint32_t>(128U - frame * 16U) : std::nullopt),
           "Source fade affects exactly the first eight rendered frames");
  }
  step = prelude.step(2U, 0U);
  expect(!step.enter_intro, "Eleven frames alone do not exit a nonzero poll");
  step = prelude.step(2U, 0x40000000U);
  expect(step.enter_intro && step.fade_previous_image && prelude.rendered_frames() == 12U,
         "Any pressed word exits after the threshold, with prior-image fade");
  RacStartupPreludeV1 early;
  (void)early.step(1U, 0U);
  expect(early.step(0U, 0U).enter_intro, "Zero poll does not wait eleven frames");
}

void test_movie_input_and_feed() {
  constexpr auto chord = (UINT64_C(0x8000) << 28U) | 15U;
  for (const auto mode : {-1, 0, 1, 2, 3}) {
    for (const auto level : {-1, 0, 1}) {
      for (unsigned flags = 0U; flags < 4U; ++flags) {
        RacMovieSkipInputsV1 in{mode, flags & 1U, flags & 2U, level, 0U, 0U};
        expect(!rac_movie_skip_requested_v1(in), "No input does not skip");
        in.pressed_word = 1U;
        expect(rac_movie_skip_requested_v1(in) == (mode == 2),
               "Only mode two accepts arbitrary pressed inputs");
        in.pressed_word = 0x800U;
        expect(rac_movie_skip_requested_v1(in) ==
                   (mode != -1 && (mode != 0 || flags != 0U || level <= 0)),
               "Source start-button gate respects flags and signed level");
        in.held_word = chord;
        expect(rac_movie_skip_requested_v1(in) == (mode != -1),
               "Startup mode minus one blocks even the complete held chord");
        in.pressed_word = 0U;
        for (unsigned bit : {0U, 1U, 2U, 3U, 43U}) {
          in.held_word = chord ^ (UINT64_C(1) << bit);
          expect(!rac_movie_skip_requested_v1(in), "Every held chord bit is required");
        }
      }
    }
  }
  for (auto remaining : {std::numeric_limits<std::int32_t>::min(), -1, 0, 4, 5, 6,
                          std::numeric_limits<std::int32_t>::max()}) {
    for (auto status : {0U, 1U, 2U, 3U, 4U, UINT32_MAX})
      expect(rac_movie_should_feed_v1(remaining, status) == (remaining >= 5 && status != 3U),
             "Feeder gate preserves signed byte count and exact terminal status");
  }
}

void test_real_reader_on_synthetic_disc() {
  struct TemporaryFile {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("openrc-startup-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".iso");
    ~TemporaryFile() { std::error_code ec; std::filesystem::remove(path, ec); }
  } file;
  {
    std::ofstream output(file.path, std::ios::binary);
    output.seekp(static_cast<std::streamoff>(kImageBytes - 1U));
    output.put('\0');
    output.seekp(289 * 2048 + 0x33);
    output.put('N');
    const auto bytes = toc();
    output.seekp(1500 * 2048);
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    const std::array<char, 4U> signature{'\0', '\0', '\1', '\xba'};
    for (auto lba : {1506U, 1508U}) {
      output.seekp(lba * 2048U);
      output.write(signature.data(), signature.size());
    }
    expect(bool(output), "Synthetic source creation succeeded");
  }
  const auto catalog = read_rac_startup_catalog_v1(file.path, kLimits);
  expect(catalog.initial_selector == 0U, "File reader reads the actual boot byte");
  for (const auto &movie : catalog.movies) {
    const auto bytes = read_rac_startup_movie_v1(file.path, movie, kLimits.max_movie_bytes);
    expect(bytes.size() == movie.extent.byte_size && bytes[3U] == std::byte{0xbaU},
           "Reader excludes sector padding and validates the pack prefix");
  }
  auto bad = catalog.movies[0U];
  ++bad.source_byte_offset;
  error([&] { (void)read_rac_startup_movie_v1(file.path, bad, kLimits.max_movie_bytes); });
  bad = catalog.movies[0U];
  bad.extent.lba = 1509U;
  bad.extent.byte_size = 64U;
  bad.source_byte_offset = 1509U * 2048U;
  error([&] { (void)read_rac_startup_movie_v1(file.path, bad, kLimits.max_movie_bytes); });
}
void test_post_intro_image() {
  Bytes pixels(512U*448U*4U);
  for(std::size_t i=0;i<pixels.size();++i) pixels[i]=static_cast<std::byte>((i*17U)&255U);
  Bytes wad(16);wad[0]=std::byte{'W'};wad[1]=std::byte{'A'};wad[2]=std::byte{'D'};
  for(std::size_t at=0;at<pixels.size();) {
    if(at) wad.insert(wad.end(),{std::byte{0x11},std::byte{0},std::byte{0}});
    auto count=std::min<std::size_t>(273,pixels.size()-at);
    if(pixels.size()-at>count && pixels.size()-at-count<18) count-=18;
    wad.push_back(std::byte{0});wad.push_back(static_cast<std::byte>(count-18));
    wad.insert(wad.end(),pixels.begin()+at,pixels.begin()+at+count);at+=count;
  }
  write32(wad,3,static_cast<std::uint32_t>(wad.size()));
  Bytes source(128);write32(source,8,128);source.insert(source.end(),wad.begin(),wad.end());
  const auto image=compile_rac_startup_image_v1(source,1);
  expect(image.width==512&&image.height==448&&image.color_transfers.size()==12&&
      image.display_aspect_numerator==1&&image.display_aspect_denominator==1&&
      image.minimum_initialization_updates==150,"Post-intro source timing or geometry differs");
  expect(image.color_transfers.front()[255]==std::byte{233}&&image.color_transfers.back()[255]==std::byte{0},"Source fade integer quotient differs");
  for(std::size_t i=0;i<pixels.size();++i)
    expect(image.rgba[i]==(i%4==3?std::byte{255}:pixels[i]),"Bitmap compiler changed a source RGB channel");
  error([&]{(void)compile_rac_startup_image_v1(source,0);});
  write32(source,8,UINT32_MAX);error([&]{(void)compile_rac_startup_image_v1(source,1);});
}
} // namespace

int main() {
  try {
    test_catalog_and_selection();
    test_catalog_ranges();
    test_prelude_order_and_boundaries();
    test_movie_input_and_feed();
    test_real_reader_on_synthetic_disc();
    test_post_intro_image();
    std::cout << "RAC startup tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
