#include "openrc/rac_moby_bind_pose.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace openrc {
namespace {

constexpr std::uint64_t kRacSkeletonJointBytes = 0x40U;
constexpr std::uint64_t kRacCommonTranslationBytes = 0x10U;
constexpr std::uint16_t kRacSingleJointWeight = 255U;
constexpr std::uint16_t kRacMetalSingleJointWeight = 1U;
constexpr std::uint16_t kRacBlendedWeightTotal = 256U;
constexpr std::size_t kRacVu0SkinSlots = 64U;

struct AffineDouble {
  std::array<double, 12U> values{};
};

struct PacketSkinBindings {
  std::uint32_t class_packet_index = 0U;
  std::vector<ActorSkinBindingV1> bindings;
};

[[noreturn]] void fail(const std::string &message) {
  throw RacMobyBindPoseError(message);
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t read_le16(const std::span<const std::byte> bytes,
                                      const std::uint64_t offset) noexcept {
  const auto host = static_cast<std::size_t>(offset);
  return static_cast<std::uint16_t>(byte_value(bytes[host])) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(byte_value(bytes[host + 1U])) << 8U);
}

[[nodiscard]] std::uint32_t read_le32(const std::span<const std::byte> bytes,
                                      const std::uint64_t offset) noexcept {
  const auto host = static_cast<std::size_t>(offset);
  return static_cast<std::uint32_t>(byte_value(bytes[host])) |
         (static_cast<std::uint32_t>(byte_value(bytes[host + 1U])) << 8U) |
         (static_cast<std::uint32_t>(byte_value(bytes[host + 2U])) << 16U) |
         (static_cast<std::uint32_t>(byte_value(bytes[host + 3U])) << 24U);
}

[[nodiscard]] float read_float(const std::span<const std::byte> bytes,
                               const std::uint64_t offset) noexcept {
  return std::bit_cast<float>(read_le32(bytes, offset));
}

void require_range(const std::uint64_t offset, const std::uint64_t size,
                   const std::size_t input_size,
                   const char *const description) {
  const auto available = static_cast<std::uint64_t>(input_size);
  if (offset > available || size > available - offset) {
    fail(std::string(description) + " exceeds the RAC1 MobyClass input");
  }
}

void require_limit(const std::uint64_t value, const std::uint64_t limit,
                   const char *const description) {
  if (value > limit) {
    fail(std::string(description) + " exceeds the caller's limit");
  }
}

void require_append_capacity(const std::uint64_t current,
                             const std::uint64_t additional,
                             const std::uint64_t limit,
                             const std::size_t host_limit,
                             const char *const description) {
  if (current > limit || additional > limit - current ||
      current > static_cast<std::uint64_t>(host_limit) ||
      additional > static_cast<std::uint64_t>(host_limit) - current ||
      current > UINT32_MAX || additional > UINT32_MAX - current) {
    fail(std::string(description) + " exceeds its bounded output capacity");
  }
}

[[nodiscard]] std::array<float, 3U>
decode_packed_normal(const std::uint8_t azimuth_byte,
                     const std::uint8_t elevation_byte) {
  const auto azimuth = static_cast<float>(azimuth_byte) *
                       (std::numbers::pi_v<float> / 128.0F);
  const auto elevation = static_cast<float>(elevation_byte) *
                         (std::numbers::pi_v<float> / 128.0F);
  const auto cos_elevation = std::cos(elevation);
  return {std::sin(azimuth) * cos_elevation,
          std::cos(azimuth) * cos_elevation, std::sin(elevation)};
}

[[nodiscard]] double determinant(const AffineDouble &value) noexcept {
  const auto &m = value.values;
  return m[0U] * (m[5U] * m[10U] - m[6U] * m[9U]) -
         m[1U] * (m[4U] * m[10U] - m[6U] * m[8U]) +
         m[2U] * (m[4U] * m[9U] - m[5U] * m[8U]);
}

[[nodiscard]] AffineDouble inverse_affine(
    const AffineDouble &input, const double minimum_absolute_determinant,
    const std::uint32_t joint_index) {
  const auto &m = input.values;
  const auto det = determinant(input);
  if (!std::isfinite(det) || std::abs(det) < minimum_absolute_determinant) {
    fail("RAC1 Moby joint " + std::to_string(joint_index) +
         " has a singular or ill-conditioned inverse-bind linear transform");
  }

  AffineDouble result;
  auto &out = result.values;
  out[0U] = (m[5U] * m[10U] - m[6U] * m[9U]) / det;
  out[1U] = (m[2U] * m[9U] - m[1U] * m[10U]) / det;
  out[2U] = (m[1U] * m[6U] - m[2U] * m[5U]) / det;
  out[4U] = (m[6U] * m[8U] - m[4U] * m[10U]) / det;
  out[5U] = (m[0U] * m[10U] - m[2U] * m[8U]) / det;
  out[6U] = (m[2U] * m[4U] - m[0U] * m[6U]) / det;
  out[8U] = (m[4U] * m[9U] - m[5U] * m[8U]) / det;
  out[9U] = (m[1U] * m[8U] - m[0U] * m[9U]) / det;
  out[10U] = (m[0U] * m[5U] - m[1U] * m[4U]) / det;
  for (std::size_t row = 0U; row < 3U; ++row) {
    out[row * 4U + 3U] =
        -(out[row * 4U + 0U] * m[3U] +
          out[row * 4U + 1U] * m[7U] +
          out[row * 4U + 2U] * m[11U]);
  }
  for (const auto component : out) {
    if (!std::isfinite(component)) {
      fail("RAC1 Moby joint " + std::to_string(joint_index) +
           " produces a non-finite bind transform");
    }
  }
  return result;
}

[[nodiscard]] AffineDouble compose_affine(const AffineDouble &left,
                                          const AffineDouble &right) {
  AffineDouble result;
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t column = 0U; column < 3U; ++column) {
      double value = 0.0;
      for (std::size_t inner = 0U; inner < 3U; ++inner) {
        value += left.values[row * 4U + inner] *
                 right.values[inner * 4U + column];
      }
      result.values[row * 4U + column] = value;
    }
    result.values[row * 4U + 3U] =
        left.values[row * 4U + 3U] +
        left.values[row * 4U + 0U] * right.values[3U] +
        left.values[row * 4U + 1U] * right.values[7U] +
        left.values[row * 4U + 2U] * right.values[11U];
  }
  return result;
}

[[nodiscard]] ActorAffineTransformV1
to_actor_affine(const AffineDouble &input, const std::uint32_t joint_index) {
  ActorAffineTransformV1 result;
  constexpr auto maximum =
      static_cast<double>(std::numeric_limits<float>::max());
  for (std::size_t index = 0U; index < result.values.size(); ++index) {
    const auto component = input.values[index];
    if (!std::isfinite(component) || std::abs(component) > maximum) {
      fail("RAC1 Moby joint " + std::to_string(joint_index) +
           " cannot be represented by the neutral affine contract");
    }
    result.values[index] = static_cast<float>(component);
  }
  return result;
}

[[nodiscard]] std::size_t vu0_slot(const std::uint8_t address,
                                   const char *const description) {
  if ((address & 0x03U) != 0U) {
    fail(std::string(description) + " uses an unaligned VU0 byte address");
  }
  const auto slot = static_cast<std::size_t>(address / 4U);
  if (slot >= kRacVu0SkinSlots) {
    fail(std::string(description) + " exceeds the bounded VU0 skin state");
  }
  return slot;
}

[[nodiscard]] ActorSkinBindingV1
single_joint_binding(const std::uint16_t joint_index,
                     const std::uint32_t joint_count,
                     const char *const description) {
  if (joint_index >= joint_count) {
    fail(std::string(description) + " references a joint outside the bind rig");
  }
  ActorSkinBindingV1 result;
  result.influence_count = 1U;
  result.joint_indices[0U] = joint_index;
  result.weight_numerators[0U] = kRacSingleJointWeight;
  result.weight_sum = kRacSingleJointWeight;
  return result;
}

[[nodiscard]] ActorSkinBindingV1 metal_skin_binding(
    const RacMobyMetalPacketVertexV1 &vertex,
    const std::uint32_t joint_count) {
  if (vertex.influence_count == 0U ||
      vertex.influence_count > kActorMaximumSkinInfluencesV1) {
    fail("A jointed RAC1 Moby metal vertex has an invalid influence count");
  }
  ActorSkinBindingV1 result;
  result.influence_count = vertex.influence_count;
  for (std::size_t index = 0U; index < vertex.influence_count; ++index) {
    if (vertex.joint_indices[index] >= joint_count) {
      fail("A RAC1 Moby metal vertex references a joint outside the bind rig");
    }
    for (std::size_t earlier = 0U; earlier < index; ++earlier) {
      if (vertex.joint_indices[earlier] == vertex.joint_indices[index]) {
        fail("A RAC1 Moby metal vertex repeats an active joint");
      }
    }
    result.joint_indices[index] = vertex.joint_indices[index];
    result.weight_numerators[index] =
        vertex.influence_count == 1U ? kRacMetalSingleJointWeight
                                     : vertex.weight_numerators[index];
    result.weight_sum = static_cast<std::uint16_t>(
        result.weight_sum + result.weight_numerators[index]);
  }
  const auto expected_sum = vertex.influence_count == 1U
                                ? kRacMetalSingleJointWeight
                                : kRacBlendedWeightTotal;
  if (result.weight_sum != expected_sum) {
    fail("A RAC1 Moby metal vertex has an invalid exact weight sum");
  }
  return result;
}

[[nodiscard]] ActorSkinBindingV1 blended_binding(
    const std::array<ActorSkinBindingV1, 3U> &sources,
    const std::array<std::uint8_t, 3U> &weights,
    const std::uint8_t influence_count, const char *const description) {
  ActorSkinBindingV1 result;
  result.influence_count = influence_count;
  std::uint32_t sum = 0U;
  for (std::size_t index = 0U; index < influence_count; ++index) {
    if (sources[index].influence_count != 1U ||
        sources[index].weight_sum != kRacSingleJointWeight) {
      fail(std::string(description) +
           " loads an already blended or invalid VU0 matrix");
    }
    result.joint_indices[index] = sources[index].joint_indices[0U];
    result.weight_numerators[index] = weights[index];
    sum += weights[index];
  }
  if (sum != kRacBlendedWeightTotal) {
    fail(std::string(description) +
         " has an unverified integer skin-weight total");
  }
  result.weight_sum = static_cast<std::uint16_t>(sum);
  return result;
}

[[nodiscard]] ActorSkinBindingV1 load_skin_binding(
    const std::array<std::optional<ActorSkinBindingV1>, kRacVu0SkinSlots>
        &state,
    const std::uint8_t address, const char *const description) {
  const auto slot = vu0_slot(address, description);
  if (!state[slot]) {
    fail(std::string(description) + " reads uninitialized VU0 skin state");
  }
  return *state[slot];
}

void store_skin_binding(
    std::array<std::optional<ActorSkinBindingV1>, kRacVu0SkinSlots> &state,
    const std::uint8_t address, const ActorSkinBindingV1 &binding,
    const char *const description) {
  state[vu0_slot(address, description)] = binding;
}

[[nodiscard]] std::uint16_t
control_joint_index(const RacMobyPacketLocalVertexV1 &vertex) noexcept {
  const auto low =
      static_cast<std::uint16_t>(vertex.control_bytes[0U]) |
      static_cast<std::uint16_t>(
          static_cast<std::uint16_t>(vertex.control_bytes[1U]) << 8U);
  return static_cast<std::uint16_t>((low >> 9U) & 0x7fU);
}

[[nodiscard]] std::vector<ActorSkinBindingV1> decode_packet_skin_bindings(
    const RacMobyPacketGeometryV1 &packet, const std::uint32_t joint_count,
    std::array<std::optional<ActorSkinBindingV1>, kRacVu0SkinSlots> &state) {
  for (const auto &transfer : packet.matrix_transfers) {
    const auto binding = single_joint_binding(
        transfer.scratchpad_joint_index, joint_count,
        "A RAC1 Moby pre-loop matrix transfer");
    store_skin_binding(state, transfer.vu0_destination, binding,
                       "A RAC1 Moby pre-loop matrix transfer");
  }

  const auto two_way_count = packet.vertex_header.two_way_blend_vertex_count;
  const auto three_way_count =
      packet.vertex_header.three_way_blend_vertex_count;
  if (static_cast<std::uint64_t>(two_way_count) + three_way_count >
      packet.vertices.size()) {
    fail("A RAC1 Moby packet has invalid skinning vertex partitions");
  }

  std::vector<ActorSkinBindingV1> result;
  if (packet.vertices.size() > result.max_size()) {
    fail("A RAC1 Moby packet's skin bindings exceed the host container");
  }
  result.reserve(packet.vertices.size());
  for (std::size_t index = 0U; index < packet.vertices.size(); ++index) {
    const auto &vertex = packet.vertices[index];
    const auto joint_index = control_joint_index(vertex);
    const auto &control = vertex.control_bytes;

    if (index < two_way_count) {
      const auto uploaded = single_joint_binding(
          joint_index, joint_count, "A RAC1 Moby two-way skin operation");
      store_skin_binding(state, control[6U], uploaded,
                         "A RAC1 Moby two-way matrix upload");
      if (control[2U] == control[6U] || control[3U] == control[6U]) {
        fail("A RAC1 Moby two-way skin operation loads and overwrites the "
             "same VU0 slot");
      }
      const std::array<ActorSkinBindingV1, 3U> sources{
          load_skin_binding(state, control[2U],
                            "A RAC1 Moby two-way skin operation"),
          load_skin_binding(state, control[3U],
                            "A RAC1 Moby two-way skin operation"),
          {}};
      const auto binding = blended_binding(
          sources, {control[4U], control[5U], 0U}, 2U,
          "A RAC1 Moby two-way skin operation");
      store_skin_binding(state, control[7U], binding,
                         "A RAC1 Moby two-way blend result");
      result.push_back(binding);
      continue;
    }

    if (index < static_cast<std::uint64_t>(two_way_count) +
                    three_way_count) {
      const auto third_address = static_cast<std::uint16_t>(joint_index * 2U);
      if (third_address > std::numeric_limits<std::uint8_t>::max()) {
        fail("A RAC1 Moby three-way skin operation overflows its VU0 address");
      }
      const std::array<ActorSkinBindingV1, 3U> sources{
          load_skin_binding(state, control[2U],
                            "A RAC1 Moby three-way skin operation"),
          load_skin_binding(state, control[3U],
                            "A RAC1 Moby three-way skin operation"),
          load_skin_binding(state, static_cast<std::uint8_t>(third_address),
                            "A RAC1 Moby three-way skin operation")};
      const auto binding = blended_binding(
          sources, {control[4U], control[5U], control[6U]}, 3U,
          "A RAC1 Moby three-way skin operation");
      store_skin_binding(state, control[7U], binding,
                         "A RAC1 Moby three-way blend result");
      result.push_back(binding);
      continue;
    }

    const auto uploaded = single_joint_binding(
        joint_index, joint_count, "A RAC1 Moby regular skin operation");
    store_skin_binding(state, control[3U], uploaded,
                       "A RAC1 Moby regular matrix upload");
    if (control[2U] == control[3U]) {
      fail("A RAC1 Moby regular skin operation loads and overwrites the same "
           "VU0 slot");
    }
    result.push_back(load_skin_binding(
        state, control[2U], "A RAC1 Moby regular skin operation"));
  }
  return result;
}

[[nodiscard]] RacMobyPacketKindV1
packet_kind_for_lod(const RacMobyLodV1 lod) {
  switch (lod) {
  case RacMobyLodV1::high:
    return RacMobyPacketKindV1::high_lod;
  case RacMobyLodV1::low:
    return RacMobyPacketKindV1::low_lod;
  }
  fail("RacMobyBindPoseGeometryV1 received an invalid LOD");
}

[[nodiscard]] const PacketSkinBindings &find_packet_bindings(
    const std::vector<PacketSkinBindings> &packets,
    const std::uint32_t class_packet_index) {
  const auto match = std::lower_bound(
      packets.begin(), packets.end(), class_packet_index,
      [](const PacketSkinBindings &candidate, const std::uint32_t index) {
        return candidate.class_packet_index < index;
      });
  if (match == packets.end() ||
      match->class_packet_index != class_packet_index) {
    fail("Assembled RAC1 Moby provenance references an absent skin packet");
  }
  return *match;
}

} // namespace

RacMobyBindRigV1 decode_rac_moby_bind_rig_v1(
    const std::span<const std::byte> class_bytes, const RacMobyClassV1 &moby,
    const RacMobyBindRigLimitsV1 limits) {
  if (limits.max_input_bytes == 0U || limits.max_joints == 0U ||
      !std::isfinite(limits.minimum_absolute_linear_determinant) ||
      limits.minimum_absolute_linear_determinant <= 0.0) {
    fail("RacMobyBindRigV1 caller limits must be finite and positive");
  }
  if (class_bytes.size() != moby.input_bytes) {
    fail("RacMobyBindRigV1 input size disagrees with RacMobyClassV1");
  }
  require_limit(class_bytes.size(), limits.max_input_bytes,
                "The RAC1 Moby bind-rig input size");
  if (!std::isfinite(moby.scale) || moby.scale <= 0.0F) {
    fail("RacMobyBindRigV1 requires a finite positive class scale");
  }
  if (moby.joint_count == 0U) {
    fail("RacMobyBindRigV1 requires a jointed RAC1 Moby class");
  }
  require_limit(moby.joint_count, limits.max_joints,
                "The RAC1 Moby bind-rig joint count");

  const auto skeleton_bytes =
      static_cast<std::uint64_t>(moby.joint_count) * kRacSkeletonJointBytes;
  const auto common_bytes = static_cast<std::uint64_t>(moby.joint_count) *
                            kRacCommonTranslationBytes;
  if (moby.skeleton_range.size != skeleton_bytes ||
      moby.common_translation_range.size != common_bytes ||
      moby.skeleton_range.offset != moby.skeleton_offset ||
      moby.common_translation_range.offset != moby.common_translation_offset ||
      (moby.skeleton_range.offset & 0x0fU) != 0U ||
      (moby.common_translation_range.offset & 0x0fU) != 0U) {
    fail("RacMobyBindRigV1 requires complete aligned skeleton and common "
         "translation ranges");
  }
  require_range(moby.skeleton_range.offset, moby.skeleton_range.size,
                class_bytes.size(), "The RAC1 Moby skeleton");
  require_range(moby.common_translation_range.offset,
                moby.common_translation_range.size, class_bytes.size(),
                "The RAC1 Moby common translations");
  const auto skeleton_end = moby.skeleton_range.offset +
                            moby.skeleton_range.size;
  const auto common_end = moby.common_translation_range.offset +
                          moby.common_translation_range.size;
  if (moby.skeleton_range.offset < common_end &&
      moby.common_translation_range.offset < skeleton_end) {
    fail("The RAC1 Moby skeleton and common translations overlap");
  }

  RacMobyBindRigV1 result;
  result.actor_rig.joints.reserve(moby.joint_count);
  result.source_common_translations.reserve(moby.joint_count);
  std::vector<AffineDouble> inverse_bind_transforms;
  inverse_bind_transforms.reserve(moby.joint_count);

  const auto translation_scale = static_cast<double>(moby.scale) / 1024.0;
  for (std::uint32_t joint_index = 0U; joint_index < moby.joint_count;
       ++joint_index) {
    const auto skeleton_offset = moby.skeleton_range.offset +
                                 joint_index * kRacSkeletonJointBytes;
    const auto common_offset = moby.common_translation_range.offset +
                               joint_index * kRacCommonTranslationBytes;

    AffineDouble inverse_bind;
    for (std::size_t row = 0U; row < 3U; ++row) {
      for (std::size_t column = 0U; column < 3U; ++column) {
        inverse_bind.values[row * 4U + column] = read_float(
            class_bytes, skeleton_offset + column * 0x10U + row * 4U);
      }
      inverse_bind.values[row * 4U + 3U] =
          static_cast<double>(read_float(
              class_bytes, skeleton_offset + 0x30U + row * 4U)) *
          translation_scale;
    }
    for (const auto component : inverse_bind.values) {
      if (!std::isfinite(component)) {
        fail("RAC1 Moby joint " + std::to_string(joint_index) +
             " contains a non-finite inverse-bind transform");
      }
    }

    std::array<float, 3U> source_common_translation{};
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
      source_common_translation[axis] =
          read_float(class_bytes, common_offset + axis * 4U);
      if (!std::isfinite(source_common_translation[axis])) {
        fail("RAC1 Moby joint " + std::to_string(joint_index) +
             " contains a non-finite common translation");
      }
    }
    const auto parent_byte_offset = read_le16(class_bytes, common_offset + 0x0cU);
    std::int32_t parent_index = -1;
    if (joint_index == 0U) {
      if (parent_byte_offset != 0U) {
        fail("The RAC1 Moby bind rig's root has a non-zero parent offset");
      }
    } else {
      if ((parent_byte_offset % kRacSkeletonJointBytes) != 0U) {
        fail("RAC1 Moby joint " + std::to_string(joint_index) +
             " has an unaligned parent offset");
      }
      const auto parent = parent_byte_offset / kRacSkeletonJointBytes;
      if (parent >= joint_index) {
        fail("RAC1 Moby joint " + std::to_string(joint_index) +
             " does not reference an earlier parent");
      }
      parent_index = static_cast<std::int32_t>(parent);
    }

    const auto global_bind = inverse_affine(
        inverse_bind, limits.minimum_absolute_linear_determinant, joint_index);
    AffineDouble resolved_local = global_bind;
    if (parent_index >= 0) {
      const auto &parent_inverse =
          inverse_bind_transforms[static_cast<std::size_t>(parent_index)];
      resolved_local = compose_affine(parent_inverse, global_bind);
    }

    result.actor_rig.joints.push_back(ActorRigJointV1{
        parent_index, to_actor_affine(resolved_local, joint_index),
        to_actor_affine(inverse_bind, joint_index)});
    result.source_common_translations.push_back(source_common_translation);
    inverse_bind_transforms.push_back(inverse_bind);
  }
  return result;
}

RacMobyBindPoseGeometryV1 compile_rac_moby_bind_pose_geometry_v1(
    const std::span<const std::byte> class_bytes, const RacMobyClassV1 &moby,
    const RacMobyLodV1 lod, const RacMobyBindPoseLimitsV1 limits) {
  if (limits.max_output_skin_bindings == 0U) {
    fail("RacMobyBindPoseGeometryV1 skin-binding limit must be non-zero");
  }
  const auto selected_kind = packet_kind_for_lod(lod);
  const auto high_end = static_cast<std::uint32_t>(moby.high_lod_packet_count);
  const auto low_end = high_end + moby.low_lod_packet_count;
  const auto packet_count = low_end + moby.metal_packet_count;
  if (moby.packets.size() != packet_count) {
    fail("RacMobyBindPoseGeometryV1 packet storage disagrees with its declared "
         "directory");
  }
  for (std::uint32_t index = 0U; index < packet_count; ++index) {
    const auto expected_kind =
        index < high_end
            ? RacMobyPacketKindV1::high_lod
            : (index < low_end ? RacMobyPacketKindV1::low_lod
                               : RacMobyPacketKindV1::metal);
    if (moby.packets[index].kind != expected_kind) {
      fail("RacMobyBindPoseGeometryV1 packet order disagrees with its "
           "declared directory");
    }
  }

  RacMobyBindPoseGeometryV1 result;
  result.bind_rig = decode_rac_moby_bind_rig_v1(class_bytes, moby,
                                                limits.rig_limits);
  try {
    result.geometry = assemble_rac_moby_model_geometry_v1(
        class_bytes, moby, lod, limits.geometry_limits);
  } catch (const RacMobyModelGeometryError &error) {
    fail(std::string("RAC1 Moby bind-pose geometry reconstruction failed: ") +
         error.what());
  }
  if (!result.geometry.requires_bind_transforms) {
    fail("RacMobyBindPoseGeometryV1 unexpectedly assembled a static class");
  }
  require_limit(result.geometry.vertices.size(),
                limits.max_output_skin_bindings,
                "The RAC1 Moby output skin-binding count");
  if (result.geometry.vertices.size() >
      result.vertex_skin_bindings.max_size()) {
    fail("The RAC1 Moby output skin bindings exceed the host container");
  }
  result.vertex_skin_bindings.reserve(result.geometry.vertices.size());

  std::array<std::optional<ActorSkinBindingV1>, kRacVu0SkinSlots> vu0_state{};
  std::vector<PacketSkinBindings> packet_bindings;
  if (result.geometry.packets.size() > packet_bindings.max_size()) {
    fail("The RAC1 Moby skin packets exceed the host container");
  }
  packet_bindings.reserve(result.geometry.packets.size());
  std::uint64_t local_binding_count = 0U;
  for (std::size_t class_packet_index = 0U;
       class_packet_index < moby.packets.size(); ++class_packet_index) {
    const auto &packet = moby.packets[class_packet_index];
    if (packet.kind != selected_kind) {
      continue;
    }
    if (class_packet_index > UINT32_MAX) {
      fail("A RAC1 Moby class packet index exceeds the V1 skin domain");
    }

    RacMobyPacketGeometryV1 geometry;
    try {
      geometry = parse_rac_moby_packet_geometry_v1(
          class_bytes, packet, moby.scale,
          limits.geometry_limits.packet_limits);
    } catch (const RacMobyPacketGeometryError &error) {
      fail("RAC1 Moby packet " + std::to_string(class_packet_index) +
           " failed skin reconstruction: " + error.what());
    }
    if (local_binding_count > limits.max_output_skin_bindings ||
        geometry.vertices.size() >
            limits.max_output_skin_bindings - local_binding_count) {
      fail("The RAC1 Moby packet-local skin bindings exceed the caller's "
           "limit");
    }
    local_binding_count += geometry.vertices.size();
    packet_bindings.push_back(PacketSkinBindings{
        static_cast<std::uint32_t>(class_packet_index),
        decode_packet_skin_bindings(geometry, moby.joint_count, vu0_state)});
  }
  if (packet_bindings.size() != result.geometry.packets.size()) {
    fail("RAC1 Moby geometry and skin packet selections disagree");
  }
  for (std::size_t index = 0U; index < packet_bindings.size(); ++index) {
    if (packet_bindings[index].class_packet_index !=
        result.geometry.packets[index].class_packet_index) {
      fail("RAC1 Moby geometry and skin packet provenance disagree");
    }
  }

  for (const auto &vertex : result.geometry.vertices) {
    const auto &source_packet = find_packet_bindings(
        packet_bindings, vertex.source_class_packet_index);
    if (vertex.source_vertex_index >= source_packet.bindings.size()) {
      fail("Assembled RAC1 Moby provenance exceeds its source skin packet");
    }
    result.vertex_skin_bindings.push_back(
        source_packet.bindings[vertex.source_vertex_index]);
  }
  if (result.vertex_skin_bindings.size() != result.geometry.vertices.size()) {
    fail("RAC1 Moby geometry and skin-binding outputs are not parallel");
  }
  return result;
}

RacMobyBindPoseGeometryV1 compile_rac_moby_complete_bind_pose_geometry_v1(
    const std::span<const std::byte> class_bytes, const RacMobyClassV1 &moby,
    const RacMobyLodV1 lod, const RacMobyBindPoseLimitsV1 limits) {
  auto result =
      compile_rac_moby_bind_pose_geometry_v1(class_bytes, moby, lod, limits);
  if (moby.metal_packet_count == 0U) {
    return result;
  }

  const auto selected_packet_count = result.geometry.packets.size();
  if (selected_packet_count > limits.geometry_limits.max_packets ||
      moby.metal_packet_count > limits.geometry_limits.max_packets -
                                    selected_packet_count) {
    fail("The complete RAC1 Moby packet count exceeds the caller's limit");
  }

  RacMobyMetalBindPoseGeometryV1 metal;
  metal.packets.reserve(moby.metal_packet_count);
  std::optional<std::int32_t> current_effect_material;
  const auto first_metal_packet =
      static_cast<std::size_t>(moby.metal_packet_begin);
  const auto end_metal_packet =
      first_metal_packet + static_cast<std::size_t>(moby.metal_packet_count);

  for (std::size_t class_packet_index = first_metal_packet;
       class_packet_index < end_metal_packet; ++class_packet_index) {
    if (class_packet_index >= moby.packets.size() ||
        moby.packets[class_packet_index].kind !=
            RacMobyPacketKindV1::metal) {
      fail("The complete RAC1 Moby metal packet directory is inconsistent");
    }
    if (class_packet_index > UINT32_MAX) {
      fail("A RAC1 Moby metal class-packet index exceeds the V1 domain");
    }

    RacMobyMetalPacketGeometryV1 packet_geometry;
    try {
      packet_geometry = parse_rac_moby_metal_packet_geometry_v1(
          class_bytes, moby.packets[class_packet_index], moby.scale,
          limits.geometry_limits.packet_limits);
    } catch (const RacMobyPacketGeometryError &error) {
      fail("RAC1 Moby metal packet " + std::to_string(class_packet_index) +
           " failed geometry reconstruction: " + error.what());
    }

    require_append_capacity(
        result.geometry.vertices.size() + metal.vertices.size(),
        packet_geometry.vertices.size(),
        limits.geometry_limits.max_output_vertices, metal.vertices.max_size(),
        "The complete RAC1 Moby vertex count");
    require_append_capacity(
        result.geometry.triangles.size() + metal.triangles.size(),
        packet_geometry.triangles.size(),
        limits.geometry_limits.max_output_triangles,
        metal.triangles.max_size(), "The complete RAC1 Moby triangle count");
    require_append_capacity(
        result.vertex_skin_bindings.size() +
            metal.vertex_skin_bindings.size(),
        packet_geometry.vertices.size(), limits.max_output_skin_bindings,
        metal.vertex_skin_bindings.max_size(),
        "The complete RAC1 Moby skin-binding count");

    const auto packet_index = static_cast<std::uint32_t>(class_packet_index);
    const auto vertex_begin = static_cast<std::uint32_t>(metal.vertices.size());
    const auto triangle_begin =
        static_cast<std::uint32_t>(metal.triangles.size());
    const auto entry_effect_material = current_effect_material;

    for (std::size_t local_index = 0U;
         local_index < packet_geometry.vertices.size(); ++local_index) {
      const auto &source = packet_geometry.vertices[local_index];
      if (local_index > UINT32_MAX) {
        fail("A RAC1 Moby metal transfer vertex exceeds the V1 domain");
      }
      const auto transfer_index = static_cast<std::uint32_t>(local_index);
      metal.vertices.push_back(RacMobyModelVertexV1{
          source.diagnostic_position,
          decode_packed_normal(source.normal_azimuth,
                               source.normal_elevation),
          {0.0F, 0.0F},
          0U,
          packet_index,
          transfer_index,
          packet_index,
          transfer_index,
          false,
          source.source_range,
          source.source_range});
      metal.vertex_skin_bindings.push_back(
          metal_skin_binding(source, moby.joint_count));
    }

    for (const auto &source : packet_geometry.triangles) {
      const auto material = source.texture_index
                                ? source.texture_index
                                : current_effect_material;
      if (!material || (*material != -2 && *material != -3)) {
        fail("A RAC1 Moby metal triangle has no source-proven effect "
             "material state");
      }
      RacMobyModelTriangleV1 triangle;
      triangle.texture_index = *material;
      triangle.class_packet_index = packet_index;
      for (std::size_t corner = 0U; corner < triangle.vertex_indices.size();
           ++corner) {
        const auto transfer_index = source.transfer_vertex_indices[corner];
        if (transfer_index >= packet_geometry.vertices.size()) {
          fail("A RAC1 Moby metal triangle exceeds its direct vertex table");
        }
        triangle.vertex_indices[corner] = vertex_begin + transfer_index;
      }
      metal.triangles.push_back(triangle);
    }

    if (packet_geometry.consumed_texture_primitive_count != 0U) {
      if (packet_geometry.consumed_texture_primitive_count >
          packet_geometry.texture_primitives.size()) {
        fail("A RAC1 Moby metal packet consumed an unavailable effect "
             "primitive");
      }
      current_effect_material =
          packet_geometry
              .texture_primitives[static_cast<std::size_t>(
                  packet_geometry.consumed_texture_primitive_count - 1U)]
              .texture_index;
    }

    metal.packets.push_back(RacMobyMetalBindPosePacketV1{
        packet_index,
        vertex_begin,
        static_cast<std::uint32_t>(packet_geometry.vertices.size()),
        triangle_begin,
        static_cast<std::uint32_t>(packet_geometry.triangles.size()),
        entry_effect_material,
        current_effect_material});
  }

  if (metal.vertices.empty() || metal.triangles.empty() ||
      metal.vertices.size() != metal.vertex_skin_bindings.size()) {
    fail("The complete RAC1 Moby metal output is empty or inconsistent");
  }
  result.metal_overlay = std::move(metal);
  return result;
}

} // namespace openrc
