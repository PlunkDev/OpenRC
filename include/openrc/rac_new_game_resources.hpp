#pragma once

#include "openrc/loading_presentation.hpp"
#include "openrc/rac_new_game_flow.hpp"
#include "openrc/rac_frontend_loading_raster.hpp"
#include "openrc/rac_pss.hpp"
#include <filesystem>

namespace openrc {
[[nodiscard]] LoadingPresentationV1 compile_rac_loading_presentation_v1(
    const RacFrontendLoadingAssetsV1 &assets,std::uint32_t first_card,
    std::uint32_t second_card,std::uint32_t updates_per_second,
    std::uint32_t display_origin_y=224U,LoadingPresentationLimitsV1 limits={});
struct RacNewGameResourceLimitsV1 {
  std::uint64_t max_elf_bytes=32U*1024U*1024U;
  std::uint64_t max_wad_source_bytes=1024U*1024U,max_wad_decoded_bytes=1024U*1024U;
  std::uint64_t max_total_source_movie_bytes=128U*1024U*1024U;
  std::uint64_t max_total_payload_bytes=256U*1024U*1024U;
  MediaClipLimitsV1 media;
  LoadingPresentationLimitsV1 loading;
};
struct RacNewGamePreparedResourcesV1 {
  std::vector<LevelPackageResourceV1> resources;
  RacNewGameFlowResourcesV1 sequence_bindings;
  PreparedContentDigestV1 source_elf_sha256{},source_toc_sha256{};
  std::array<PreparedContentDigestV1,3> source_movie_sha256{};
  PreparedContentDigestV1 source_loading_wad_sha256{};
};
// Supported original ELF/TOC admission, then existing bounded WAD/PIF/raster
// and PSS compilers. Returned payloads are exclusively neutral; source hashes
// and ranges remain compiler provenance. This does not perform native loader,
// audio teardown, display or section-admission barriers from the sequence.
[[nodiscard]] RacNewGamePreparedResourcesV1 compile_rac_new_game_resources_v1(
    const std::filesystem::path &source_image,std::span<const std::byte> source_elf,
    std::uint32_t language,std::uint32_t video_selector,
    std::uint32_t updates_per_second,RacNewGameResourceLimitsV1 limits={});
} // namespace openrc
