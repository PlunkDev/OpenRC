#pragma once

#include "openrc/disc_toc.hpp"
#include "openrc/prepared_game_v2_publish.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace openrc {

inline constexpr std::uint32_t kNativeGamePreparationNoLevelV1 = UINT32_MAX;
inline constexpr std::string_view kNativeGameIdV1 = "openrc-rac-2002";
inline constexpr std::string_view kNativeGameBuildIdV1 = "SCES-50916-PAL-v2.00";
inline constexpr std::string_view kNativeGameCompilerIdV1 =
    "openrc-asset-compiler";
// The profile suffix is intentionally part of publication identity: packages
// without the complete source-addressed Ratchet animation bank, Veldin
// class-749 actor corpus, and explicit source state-0 entry classification must
// be rebuilt. v5 also invalidates preparation with the incorrect same-pair LOI
// operand: source VU programs must use the preceding I value. This changes
// compiler semantics, not the eight-resource runtime schema. v6 replaces
// ADD/SUB with the integer reference model; v7 also replaces standalone
// MUL/MULA values with the ordered Booth/carry reference model. Earlier
// prepared arithmetic must not be accepted as current compiler output. v8
// retains the exact v7 level semantics and adds the original startup media
// in one shared package, outside the canonical nineteen-level set. v9 changes
// compound ACC arithmetic and adds the original post-intro/frontend resources.
// Its level output must be recompiled; the former v7->v8 byte reuse is invalid.
// v10 preserves conservative unknown VU loads and adds prepared frontend
// terrain/sky geometry with encoded color, alpha-failure and depth policies.
// v12 adds explicit frontend transition state, feedback fades and qualified
// finite frontend audio to the original main/dialog resources and shared state.
// The runtime's remaining source consumers still govern end-to-end admission.
// v11 published the original main/dialog resources, canonical frontend state
// and the complete prepared New Game presentation sequence in the same package.
// v13 adds the qualified scenic audio programs, voice bank and scene cue table.
// Their resource references and clocks are admitted as part of the shared package.
// v14 adds canonical level-local storage and its source-compiled installation
// in the shared New Game continuation. The nineteen level payloads are unchanged.
#ifdef OPENRC_VERSION
inline constexpr std::string_view kNativeGameCompilerVersionV1 =
    OPENRC_VERSION "-native-eight-resource-v14-level-installation";
#else
inline constexpr std::string_view kNativeGameCompilerVersionV1 =
    "0.1.0-dev-native-eight-resource-v14-level-installation";
#endif
inline constexpr std::string_view kNativeGameSharedPackagePathV1 =
    "shared.orlevel";
inline constexpr std::string_view kNativeGameStartupIntroResourceIdV1 =
    "startup/intro";
inline constexpr std::string_view kNativeGameStartupMediaCompilePassV1 =
    "openrc.rac-startup-media-compile.v1";
inline constexpr std::uint64_t kNativeGameLevelPackageMaxBytesV1 =
    UINT64_C(768) * 1024U * 1024U;
inline constexpr std::uint64_t kNativeGameSharedPackageMaxBytesV1 =
    UINT64_C(320) * 1024U * 1024U;
inline constexpr std::uint64_t kNativeGamePreparedPackageMaxBytesV1 =
    kNativeGameLevelPackageMaxBytesV1 *
        static_cast<std::uint64_t>(kDiscTocLevelCount) +
    kNativeGameSharedPackageMaxBytesV1;

// Shared compiler/reader policy. Keeping this public prevents launchers and
// future tools from silently accepting a broader package language.
[[nodiscard]] constexpr PreparedGameV2FilesystemLimitsV1
make_native_game_prepared_game_limits_v1() {
  return PreparedGameV2FilesystemLimitsV1{
      PreparedGameV2Limits{
          1024U * 1024U, static_cast<std::uint32_t>(kDiscTocLevelCount), 128U,
          1024U, kNativeGamePreparedPackageMaxBytesV1, 64U * 1024U * 1024U},
      LevelPackageV1Limits{
          kNativeGameLevelPackageMaxBytesV1, 64U, 32U, 512U, 1024U,
          UINT64_C(512) * 1024U * 1024U,
          kNativeGameLevelPackageMaxBytesV1 - kLevelPackageHeaderBytesV1, 64U},
      kNativeGamePreparedPackageMaxBytesV1};
}

// Progress is intentionally source- and renderer-neutral so CLI and GUI
// frontends can share the same compiler without parsing human-readable text.
enum class NativeGamePreparationPhaseV1 : std::uint32_t {
  validating_inputs = 0U,
  hashing_inputs = 1U,
  checking_existing_publication = 2U,
  loading_level_assets = 3U,
  compiling_level_foundation = 4U,
  recovering_level_scene = 5U,
  compiling_render_scene = 6U,
  encoding_level_package = 7U,
  verifying_inputs = 8U,
  publishing = 9U,
  publishing_staged = 10U,
  publishing_commit = 11U,
  reusing_existing_publication = 12U,
  compiling_player_actor = 13U,
  compiling_entity_scene = 14U,
  compiling_player_animation = 15U,
  compiling_startup_media = 16U,
};

struct NativeGamePreparationProgressV1 {
  NativeGamePreparationPhaseV1 phase =
      NativeGamePreparationPhaseV1::validating_inputs;
  std::uint32_t level_id = kNativeGamePreparationNoLevelV1;
  std::uint32_t completed_levels = 0U;
  std::uint32_t total_levels = 0U;
};

// Return false to request cooperative cancellation. The callback is invoked
// only at safe stage boundaries and must not throw.
using NativeGamePreparationProgressCallbackV1 = bool (*)(
    const NativeGamePreparationProgressV1 &progress, void *context) noexcept;

struct NativeGamePreparationControlV1 {
  NativeGamePreparationProgressCallbackV1 progress = nullptr;
  void *context = nullptr;
};

struct NativeGamePreparationRequestV1 {
  std::filesystem::path disc_image;
  std::filesystem::path prepared_boot_executable;
  std::filesystem::path destination_root;
  // Frontends that already hashed the source to choose a content-addressed
  // destination pass that identity here. The compiler rejects any source
  // replacement between those operations.
  std::optional<PreparedContentDigestV1> expected_source_image_sha256;
};

struct NativeGamePreparationResultV1 {
  PublishedPreparedGameV2V1 publication;
  PreparedContentDigestV1 source_image_sha256{};
  PreparedContentDigestV1 prepared_boot_executable_sha256{};
  bool already_prepared = false;
};

class NativeGamePreparationErrorV1 : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class NativeGamePreparationCancelledV1 final
    : public NativeGamePreparationErrorV1 {
public:
  using NativeGamePreparationErrorV1::NativeGamePreparationErrorV1;
};

// Verifies that an already loaded publication is the exact current native
// OpenRC profile, not merely a generically mountable PreparedGameV2 tree. The
// check is read-only and covers manifest/compiler identity, the canonical
// 19-level set, the complete eight-resource/provenance contract, a neutral
// runtime mount of every level, and the shared original startup media. Older
// generated data therefore routes back to
// preparation instead of silently starting with missing gameplay features.
void validate_current_native_game_publication_v1(
    const PreparedGameV2RootV1 &prepared);

// Compiles the complete canonical RAC1 PAL v2.00 level set and atomically
// publishes a path-independent PreparedGameV2 tree. All three request paths
// must be absolute. The source files are hashed before and after compilation;
// an existing publication is reused only after full hardened verification.
[[nodiscard]] NativeGamePreparationResultV1
prepare_native_game_v1(const NativeGamePreparationRequestV1 &request,
                       NativeGamePreparationControlV1 control = {});

} // namespace openrc
