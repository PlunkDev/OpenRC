#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace openrc {

inline constexpr std::size_t kActorAffineRowsV1 = 3U;
inline constexpr std::size_t kActorAffineColumnsV1 = 4U;
inline constexpr std::size_t kActorMaximumSkinInfluencesV1 = 3U;

// A general affine transform stored as a row-major 3-by-4 matrix. The linear
// 3-by-3 part is not required to be orthogonal: authored bind data may contain
// scale or shear and consumers must preserve it.
struct ActorAffineTransformV1 {
  std::array<float, kActorAffineRowsV1 * kActorAffineColumnsV1> values{
      1.0F, 0.0F, 0.0F, 0.0F,
      0.0F, 1.0F, 0.0F, 0.0F,
      0.0F, 0.0F, 1.0F, 0.0F,
  };

  [[nodiscard]] float at(const std::size_t row,
                         const std::size_t column) const {
    if (row >= kActorAffineRowsV1 || column >= kActorAffineColumnsV1) {
      throw std::out_of_range("ActorAffineTransformV1 index is out of range");
    }
    return values[row * kActorAffineColumnsV1 + column];
  }

  [[nodiscard]] bool
  operator==(const ActorAffineTransformV1 &) const = default;
};

struct ActorRigJointV1 {
  // Exactly one root uses -1. Every other joint names an earlier joint.
  std::int32_t parent_index = -1;
  ActorAffineTransformV1 local_bind_transform;
  ActorAffineTransformV1 inverse_bind_transform;
};

struct ActorRigV1 {
  std::vector<ActorRigJointV1> joints;
};

// Exact bounded integer weights. A runtime obtains a normalized influence by
// dividing weight_numerators[i] by weight_sum; retaining the sum preserves the
// verified distinction between source encodings whose totals are 255 and 256.
struct ActorSkinBindingV1 {
  std::uint8_t influence_count = 0U;
  std::array<std::uint16_t, kActorMaximumSkinInfluencesV1> joint_indices{};
  std::array<std::uint16_t, kActorMaximumSkinInfluencesV1>
      weight_numerators{};
  std::uint16_t weight_sum = 0U;

  [[nodiscard]] float normalized_weight(const std::size_t index) const noexcept {
    if (index >= weight_numerators.size() || index >= influence_count ||
        weight_sum == 0U) {
      return 0.0F;
    }
    return static_cast<float>(weight_numerators[index]) /
           static_cast<float>(weight_sum);
  }

  [[nodiscard]] bool
  operator==(const ActorSkinBindingV1 &) const = default;
};

} // namespace openrc
