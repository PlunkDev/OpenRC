#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kDiscTocSectorSize = 2048;
inline constexpr std::uint32_t kDiscTocGlobalLba = 1500;
inline constexpr std::size_t kDiscTocGlobalExtentSlotCount = 479;
inline constexpr std::size_t kDiscTocLevelCount = 19;
inline constexpr std::size_t kDiscTocPrimaryExtentCount = 4;

enum class DiscTocSignature {
    wad,
    vagp,
    two_fip,
    ps2d,
    other,
};

struct DiscTocExtent {
    std::uint32_t lba = 0;
    std::uint32_t sectors = 0;
};

struct DiscTocGlobalExtent {
    std::size_t slot = 0;
    DiscTocExtent extent;
    DiscTocSignature signature = DiscTocSignature::other;
};

struct DiscTocLevelDescriptor {
    std::uint32_t level_id = 0;
    std::uint32_t toc_lba = 0;
    std::uint32_t auxiliary = 0;
    std::array<DiscTocExtent, kDiscTocPrimaryExtentCount> primary_extents{};
};

struct DiscTocReport {
    std::filesystem::path image_path;
    std::uint64_t image_bytes = 0;
    std::uint64_t image_sectors = 0;
    std::uint64_t declared_volume_bytes = 0;
    std::uint64_t declared_volume_sectors = 0;
    std::uint32_t version = 0;
    std::uint32_t byte_size = 0;
    std::size_t global_extent_slot_count = 0;
    std::size_t empty_global_extent_slot_count = 0;
    std::vector<DiscTocGlobalExtent> global_extents;
    std::vector<DiscTocLevelDescriptor> levels;
};

struct DiscTocSizedLba {
    std::uint32_t lba = 0;
    std::uint32_t byte_size = 0;
};

struct DiscTocAssetRef {
    std::uint32_t lba = 0;
    std::uint32_t logical_bytes = 0;
    std::uint32_t occupied_sectors = 0;
    DiscTocSignature signature = DiscTocSignature::other;
};

struct DiscTocWadRun {
    std::vector<DiscTocAssetRef> wads;
    std::optional<std::uint32_t> trailing_zero_sector_lba;
};

struct DiscTocResourceBlock {
    std::array<std::uint32_t, 6> speech_vag_lbas{};
    std::array<DiscTocWadRun, 2> wad_runs{};
};

struct DiscTocLocalTables {
    std::array<DiscTocSizedLba, 30> sized_vags{};
    std::array<std::uint32_t, 11> music_vag_lbas{};
    std::array<DiscTocResourceBlock, 15> resource_blocks{};
};

struct DiscTocSubrange {
    std::uint32_t relative_offset = 0;
    std::uint32_t byte_size = 0;
    DiscTocSignature signature = DiscTocSignature::other;
};

struct DiscTocExtent0Index {
    std::array<DiscTocSubrange, 16> subranges{};
    std::size_t used_subrange_count = 0;
};

struct DiscTocOpaquePair {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
};

struct DiscTocExtent3Tables {
    std::array<std::vector<DiscTocOpaquePair>, 3> tables{};
};

struct DiscTocLevelAssets {
    std::uint32_t level_id = 0;
    DiscTocLocalTables local_tables;
    std::vector<DiscTocAssetRef> referenced_vags;
    DiscTocExtent0Index primary_extent0;
    std::array<DiscTocAssetRef, 2> primary_wads{};
    DiscTocExtent3Tables primary_extent3;
};

struct DiscTocAssetReport {
    DiscTocReport layout;
    std::vector<DiscTocLevelAssets> levels;
};

class DiscTocError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] DiscTocReport inspect_disc_toc(const std::filesystem::path& image_path);
[[nodiscard]] DiscTocAssetReport inspect_disc_toc_assets(
    const std::filesystem::path& image_path);
[[nodiscard]] std::string_view disc_toc_signature_name(DiscTocSignature signature) noexcept;

} // namespace openrc
