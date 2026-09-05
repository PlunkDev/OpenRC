#include "openrc/destructible_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw DestructibleSceneError(message);
}

void validate_limits(const DestructibleSceneLimitsV1 &limits) {
  if (limits.max_destructibles == 0U || limits.max_total_drops == 0U ||
      limits.max_drops_per_destructible == 0U ||
      limits.max_item_key_bytes == 0U || limits.max_total_key_bytes == 0U ||
      limits.max_health == 0U || limits.max_drop_amount == 0U ||
      !std::isfinite(limits.max_absolute_local_hit_center) ||
      !(limits.max_absolute_local_hit_center > 0.0F) ||
      !std::isfinite(limits.max_hit_radius) ||
      !(limits.max_hit_radius > 0.0F)) {
    fail("DestructibleSceneV1 caller limits must all be non-zero and bounded");
  }
}

[[nodiscard]] bool key_character(const unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('a') &&
          value <= static_cast<unsigned char>('z')) ||
         (value >= static_cast<unsigned char>('0') &&
          value <= static_cast<unsigned char>('9')) ||
         value == static_cast<unsigned char>('.') ||
         value == static_cast<unsigned char>('_') ||
         value == static_cast<unsigned char>('-') ||
         value == static_cast<unsigned char>('/');
}

void validate_key(const std::string_view value,
                  const std::uint32_t maximum_bytes) {
  if (value.empty() || value.size() > maximum_bytes || value.front() == '/' ||
      value.back() == '/') {
    fail("DestructibleSceneV1 item key is not a canonical semantic key");
  }
  for (const char character : value) {
    if (!key_character(static_cast<unsigned char>(character))) {
      fail("DestructibleSceneV1 item key contains a non-canonical character");
    }
  }
  std::size_t component_begin = 0U;
  while (component_begin < value.size()) {
    const auto separator = value.find('/', component_begin);
    const auto component_end =
        separator == std::string_view::npos ? value.size() : separator;
    const auto component =
        value.substr(component_begin, component_end - component_begin);
    if (component.empty() || component == "." || component == "..") {
      fail("DestructibleSceneV1 item key contains an unsafe component");
    }
    if (separator == std::string_view::npos) {
      break;
    }
    component_begin = separator + 1U;
  }
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char *const description) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(std::string(description) + " overflows uint64_t");
  }
  return left + right;
}

} // namespace

void validate_destructible_scene_v1(const DestructibleSceneV1 &scene,
                                    const DestructibleSceneLimitsV1 limits) {
  validate_limits(limits);
  if (scene.schema_version != kDestructibleSceneSchemaVersionV1) {
    fail("DestructibleSceneV1 has an unknown schema version");
  }
  if (scene.destructibles.size() > limits.max_destructibles) {
    fail("DestructibleSceneV1 exceeds its caller destructible limit");
  }

  std::uint64_t total_drops = 0U;
  std::uint64_t total_key_bytes = 0U;
  for (std::size_t index = 0U; index < scene.destructibles.size(); ++index) {
    const auto &destructible = scene.destructibles[index];
    if (index != 0U &&
        scene.destructibles[index - 1U].authored_id >=
            destructible.authored_id) {
      fail("DestructibleSceneV1 authored IDs are duplicate or out of order");
    }
    if ((destructible.flags & ~kDestructibleDefinitionKnownFlagsV1) != 0U) {
      fail("DestructibleSceneV1 definition has unknown flags");
    }
    if (destructible.max_health == 0U ||
        destructible.max_health > limits.max_health) {
      fail("DestructibleSceneV1 max health is zero or exceeds its caller limit");
    }
    if (destructible.accepted_damage_channels == 0U ||
        (destructible.accepted_damage_channels &
         ~game::kDamageChannelKnownMaskV1) != 0U) {
      fail("DestructibleSceneV1 accepted damage channels are empty or unknown");
    }
    for (const float value : destructible.local_hit_center) {
      if (!std::isfinite(value)) {
        fail("DestructibleSceneV1 has a non-finite local hit center");
      }
      if (std::fabs(value) > limits.max_absolute_local_hit_center) {
        fail("DestructibleSceneV1 local hit center exceeds its caller limit");
      }
      if (value == 0.0F && std::signbit(value)) {
        fail("DestructibleSceneV1 has non-canonical signed zero in its local "
             "hit center");
      }
    }
    if (!std::isfinite(destructible.hit_radius) ||
        !(destructible.hit_radius > 0.0F) ||
        destructible.hit_radius > limits.max_hit_radius) {
      fail("DestructibleSceneV1 hit radius must be finite, positive, and "
           "within its caller limit");
    }
    if (destructible.drops.size() > limits.max_drops_per_destructible) {
      fail("DestructibleSceneV1 exceeds its per-destructible drop limit");
    }
    total_drops = checked_add(total_drops, destructible.drops.size(),
                              "DestructibleSceneV1 total drop count");
    if (total_drops > limits.max_total_drops) {
      fail("DestructibleSceneV1 exceeds its aggregate drop limit");
    }

    for (std::size_t drop_index = 0U;
         drop_index < destructible.drops.size(); ++drop_index) {
      const auto &drop = destructible.drops[drop_index];
      if (drop_index != 0U &&
          destructible.drops[drop_index - 1U].item_key >= drop.item_key) {
        fail("DestructibleSceneV1 drop item keys are duplicate or out of "
             "order");
      }
      if ((drop.flags & ~kDestructibleDropKnownFlagsV1) != 0U) {
        fail("DestructibleSceneV1 drop has unknown flags");
      }
      if (drop.amount == 0U || drop.amount > limits.max_drop_amount) {
        fail("DestructibleSceneV1 drop amount is zero or exceeds its caller "
             "limit");
      }
      validate_key(drop.item_key, limits.max_item_key_bytes);
      total_key_bytes =
          checked_add(total_key_bytes, drop.item_key.size(),
                      "DestructibleSceneV1 aggregate key bytes");
      if (total_key_bytes > limits.max_total_key_bytes) {
        fail("DestructibleSceneV1 exceeds its aggregate key-byte limit");
      }
    }
  }
}

DestructibleSceneV1 canonicalize_destructible_scene_v1(
    DestructibleSceneV1 scene, const DestructibleSceneLimitsV1 limits) {
  std::sort(scene.destructibles.begin(), scene.destructibles.end(),
            [](const DestructibleDefinitionV1 &left,
               const DestructibleDefinitionV1 &right) {
              return left.authored_id < right.authored_id;
            });
  for (auto &destructible : scene.destructibles) {
    std::sort(destructible.drops.begin(), destructible.drops.end(),
              [](const DestructibleDropV1 &left,
                 const DestructibleDropV1 &right) {
                return left.item_key < right.item_key;
              });
    for (float &value : destructible.local_hit_center) {
      if (!std::isfinite(value)) {
        fail("DestructibleSceneV1 has a non-finite local hit center");
      }
      value = value == 0.0F ? 0.0F : value;
    }
  }
  validate_destructible_scene_v1(scene, limits);
  return scene;
}

} // namespace openrc
