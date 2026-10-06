#include "openrc/rac_startup.hpp"
#include "openrc/rac_frontend_new_game.hpp"
#include "openrc/wad.hpp"

#include <bit>
#include <fstream>
#include <limits>

namespace openrc {
namespace {

constexpr std::size_t kLogicalTocBytes = 0x2960U;
constexpr std::size_t kTocEnvelopeBytes = 6U * kDiscTocSectorSize;
constexpr std::array<std::uint32_t, 2U> kMovieOffsets{0x17f8U, 0x1800U};
constexpr std::array<std::uint32_t, 2U> kMovieCalls{0x1e9c34U, 0x1e9c70U};

[[noreturn]] void fail(const char *message) { throw RacStartupError(message); }

std::uint32_t read32(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t result = 0U;
  for (unsigned lane = 0U; lane < 4U; ++lane)
    result |= std::to_integer<std::uint32_t>(bytes[offset + lane]) << (8U * lane);
  return result;
}

std::uint64_t validate_extent(const RacStartupMovieV1 &movie,
                              std::uint64_t image_bytes,
                              std::uint64_t max_bytes) {
  const auto bytes = std::uint64_t{movie.extent.byte_size};
  const auto offset = std::uint64_t{movie.extent.lba} * kDiscTocSectorSize;
  const auto occupied = (bytes + kDiscTocSectorSize - 1U) /
                        kDiscTocSectorSize * kDiscTocSectorSize;
  if (max_bytes == 0U || bytes < 4U || bytes > max_bytes ||
      movie.extent.lba < kDiscTocGlobalLba + 6U ||
      movie.source_byte_offset != offset || offset > image_bytes ||
      occupied > image_bytes - offset)
    fail("RAC startup movie has an invalid bounded disc extent");
  return offset + occupied;
}

void read_exact(std::ifstream &input, std::uint64_t offset,
                std::span<std::byte> destination) {
  if (offset > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamoff>::max()) ||
      destination.size() > static_cast<std::uint64_t>(
                               std::numeric_limits<std::streamsize>::max()))
    fail("RAC startup source range exceeds host stream limits");
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  input.read(reinterpret_cast<char *>(destination.data()),
             static_cast<std::streamsize>(destination.size()));
  if (!input || input.gcount() != static_cast<std::streamsize>(destination.size()))
    fail("RAC startup source read is truncated or failed");
}

} // namespace

RacStartupCatalogV1 parse_rac_startup_catalog_v1(
    std::span<const std::byte> global_toc, std::uint64_t image_bytes,
    std::byte boot_region_byte, RacStartupLimitsV1 limits) {
  if (limits.max_movie_bytes == 0U || limits.max_total_movie_bytes == 0U)
    fail("RAC startup catalog limits must be positive");
  if ((global_toc.size() != kLogicalTocBytes &&
       global_toc.size() != kTocEnvelopeBytes) ||
      read32(global_toc, 0U) != 1U ||
      read32(global_toc, 4U) != kLogicalTocBytes ||
      image_bytes % kDiscTocSectorSize != 0U)
    fail("RAC startup global TOC has an unsupported envelope");
  RacStartupCatalogV1 result;
  result.image_bytes = image_bytes;
  result.initial_selector = boot_region_byte == std::byte{'N'} ? 0U : 1U;
  std::uint64_t total = 0U;
  std::array<std::uint64_t, 2U> ends{};
  for (std::size_t index = 0U; index < result.movies.size(); ++index) {
    const auto at = kMovieOffsets[index];
    auto &movie = result.movies[index];
    movie.source_toc_offset = at;
    movie.source_call_pc = kMovieCalls[index];
    movie.extent = {read32(global_toc, at), read32(global_toc, at + 4U)};
    movie.source_byte_offset = std::uint64_t{movie.extent.lba} *
                               kDiscTocSectorSize;
    ends[index] = validate_extent(movie, image_bytes, limits.max_movie_bytes);
    if (movie.extent.byte_size > limits.max_total_movie_bytes - total)
      fail("RAC startup movies exceed their aggregate byte limit");
    total += movie.extent.byte_size;
  }
  if (result.movies[0U].source_byte_offset < ends[1U] &&
      result.movies[1U].source_byte_offset < ends[0U])
    fail("RAC startup movie extents overlap");
  return result;
}

RacStartupCatalogV1 read_rac_startup_catalog_v1(
    const std::filesystem::path &image, RacStartupLimitsV1 limits) {
  std::ifstream input(image, std::ios::binary);
  if (!input) fail("Cannot open the RAC startup source image");
  const auto image_bytes = std::filesystem::file_size(image);
  std::array<std::byte, kLogicalTocBytes> toc{};
  read_exact(input, std::uint64_t{kDiscTocGlobalLba} * kDiscTocSectorSize, toc);
  std::array<std::byte, 1U> boot_region{};
  read_exact(input, UINT64_C(289) * kDiscTocSectorSize + 0x33U, boot_region);
  return parse_rac_startup_catalog_v1(toc, image_bytes, boot_region[0U], limits);
}

const RacStartupMovieV1 &select_rac_startup_movie_v1(
    const RacStartupCatalogV1 &catalog,
    std::uint32_t actual_selector_15ee80) noexcept {
  return catalog.movies[actual_selector_15ee80 == 0U ? 0U : 1U];
}

std::vector<std::byte> read_rac_startup_movie_v1(
    const std::filesystem::path &image, const RacStartupMovieV1 &movie,
    std::uint64_t max_movie_bytes) {
  std::ifstream input(image, std::ios::binary);
  if (!input) fail("Cannot open the RAC startup source image");
  (void)validate_extent(movie, std::filesystem::file_size(image), max_movie_bytes);
  if (movie.extent.byte_size > std::vector<std::byte>{}.max_size())
    fail("RAC startup movie exceeds host allocation limits");
  std::vector<std::byte> bytes(movie.extent.byte_size);
  read_exact(input, movie.source_byte_offset, bytes);
  if (bytes[0U] != std::byte{0U} || bytes[1U] != std::byte{0U} ||
      bytes[2U] != std::byte{1U} || bytes[3U] != std::byte{0xbaU})
    fail("RAC startup movie is missing its MPEG program-stream pack signature");
  return bytes;
}

RacStartupWadV1 read_rac_startup_wad_v1(
    const std::filesystem::path& image,std::uint32_t toc_offset,
    std::uint64_t max_source_bytes,std::uint64_t max_decoded_bytes) {
  if(toc_offset<8U||toc_offset>kLogicalTocBytes-8U||toc_offset%8U||
      !max_source_bytes||!max_decoded_bytes) fail("Invalid startup WAD catalog range or limits");
  std::ifstream input(image,std::ios::binary);
  if(!input) fail("Cannot open startup WAD source image");
  std::array<std::byte,8> row{};
  read_exact(input,std::uint64_t(kDiscTocGlobalLba)*kDiscTocSectorSize+toc_offset,row);
  RacStartupWadV1 result;result.extent={read32(row,0),read32(row,4)};
  result.source_byte_offset=std::uint64_t(result.extent.lba)*kDiscTocSectorSize;
  const auto count=std::uint64_t(result.extent.sectors)*kDiscTocSectorSize;
  const auto total=std::filesystem::file_size(image);
  if(result.extent.lba<kDiscTocGlobalLba+6U||count<16U||count>max_source_bytes||
      count>result.source_bytes.max_size()||result.source_byte_offset>total||
      count>total-result.source_byte_offset) fail("Startup WAD leaves its bounded source extent");
  result.source_bytes.resize(static_cast<std::size_t>(count));
  read_exact(input,result.source_byte_offset,result.source_bytes);
  const auto logical=read32(result.source_bytes,3);
  if(logical<16U||logical>result.source_bytes.size()) fail("Invalid startup WAD logical extent");
  try { result.decoded_bytes=decode_wad_bytes(std::span(result.source_bytes).first(logical),max_decoded_bytes).bytes; }
  catch(const WadError& error) { throw RacStartupError(error.what()); }
  return result;
}

ImagePresentationV1 compile_rac_startup_image_v1(
    std::span<const std::byte> source,std::uint32_t selector) {
  if(selector!=1U||source.size()<0x74U||source.size()>16U*1024U*1024U)
    fail("Startup bitmap owner requires the supported PAL source selection");
  // Source 1e9ca8 uses word+8; the nested logical WAD excludes its padding.
  const auto at=read32(source,8U);
  if(at>source.size()||source.size()-at<16U) fail("Startup bitmap leaves its source table");
  const auto logical=read32(source,at+3U);
  if(logical<16U||logical>source.size()-at) fail("Startup bitmap WAD is truncated");
  ImagePresentationV1 result;
  result.width=512;result.height=448;
  // 201af0 transfers512x448, then1e9d14->1fb8a8/151c60 stretches it to
  // the512x512 logical display. Source alpha is not coverage; there is no
  // invented64-row padding or analog overscan correction.
  result.display_aspect_numerator=1;result.display_aspect_denominator=1;
  try { result.rgba=decode_wad_bytes(source.subspan(at,logical),512U*448U*4U).bytes; }
  catch(const WadError& error) { throw RacStartupError(error.what()); }
  if(result.rgba.size()!=512U*448U*4U) fail("Startup bitmap does not cover the selected framebuffer");
  for(std::size_t i=3;i<result.rgba.size();i+=4) result.rgba[i]=std::byte{255};
  result.updates_per_second=50;
  const auto fade=evaluate_rac_frontend_timer_v1(12U,0x3f800000U);
  if(fade==0||fade>1024U) fail("Startup fade exceeds its source owner domain");
  auto transfers=compile_rac_frontend_fade_v1(static_cast<std::uint32_t>(fade),result.updates_per_second);
  result.transfer_lead_updates=transfers.lead_updates;result.transfer_tail_updates=transfers.tail_updates;
  result.color_transfers=std::move(transfers.transfers);
  result.initialization_clock_hz=15625;
  result.initialization_clock_modulus=65536;
  result.initialization_credit_divisor=265;
  const auto minimum=evaluate_rac_frontend_timer_v1(180U,0x3f555555U);
  if(minimum>1000U) fail("Startup initialization gate exceeds its source owner domain");
  result.minimum_initialization_updates=static_cast<std::uint32_t>(minimum);
  validate_image_presentation_v1(result);
  return result;
}

FrameColorTransferSequenceV1 compile_rac_frontend_fade_v1(
    std::uint32_t updates,std::uint32_t cadence,FrameColorTransferSequenceLimitsV1 limits) {
  if(!updates||updates>1024U||(cadence!=50U&&cadence!=60U)||updates>limits.max_transfers||
      limits.max_bytes<80U||std::uint64_t(updates)*256U>limits.max_bytes-80U)
    fail("Frontend fade exceeds its source duration/cadence domain");
  FrameColorTransferSequenceV1 result;result.updates_per_second=cadence;result.lead_updates=1;result.tail_updates=1;
  for(auto remaining=updates;remaining>0;--remaining) {
    // 1f4ea0 signed integer quotient, RGBAQ alpha, then untextured sprite
    // 13ced0 (PRIM146). ALPHA44 yields floor(Cd*(128-alpha)/128).
    // 2349b8/234948 rotate command storage, not display framebuffers.
    const auto alpha=128U-(remaining-1U)*128U/remaining;
    std::array<std::byte,256> transfer{};
    for(std::uint32_t c=0;c<256;++c) transfer[c]=static_cast<std::byte>(c*(128U-alpha)/128U);
    result.transfers.push_back(transfer);
  }
  validate_frame_color_transfer_sequence_v1(result,limits);
  return result;
}

RacStartupPreludeStepV1 RacStartupPreludeV1::step(
    std::uint32_t completed_poll_result, std::uint32_t pressed_word) {
  if (entered_intro_) fail("RAC startup prelude already entered intro");
  if (completed_poll_result > 2U)
    fail("RAC startup poll result is outside the recovered 209bb8 domain");
  RacStartupPreludeStepV1 result;
  // SLTI is signed; unsigned counters preserve the source ADDIU wrap.
  if (completed_poll_result == 0U ||
      (std::bit_cast<std::int32_t>(frames_) >= 11 && pressed_word != 0U)) {
    result.enter_intro = true;
    result.fade_previous_image = previous_poll_ != 0U;
    entered_intro_ = true;
    return result;
  }
  result.reload_image = previous_poll_ != completed_poll_result;
  result.image_row_family = completed_poll_result;
  if (std::bit_cast<std::int32_t>(frames_) < 8)
    result.black_overlay_alpha = alpha_;
  previous_poll_ = completed_poll_result;
  alpha_ -= 16U;
  ++frames_;
  return result;
}

std::uint32_t RacStartupPreludeV1::rendered_frames() const noexcept {
  return frames_;
}
bool RacStartupPreludeV1::entered_intro() const noexcept { return entered_intro_; }

bool rac_movie_skip_requested_v1(const RacMovieSkipInputsV1 &input) noexcept {
  if (input.movie_mode == -1) return false;
  if (input.movie_mode == 2 && input.pressed_word != 0U) return true;
  if ((input.flag_15efa0 != 0U || input.flag_15ef20 != 0U ||
       input.movie_mode != 0 || input.level_15ee84 <= 0) &&
      (input.pressed_word & 0x800U) != 0U)
    return true;
  constexpr auto chord = (UINT64_C(0x8000) << 28U) | 0xfU;
  return (input.held_word & chord) == chord;
}

bool rac_movie_should_feed_v1(std::int32_t remaining_bytes,
                              std::uint32_t decoder_status) noexcept {
  return remaining_bytes >= 5 && decoder_status != 3U;
}

} // namespace openrc
