#pragma once

#include "openrc/disc_toc.hpp"
#include "openrc/image_presentation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

// Compiler-side source bindings for entry 1e99d8. These records are lowered
// into neutral prepared media; source addresses and ISO readers are not a
// runtime startup resource or an implementation of the frontend.
struct RacStartupMovieV1 {
  std::uint32_t source_toc_offset = 0U;
  std::uint32_t source_call_pc = 0U;
  DiscTocSizedLba extent;
  std::uint64_t source_byte_offset = 0U;
};

struct RacStartupCatalogV1 {
  std::uint64_t image_bytes = 0U;
  // 201f64 reads sector289; 201f74..90 computes byte[0x33] != 'N'.
  std::uint32_t initial_selector = 0U;
  // Branch 15ee80==0, then branch 15ee80!=0. These are alternatives,
  // not a two-movie sequence. No locale or video-mode name is inferred.
  std::array<RacStartupMovieV1, 2U> movies;
};

struct RacStartupLimitsV1 {
  std::uint64_t max_movie_bytes = 0U;
  std::uint64_t max_total_movie_bytes = 0U;
};

class RacStartupError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Accept the logical global TOC (0x2960 bytes) or its six-sector envelope.
// Both selected records use BYTE sizes, not the global extent sector count.
[[nodiscard]] RacStartupCatalogV1 parse_rac_startup_catalog_v1(
    std::span<const std::byte> global_toc, std::uint64_t image_bytes,
    std::byte boot_region_byte, RacStartupLimitsV1 limits);

[[nodiscard]] RacStartupCatalogV1 read_rac_startup_catalog_v1(
    const std::filesystem::path &image, RacStartupLimitsV1 limits);

[[nodiscard]] const RacStartupMovieV1 &select_rac_startup_movie_v1(
    const RacStartupCatalogV1 &catalog,
    std::uint32_t actual_selector_15ee80) noexcept;

// Read exactly the logical source movie, excluding its sector padding.
// This checks the MPEG program-stream pack signature; decoding and real
// decoder completion remain the consuming compiler/player's responsibility.
[[nodiscard]] std::vector<std::byte> read_rac_startup_movie_v1(
    const std::filesystem::path &image, const RacStartupMovieV1 &movie,
    std::uint64_t max_movie_bytes);

struct RacStartupWadV1 {
  DiscTocExtent extent;
  std::uint64_t source_byte_offset=0;
  std::vector<std::byte> source_bytes;
  std::vector<std::byte> decoded_bytes;
};
// Reads a bounded sector-sized global WAD record; original offsets remain on
// the compiler side. Used for the boot bitmap and frontend asset catalog.
[[nodiscard]] RacStartupWadV1 read_rac_startup_wad_v1(
    const std::filesystem::path& image, std::uint32_t toc_offset,
    std::uint64_t max_source_bytes, std::uint64_t max_decoded_bytes);
// The supported PAL startup owner: 1e9c7c..1ebd68, including its actual
// unit-scale pre-init fade and changed 5/6 scale after 1eabe8->1e9ec8.
[[nodiscard]] ImagePresentationV1 compile_rac_startup_image_v1(
    std::span<const std::byte> decoded_boot_bitmap_wad,
    std::uint32_t actual_selector_15ee80);

// Original1f4e08 feedback fade. Literal source duration counts color
// transfers; one video-field wait precedes and one follows the body. Tables
// preserve the integer quotient and the GS byte rounding between updates.
[[nodiscard]] FrameColorTransferSequenceV1 compile_rac_frontend_fade_v1(
    std::uint32_t source_updates,std::uint32_t updates_per_second,
    FrameColorTransferSequenceLimitsV1 limits={});

// The original pre-intro loop consumes the completed 209bb8 result (0/1/2).
// It does not invent a memory-card response or execute that I/O procedure.
struct RacStartupPreludeStepV1 {
  bool enter_intro = false;
  bool reload_image = false;
  std::uint32_t image_row_family = 0U; // source poll 1 or 2, without a UI name
  std::optional<std::uint32_t> black_overlay_alpha;
  // Before entering intro, source calls 1f4e08(8) iff a prior image was drawn.
  bool fade_previous_image = false;
};

class RacStartupPreludeV1 final {
public:
  [[nodiscard]] RacStartupPreludeStepV1 step(
      std::uint32_t completed_poll_result, std::uint32_t pressed_word);
  [[nodiscard]] std::uint32_t rendered_frames() const noexcept;
  [[nodiscard]] bool entered_intro() const noexcept;

private:
  std::uint32_t previous_poll_ = 0U;
  std::uint32_t frames_ = 0U;
  std::uint32_t alpha_ = 128U;
  bool entered_intro_ = false;
};

// Source 23b7dc..23b888. Named source inputs are actual words, never defaults
// for unobserved live state. Startup explicitly sets movie_mode=-1 before
// 23b670, so its intro cannot be skipped by this input path.
struct RacMovieSkipInputsV1 {
  std::int32_t movie_mode = -1;
  std::uint32_t flag_15efa0 = 0U;
  std::uint32_t flag_15ef20 = 0U;
  std::int32_t level_15ee84 = 0;
  std::uint32_t pressed_word = 0U;
  std::uint64_t held_word = 0U;
};

[[nodiscard]] bool
rac_movie_skip_requested_v1(const RacMovieSkipInputsV1 &input) noexcept;

// The feeder's exact loop gate (23ba44..23ba60). False begins the drain;
// it does not mean the last frame/audio has been presented. Source then
// waits for 23e0d8, and for 23e1b0 or decoder status 3, before cleanup.
[[nodiscard]] bool rac_movie_should_feed_v1(
    std::int32_t remaining_bytes, std::uint32_t decoder_status) noexcept;

} // namespace openrc
