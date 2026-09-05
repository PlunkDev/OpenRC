#pragma once

#include "openrc/actor_rig.hpp"
#include "openrc/prepared_game_v2.hpp"
#include "openrc/render_scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openrc {

inline constexpr std::uint32_t kActorLibrarySchemaVersionV1 = 1U;

// One immutable rig addressed both by a stable semantic key and by the digest
// of its canonical joint data. IDs are dense indices local to one library;
// models deliberately reference the semantic key so an overlay can replace a
// visual without rewriting an entity scene.
struct ActorRigAssetV1 {
  std::uint32_t id = 0U;
  std::string semantic_key;
  PreparedContentDigestV1 content_sha256{};
  ActorRigV1 rig;

  [[nodiscard]] bool operator==(const ActorRigAssetV1 &) const;
};

// Positions and normals are in model bind space. A consumer skins them with
// current_global * inverse_bind (and the corresponding normal transform), then
// lets an entity instance supply model-to-world. Normals are finite but are not
// required to be pre-normalized.
struct ActorSkinnedVertexV1 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float nx = 0.0F;
  float ny = 0.0F;
  float nz = 1.0F;
  float u = 0.0F;
  float v = 0.0F;
  std::uint32_t rgba8 = UINT32_C(0xffffffff);
  ActorSkinBindingV1 skin;

  [[nodiscard]] bool operator==(const ActorSkinnedVertexV1 &) const = default;
};

struct ActorSkinnedMeshV1 {
  std::uint32_t id = 0U;
  std::vector<ActorSkinnedVertexV1> vertices;
  std::vector<std::uint32_t> triangle_indices;
  std::vector<RenderSceneDrawRangeV1> draw_ranges;

  [[nodiscard]] bool operator==(const ActorSkinnedMeshV1 &) const = default;
};

// Texture and material semantics intentionally match RenderSceneV1, but all
// IDs are scoped to this model. V1 stores one render-ready high-detail model,
// not source LOD packets or PS2 addresses. The actor codec is otherwise
// independent and carries skin bindings that the static scene cannot represent.
struct ActorModelV1 {
  std::uint32_t id = 0U;
  std::string semantic_key;
  PreparedContentDigestV1 content_sha256{};
  std::string rig_key;
  std::vector<RenderSceneTextureV1> textures;
  std::vector<RenderSceneMaterialV1> materials;
  std::vector<ActorSkinnedMeshV1> meshes;

  [[nodiscard]] bool operator==(const ActorModelV1 &) const = default;
};

struct ActorLibraryV1 {
  std::uint32_t schema_version = kActorLibrarySchemaVersionV1;
  std::vector<ActorRigAssetV1> rigs;
  std::vector<ActorModelV1> models;

  [[nodiscard]] bool operator==(const ActorLibraryV1 &) const = default;
};

// Every count is aggregate across the complete library unless explicitly
// described as per-rig/per-texture. Readers check these bounds before reserve
// or allocation and then require every nested partition to match the header.
struct ActorLibraryLimitsV1 {
  std::uint32_t max_rigs = 0U;
  std::uint32_t max_models = 0U;
  std::uint32_t max_semantic_key_bytes = 0U;
  std::uint64_t max_total_semantic_key_bytes = 0U;
  std::uint32_t max_joints_per_rig = 0U;
  std::uint64_t max_total_joints = 0U;
  std::uint32_t max_textures = 0U;
  std::uint32_t max_mips_per_texture = 0U;
  std::uint32_t max_total_texture_mips = 0U;
  std::uint32_t max_materials = 0U;
  std::uint32_t max_meshes = 0U;
  std::uint64_t max_draw_ranges = 0U;
  std::uint64_t max_vertices = 0U;
  std::uint64_t max_triangle_indices = 0U;
  std::uint32_t max_texture_width = 0U;
  std::uint32_t max_texture_height = 0U;
  std::uint64_t max_texels_per_texture = 0U;
  std::uint64_t max_total_rgba8_bytes = 0U;

  [[nodiscard]] bool operator==(const ActorLibraryLimitsV1 &) const = default;
};

class ActorLibraryError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Validation requires canonical dense table order, canonical positive zero,
// complete triangle draw partitions, valid references, exact skin weights,
// current content digests, and all explicit caller limits.
void validate_actor_library_v1(const ActorLibraryV1 &library,
                               ActorLibraryLimitsV1 limits);

// Sorts every ID-bearing table, canonicalizes finite signed zero and active
// skin influence order, computes zero content digests, and validates the
// complete result. A supplied non-zero digest must already match.
[[nodiscard]] ActorLibraryV1
canonicalize_actor_library_v1(ActorLibraryV1 library,
                              ActorLibraryLimitsV1 limits);

// Combines independently compiled neutral actor libraries into one package
// resource. Top-level IDs are reassigned densely in input order. An identical
// rig addressed by the same semantic key is retained once, while a different
// rig behind an already used key and every repeated model key fail closed.
// Each input and the aggregate result are validated under the caller's same
// explicit limits.
[[nodiscard]] ActorLibraryV1
compose_actor_libraries_v1(std::span<const ActorLibraryV1> libraries,
                           ActorLibraryLimitsV1 limits);

[[nodiscard]] PreparedContentDigestV1
actor_rig_content_sha256_v1(const ActorRigV1 &rig);

// The model digest includes the referenced rig content digest, not its local
// table ID. semantic_key and content_sha256 are excluded from their own
// content preimages.
[[nodiscard]] PreparedContentDigestV1 actor_model_content_sha256_v1(
    const ActorModelV1 &model,
    const PreparedContentDigestV1 &rig_content_sha256);

} // namespace openrc
