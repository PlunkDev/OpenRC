#include "openrc/rac_actor_library_compile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] openrc::ActorAffineTransformV1 identity_transform() {
  openrc::ActorAffineTransformV1 result;
  result.values = {1.0F, 0.0F, 0.0F, 0.0F,
                   0.0F, 1.0F, 0.0F, 0.0F,
                   0.0F, 0.0F, 1.0F, 0.0F};
  return result;
}

[[nodiscard]] openrc::ActorSkinBindingV1 single_skin(
    const std::uint16_t joint) {
  openrc::ActorSkinBindingV1 result;
  result.influence_count = 1U;
  result.joint_indices[0U] = joint;
  result.weight_numerators[0U] = 255U;
  result.weight_sum = 255U;
  return result;
}

[[nodiscard]] openrc::ActorSkinBindingV1 two_skin() {
  openrc::ActorSkinBindingV1 result;
  result.influence_count = 2U;
  result.joint_indices = {0U, 2U, 0U};
  result.weight_numerators = {64U, 192U, 0U};
  result.weight_sum = 256U;
  return result;
}

[[nodiscard]] openrc::ActorSkinBindingV1 three_skin() {
  openrc::ActorSkinBindingV1 result;
  result.influence_count = 3U;
  result.joint_indices = {0U, 1U, 2U};
  result.weight_numerators = {50U, 100U, 106U};
  result.weight_sum = 256U;
  return result;
}

[[nodiscard]] openrc::RacMobyModelVertexV1 make_vertex(
    const std::array<float, 3U> position,
    const std::array<float, 3U> normal,
    const std::array<float, 2U> texture_coordinate) {
  openrc::RacMobyModelVertexV1 result;
  result.diagnostic_position = position;
  result.diagnostic_normal = normal;
  result.texture_coordinate = texture_coordinate;
  return result;
}

[[nodiscard]] openrc::RacLevelMobyTextureV1 make_texture(
    const std::uint32_t global_index, const std::int16_t width,
    const std::int16_t height, const std::uint8_t marker) {
  openrc::RacLevelMobyTextureV1 result;
  result.global_index = global_index;
  result.entry.width = width;
  result.entry.height = height;
  const auto pixels = static_cast<std::size_t>(width) *
                      static_cast<std::size_t>(height);
  result.indices.resize(pixels, std::byte{0U});
  result.rgba.resize(pixels * 4U, static_cast<std::byte>(marker));
  return result;
}

[[nodiscard]] openrc::ActorLibraryLimitsV1 limits() {
  openrc::ActorLibraryLimitsV1 result;
  result.max_rigs = 1U;
  result.max_models = 1U;
  result.max_semantic_key_bytes = 64U;
  result.max_total_semantic_key_bytes = 192U;
  result.max_joints_per_rig = 8U;
  result.max_total_joints = 8U;
  result.max_textures = 8U;
  result.max_mips_per_texture = 1U;
  result.max_total_texture_mips = 8U;
  result.max_materials = 8U;
  result.max_meshes = 1U;
  result.max_draw_ranges = 8U;
  result.max_vertices = 16U;
  result.max_triangle_indices = 64U;
  result.max_texture_width = 8U;
  result.max_texture_height = 8U;
  result.max_texels_per_texture = 64U;
  result.max_total_rgba8_bytes = 1024U;
  return result;
}

[[nodiscard]] openrc::RacActorLibraryCompileRequestV1 make_request() {
  openrc::RacActorLibraryCompileRequestV1 request;
  request.rig_semantic_key = "rigs/player-test";
  request.model_semantic_key = "models/player-test";

  const auto identity = identity_transform();
  request.bind_pose.bind_rig.actor_rig.joints = {
      {-1, identity, identity},
      {0, identity, identity},
      {1, identity, identity},
  };
  request.bind_pose.bind_rig.source_common_translations.resize(3U);
  request.bind_pose.geometry.lod = openrc::RacMobyLodV1::high;
  request.bind_pose.geometry.requires_bind_transforms = true;
  request.bind_pose.geometry.vertices = {
      make_vertex({0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
                  {0.0F, 0.0F}),
      make_vertex({1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
                  {1.0F, 0.0F}),
      make_vertex({0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
                  {0.0F, 1.0F}),
      make_vertex({0.0F, 0.0F, 1.0F}, {-1.0F, 0.0F, 0.0F},
                  {1.0F, 1.0F}),
      make_vertex({9.0F, 9.0F, 9.0F}, {0.0F, -1.0F, 0.0F},
                  {0.5F, 0.5F}),
  };
  request.bind_pose.vertex_skin_bindings = {
      single_skin(0U), single_skin(1U), two_skin(), three_skin(),
      single_skin(2U)};
  request.bind_pose.geometry.triangles = {
      {{2U, 0U, 1U}, 1, 7U},
      {{1U, 3U, 2U}, 1, 7U},
      {{0U, 3U, 1U}, -1, 8U},
      {{3U, 2U, 0U}, 0, 9U},
      {{2U, 3U, 1U}, 1, 10U},
  };

  request.texture_slots.fill(0xffU);
  request.texture_slots[0U] = 1U;
  request.texture_slots[1U] = 2U;
  request.used_texture_slot_count = 2U;
  request.texture_bank.textures = {
      make_texture(0U, 1, 1, 0x10U),
      make_texture(1U, 2, 1, 0x20U),
      make_texture(2U, 1, 2, 0x30U),
  };
  return request;
}

[[nodiscard]] openrc::RacActorLibraryCompileRequestV1
make_request_with_metal_overlay() {
  auto request = make_request();
  openrc::RacMobyMetalBindPoseGeometryV1 metal;
  metal.vertices = {
      make_vertex({0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
                  {0.0F, 0.0F}),
      make_vertex({1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
                  {0.0F, 0.0F}),
      make_vertex({0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
                  {0.0F, 0.0F}),
  };
  metal.vertex_skin_bindings = {single_skin(0U), single_skin(1U),
                                 single_skin(2U)};
  metal.vertex_skin_bindings[0U].weight_numerators[0U] = 1U;
  metal.vertex_skin_bindings[0U].weight_sum = 1U;
  metal.triangles = {{{0U, 1U, 2U}, -2, 11U}};
  request.bind_pose.metal_overlay = std::move(metal);
  request.special_material_policies.push_back({-2, UINT32_C(0xffb0c0d0)});
  return request;
}

template <typename Mutation>
void expect_rejected(Mutation &&mutation, const std::string &message) {
  auto request = make_request();
  auto output_limits = limits();
  std::invoke(std::forward<Mutation>(mutation), request, output_limits);
  try {
    (void)openrc::compile_rac_actor_library_v1(request, output_limits);
  } catch (const openrc::RacActorLibraryCompileError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_deterministic_neutral_compilation() {
  const auto request = make_request();
  const auto first =
      openrc::compile_rac_actor_library_v1(request, limits());
  const auto second =
      openrc::compile_rac_actor_library_v1(request, limits());
  expect(first == second, "RAC1 actor compilation is not deterministic");
  expect(first.schema_version == openrc::kActorLibrarySchemaVersionV1 &&
             first.rigs.size() == 1U && first.models.size() == 1U,
         "the neutral actor library envelope is wrong");

  const auto &rig = first.rigs[0U];
  const auto &model = first.models[0U];
  expect(rig.id == 0U && model.id == 0U &&
             rig.semantic_key == request.rig_semantic_key &&
             model.semantic_key == request.model_semantic_key &&
             model.rig_key == request.rig_semantic_key &&
             !openrc::is_zero_prepared_digest_v1(rig.content_sha256) &&
             !openrc::is_zero_prepared_digest_v1(model.content_sha256),
         "canonical actor identity or content digests are wrong");
  const auto &source_joints = request.bind_pose.bind_rig.actor_rig.joints;
  expect(rig.rig.joints.size() == source_joints.size(),
         "the neutral adapter changed the bind-rig size");
  for (std::size_t index = 0U; index < source_joints.size(); ++index) {
    expect(rig.rig.joints[index].parent_index ==
                   source_joints[index].parent_index &&
               rig.rig.joints[index].local_bind_transform ==
                   source_joints[index].local_bind_transform &&
               rig.rig.joints[index].inverse_bind_transform ==
                   source_joints[index].inverse_bind_transform,
           "the neutral adapter changed a bind-rig joint");
  }
}

void test_compaction_order_normals_and_skin() {
  const auto request = make_request();
  const auto library =
      openrc::compile_rac_actor_library_v1(request, limits());
  const auto &mesh = library.models[0U].meshes[0U];
  expect(mesh.id == 0U && mesh.vertices.size() == 4U &&
             mesh.triangle_indices ==
                 std::vector<std::uint32_t>{0U, 1U, 2U, 2U, 3U, 0U,
                                            1U, 3U, 2U, 3U, 0U, 1U,
                                            0U, 3U, 2U},
         "triangle order or first-reference vertex compaction changed");

  const std::array<std::uint32_t, 4U> source_order{2U, 0U, 1U, 3U};
  for (std::size_t output = 0U; output < source_order.size(); ++output) {
    const auto source = source_order[output];
    const auto &actual = mesh.vertices[output];
    const auto &expected_vertex = request.bind_pose.geometry.vertices[source];
    expect(actual.x == expected_vertex.diagnostic_position[0U] &&
               actual.y == expected_vertex.diagnostic_position[1U] &&
               actual.z == expected_vertex.diagnostic_position[2U] &&
               actual.nx == expected_vertex.diagnostic_normal[0U] &&
               actual.ny == expected_vertex.diagnostic_normal[1U] &&
               actual.nz == expected_vertex.diagnostic_normal[2U] &&
               actual.u == expected_vertex.texture_coordinate[0U] &&
               actual.v == expected_vertex.texture_coordinate[1U] &&
               actual.rgba8 == UINT32_C(0xffffffff) &&
               actual.skin ==
                   request.bind_pose.vertex_skin_bindings[source],
           "a compacted neutral vertex lost geometry, normal, UV, or skin "
           "data");
  }
}

void test_dense_textures_materials_and_draw_partition() {
  const auto library =
      openrc::compile_rac_actor_library_v1(make_request(), limits());
  const auto &model = library.models[0U];
  expect(model.textures.size() == 2U && model.materials.size() == 3U,
         "unused source images or materials entered the neutral model");
  expect(model.textures[0U].id == 0U &&
             model.textures[0U].color_space ==
                 openrc::RenderSceneTextureColorSpaceV1::srgb &&
             model.textures[0U].mips.size() == 1U &&
             model.textures[0U].mips[0U].width == 1U &&
             model.textures[0U].mips[0U].height == 2U &&
             model.textures[0U].mips[0U].rgba8.front() == std::byte{0x30U} &&
             model.textures[1U].id == 1U &&
             model.textures[1U].mips[0U].width == 2U &&
             model.textures[1U].mips[0U].height == 1U &&
             model.textures[1U].mips[0U].rgba8.front() == std::byte{0x20U},
         "referenced RAC1 images were not copied densely in first-use order");

  const auto &textured_first = model.materials[0U];
  const auto &untextured = model.materials[1U];
  const auto &textured_second = model.materials[2U];
  expect(textured_first.base_color_texture_id == 0U &&
             textured_second.base_color_texture_id == 1U &&
             textured_first.base_color_rgba8 == UINT32_C(0xffffffff) &&
             textured_first.use_vertex_color && textured_first.double_sided &&
             textured_first.alpha_mode ==
                 openrc::RenderSceneAlphaModeV1::mask &&
             textured_first.alpha_cutoff_rgba8 == 1U &&
             !untextured.base_color_texture_id && untextured.use_vertex_color &&
             untextured.double_sided &&
             untextured.alpha_mode ==
                 openrc::RenderSceneAlphaModeV1::opaque &&
             untextured.alpha_cutoff_rgba8 == 0U,
         "the neutral actor material policy is wrong");

  const std::vector<openrc::RenderSceneDrawRangeV1> expected{
      {0U, 0U, 6U}, {1U, 6U, 3U}, {2U, 9U, 3U}, {0U, 12U, 3U}};
  expect(model.meshes[0U].draw_ranges == expected,
         "adjacent actor material runs were not partitioned exactly");
}

void test_explicit_metal_overlay_compiles_as_separate_mesh() {
  auto output_limits = limits();
  output_limits.max_meshes = 2U;
  const auto library = openrc::compile_rac_actor_library_v1(
      make_request_with_metal_overlay(), output_limits);
  const auto &model = library.models[0U];
  expect(model.meshes.size() == 2U && model.materials.size() == 4U,
         "the RAC1 metal overlay was not retained as a separate mesh");
  const auto &mesh = model.meshes[1U];
  expect(mesh.id == 1U && mesh.vertices.size() == 3U &&
             mesh.triangle_indices ==
                 std::vector<std::uint32_t>{0U, 1U, 2U} &&
             mesh.draw_ranges ==
                 std::vector<openrc::RenderSceneDrawRangeV1>{
                     {3U, 0U, 3U}},
         "the neutral RAC1 metal mesh topology or draw range is wrong");
  expect(mesh.vertices[0U].skin.influence_count == 1U &&
             mesh.vertices[0U].skin.weight_numerators[0U] == 1U &&
             mesh.vertices[0U].skin.weight_sum == 1U,
         "the exact implicit metal skin weight was changed");
  const auto &material = model.materials[3U];
  expect(!material.base_color_texture_id &&
             material.base_color_rgba8 == UINT32_C(0xffb0c0d0) &&
             !material.use_vertex_color && material.double_sided &&
             material.alpha_mode == openrc::RenderSceneAlphaModeV1::opaque,
         "the explicit neutral metal fallback policy was not preserved");
}

void test_metal_overlay_requires_explicit_bounded_policy() {
  expect_rejected(
      [](auto &request, auto &output_limits) {
        request = make_request_with_metal_overlay();
        request.special_material_policies.clear();
        output_limits.max_meshes = 2U;
      },
      "a RAC1 metal effect was guessed without a neutral fallback policy");
  expect_rejected(
      [](auto &request, auto &output_limits) {
        request = make_request_with_metal_overlay();
        request.special_material_policies.push_back(
            {-2, UINT32_C(0xffffffff)});
        output_limits.max_meshes = 2U;
      },
      "a duplicate RAC1 effect-material policy was accepted");
  expect_rejected(
      [](auto &request, auto &output_limits) {
        request = make_request_with_metal_overlay();
        request.special_material_policies[0U].source_effect_material_index =
            -4;
        output_limits.max_meshes = 2U;
      },
      "an unproven RAC1 effect-material sentinel was accepted");
  expect_rejected(
      [](auto &request, auto &) {
        request = make_request_with_metal_overlay();
      },
      "the aggregate neutral mesh limit ignored a RAC1 metal overlay");
  expect_rejected(
      [](auto &request, auto &output_limits) {
        request = make_request_with_metal_overlay();
        request.bind_pose.metal_overlay->vertex_skin_bindings.pop_back();
        output_limits.max_meshes = 2U;
      },
      "non-parallel RAC1 metal skin data was accepted");
}

void test_invalid_source_domains_are_rejected() {
  expect_rejected(
      [](auto &request, auto &) {
        request.bind_pose.geometry.lod = openrc::RacMobyLodV1::low;
      },
      "low LOD entered the high-only neutral adapter");
  expect_rejected(
      [](auto &request, auto &) {
        request.bind_pose.geometry.requires_bind_transforms = false;
      },
      "static geometry entered the bind-pose actor adapter");
  expect_rejected(
      [](auto &request, auto &) {
        request.bind_pose.vertex_skin_bindings.pop_back();
      },
      "non-parallel actor skin data was accepted");
  expect_rejected(
      [](auto &request, auto &) {
        request.bind_pose.geometry.triangles[0U].vertex_indices[0U] = 99U;
      },
      "a missing source vertex was accepted");
  expect_rejected(
      [](auto &request, auto &) {
        request.bind_pose.geometry.triangles[0U].texture_index = -2;
        request.special_material_policies.push_back(
            {-2, UINT32_C(0xffb0c0d0)});
      },
      "a metal-only effect material was accepted in regular geometry");
  expect_rejected(
      [](auto &request, auto &) {
        request.bind_pose.geometry.triangles[0U].texture_index = 2;
      },
      "a local texture slot outside the used prefix was accepted");
  expect_rejected(
      [](auto &request, auto &) { request.texture_slots[1U] = 0xffU; },
      "an early active-slot sentinel was accepted");
  expect_rejected(
      [](auto &request, auto &) { request.texture_slots[2U] = 0U; },
      "texture data after the used prefix was accepted");
}

void test_invalid_referenced_images_are_rejected() {
  expect_rejected(
      [](auto &request, auto &) { request.texture_slots[1U] = 9U; },
      "a missing global texture was accepted");
  expect_rejected(
      [](auto &request, auto &) {
        request.texture_bank.textures[2U].global_index = 1U;
      },
      "a displaced global texture was accepted");
  expect_rejected(
      [](auto &request, auto &) {
        request.texture_bank.textures[2U].entry.width = 0;
      },
      "a zero-width referenced texture was accepted");
  expect_rejected(
      [](auto &request, auto &) {
        request.texture_bank.textures[2U].rgba.pop_back();
      },
      "an inexact referenced RGBA8 payload was accepted");
  expect_rejected(
      [](auto &request, auto &) {
        request.texture_bank.textures[2U].indices.pop_back();
      },
      "an inexact referenced index payload was accepted");
}

void test_explicit_output_limits_are_enforced() {
  expect_rejected(
      [](auto &, auto &output_limits) { output_limits.max_vertices = 3U; },
      "the compacted vertex limit was ignored");
  expect_rejected(
      [](auto &, auto &output_limits) {
        output_limits.max_triangle_indices = 14U;
      },
      "the triangle-index limit was ignored");
  expect_rejected(
      [](auto &, auto &output_limits) { output_limits.max_draw_ranges = 3U; },
      "the draw-range limit was ignored");
  expect_rejected(
      [](auto &, auto &output_limits) { output_limits.max_textures = 1U; },
      "the texture-count limit was ignored");
  expect_rejected(
      [](auto &, auto &output_limits) { output_limits.max_materials = 2U; },
      "the material-count limit was ignored");
  expect_rejected(
      [](auto &, auto &output_limits) {
        output_limits.max_texture_height = 1U;
      },
      "the texture-dimension limit was ignored");
  expect_rejected(
      [](auto &, auto &output_limits) {
        output_limits.max_total_rgba8_bytes = 12U;
      },
      "the aggregate RGBA8 limit was ignored");
}

} // namespace

int main() {
  try {
    test_deterministic_neutral_compilation();
    test_compaction_order_normals_and_skin();
    test_dense_textures_materials_and_draw_partition();
    test_explicit_metal_overlay_compiles_as_separate_mesh();
    test_metal_overlay_requires_explicit_bounded_policy();
    test_invalid_source_domains_are_rejected();
    test_invalid_referenced_images_are_rejected();
    test_explicit_output_limits_are_enforced();
    std::cout << "OpenRC RAC actor-library compiler tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "OpenRC RAC actor-library compiler tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
