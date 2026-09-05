#include "openrc/rac_level_bootstrap_compile.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace openrc {
namespace {

[[noreturn]] void fail(const std::string &message) {
  throw RacLevelBootstrapCompileError(message);
}

[[nodiscard]] double neutral_number(const float value,
                                    const char *const description) {
  if (!std::isfinite(value)) {
    fail(std::string("RAC1 level bootstrap has a non-finite ") + description);
  }
  const auto result = static_cast<double>(value);
  return result == 0.0 ? 0.0 : result;
}

} // namespace

LevelBootstrapV1
compile_rac_level_bootstrap_v1(const RacGameplayBankV1 &gameplay,
                               const std::uint32_t level_id) {
  if (gameplay.static_mobies.size() != gameplay.static_moby_count) {
    fail("RAC1 gameplay static-Moby count disagrees with its typed list");
  }

  const RacGameplayMobyInstanceV1 *player = nullptr;
  for (const auto &moby : gameplay.static_mobies) {
    if (moby.class_id != 0U) {
      continue;
    }
    if (player != nullptr) {
      fail("RAC1 gameplay contains duplicate static class-0 player Mobies");
    }
    player = &moby;
  }
  if (player == nullptr) {
    fail("RAC1 gameplay has no static class-0 player Moby");
  }

  LevelBootstrapV1 result;
  result.level_id = level_id;
  result.death_height_world =
      neutral_number(gameplay.level_settings.death_height, "death height");
  result.default_spawn_id = 0U;
  result.spawn_points.push_back(LevelSpawnPointV1{
      0U,
      {
          neutral_number(player->position[0U], "player spawn X"),
          neutral_number(player->position[1U], "player spawn Y"),
          neutral_number(player->position[2U], "player spawn Z"),
      },
      neutral_number(player->rotation[2U], "player spawn yaw"),
  });
  return result;
}

} // namespace openrc
