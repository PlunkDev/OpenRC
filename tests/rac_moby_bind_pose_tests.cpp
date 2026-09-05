#include "openrc/rac_moby_bind_pose.hpp"

#include <array>
#include <bit>
#include <cmath>
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

constexpr std::uint32_t kFirstVifOffset = 0x000U;
constexpr std::uint32_t kFirstVifBytes = 0x070U;
constexpr std::uint32_t kFirstVertexOffset = 0x070U;
constexpr std::uint32_t kFirstVertexBytes = 0x0a0U;
constexpr std::uint32_t kSecondVifOffset = 0x110U;
constexpr std::uint32_t kSecondVifBytes = 0x030U;
constexpr std::uint32_t kSecondVertexOffset = 0x140U;
constexpr std::uint32_t kSecondVertexBytes = 0x0a0U;
constexpr std::uint32_t kSkeletonOffset = 0x1e0U;
constexpr std::uint32_t kCommonTranslationOffset = 0x2a0U;
constexpr std::uint32_t kFixtureBytes = 0x2d0U;

constexpr openrc::RacMobyPacketGeometryLimitsV1 kPacketLimits{
    0x10000U, 64U, 64U, 256U, 256U, 64U, 1024U};
constexpr openrc::RacMobyModelGeometryLimitsV1 kGeometryLimits{
    kPacketLimits, 16U, 1024U, 4096U};
constexpr openrc::RacMobyBindRigLimitsV1 kRigLimits{
    0x10000U, 64U, 1.0e-8};
constexpr openrc::RacMobyBindPoseLimitsV1 kLimits{
    kRigLimits, kGeometryLimits, 1024U};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void expect_near(const float actual, const float expected,
                 const std::string &message) {
  if (std::abs(actual - expected) > 1.0e-5F) {
    throw std::runtime_error(message);
  }
}

void write_le16(std::vector<std::byte> &bytes, const std::size_t offset,
                const std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_le_s16(std::vector<std::byte> &bytes, const std::size_t offset,
                  const std::int16_t value) {
  write_le16(bytes, offset, std::bit_cast<std::uint16_t>(value));
}

void write_le32(std::vector<std::byte> &bytes, const std::size_t offset,
                const std::uint32_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
  bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xffU);
  bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xffU);
}

void write_float(std::vector<std::byte> &bytes, const std::size_t offset,
                 const float value) {
  write_le32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] std::uint32_t vif_code(const std::uint8_t opcode,
                                     const std::uint8_t vector_count,
                                     const std::uint16_t destination) {
  return (static_cast<std::uint32_t>(opcode) << 24U) |
         (static_cast<std::uint32_t>(vector_count) << 16U) |
         UINT32_C(0x8000) | destination;
}

void write_vertex_record(std::vector<std::byte> &bytes,
                         const std::size_t offset, const std::int16_t x,
                         const std::int16_t y, const std::int16_t z,
                         const std::array<std::uint8_t, 8U> control,
                         const std::uint8_t azimuth = 0U,
                         const std::uint8_t elevation = 0U) {
  for (std::size_t index = 0U; index < control.size(); ++index) {
    bytes[offset + index] = static_cast<std::byte>(control[index]);
  }
  bytes[offset + 0x08U] = static_cast<std::byte>(azimuth);
  bytes[offset + 0x09U] = static_cast<std::byte>(elevation);
  write_le_s16(bytes, offset + 0x0aU, x);
  write_le_s16(bytes, offset + 0x0cU, y);
  write_le_s16(bytes, offset + 0x0eU, z);
}

[[nodiscard]] std::array<std::uint8_t, 8U>
control(const std::uint16_t high_bits, const std::uint8_t byte2,
        const std::uint8_t byte3, const std::uint8_t byte4,
        const std::uint8_t byte5, const std::uint8_t byte6,
        const std::uint8_t byte7) {
  return {static_cast<std::uint8_t>(high_bits & 0xffU),
          static_cast<std::uint8_t>(high_bits >> 8U), byte2, byte3, byte4,
          byte5, byte6, byte7};
}

void write_first_packet(std::vector<std::byte> &bytes) {
  const auto vif = static_cast<std::size_t>(kFirstVifOffset);
  write_le32(bytes, vif + 0x00U, vif_code(0x75U, 3U, 0x00c2U));
  write_le_s16(bytes, vif + 0x04U, 0);
  write_le_s16(bytes, vif + 0x06U, 0);
  write_le_s16(bytes, vif + 0x08U, 4096);
  write_le_s16(bytes, vif + 0x0aU, 0);
  write_le_s16(bytes, vif + 0x0cU, 0);
  write_le_s16(bytes, vif + 0x0eU, 4096);

  write_le32(bytes, vif + 0x10U, vif_code(0x6eU, 3U, 0x012dU));
  bytes[vif + 0x14U] = std::byte{0xff};
  bytes[vif + 0x15U] = std::byte{3};
  bytes[vif + 0x16U] = std::byte{0x81};
  const std::array<std::uint8_t, 8U> indices{
      0x00U, 0x82U, 0x03U, 0x01U, 0x01U, 0x01U, 0x00U, 0x00U};
  for (std::size_t index = 0U; index < indices.size(); ++index) {
    bytes[vif + 0x18U + index] = static_cast<std::byte>(indices[index]);
  }

  write_le32(bytes, vif + 0x20U, vif_code(0x6cU, 4U, 0x0130U));
  write_le32(bytes, vif + 0x44U, 7U);

  const auto vertex = static_cast<std::size_t>(kFirstVertexOffset);
  write_le32(bytes, vertex + 0x00U, 2U);
  write_le32(bytes, vertex + 0x04U, 1U);
  write_le32(bytes, vertex + 0x08U, 1U);
  write_le32(bytes, vertex + 0x0cU, 1U);
  write_le32(bytes, vertex + 0x14U, 3U);
  write_le32(bytes, vertex + 0x18U, 0x30U);
  write_le32(bytes, vertex + 0x1cU, kFirstVertexBytes);
  bytes[vertex + 0x20U] = std::byte{0};
  bytes[vertex + 0x21U] = std::byte{0};
  bytes[vertex + 0x22U] = std::byte{1};
  bytes[vertex + 0x23U] = std::byte{4};

  write_vertex_record(bytes, vertex + 0x30U, 0, 0, 0,
                      control(2U << 9U, 0U, 4U, 100U, 156U, 8U, 12U));
  write_vertex_record(bytes, vertex + 0x40U, 512, 0, 0,
                      control(4U << 9U, 0U, 4U, 50U, 100U, 106U, 16U),
                      64U, 0U);
  write_vertex_record(bytes, vertex + 0x50U, 0, 512, 0,
                      control(2U << 9U, 12U, 20U, 0U, 0U, 0U, 0U), 0U,
                      64U);
  write_le16(bytes, vertex + 0x94U, 5U);
  write_le16(bytes, vertex + 0x96U, 6U);
  write_le16(bytes, vertex + 0x98U, 7U);
}

void write_second_packet(std::vector<std::byte> &bytes) {
  const auto vif = static_cast<std::size_t>(kSecondVifOffset);
  write_le32(bytes, vif + 0x00U, vif_code(0x75U, 4U, 0x00c2U));
  write_le_s16(bytes, vif + 0x04U, 0);
  write_le_s16(bytes, vif + 0x06U, 0);
  write_le_s16(bytes, vif + 0x08U, 4096);
  write_le_s16(bytes, vif + 0x0aU, 0);
  write_le_s16(bytes, vif + 0x0cU, 0);
  write_le_s16(bytes, vif + 0x0eU, 4096);
  write_le_s16(bytes, vif + 0x10U, 2048);
  write_le_s16(bytes, vif + 0x12U, 1024);

  write_le32(bytes, vif + 0x14U, vif_code(0x6eU, 3U, 0x012dU));
  bytes[vif + 0x18U] = std::byte{0xff};
  const std::array<std::uint8_t, 8U> indices{
      0x81U, 0x82U, 0x04U, 0x01U, 0x01U, 0x01U, 0x00U, 0x00U};
  for (std::size_t index = 0U; index < indices.size(); ++index) {
    bytes[vif + 0x1cU + index] = static_cast<std::byte>(indices[index]);
  }

  const auto vertex = static_cast<std::size_t>(kSecondVertexOffset);
  write_le32(bytes, vertex + 0x0cU, 3U);
  write_le32(bytes, vertex + 0x10U, 1U);
  write_le32(bytes, vertex + 0x14U, 4U);
  write_le32(bytes, vertex + 0x18U, 0x30U);
  write_le32(bytes, vertex + 0x1cU, kSecondVertexBytes);
  write_le16(bytes, vertex + 0x20U, 5U << 7U);
  write_vertex_record(bytes, vertex + 0x30U, 0, 0, 512,
                      control(0U << 9U, 16U, 24U, 0U, 0U, 0U, 0U));
  write_vertex_record(bytes, vertex + 0x40U, 512, 0, 512,
                      control(1U << 9U, 0U, 28U, 0U, 0U, 0U, 0U));
  write_vertex_record(bytes, vertex + 0x50U, 0, 512, 512,
                      control(2U << 9U, 4U, 32U, 0U, 0U, 0U, 0U));
  write_le16(bytes, vertex + 0x94U, 8U);
  write_le16(bytes, vertex + 0x96U, 9U);
  write_le16(bytes, vertex + 0x98U, 10U);
}

void write_bind_joint(std::vector<std::byte> &bytes,
                      const std::uint32_t joint_index,
                      const std::array<float, 9U> inverse_linear,
                      const std::array<float, 3U> inverse_translation,
                      const std::array<float, 3U> common_translation,
                      const std::uint16_t parent_byte_offset) {
  const auto skeleton = static_cast<std::size_t>(kSkeletonOffset) +
                        static_cast<std::size_t>(joint_index) * 0x40U;
  const auto common = static_cast<std::size_t>(kCommonTranslationOffset) +
                      static_cast<std::size_t>(joint_index) * 0x10U;
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      write_float(bytes, skeleton + column * 0x10U + row * 4U,
                  inverse_linear[row * 3U + column]);
    }
  }
  for (std::size_t axis = 0U; axis < 3U; ++axis) {
    write_float(bytes, skeleton + 0x30U + axis * 4U,
                inverse_translation[axis]);
    write_float(bytes, common + axis * 4U, common_translation[axis]);
    write_float(bytes, skeleton + 0x0cU + axis * 0x10U,
                common_translation[axis]);
  }
  write_le16(bytes, common + 0x0cU, parent_byte_offset);
  write_le16(bytes, common + 0x0eU,
             joint_index == 0U ? 0U : UINT16_C(0x7000));
  write_le16(bytes, skeleton + 0x3cU, parent_byte_offset);
  write_le16(bytes, skeleton + 0x3eU,
             joint_index == 0U ? 0U : UINT16_C(0x7000));
}

[[nodiscard]] std::vector<std::byte> make_fixture_bytes() {
  std::vector<std::byte> bytes(kFixtureBytes, std::byte{0});
  write_first_packet(bytes);
  write_second_packet(bytes);
  write_bind_joint(bytes, 0U, {2.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F,
                               0.0F, 0.0F, 1.0F},
                   {0.0F, 0.0F, 0.0F},
                   {0.0F, 0.0F, 0.0F}, 0U);
  write_bind_joint(bytes, 1U, {1.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0F,
                               0.0F, 0.0F, 1.0F},
                   {256.0F, -2048.0F, -1536.0F},
                   {512.0F, 1024.0F, 1536.0F}, 0U);
  write_bind_joint(bytes, 2U, {1.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F,
                               0.0F, 0.0F, 2.0F},
                   {-1152.0F, -1280.0F, -3328.0F},
                   {-256.0F, 512.0F, 128.0F}, 0x40U);
  return bytes;
}

[[nodiscard]] openrc::RacMobyClassV1 make_fixture_class() {
  openrc::RacMobyClassV1 moby;
  moby.input_bytes = kFixtureBytes;
  moby.high_lod_packet_count = 2U;
  moby.joint_count = 3U;
  moby.scale = 2.0F;
  moby.skeleton_offset = kSkeletonOffset;
  moby.common_translation_offset = kCommonTranslationOffset;
  moby.skeleton_range = {kSkeletonOffset, 3U * 0x40U};
  moby.common_translation_range = {kCommonTranslationOffset, 3U * 0x10U};
  moby.packets = {
      {openrc::RacMobyPacketKindV1::high_lod,
       {},
       {kFirstVifOffset, kFirstVifBytes},
       {kFirstVertexOffset, kFirstVertexBytes},
       4U,
       10U,
       3U},
      {openrc::RacMobyPacketKindV1::high_lod,
       {},
       {kSecondVifOffset, kSecondVifBytes},
       {kSecondVertexOffset, kSecondVertexBytes},
       0U,
       10U,
       4U}};
  return moby;
}

template <typename Mutation>
void expect_rejected(Mutation &&mutation, const std::string &message) {
  auto bytes = make_fixture_bytes();
  auto moby = make_fixture_class();
  auto limits = kLimits;
  std::invoke(std::forward<Mutation>(mutation), bytes, moby, limits);
  try {
    (void)openrc::compile_rac_moby_bind_pose_geometry_v1(
        bytes, moby, openrc::RacMobyLodV1::high, limits);
  } catch (const openrc::RacMobyBindPoseError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_general_affine_bind_rig() {
  const auto rig = openrc::decode_rac_moby_bind_rig_v1(
      make_fixture_bytes(), make_fixture_class(), kRigLimits);
  expect(rig.actor_rig.joints.size() == 3U &&
             rig.source_common_translations.size() == 3U &&
             rig.actor_rig.joints[0U].parent_index == -1 &&
             rig.actor_rig.joints[1U].parent_index == 0 &&
             rig.actor_rig.joints[2U].parent_index == 1,
         "the neutral bind hierarchy is wrong");
  expect_near(rig.actor_rig.joints[0U].inverse_bind_transform.at(0U, 0U), 2.0F,
              "the non-orthogonal inverse-bind linear data was changed");
  expect_near(rig.actor_rig.joints[0U].inverse_bind_transform.at(0U, 1U), 1.0F,
              "source Vec4 columns were decoded with the wrong orientation");
  expect_near(rig.actor_rig.joints[0U].local_bind_transform.at(0U, 0U), 0.5F,
              "the general affine root was not inverted");
  expect_near(rig.actor_rig.joints[0U].local_bind_transform.at(0U, 1U), -0.5F,
              "the root shear was not preserved during affine inversion");
  expect_near(rig.actor_rig.joints[1U].local_bind_transform.at(0U, 0U), 2.0F,
              "the child local-bind linear transform is wrong");
  expect_near(rig.actor_rig.joints[1U].local_bind_transform.at(0U, 1U), 0.5F,
              "the child local-bind composition lost its shear");
  expect_near(rig.actor_rig.joints[1U].local_bind_transform.at(1U, 1U), 0.5F,
              "the child local-bind composition lost its scale");
  expect_near(rig.actor_rig.joints[1U].local_bind_transform.at(0U, 3U), 1.0F,
              "joint 1 local X is wrong");
  expect_near(rig.actor_rig.joints[1U].local_bind_transform.at(1U, 3U), 2.0F,
              "joint 1 local Y is wrong");
  expect_near(rig.actor_rig.joints[1U].local_bind_transform.at(2U, 3U), 3.0F,
              "joint 1 local Z is wrong");
  expect_near(rig.actor_rig.joints[2U].local_bind_transform.at(0U, 3U), -0.5F,
              "joint 2 local X is wrong");
  expect_near(rig.actor_rig.joints[2U].local_bind_transform.at(1U, 3U), 1.0F,
              "joint 2 local Y is wrong");
  expect_near(rig.actor_rig.joints[2U].local_bind_transform.at(2U, 3U), 0.25F,
              "joint 2 local Z is wrong");
  expect_near(rig.actor_rig.joints[2U].local_bind_transform.at(0U, 2U), -0.5F,
              "joint 2 local-bind composition lost its off-diagonal term");
  expect_near(rig.actor_rig.joints[2U].local_bind_transform.at(1U, 1U), 2.0F,
              "joint 2 local-bind composition lost its scale");
  expect_near(rig.source_common_translations[1U][0U], 512.0F,
              "the RAC common-translation signal was not preserved");
}

void expect_binding(const openrc::ActorSkinBindingV1 &binding,
                    const std::uint8_t count,
                    const std::array<std::uint16_t, 3U> joints,
                    const std::array<std::uint16_t, 3U> weights,
                    const std::uint16_t sum, const std::string &message) {
  expect(binding.influence_count == count &&
             binding.joint_indices == joints &&
             binding.weight_numerators == weights &&
             binding.weight_sum == sum,
         message);
}

void test_skin_state_and_duplicate_propagation() {
  const auto result = openrc::compile_rac_moby_bind_pose_geometry_v1(
      make_fixture_bytes(), make_fixture_class(),
      openrc::RacMobyLodV1::high, kLimits);
  expect(result.bind_rig.actor_rig.joints.size() == 3U &&
             result.geometry.vertices.size() == 7U &&
             result.vertex_skin_bindings.size() == 7U,
         "the bind-pose output domains are wrong");
  expect_binding(result.vertex_skin_bindings[0U], 2U, {0U, 1U, 0U},
                 {100U, 156U, 0U}, 256U,
                 "the two-way skin binding is wrong");
  expect_binding(result.vertex_skin_bindings[1U], 3U, {0U, 1U, 2U},
                 {50U, 100U, 106U}, 256U,
                 "the three-way skin binding is wrong");
  expect_binding(result.vertex_skin_bindings[2U], 2U, {0U, 1U, 0U},
                 {100U, 156U, 0U}, 256U,
                 "a regular vertex did not load a blended VU0 binding");
  expect_binding(result.vertex_skin_bindings[3U], 3U, {0U, 1U, 2U},
                 {50U, 100U, 106U}, 256U,
                 "VU0 skin state was not retained between packets");
  expect_binding(result.vertex_skin_bindings[4U], 1U, {0U, 0U, 0U},
                 {255U, 0U, 0U}, 255U,
                 "the exact 255 single-joint weight was lost");
  expect(result.geometry.vertices[6U].duplicate &&
             result.geometry.vertices[6U].source_class_packet_index == 0U &&
             result.geometry.vertices[6U].source_vertex_index == 0U &&
             result.vertex_skin_bindings[6U] ==
                 result.vertex_skin_bindings[0U],
         "an inherited duplicate lost its source skin binding");
  expect_near(result.vertex_skin_bindings[0U].normalized_weight(0U),
              100.0F / 256.0F,
              "the neutral normalized-weight derivation is wrong");
}

void test_public_domains_reject_invalid_values() {
  openrc::ActorAffineTransformV1 transform;
  try {
    (void)transform.at(openrc::kActorAffineRowsV1, 0U);
    throw std::runtime_error("ActorAffineTransformV1::at accepted an invalid row");
  } catch (const std::out_of_range &) {
  }

  try {
    (void)openrc::compile_rac_moby_bind_pose_geometry_v1(
        make_fixture_bytes(), make_fixture_class(),
        static_cast<openrc::RacMobyLodV1>(0xffU), kLimits);
    throw std::runtime_error("the bind-pose compiler accepted an invalid LOD");
  } catch (const openrc::RacMobyBindPoseError &) {
  }
}

void test_hard_validation() {
  expect_rejected(
      [](auto &, auto &, auto &limits) { limits = {}; },
      "zero bind-pose limits were accepted");
  expect_rejected(
      [](auto &, auto &, auto &limits) {
        limits.rig_limits.max_joints = 2U;
      },
      "the bind-rig joint limit was ignored");
  expect_rejected(
      [](auto &, auto &, auto &limits) {
        limits.max_output_skin_bindings = 6U;
      },
      "the output skin-binding limit was ignored");
  expect_rejected(
      [](auto &bytes, auto &, auto &) {
        write_float(bytes, kSkeletonOffset + 0x00U,
                    std::numeric_limits<float>::quiet_NaN());
      },
      "a non-finite inverse-bind component was accepted");
  expect_rejected(
      [](auto &bytes, auto &, auto &) {
        write_float(bytes, kSkeletonOffset + 0x00U, 0.0F);
      },
      "a singular inverse-bind transform was accepted");
  expect_rejected(
      [](auto &bytes, auto &, auto &) {
        write_le16(bytes, kCommonTranslationOffset + 0x10U + 0x0cU, 1U);
      },
      "an unaligned parent offset was accepted");
  expect_rejected(
      [](auto &bytes, auto &, auto &) {
        write_le16(bytes, kCommonTranslationOffset + 0x10U + 0x0cU, 0x40U);
      },
      "a forward/self parent edge was accepted");
  expect_rejected(
      [](auto &bytes, auto &, auto &) {
        write_float(bytes, kCommonTranslationOffset + 0x10U,
                    std::numeric_limits<float>::quiet_NaN());
      },
      "a non-finite common-translation component was accepted");
  expect_rejected(
      [](auto &bytes, auto &, auto &) {
        bytes[kFirstVertexOffset + 0x30U + 0x02U] = std::byte{36};
      },
      "an uninitialized VU0 matrix load was accepted");
  expect_rejected(
      [](auto &bytes, auto &, auto &) {
        bytes[kFirstVertexOffset + 0x30U + 0x06U] = std::byte{0};
      },
      "a same-iteration VU0 load/store collision was accepted");
  expect_rejected(
      [](auto &bytes, auto &, auto &) {
        write_le16(bytes, kFirstVertexOffset + 0x30U, 3U << 9U);
      },
      "an out-of-range uploaded joint was accepted");
  expect_rejected(
      [](auto &bytes, auto &, auto &) {
        bytes[kFirstVertexOffset + 0x30U + 0x04U] = std::byte{99};
      },
      "a single-joint-only 255 total was accepted for a blend");
  expect_rejected(
      [](auto &bytes, auto &, auto &) {
        bytes[kFirstVertexOffset + 0x30U + 0x02U] = std::byte{1};
      },
      "an unaligned VU0 address was accepted");
}

} // namespace

int main() {
  try {
    test_general_affine_bind_rig();
    test_skin_state_and_duplicate_propagation();
    test_public_domains_reject_invalid_values();
    test_hard_validation();
    std::cout << "rac_moby_bind_pose_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rac_moby_bind_pose_tests: " << error.what() << '\n';
    return 1;
  }
}
