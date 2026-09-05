#include "openrc/level_actor_animation_compile.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr openrc::ActorAnimationIoLimitsV1 kAnimationLimits{
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

constexpr openrc::LevelPackageV1Limits kPackageLimits{
    2U * 1024U * 1024U, 8U, 8U, 32U, 128U, 1U * 1024U * 1024U,
    2U * 1024U * 1024U, 8U,
};

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_compile_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::LevelActorAnimationCompileError &) {
    return;
  }
  throw std::runtime_error(message);
}

template <typename Callback>
void expect_package_error(Callback &&callback, const std::string &message) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const openrc::LevelPackageV1Error &) {
    return;
  }
  throw std::runtime_error(message);
}

[[nodiscard]] openrc::PreparedContentDigestV1
digest_of(const std::string_view value) {
  return openrc::prepared_content_sha256_v1(std::as_bytes(std::span(value)));
}

[[nodiscard]] openrc::PreparedContentDigestV1 digest(const std::uint8_t seed) {
  openrc::PreparedContentDigestV1 result{};
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = static_cast<std::byte>(
        static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(index)));
  }
  return result;
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
generated(const std::string_view pass) {
  return {
      openrc::LevelPackageProvenanceKindV1::generated,
      std::string(pass),
      0U,
      0U,
      {},
  };
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
direct_source(const openrc::LevelPackageProvenanceKindV1 kind,
              const std::string_view locator, const std::uint64_t offset,
              const std::string_view identity) {
  return {
      kind, std::string(locator), offset, identity.size(), digest_of(identity),
  };
}

[[nodiscard]] openrc::LevelPackageProvenanceV1
mod_source(const std::string_view identity = "actor-animation-mod") {
  return direct_source(openrc::LevelPackageProvenanceKindV1::mod_resource,
                       "mods/actor-animation-test/source", 0U, identity);
}

[[nodiscard]] openrc::LevelPackageResourceV1
base_resource(const std::string_view id, const std::string_view type,
              const std::string_view pass, std::vector<std::byte> payload) {
  openrc::LevelPackageResourceV1 result;
  result.resource_id = std::string(id);
  result.type_id = std::string(type);
  result.schema_version = 1U;
  result.operation = openrc::LevelPackageResourceOperationV1::upsert;
  result.flags = openrc::kLevelPackageResourceOverlayReplaceableV1;
  result.provenance.push_back(generated(pass));
  result.payload = std::move(payload);
  return result;
}

[[nodiscard]] openrc::LevelPackageV1 make_base() {
  openrc::LevelPackageV1 result;
  result.level_id = 0U;
  result.content_api_version = 1U;
  result.build_id = "SCES-50916-PAL-v2.00";
  result.layer_kind = openrc::LevelPackageLayerKindV1::base;
  result.layer_id = "base";
  result.priority = 0;

  // Reversed input order proves that attachment preserves the canonical bytes
  // of both existing resources.
  result.resources.push_back(base_resource(
      "world/collision", "openrc.collision-world", "compiler/test/collision-v1",
      {std::byte{0x04U}, std::byte{0x05U}, std::byte{0x06U}}));
  result.resources.push_back(base_resource(
      "world/bootstrap", "openrc.level-bootstrap", "compiler/test/bootstrap-v1",
      {std::byte{0x01U}, std::byte{0x02U}}));
  return result;
}

[[nodiscard]] openrc::ActorJointPoseV1 pose(const float x,
                                            const bool signed_zero) {
  openrc::ActorJointPoseV1 result;
  result.normalized_rotation_xyzw = {0.0F, 0.0F, 0.0F, -2.0F};
  result.translation = {x, signed_zero ? -0.0F : 0.0F, 0.0F};
  result.terminal_scale = {1.0F, 1.0F, signed_zero ? -0.0F : 0.0F};
  return result;
}

[[nodiscard]] openrc::ActorAnimationClipV1
clip(const std::uint32_t id, std::string key, const float x,
     const openrc::ActorAnimationWrapModeV1 wrap_mode, const bool signed_zero) {
  openrc::ActorAnimationClipV1 result;
  result.id = id;
  result.semantic_key = std::move(key);
  result.rig_key = "actors/ratchet/rig";
  result.rig_content_sha256 = digest(static_cast<std::uint8_t>(0x30U + id));
  result.source_updates_per_second = 50U;
  result.wrap_mode = wrap_mode;
  result.frames = {
      {signed_zero ? -0.0F : 0.0F,
       {pose(x, signed_zero), pose(x + 1.0F, false)}},
      {0.5F, {pose(x + 2.0F, false), pose(x + 3.0F, false)}},
  };
  return result;
}

[[nodiscard]] openrc::ActorAnimationBankV1
make_bank(const bool reversed = true, const bool signed_zero = true) {
  auto idle = clip(0U, "actors/ratchet/idle", 0.0F,
                   openrc::ActorAnimationWrapModeV1::loop, signed_zero);
  auto run = clip(1U, "actors/ratchet/run", 10.0F,
                  openrc::ActorAnimationWrapModeV1::loop, false);
  openrc::ActorAnimationBankV1 result;
  if (reversed) {
    result.clips = {std::move(run), std::move(idle)};
  } else {
    result.clips = {std::move(idle), std::move(run)};
  }
  return result;
}

[[nodiscard]] std::array<openrc::LevelPackageProvenanceV1, 2U> make_sources() {
  // Deliberately reversed relative to canonical provenance-kind order.
  return {
      direct_source(openrc::LevelPackageProvenanceKindV1::prepared_resource,
                    "rac1/level/000/ratchet/sequence-table", 0U,
                    "decoded-ratchet-sequences"),
      direct_source(openrc::LevelPackageProvenanceKindV1::iso_range,
                    "disc/level/000/core", 0x123400U,
                    "complete-level-core-source-range"),
  };
}

[[nodiscard]] const openrc::LevelPackageResourceV1 &
find_resource(const openrc::LevelPackageV1 &package,
              const std::string_view id) {
  const auto found =
      std::find_if(package.resources.begin(), package.resources.end(),
                   [id](const openrc::LevelPackageResourceV1 &resource) {
                     return resource.resource_id == id;
                   });
  if (found == package.resources.end()) {
    throw std::runtime_error("test package is missing resource " +
                             std::string(id));
  }
  return *found;
}

[[nodiscard]] bool
provenance_equal(const openrc::LevelPackageProvenanceV1 &left,
                 const openrc::LevelPackageProvenanceV1 &right) {
  return left.kind == right.kind &&
         left.source_locator == right.source_locator &&
         left.source_offset == right.source_offset &&
         left.source_bytes == right.source_bytes &&
         left.source_sha256 == right.source_sha256;
}

void test_success_determinism_and_preservation() {
  const auto sources = make_sources();
  const auto first = openrc::attach_actor_animation_bank_to_level_package_v1(
      make_base(), make_bank(), sources, kAnimationLimits, kPackageLimits);
  const auto second = openrc::attach_actor_animation_bank_to_level_package_v1(
      make_base(), make_bank(false, false), sources, kAnimationLimits,
      kPackageLimits);
  const auto first_bytes =
      openrc::encode_level_package_v1(first, kPackageLimits);
  const auto second_bytes =
      openrc::encode_level_package_v1(second, kPackageLimits);

  expect(first_bytes == second_bytes,
         "actor-animation attachment is not byte deterministic");
  expect(first.resources.size() == 3U &&
             first.resources[0U].resource_id == "actors/animations" &&
             first.resources[1U].resource_id == "world/bootstrap" &&
             first.resources[2U].resource_id == "world/collision",
         "actor-animation attachment returned non-canonical resource order");

  const auto &resource =
      find_resource(first, openrc::kActorAnimationResourceIdV1);
  expect(
      resource.type_id == openrc::kActorAnimationResourceTypeIdV1 &&
          resource.schema_version ==
              openrc::kActorAnimationResourceSchemaVersionV1 &&
          resource.operation ==
              openrc::LevelPackageResourceOperationV1::upsert &&
          resource.flags == openrc::kLevelPackageResourceOverlayReplaceableV1 &&
          (resource.flags & openrc::kLevelPackageResourceOverlayRemovableV1) ==
              0U &&
          resource.payload_sha256 ==
              openrc::prepared_content_sha256_v1(resource.payload),
      "actor-animation resource identity, flags, or digest are wrong");
  expect(resource.provenance.size() == 3U &&
             provenance_equal(resource.provenance[0U], sources[1U]) &&
             provenance_equal(resource.provenance[1U], sources[0U]),
         "actor-animation direct provenance is not exact and canonical");
  const auto &pass = resource.provenance[2U];
  expect(pass.kind == openrc::LevelPackageProvenanceKindV1::generated &&
             pass.source_locator == openrc::kLevelActorAnimationCompilePassV1 &&
             pass.source_offset == 0U && pass.source_bytes == 0U &&
             openrc::is_zero_prepared_digest_v1(pass.source_sha256),
         "actor-animation compiler-pass provenance is not exact");

  const auto decoded = openrc::decode_actor_animation_bank_v1(resource.payload,
                                                              kAnimationLimits);
  expect(decoded == openrc::canonicalize_actor_animation_bank_v1(
                        make_bank(), kAnimationLimits.bank),
         "attached ActorAnimationBankV1 changed neutral semantics");
  const auto parsed =
      openrc::parse_level_package_v1(first_bytes, kPackageLimits);
  expect(openrc::encode_level_package_v1(parsed, kPackageLimits) == first_bytes,
         "attached package did not survive a canonical round trip");

  auto stripped = first;
  std::erase_if(
      stripped.resources, [](const openrc::LevelPackageResourceV1 &candidate) {
        return candidate.resource_id == openrc::kActorAnimationResourceIdV1;
      });
  expect(openrc::encode_level_package_v1(stripped, kPackageLimits) ==
             openrc::encode_level_package_v1(make_base(), kPackageLimits),
         "actor-animation attachment rewrote an existing resource");
}

void test_rejects_duplicate_overlay_and_bad_provenance() {
  const auto sources = make_sources();

  auto duplicate = make_base();
  duplicate.resources.push_back(base_resource(
      openrc::kActorAnimationResourceIdV1,
      openrc::kActorAnimationResourceTypeIdV1,
      "compiler/test/existing-actor-animation", {std::byte{0x01U}}));
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                duplicate, make_bank(), sources, kAnimationLimits,
                kPackageLimits));
      },
      "attachment accepted an existing actors/animations resource");

  auto overlay = make_base();
  overlay.layer_kind = openrc::LevelPackageLayerKindV1::overlay;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                overlay, make_bank(), sources, kAnimationLimits,
                kPackageLimits));
      },
      "attachment accepted an overlay package");

  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), make_bank(), {}, kAnimationLimits,
                kPackageLimits));
      },
      "attachment accepted empty source provenance");

  for (const auto dishonest_kind :
       {openrc::LevelPackageProvenanceKindV1::generated,
        openrc::LevelPackageProvenanceKindV1::mod_resource}) {
    auto dishonest = sources;
    dishonest[0U].kind = dishonest_kind;
    expect_compile_error(
        [&] {
          static_cast<void>(
              openrc::attach_actor_animation_bank_to_level_package_v1(
                  make_base(), make_bank(), dishonest, kAnimationLimits,
                  kPackageLimits));
        },
        "attachment accepted generated or mod source provenance");
  }

  auto zero_bytes = sources;
  zero_bytes[0U].source_bytes = 0U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), make_bank(), zero_bytes, kAnimationLimits,
                kPackageLimits));
      },
      "attachment accepted zero-byte source provenance");

  auto zero_digest = sources;
  zero_digest[0U].source_sha256 = {};
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), make_bank(), zero_digest, kAnimationLimits,
                kPackageLimits));
      },
      "attachment accepted source provenance without a digest");

  auto overflowing = sources;
  overflowing[0U].source_offset = std::numeric_limits<std::uint64_t>::max();
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), make_bank(), overflowing, kAnimationLimits,
                kPackageLimits));
      },
      "attachment accepted an overflowing source range");

  const std::array repeated{sources[0U], sources[0U]};
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), make_bank(), repeated, kAnimationLimits,
                kPackageLimits));
      },
      "attachment accepted duplicate source provenance");
}

void test_rejects_tight_limits_and_invalid_bank() {
  const auto sources = make_sources();
  const auto animation_payload =
      openrc::encode_actor_animation_bank_v1(make_bank(), kAnimationLimits);
  const auto canonical_base_bytes =
      openrc::encode_level_package_v1(make_base(), kPackageLimits);
  constexpr std::uint64_t kBasePayloadBytes = 5U;

  auto limits = kPackageLimits;
  limits.max_resources = 2U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), make_bank(), sources, kAnimationLimits, limits));
      },
      "attachment ignored its package resource limit");

  limits = kPackageLimits;
  limits.max_provenance_per_resource = 2U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), make_bank(), sources, kAnimationLimits, limits));
      },
      "attachment ignored its per-resource provenance limit");

  limits = kPackageLimits;
  limits.max_total_provenance_records = 4U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), make_bank(), sources, kAnimationLimits, limits));
      },
      "attachment ignored its aggregate provenance limit");

  limits = kPackageLimits;
  limits.max_payload_bytes = animation_payload.size() - 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), make_bank(), sources, kAnimationLimits, limits));
      },
      "attachment ignored its per-resource payload limit");

  limits = kPackageLimits;
  limits.max_total_payload_bytes =
      kBasePayloadBytes + animation_payload.size() - 1U;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), make_bank(), sources, kAnimationLimits, limits));
      },
      "attachment ignored its aggregate payload limit");

  limits = kPackageLimits;
  limits.max_input_bytes = canonical_base_bytes.size();
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), make_bank(), sources, kAnimationLimits, limits));
      },
      "attachment ignored its encoded package byte limit");

  auto animation_limits = kAnimationLimits;
  animation_limits.max_encoded_bytes = openrc::kActorAnimationIoHeaderBytesV1;
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), make_bank(), sources, animation_limits,
                kPackageLimits));
      },
      "attachment ignored its ActorAnimationBankV1 encoded-byte limit");

  auto invalid = make_bank();
  invalid.clips[0U].rig_content_sha256 = {};
  expect_compile_error(
      [&] {
        static_cast<void>(
            openrc::attach_actor_animation_bank_to_level_package_v1(
                make_base(), invalid, sources, kAnimationLimits,
                kPackageLimits));
      },
      "attachment accepted an invalid ActorAnimationBankV1");
}

[[nodiscard]] openrc::LevelPackageV1 make_animation_overlay(
    const openrc::LevelPackageV1 &base,
    const openrc::LevelPackageResourceOperationV1 operation) {
  openrc::LevelPackageV1 result;
  result.level_id = base.level_id;
  result.content_api_version = base.content_api_version;
  result.build_id = base.build_id;
  result.layer_kind = openrc::LevelPackageLayerKindV1::overlay;
  result.layer_id = operation == openrc::LevelPackageResourceOperationV1::upsert
                        ? "mods/actor-animation-replacement"
                        : "mods/actor-animation-removal";
  result.priority = 10;
  result.required_base_package_sha256 =
      openrc::level_package_sha256_v1(base, kPackageLimits);

  openrc::LevelPackageResourceV1 resource;
  resource.resource_id = std::string(openrc::kActorAnimationResourceIdV1);
  resource.type_id = std::string(openrc::kActorAnimationResourceTypeIdV1);
  resource.schema_version = openrc::kActorAnimationResourceSchemaVersionV1;
  resource.operation = operation;
  resource.provenance.push_back(mod_source());
  if (operation == openrc::LevelPackageResourceOperationV1::upsert) {
    resource.flags = openrc::kLevelPackageResourceOverlayReplaceableV1;
    auto replacement_bank = make_bank(false, false);
    replacement_bank.clips[0U].frames[0U].joint_poses[0U].translation[0U] =
        42.0F;
    resource.payload = openrc::encode_actor_animation_bank_v1(replacement_bank,
                                                              kAnimationLimits);
  }
  result.resources.push_back(std::move(resource));
  return result;
}

void test_overlay_replacement_allowed_but_removal_forbidden() {
  const auto sources = make_sources();
  const auto base = openrc::attach_actor_animation_bank_to_level_package_v1(
      make_base(), make_bank(), sources, kAnimationLimits, kPackageLimits);

  const auto replacement = make_animation_overlay(
      base, openrc::LevelPackageResourceOperationV1::upsert);
  const std::array replacement_layers{replacement};
  const auto resolved = openrc::resolve_level_package_v1(
      base, replacement_layers, kPackageLimits);
  const auto resolved_animation = std::find_if(
      resolved.resources.begin(), resolved.resources.end(),
      [](const openrc::LevelPackageResourceV1 &resource) {
        return resource.resource_id == openrc::kActorAnimationResourceIdV1;
      });
  expect(resolved_animation != resolved.resources.end() &&
             resolved_animation->payload == replacement.resources[0U].payload,
         "an explicit overlay could not replace the actor-animation bank");

  const auto removal = make_animation_overlay(
      base, openrc::LevelPackageResourceOperationV1::remove);
  const std::array removal_layers{removal};
  expect_package_error(
      [&] {
        static_cast<void>(openrc::resolve_level_package_v1(base, removal_layers,
                                                           kPackageLimits));
      },
      "an overlay removed the required actor-animation bank");
}

} // namespace

int main() {
  try {
    test_success_determinism_and_preservation();
    test_rejects_duplicate_overlay_and_bad_provenance();
    test_rejects_tight_limits_and_invalid_bank();
    test_overlay_replacement_allowed_but_removal_forbidden();
    std::cout << "level actor-animation compile tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "level actor-animation compile tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
