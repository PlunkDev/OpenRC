#include "openrc/gameplay_scene.hpp"

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
  throw GameplaySceneError(message);
}

void validate_limits(const GameplaySceneLimitsV1 &limits) {
  if (limits.max_collectibles == 0U || limits.max_item_key_bytes == 0U ||
      limits.max_total_key_bytes == 0U) {
    fail("GameplaySceneV1 caller limits must all be non-zero and bounded");
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
    fail("GameplaySceneV1 item key is not a canonical semantic key");
  }
  for (const char character : value) {
    if (!key_character(static_cast<unsigned char>(character))) {
      fail("GameplaySceneV1 item key contains a non-canonical character");
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
      fail("GameplaySceneV1 item key contains an unsafe component");
    }
    if (separator == std::string_view::npos) {
      break;
    }
    component_begin = separator + 1U;
  }
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail("GameplaySceneV1 aggregate key bytes overflow uint64_t");
  }
  return left + right;
}

} // namespace

void validate_gameplay_scene_v1(const GameplaySceneV1 &scene,
                                const GameplaySceneLimitsV1 limits) {
  validate_limits(limits);
  if (scene.schema_version != kGameplaySceneSchemaVersionV1) {
    fail("GameplaySceneV1 has an unknown schema version");
  }
  if (scene.collectibles.size() > limits.max_collectibles) {
    fail("GameplaySceneV1 exceeds its caller collectible limit");
  }

  std::uint64_t total_key_bytes = 0U;
  for (std::size_t index = 0U; index < scene.collectibles.size(); ++index) {
    const auto &collectible = scene.collectibles[index];
    if (index != 0U &&
        scene.collectibles[index - 1U].authored_id >= collectible.authored_id) {
      fail("GameplaySceneV1 collectible authored IDs are duplicate or out of "
           "order");
    }
    if ((collectible.flags & ~kGameplayCollectibleKnownFlagsV1) != 0U) {
      fail("GameplaySceneV1 collectible has unknown flags");
    }
    if (collectible.amount == 0U) {
      fail("GameplaySceneV1 collectible amount must be positive");
    }
    validate_key(collectible.item_key, limits.max_item_key_bytes);
    total_key_bytes = checked_add(total_key_bytes, collectible.item_key.size());
    for (const float value : collectible.local_center) {
      if (!std::isfinite(value)) {
        fail("GameplaySceneV1 collectible has a non-finite local center");
      }
      if (value == 0.0F && std::signbit(value)) {
        fail("GameplaySceneV1 collectible has non-canonical signed zero in its "
             "local center");
      }
    }
    if (!std::isfinite(collectible.collection_radius)) {
      fail("GameplaySceneV1 collectible has a non-finite collection radius");
    }
    if (collectible.collection_radius == 0.0F &&
        std::signbit(collectible.collection_radius)) {
      fail("GameplaySceneV1 collectible has non-canonical signed zero in its "
           "collection radius");
    }
    if (!(collectible.collection_radius > 0.0F)) {
      fail("GameplaySceneV1 collectible collection radius must be positive");
    }
  }

  if (total_key_bytes > limits.max_total_key_bytes) {
    fail("GameplaySceneV1 exceeds its aggregate key-byte limit");
  }
}

GameplaySceneV1
canonicalize_gameplay_scene_v1(GameplaySceneV1 scene,
                               const GameplaySceneLimitsV1 limits) {
  std::sort(scene.collectibles.begin(), scene.collectibles.end(),
            [](const GameplayCollectibleV1 &left,
               const GameplayCollectibleV1 &right) {
              return left.authored_id < right.authored_id;
            });
  for (auto &collectible : scene.collectibles) {
    for (float &value : collectible.local_center) {
      if (!std::isfinite(value)) {
        fail("GameplaySceneV1 collectible has a non-finite local center");
      }
      value = value == 0.0F ? 0.0F : value;
    }
  }
  validate_gameplay_scene_v1(scene, limits);
  return scene;
}

} // namespace openrc
