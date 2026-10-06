#include "openrc/rac_moby_post_execute.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using namespace openrc;
using namespace openrc::game;
constexpr RacMobyPostLimitsV1 kPostLimits{4U, 4U, 8U};
constexpr OrderedSpatialIndexLimitsV1 kIndexLimits{4096U, 768U, 300U};
constexpr SpatialCellRectangleV1 kOld{1U, 1U, 1U, 1U};
constexpr SpatialCellRectangleV1 kNew{2U, 3U, 2U, 3U};

void check(const bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template<class Function> void rejects(Function &&function) {
  try { function(); }
  catch (const RacMobyPostError &) { return; }
  throw std::runtime_error("Unowned or unsupported composed Moby post was accepted");
}

struct Fixture {
  RacMobyPostActorV1 actor;
  std::vector<RacMobyPostSequenceHeaderV1> headers;
  std::vector<RacMobyPostSnapshotVectorV1> snapshots;
  Fixture() {
    actor.flags = 0x100U;
    actor.model_reference = 0x3000U;
    actor.scale_bits = 0x3f800000U;
    actor.rotation_bits = {0x7fc12345U, 0xffffffffU, 0x7f800000U, 0x12345678U};
    actor.position_bits = {0x42000000U, 0x42400000U, 0x42c00000U, 0xffffffffU};
    actor.matrix_bits = {{{0x3f800000U, 0U, 0U, 0x7fc12345U},
                          {0U, 0x3f800000U, 0U, 0x80000000U},
                          {0U, 0U, 0x3f800000U, 0xffffffffU}}};
    actor.counter_word_bits = 0xabcdffffU;
    actor.spatial_reference = 0x9000U;
    actor.packed_bounds_bits = 0x80807f7fU;
    actor.live_index_bits = 42U;
  }
  RacMobyPostBindingsV1 bindings() const { return {0x1000U, headers, snapshots}; }
  RacMobyPostSpatialOwnerV1 owner(OrderedSpatialIndexV1 &index) const {
    return {index, index.revision(), 0x1000U, actor.live_index_bits,
            static_cast<std::uint16_t>(actor.live_index_bits)};
  }
};

OrderedSpatialIndexV1 index() {
  return OrderedSpatialIndexV1(
      make_cleared_ordered_spatial_index_v1({64U, 64U, 32U}, kIndexLimits), kIndexLimits);
}

void no_unvisited_owner_or_numeric_shortcut() {
  Fixture f;
  auto spatial = index();
  auto wrong_owner = f.owner(spatial);
  wrong_owner.expected_revision = 99U;
  wrong_owner.source_actor_address_bits = 0x7777U;
  const auto spatial_before = spatial.snapshot();
  f.actor.state_byte = 0x80U;
  f.actor.flags = 0U; // would request rotation if this early return were lost
  const auto before = f.actor;
  const auto returned = execute_rac_moby_post_v1(
      f.actor, f.bindings(), nullptr, &wrong_owner, kPostLimits);
  check(returned.actor == before && returned.writes.empty() &&
            returned.return_reason == RacMobyPostReturnReasonV1::negative_state &&
            !returned.spatial && !returned.rotation_calls && !returned.scale_calls &&
            returned.warnings.empty() && spatial.snapshot() == spatial_before,
        "Unreached numeric/spatial owners were validated or the actor was reset");
  f.actor.state_byte = 0U;
  f.actor.rotation_bits = {};
  // Even zero rotation must use original d18 if its source gate is reached.
  rejects([&] { static_cast<void>(execute_rac_moby_post_v1(
      f.actor, f.bindings(), nullptr, &wrong_owner, kPostLimits)); });
  check(spatial.snapshot() == spatial_before, "Missing rotation owner changed the index");

  f.actor.flags = 0x100U;
  f.actor.spatial_reference = 0U;
  const auto null = execute_rac_moby_post_v1(
      f.actor, f.bindings(), nullptr, &wrong_owner, kPostLimits);
  check(null.return_reason == RacMobyPostReturnReasonV1::null_spatial_reference &&
            null.scale_calls == 1U && null.derived_calls == 1U &&
            null.actor.matrix_bits == f.actor.matrix_bits &&
            null.actor.rotation_bits == f.actor.rotation_bits &&
            null.actor.derived_vector_bits == RacMobyPostVectorV1{
                0x47000000U, 0x47400000U, 0x47c00000U, 0U} &&
            null.actor.counter_word_bits == 0xabcd0000U &&
            null.actor.packed_bounds_bits == f.actor.packed_bounds_bits &&
            spatial.snapshot() == spatial_before,
        "Cached matrix, unused W, source scale/derived or late null return differs");

  f.actor.spatial_reference = 0x9000U;
  f.actor.packed_bounds_bits = 0x03020302U;
  const auto unchanged = execute_rac_moby_post_v1(
      f.actor, f.bindings(), nullptr, &wrong_owner, kPostLimits);
  check(unchanged.return_reason == RacMobyPostReturnReasonV1::unchanged_bounds &&
            !unchanged.spatial && spatial.snapshot() == spatial_before,
        "Same active bounds incorrectly inspected or mutated spatial ownership");
  f.actor.position_bits[0] = 0xbf800000U;
  const auto rejected = execute_rac_moby_post_v1(
      f.actor, f.bindings(), nullptr, &wrong_owner, kPostLimits);
  check(rejected.return_reason == RacMobyPostReturnReasonV1::rejected_minimum &&
            !rejected.spatial && rejected.actor.packed_bounds_bits == f.actor.packed_bounds_bits &&
            spatial.snapshot() == spatial_before,
        "Minimum rejection became a fabricated spatial completion");
}

void actual_fresh_and_shared_membership() {
  Fixture f;
  auto spatial = index();
  auto owner = f.owner(spatial);
  const auto original = f.actor;
  const auto first = execute_rac_moby_post_v1(
      f.actor, f.bindings(), nullptr, &owner, kPostLimits);
  check(first.spatial && !first.return_reason && first.spatial->member_token == 42U &&
            !first.spatial->old_rectangle && first.spatial->new_rectangle == kNew &&
            first.spatial->revision_before == 0U && first.spatial->revision_after == 1U &&
            spatial.revision() == 1U && spatial.members_at(2U, 3U).size() == 1U &&
            spatial.members_at(2U, 3U)[0U] == 42U && f.actor == original,
        "Fresh spatial tail did not mutate the supplied index and return the staged actor");
  auto expected = original;
  expected.derived_vector_bits = {0x47000000U, 0x47400000U, 0x47c00000U, 0U};
  expected.counter_word_bits = 0xabcd0000U;
  expected.packed_bounds_bits = 0x03020302U;
  check(first.actor == expected && first.writes.size() == 6U &&
            std::get<RacMobyPostPackedBoundsWriteV1>(first.writes.back()).packed_bits == 0x03020302U,
        "Complete post lost the actual packed SW or changed unrelated actor fields");
  constexpr std::array fields{RacMobyPostFieldV1::matrix0, RacMobyPostFieldV1::matrix1,
      RacMobyPostFieldV1::matrix2, RacMobyPostFieldV1::derived_vector,
      RacMobyPostFieldV1::counter_low16};
  for (unsigned i = 0U; i < fields.size(); ++i)
    check(std::get<RacMobyPostWriteV1>(first.writes[i]).field == fields[i],
          "Composed post changed the source actor-store order");

  auto shared = index();
  for (const auto token : {7U, 42U, 9U})
    shared.set_membership(static_cast<std::uint16_t>(token), kOld, shared.revision());
  f.actor.packed_bounds_bits = 0x01010101U;
  auto shared_owner = f.owner(shared);
  const auto moved = execute_rac_moby_post_v1(
      f.actor, f.bindings(), nullptr, &shared_owner, kPostLimits);
  const std::array<std::uint16_t, 2U> old_members{7U, 9U};
  check(moved.spatial && moved.spatial->old_rectangle == kOld &&
            moved.spatial->new_rectangle == kNew && shared.revision() == 4U &&
            std::ranges::equal(shared.members_at(1U, 1U), old_members) &&
            shared.members_at(2U, 3U)[0U] == 42U,
        "Spatial delta bypassed actual old membership or last-member replacement");
  const auto after = shared.snapshot();
  check(after.member_pool[0U] == 7U && after.member_pool[1U] == 9U &&
            after.member_pool[2U] == 0U && after.member_pool[16U] == 42U,
        "Spatial mutation did not preserve actual ordered pool storage");
}

void real_inactive_tail_and_same_negative_word() {
  Fixture f;
  auto spatial = index();
  spatial.set_membership(42U, kOld, spatial.revision());
  f.actor.packed_bounds_bits = 0x01010101U;
  f.actor.cached_header_bits[3U] = 0x49800000U; // radius1048576
  f.actor.position_bits[0U] = 0x44800000U; // center1048576 after MUL1024
  f.actor.position_bits[1U] = 0x44800000U;
  auto owner = f.owner(spatial);
  const auto removed = execute_rac_moby_post_v1(
      f.actor, f.bindings(), nullptr, &owner, kPostLimits);
  check(removed.spatial && removed.spatial->old_rectangle == kOld &&
            !removed.spatial->new_rectangle && removed.actor.packed_bounds_bits == 0x80800000U &&
            spatial.members_at(1U, 1U).empty() && spatial.revision() == 2U &&
            std::get<RacMobyPostPackedBoundsWriteV1>(removed.writes.back()).packed_bits == 0x80800000U,
        "Negative new maxY omitted removal or normalized the actual packed store");
  f.actor = removed.actor;
  owner = f.owner(spatial);
  const auto before = spatial.snapshot();
  const auto again = execute_rac_moby_post_v1(
      f.actor, f.bindings(), nullptr, &owner, kPostLimits);
  const auto after = spatial.snapshot();
  check(again.spatial && !again.return_reason && !again.spatial->old_rectangle &&
            !again.spatial->new_rectangle && again.actor.packed_bounds_bits == f.actor.packed_bounds_bits &&
            after.revision == before.revision + 1U && after.cells == before.cells &&
            after.member_pool == before.member_pool && after.allocation_words == before.allocation_words &&
            after.memberships == before.memberships,
        "Equal negative low32 incorrectly bypassed sign-extended source comparison and bounds SW");
}

void blend_and_cache_paths() {
  Fixture f;
  f.actor.spatial_reference = 0U;
  f.actor.position_bits = {};
  f.actor.previous_sequence = 0U;
  f.actor.current_sequence = 1U;
  f.actor.header_cache_key = 7U;
  f.actor.cached_header_bits = {0xdeU, 0xadU, 0xbeU, 0xefU};
  f.actor.phase_bits = 0x3e800000U;
  f.headers = {{0U, 0x4000U, {0x40000000U, 0x40800000U, 0x41000000U, 0x40000000U}},
               {1U, 0x5000U, {0x40c00000U, 0x41400000U, 0x41800000U, 0x40c00000U}}};
  const auto mixed = execute_rac_moby_post_v1(f.actor, f.bindings(), nullptr, nullptr, kPostLimits);
  check(mixed.blend_calls == 1U && mixed.scale_calls == 1U && mixed.derived_calls == 1U &&
            mixed.actor.derived_vector_bits == RacMobyPostVectorV1{
                0x40400000U, 0x40c00000U, 0x41200000U, 0x40400000U} &&
            mixed.actor.cached_header_bits == f.actor.cached_header_bits &&
            mixed.actor.header_cache_key == 7U,
        "Ordered three-FMAC blend or its preserved cache/W contract differs");
  f.actor.previous_sequence = 255U;
  f.actor.previous_frame = 3U;
  f.snapshots = {{3U, f.headers[0U].header_bits}};
  f.headers.erase(f.headers.begin());
  const auto snapshot = execute_rac_moby_post_v1(f.actor, f.bindings(), nullptr, nullptr, kPostLimits);
  check(snapshot.actor.derived_vector_bits == mixed.actor.derived_vector_bits,
        "PreviousFF failed to consume its actual snapshot vector in the same blend");
  f.actor.previous_sequence = 1U;
  const auto cold = execute_rac_moby_post_v1(f.actor, f.bindings(), nullptr, nullptr, kPostLimits);
  check(cold.blend_calls == 0U && cold.actor.header_cache_key == 1U &&
            cold.actor.cached_header_bits == f.headers[0U].header_bits && cold.writes.size() == 7U &&
            std::get<RacMobyPostWriteV1>(cold.writes[0U]).field == RacMobyPostFieldV1::header_cache_key &&
            std::get<RacMobyPostWriteV1>(cold.writes[1U]).field == RacMobyPostFieldV1::cached_header,
        "Cold late sequence1 was replaced by a cached or authored sequence0 shortcut");
}

void failure_preserves_real_owners() {
  for (unsigned failure = 0U; failure < 10U; ++failure) {
    Fixture f;
    auto spatial = index();
    auto owner = f.owner(spatial);
    auto limits = kPostLimits;
    switch (failure) {
    case 0U: ++owner.expected_revision; break;
    case 1U: owner.source_actor_address_bits += 256U; break;
    case 2U: ++owner.source_live_index_bits; break;
    case 3U: ++owner.member_token; break;
    case 4U:
      f.actor.live_index_bits = 0x1002aU;
      owner.source_live_index_bits = f.actor.live_index_bits;
      break;
    case 5U: f.actor.packed_bounds_bits = 0x01010101U; break; // missing old owner
    case 6U:
      spatial.set_membership(42U, kOld, spatial.revision());
      owner.expected_revision = spatial.revision();
      f.actor.packed_bounds_bits = 0x02020202U; break; // wrong actual old rectangle
    case 7U: f.actor.packed_bounds_bits = 0x01010103U; break; // inverted old rectangle
    case 8U:
      f.actor.cached_header_bits[3U] = 0x49000000U;
      f.actor.position_bits[0U] = 0x44000000U;
      f.actor.position_bits[1U] = 0x44000000U; break; // active maximum64
    case 9U: limits.max_recorded_writes = 5U; break; // last packed SW exceeds budget
    }
    const auto before_actor = f.actor;
    const auto before_index = spatial.snapshot();
    rejects([&] { static_cast<void>(execute_rac_moby_post_v1(
        f.actor, f.bindings(), nullptr, &owner, limits)); });
    check(f.actor == before_actor && spatial.snapshot() == before_index,
          "Late post failure published partial actor/index storage");
  }
  Fixture f;
  auto full = make_cleared_ordered_spatial_index_v1({64U, 64U, 32U}, kIndexLimits);
  full.allocation_words[0U] = 0xffffffffU; // explicit external reservations
  OrderedSpatialIndexV1 exhausted(full, kIndexLimits);
  auto owner = f.owner(exhausted);
  const auto original = f.actor;
  rejects([&] { static_cast<void>(execute_rac_moby_post_v1(
      f.actor, f.bindings(), nullptr, &owner, kPostLimits)); });
  check(exhausted.snapshot() == full && f.actor == original,
        "Exhausted actual pool was cleared, extended or partially changed");
  auto small = OrderedSpatialIndexV1(
      make_cleared_ordered_spatial_index_v1({32U, 32U, 32U}, kIndexLimits), kIndexLimits);
  auto small_owner = f.owner(small);
  const auto small_before = small.snapshot();
  rejects([&] { static_cast<void>(execute_rac_moby_post_v1(
      f.actor, f.bindings(), nullptr, &small_owner, kPostLimits)); });
  check(small.snapshot() == small_before, "Post silently substituted a different source grid");
  rejects([&] { static_cast<void>(execute_rac_moby_post_v1(
      f.actor, f.bindings(), nullptr, nullptr, kPostLimits)); });
}

void actual_source_rotation(const std::filesystem::path &path) {
  const auto size = std::filesystem::file_size(path);
  check(size && size <= 32U * 1024U * 1024U, "Source ELF exceeds its test bound");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  std::ifstream input(path, std::ios::binary);
  check(static_cast<bool>(input.read(reinterpret_cast<char *>(bytes.data()), bytes.size())),
        "Cannot read owned source ELF");
  const RacMobyRotationSourceV1 rotation(bytes);
  Fixture f;
  f.actor.flags = 0U;
  f.actor.rotation_bits = {0x3f000000U, 0xbe800000U, 0x3f800000U, 0xffffffffU};
  auto spatial = index();
  auto owner = f.owner(spatial);
  const auto evaluated = rotation.evaluate({f.actor.rotation_bits});
  const auto result = execute_rac_moby_post_v1(
      f.actor, f.bindings(), &rotation, &owner, kPostLimits);
  check(result.rotation_calls == 1U && result.rotation_instruction_pairs == evaluated.executed_instruction_pairs &&
            result.actor.matrix_bits == evaluated.rotation.columns && result.spatial &&
            result.spatial->new_rectangle == kNew && spatial.members_at(2U, 3U)[0U] == 42U,
        "Original nonzero d18 did not compose through the real spatial tail");
  for (const auto warning : evaluated.warnings)
    check(std::ranges::find(result.warnings, warning) != result.warnings.end(),
          "Composed post lost a rotation reference qualification");
  std::cout << "Original nonzero d18 through completed spatial tail passed; pairs="
            << result.rotation_instruction_pairs << '\n';
}

} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 1 || argc == 2, "Expected an optional owned original ELF path");
    no_unvisited_owner_or_numeric_shortcut();
    actual_fresh_and_shared_membership();
    real_inactive_tail_and_same_negative_word();
    blend_and_cache_paths();
    failure_preserves_real_owners();
    if (argc == 2) actual_source_rotation(std::filesystem::path(argv[1]));
    else std::cout << "Original d18 integration case not requested; cached/early-return paths executed\n";
    std::cout << "Composed Moby post numeric stages, real spatial ownership and atomic failures passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Composed Moby post test failed: " << error.what() << '\n';
    return 1;
  }
}
