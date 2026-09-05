#pragma once

#include "openrc/render_scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kRenderSceneIoFormatVersionV1 = 1U;
inline constexpr std::uint32_t kRenderSceneIoHeaderBytesV1 = 0x100U;
inline constexpr std::uint32_t kRenderSceneIoTextureBytesV1 = 32U;
inline constexpr std::uint32_t kRenderSceneIoTextureMipBytesV1 = 32U;
inline constexpr std::uint32_t kRenderSceneIoMaterialBytesV1 = 32U;
inline constexpr std::uint32_t kRenderSceneIoMeshBytesV1 = 48U;
inline constexpr std::uint32_t kRenderSceneIoDrawRangeBytesV1 = 24U;
inline constexpr std::uint32_t kRenderSceneIoVertexBytesV1 = 24U;
inline constexpr std::uint32_t kRenderSceneIoInstanceBytesV1 = 64U;
inline constexpr std::uint32_t kRenderSceneIoIndexBytesV1 = 4U;

inline constexpr std::string_view kRenderSceneResourceIdV1 =
    "world/render-scene";
inline constexpr std::string_view kRenderSceneResourceTypeIdV1 =
    "openrc.render-scene";
inline constexpr std::uint32_t kRenderSceneResourceSchemaVersionV1 = 1U;

struct RenderSceneIoLimitsV1 {
  std::uint64_t max_encoded_bytes = 0U;
  RenderSceneLimitsV1 scene;

  [[nodiscard]] bool operator==(const RenderSceneIoLimitsV1 &) const = default;
};

class RenderSceneIoError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Encoding canonicalizes table order and signed zero. Every table is written
// field-by-field in little-endian form; native C++ layouts are never copied.
[[nodiscard]] std::vector<std::byte>
encode_render_scene_v1(const RenderSceneV1 &scene,
                       RenderSceneIoLimitsV1 limits);

// Decoding accepts only the canonical table layout, record sizes, IDs/order,
// float encodings, exact mip/draw coverage, reserved zeros, and no trailing
// bytes. Limits are checked before allocation.
[[nodiscard]] RenderSceneV1
decode_render_scene_v1(std::span<const std::byte> bytes,
                       RenderSceneIoLimitsV1 limits);

} // namespace openrc
