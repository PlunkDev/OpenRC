#include "openrc/runtime_actor_animation.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kLevelId = 0U;
constexpr std::uint32_t kContentApiVersion = 3U;
constexpr openrc::ActorAnimationIoLimitsV1 kLimits{
    1U << 20U,
    {
        4U,
        16U,
        32U,
        8U,
        128U,
        64U,
        512U,
        120U,
        10'000.0F,
        1.0e-8,
    },
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_runtime_error(Callback &&callback, const std::string &message,
                          const std::string_view expected_text = {}) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::game::RuntimeActorAnimationError &error) {
    if (!expected_text.empty() &&
        std::string_view(error.what()).find(expected_text) ==
            std::string_view::npos) {
      throw std::runtime_error(message + ": wrong wrapped error context");
    }
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::PreparedContentDigestV1 digest(const std::uint8_t seed) {
  openrc::PreparedContentDigestV1 result{};
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = static_cast<std::byte>(
        static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(index)));
  }
  return result;
}

[[nodiscard]] openrc::ActorJointPoseV1 pose(const float x) {
  openrc::ActorJointPoseV1 result;
  result.translation = {x, 0.0F, 0.0F};
  return result;
}

[[nodiscard]] openrc::ActorAnimationClipV1
clip(const std::uint32_t id, std::string key, const float x,
     const openrc::ActorAnimationWrapModeV1 wrap_mode) {
  openrc::ActorAnimationClipV1 result;
  result.id = id;
  result.semantic_key = std::move(key);
  result.rig_key = "actors/ratchet/rig";
  result.rig_content_sha256 = digest(static_cast<std::uint8_t>(0x20U + id));
  result.source_updates_per_second = 50U;
  result.wrap_mode = wrap_mode;
  result.frames = {{0.25F, {pose(x), pose(x + 1.0F)}},
                   {0.5F, {pose(x + 2.0F), pose(x + 3.0F)}}};
  return result;
}

[[nodiscard]] openrc::ActorAnimationBankV1 make_bank() {
  openrc::ActorAnimationBankV1 result;
  result.clips = {
      clip(0U, "actors/ratchet/idle", 0.0F,
           openrc::ActorAnimationWrapModeV1::loop),
      clip(1U, "actors/ratchet/run", 10.0F,
           openrc::ActorAnimationWrapModeV1::loop),
  };
  return result;
}

[[nodiscard]] openrc::LevelPackageResourceV1 make_resource(
    const std::string_view resource_id, const std::string_view type_id,
    const std::uint32_t schema_version, std::vector<std::byte> payload) {
  openrc::LevelPackageResourceV1 result;
  result.resource_id = std::string(resource_id);
  result.type_id = std::string(type_id);
  result.schema_version = schema_version;
  result.operation = openrc::LevelPackageResourceOperationV1::upsert;
  result.payload = std::move(payload);
  result.payload_sha256 = openrc::prepared_content_sha256_v1(result.payload);
  return result;
}

[[nodiscard]] openrc::ResolvedLevelPackageV1 make_package() {
  openrc::ResolvedLevelPackageV1 result;
  result.level_id = kLevelId;
  result.content_api_version = kContentApiVersion;
  result.build_id = "SCES-50916-PAL-v2.00";
  result.resources.push_back(make_resource(
      openrc::kActorAnimationResourceIdV1,
      openrc::kActorAnimationResourceTypeIdV1,
      openrc::kActorAnimationResourceSchemaVersionV1,
      openrc::encode_actor_animation_bank_v1(make_bank(), kLimits)));
  result.resources.push_back(
      make_resource("future/unrelated", "openrc.future-resource", 17U,
                    {std::byte{0x12U}, std::byte{0x34U}, std::byte{0x56U}}));
  return result;
}

[[nodiscard]] openrc::ResolvedLevelPackageV1 make_package_without_animation() {
  auto result = make_package();
  result.resources.erase(result.resources.begin());
  return result;
}

[[nodiscard]] openrc::LevelPackageResourceV1 &
find_animation_resource(openrc::ResolvedLevelPackageV1 &package) {
  for (auto &resource : package.resources) {
    if (resource.resource_id == openrc::kActorAnimationResourceIdV1) {
      return resource;
    }
  }
  throw std::runtime_error("test actor-animation resource is missing");
}

void test_absence_is_compatible_and_extra_resources_are_ignored() {
  const auto absent =
      openrc::game::load_optional_runtime_actor_animation_bank_v1(
          make_package_without_animation(), kContentApiVersion, kLimits);
  expect(!absent,
         "runtime animation loader did not preserve old-package compatibility");

  const auto loaded =
      openrc::game::load_optional_runtime_actor_animation_bank_v1(
          make_package(), kContentApiVersion, kLimits);
  expect(loaded.has_value() &&
             *loaded == openrc::canonicalize_actor_animation_bank_v1(
                            make_bank(), kLimits.bank),
         "runtime animation loader changed data or rejected an unrelated "
         "resource");
}

void test_present_resource_requires_exact_resolved_identity() {
  auto duplicate = make_package();
  duplicate.resources.push_back(find_animation_resource(duplicate));
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_animation_bank_v1(
            duplicate, kContentApiVersion, kLimits);
      },
      "runtime animation loader accepted a duplicate resource ID");

  auto wrong_type = make_package();
  find_animation_resource(wrong_type).type_id = "openrc.not-animation-bank";
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_animation_bank_v1(
            wrong_type, kContentApiVersion, kLimits);
      },
      "runtime animation loader accepted the wrong resource type");

  auto wrong_schema = make_package();
  ++find_animation_resource(wrong_schema).schema_version;
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_animation_bank_v1(
            wrong_schema, kContentApiVersion, kLimits);
      },
      "runtime animation loader accepted the wrong resource schema");

  auto remove = make_package();
  find_animation_resource(remove).operation =
      openrc::LevelPackageResourceOperationV1::remove;
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_animation_bank_v1(
            remove, kContentApiVersion, kLimits);
      },
      "runtime animation loader accepted an unresolved remove");
}

void test_api_policy_is_checked_even_when_animation_is_absent() {
  const auto absent = make_package_without_animation();
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_animation_bank_v1(
            absent, 0U, kLimits);
      },
      "runtime animation loader accepted an implicit content API policy");
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_animation_bank_v1(
            absent, kContentApiVersion + 1U, kLimits);
      },
      "runtime animation loader accepted a mismatched content API without "
      "animations");
}

void test_digest_and_limits_are_strict() {
  auto zero_digest = make_package();
  find_animation_resource(zero_digest).payload_sha256 = {};
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_animation_bank_v1(
            zero_digest, kContentApiVersion, kLimits);
      },
      "runtime animation loader accepted a zero payload digest");

  auto stale_digest = make_package();
  find_animation_resource(stale_digest).payload[0U] ^= std::byte{0x01U};
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_animation_bank_v1(
            stale_digest, kContentApiVersion, kLimits);
      },
      "runtime animation loader accepted a stale payload digest");

  const auto package = make_package();
  auto small_bytes = kLimits;
  small_bytes.max_encoded_bytes = package.resources.front().payload.size() - 1U;
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_animation_bank_v1(
            package, kContentApiVersion, small_bytes);
      },
      "runtime animation loader ignored its encoded-byte limit");

  auto small_bank = kLimits;
  small_bank.bank.max_clips = 1U;
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_animation_bank_v1(
            package, kContentApiVersion, small_bank);
      },
      "runtime animation loader did not forward bank limits",
      "Invalid runtime actor-animation resource");
}

void test_strict_codec_errors_are_wrapped() {
  auto malformed = make_package();
  auto &resource = find_animation_resource(malformed);
  resource.payload[0U] ^= std::byte{0x01U};
  resource.payload_sha256 =
      openrc::prepared_content_sha256_v1(resource.payload);
  expect_runtime_error(
      [&] {
        (void)openrc::game::load_optional_runtime_actor_animation_bank_v1(
            malformed, kContentApiVersion, kLimits);
      },
      "runtime animation loader accepted a malformed freshly hashed payload",
      "Invalid runtime actor-animation resource");
}

} // namespace

int main() {
  try {
    test_absence_is_compatible_and_extra_resources_are_ignored();
    test_present_resource_requires_exact_resolved_identity();
    test_api_policy_is_checked_even_when_animation_is_absent();
    test_digest_and_limits_are_strict();
    test_strict_codec_errors_are_wrapped();
    std::cout << "runtime_actor_animation_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "runtime_actor_animation_tests: " << error.what() << '\n';
    return 1;
  }
}
