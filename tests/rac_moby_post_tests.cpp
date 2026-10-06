#include "openrc/rac_moby_post.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <variant>
#include <vector>

namespace {
using namespace openrc;
constexpr RacMobyPostLimitsV1 kLimits{256U, 256U, 7U};
constexpr std::uint32_t kActorAddress = 0x300000U;
constexpr RacMobyPostMatrixV1 kMatrix{{{0x3f800000U, 0U, 0U, 0x11111111U},
                                       {0U, 0x3f800000U, 0U, 0x22222222U},
                                       {0U, 0U, 0x3f800000U, 0x33333333U}}};
constexpr RacMobyPostMatrixV1 kRotationMatrix{{{0x3f800000U, 0U, 0U, 0U},
                                               {0U, 0x3f800000U, 0U, 0U},
                                               {0U, 0U, 0x3f800000U, 0U}}};

void expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename T> const T &request(const RacMobyPostV1 &post) {
  const auto *result = std::get_if<T>(&post.continuation());
  expect(result != nullptr, "unexpected exact post continuation stage");
  return *result;
}

template <typename Callable> void error(Callable &&callable) {
  bool failed = false;
  try {
    callable();
  } catch (const RacMobyPostError &) {
    failed = true;
  }
  expect(failed,
         "unresolved/invalid post must fail without fabricated success");
}

template <typename Callable>
void unchanged_error(RacMobyPostV1 &post, Callable &&callable) {
  const auto actor = post.staged_actor();
  const auto continuation = post.continuation();
  const std::vector<RacMobyPostWriteV1> writes(post.writes().begin(),
                                               post.writes().end());
  error(callable);
  expect(post.staged_actor() == actor && post.continuation() == continuation &&
             std::vector<RacMobyPostWriteV1>(post.writes().begin(),
                                             post.writes().end()) == writes,
         "failed/repeated response changed staged fields, writes or pending "
         "request");
}

struct Fixture {
  RacMobyPostActorV1 actor;
  std::vector<RacMobyPostSequenceHeaderV1> headers{
      {0U, 0x500000U, {0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U}},
      {1U, 0x500010U, {0x55555555U, 0x66666666U, 0x77777777U, 0x88888888U}},
      {255U, 0x500020U, {0x99999999U, 0xaaaaaaaaU, 0xbbbbbbbbU, 0xccccccccU}}};
  std::vector<RacMobyPostSnapshotVectorV1> snapshots{
      {7U, {0xabcd0000U, 0xabcd0001U, 0xabcd0002U, 0xabcd0003U}}};

  Fixture() {
    actor.flags = 0x100U;
    actor.model_reference = 0x400000U;
    actor.scale_bits = 0x3e155555U;
    actor.header_cache_key = 255U;
    actor.cached_header_bits = {0x12345678U, 0x23456789U, 0x3456789aU,
                                0x456789abU};
    actor.rotation_bits = {0x3f800000U, 0xbf800000U, 0x40490fdaU, 0x87654321U};
    actor.position_bits = {0x41a00000U, 0x41a00000U, 0x41a00000U, 0x99999999U};
    actor.matrix_bits = kMatrix;
    actor.derived_vector_bits = {1U, 2U, 3U, 4U};
    actor.counter_word_bits = 0xabcdffffU;
    actor.packed_bounds_bits = 0x80807f7fU;
    actor.live_index_bits = 0x12345678U;
  }

  RacMobyPostBindingsV1 bindings() const {
    return {kActorAddress, headers, snapshots};
  }
};

void test_negative_gate_and_unreached_inputs() {
  Fixture f;
  f.headers.clear();
  f.snapshots.clear();
  f.actor.model_reference = 0x7fffffffU;
  f.actor.flags = 0x8000U;
  for (std::uint32_t value = 128U; value <= 255U; ++value) {
    f.actor.state_byte = static_cast<std::uint8_t>(value);
    RacMobyPostV1 post(f.actor, f.bindings(), kLimits);
    expect(request<RacMobyPostReturnedV1>(post).reason ==
                   RacMobyPostReturnReasonV1::negative_state &&
               post.staged_actor() == f.actor && post.writes().empty(),
           "signed state<0 must bypass all model/header/numeric/spatial work");
    unchanged_error(post,
                    [&] { post.resume(RacMobyPostRotationResultV1{kMatrix}); });
  }
  f.actor.state_byte = 127U;
  RacMobyPostV1 active(f.actor, f.bindings(), kLimits);
  expect(request<RacMobyPostRotationRequestV1>(active).rotation_bits ==
                 f.actor.rotation_bits &&
             active.writes().empty(),
         "positive signed state must request the real rotation even with "
         "missing later inputs");
}

void test_late_sequence1_and_authored_sequence0_cache_order() {
  Fixture f;
  for (std::uint8_t sequence : {std::uint8_t{0U}, std::uint8_t{1U}}) {
    f.actor.previous_sequence = sequence;
    f.actor.current_sequence = sequence;
    f.actor.flags = 0U;
    RacMobyPostV1 post(f.actor, f.bindings(), kLimits);
    expect(std::holds_alternative<RacMobyPostRotationRequestV1>(
               post.continuation()) &&
               post.writes().empty() && post.staged_actor() == f.actor,
           "no zero-only or assumed identity rotation success is allowed");
    post.resume(RacMobyPostRotationResultV1{
        kRotationMatrix}); // explicit synthetic external binding, not
                           // calculated rotation
    const auto &scale = request<RacMobyPostScaleRequestV1>(post);
    expect(
        scale.header_bits == f.headers[sequence].header_bits &&
            scale.scale_bits == f.actor.scale_bits &&
            scale.position_bits == f.actor.position_bits &&
            post.staged_actor().header_cache_key == sequence &&
            post.staged_actor().cached_header_bits ==
                f.headers[sequence].header_bits,
        "cold late sequence1 must use header1, not a baked authored header0");
    const std::array expected{
        RacMobyPostFieldV1::header_cache_key, RacMobyPostFieldV1::cached_header,
        RacMobyPostFieldV1::matrix0, RacMobyPostFieldV1::matrix1,
        RacMobyPostFieldV1::matrix2};
    expect(post.writes().size() == expected.size(),
           "missing cache/matrix stores");
    for (std::size_t i = 0U; i < expected.size(); ++i)
      expect(post.writes()[i].field == expected[i],
             "source cache-before-matrix write order differs");
    unchanged_error(post,
                    [&] { post.resume(RacMobyPostRotationResultV1{kMatrix}); });
  }
}

void test_cached_equal_ff_and_value_owned_inputs() {
  Fixture f;
  f.actor.previous_sequence = 255U;
  f.actor.current_sequence = 255U;
  f.actor.model_reference = 0x7fffffffU;
  f.headers.clear();
  f.snapshots.clear();
  RacMobyPostV1 cached(f.actor, f.bindings(), kLimits);
  expect(request<RacMobyPostScaleRequestV1>(cached).header_bits ==
                 f.actor.cached_header_bits &&
             cached.writes().size() == 3U &&
             cached.staged_actor().header_cache_key == 255U,
         "equalFF/cacheFF must read cached vector, not a header/snapshot or "
         "invalid model");
  Fixture owned;
  owned.actor.flags = 0U;
  const auto original = owned.headers[0U].header_bits;
  RacMobyPostV1 post(owned.actor, owned.bindings(), kLimits);
  owned.headers[0U].header_bits.fill(0U);
  owned.headers.clear();
  owned.snapshots.clear();
  post.resume(RacMobyPostRotationResultV1{kRotationMatrix});
  expect(request<RacMobyPostScaleRequestV1>(post).header_bits == original,
         "continuation must own its resolved current values, not retain "
         "dangling input spans");
}

void test_regular_and_snapshot_blend_boundaries() {
  Fixture f;
  f.actor.previous_sequence = 0U;
  f.actor.current_sequence = 255U;
  f.actor.phase_bits = 0x7fc12345U;
  RacMobyPostV1 regular(f.actor, f.bindings(), kLimits);
  const auto &blend = request<RacMobyPostBlendRequestV1>(regular);
  expect(blend.previous_header_bits == f.headers[0U].header_bits &&
             blend.current_header_bits == f.headers[2U].header_bits &&
             blend.phase_bits == f.actor.phase_bits && regular.writes().empty(),
         "differing regular/currentFF must request source3FMAC without "
         "touching cache");
  const RacMobyPostVectorV1 supplied{0x80000000U, 0x7fffffffU, 0xffffffffU,
                                     0x3f800000U};
  regular.resume(RacMobyPostBlendResultV1{supplied});
  expect(request<RacMobyPostScaleRequestV1>(regular).header_bits == supplied &&
             regular.staged_actor().header_cache_key ==
                 f.actor.header_cache_key &&
             regular.staged_actor().cached_header_bits ==
                 f.actor.cached_header_bits &&
             regular.writes().size() == 3U,
         "blend result must not update the equal-sequence cache");
  f.actor.previous_sequence = 255U;
  f.actor.current_sequence = 1U;
  f.actor.previous_frame = 7U;
  f.headers.erase(f.headers.begin() +
                  2); // no previousFF sequence header required
  RacMobyPostV1 snapshot(f.actor, f.bindings(), kLimits);
  expect(
      request<RacMobyPostBlendRequestV1>(snapshot).previous_header_bits ==
          f.snapshots[0U].vector_bits,
      "previousFF must read vector197080+frame*16, never frame snapshot18f040");
  f.snapshots.clear();
  error([&] { RacMobyPostV1 missing(f.actor, f.bindings(), kLimits); });
}

void test_negation_only_xyz_and_typed_responses() {
  Fixture f;
  f.actor.flags = 0x8100U;
  f.actor.matrix_bits[1U] = {0x3f800000U, 0xbf800000U, 0x80000000U,
                             0x7fc12345U};
  RacMobyPostV1 post(f.actor, f.bindings(), kLimits);
  expect(
      post.staged_actor().matrix_bits[1U] ==
              RacMobyPostVectorV1{0xbf800000U, 0x3f800000U, 0U, 0x7fc12345U} &&
          post.staged_actor().matrix_bits[0U] == f.actor.matrix_bits[0U] &&
          post.staged_actor().matrix_bits[2U] == f.actor.matrix_bits[2U],
      "VSUB must negate only row21.xyz using source-reference zero semantics");
  unchanged_error(post, [&] { post.resume(RacMobyPostDerivedResultV1{}); });
  unchanged_error(post, [&] { post.resume(RacMobyPostBlendResultV1{}); });
  post.resume(RacMobyPostScaleResultV1{{0x111U, 0x222U, 0x333U, 0x49800000U},
                                       {0x444U, 0x555U, 0x666U}});
  const auto &derived = request<RacMobyPostDerivedRequestV1>(post);
  expect(derived.columns == post.staged_actor().matrix_bits &&
             derived.scaled_header_bits[3U] == 0x49800000U &&
             derived.scaled_position_bits ==
                 std::array<std::uint32_t, 3U>{0x444U, 0x555U, 0x666U},
         "pending ordered ACC chain lost literal prior stage values");
  unchanged_error(post, [&] { post.resume(RacMobyPostScaleResultV1{}); });
}

void test_derived_w_counter_and_exact_return_gates() {
  struct Case {
    std::uint32_t pointer, old_bounds, radius, center;
    int expected_reason; // -1 means actual spatial tail remains pending
  };
  const std::array cases{
      Case{0U, 0x80807f7fU, 0U, 0x46800000U, 1},
      Case{1U, 0x01010101U, 0U, 0x46800000U, 2},
      Case{1U, 0U, 0x3f800000U, 0U, 3},
      Case{0xffffffffU, 0x80800000U, 0x49800000U, 0x49800000U, -1}};
  for (const auto &test : cases) {
    Fixture f;
    f.actor.spatial_reference = test.pointer;
    f.actor.packed_bounds_bits = test.old_bounds;
    RacMobyPostV1 post(f.actor, f.bindings(), kLimits);
    post.resume(
        RacMobyPostScaleResultV1{{0U, 0U, 0U, test.radius}, {0U, 0U, 0U}});
    post.resume(
        RacMobyPostDerivedResultV1{{test.center, test.center, 0x7fc12345U}});
    const auto &actor = post.staged_actor();
    expect(actor.derived_vector_bits ==
                   RacMobyPostVectorV1{test.center, test.center, 0x7fc12345U,
                                       test.radius} &&
               actor.counter_word_bits == 0xabcd0000U &&
               actor.position_bits == f.actor.position_bits &&
               actor.packed_bounds_bits == test.old_bounds &&
               actor.scale_bits == f.actor.scale_bits &&
               actor.rotation_bits == f.actor.rotation_bits &&
               post.writes()[5U].field == RacMobyPostFieldV1::derived_vector &&
               post.writes()[6U].field == RacMobyPostFieldV1::counter_low16,
           "full derived W/low16 counter/source order or unaffected actor "
           "field differs");
    if (test.expected_reason >= 0) {
      expect(static_cast<int>(request<RacMobyPostReturnedV1>(post).reason) ==
                 test.expected_reason,
             "source null/unchanged/minimum return gate differs");
    } else {
      const auto &spatial = request<RacMobyPostSpatialRequestV1>(post);
      expect(spatial.old_packed_bits == 0x80800000U &&
                 spatial.new_packed_bits == 0x80800000U &&
                 spatial.live_index_bits == f.actor.live_index_bits,
             "full64 old signextension must request spatial tail despite equal "
             "low32");
      expect(
          !RacMobyPostV1::executes_spatial_tail &&
              !RacMobyPostV1::executes_unresolved_numeric_operations,
          "pending requests must never claim actual spatial/numeric execution");
    }
    unchanged_error(post, [&] { post.resume(RacMobyPostDerivedResultV1{}); });
  }
}

void test_owned_domains_and_atomic_write_budget() {
  Fixture f;
  error([&] { RacMobyPostV1 p(f.actor, f.bindings(), {0U, 256U, 7U}); });
  error([&] { RacMobyPostV1 p(f.actor, f.bindings(), {2U, 256U, 7U}); });
  auto b = f.bindings();
  b.source_actor_address_bits += 4U;
  error([&] { RacMobyPostV1 p(f.actor, b, kLimits); });
  b = f.bindings();
  b.source_actor_address_bits = 0xffffff10U;
  error([&] { RacMobyPostV1 p(f.actor, b, kLimits); });
  f.actor.model_reference = 0x7ffffffcU;
  f.actor.previous_sequence = 1U;
  f.actor.current_sequence = 1U;
  error([&] {
    RacMobyPostV1 p(f.actor, f.bindings(), kLimits);
  }); // real ADD traps
  f.actor.previous_sequence = 255U;
  f.actor.current_sequence = 1U;
  f.actor.previous_frame = 7U;
  error([&] {
    RacMobyPostV1 p(f.actor, f.bindings(), kLimits);
  }); // desired ADD in delay before snapshot
  f = Fixture{};
  f.actor.model_reference = kActorAddress - 0x48U;
  error([&] {
    RacMobyPostV1 p(f.actor, f.bindings(), kLimits);
  }); // table read aliases actor
  f = Fixture{};
  f.headers[0U].header_reference = kActorAddress + 0xf0U;
  error([&] { RacMobyPostV1 p(f.actor, f.bindings(), kLimits); });
  f.headers[0U].header_reference = 0x500004U;
  error([&] { RacMobyPostV1 p(f.actor, f.bindings(), kLimits); });
  f = Fixture{};
  f.actor.previous_sequence = 255U;
  f.actor.current_sequence = 1U;
  f.actor.previous_frame = 7U;
  b = f.bindings();
  b.source_actor_address_bits = 0x197080U + 7U * 16U;
  error([&] {
    RacMobyPostV1 p(f.actor, b, kLimits);
  }); // snapshot-vector owner aliases actor
  f = Fixture{};
  f.headers[1U].sequence_index = 0U;
  error([&] { RacMobyPostV1 p(f.actor, f.bindings(), kLimits); });
  f = Fixture{};
  f.actor.flags = 0U;
  RacMobyPostV1 small(f.actor, f.bindings(), {256U, 256U, 4U});
  unchanged_error(small, [&] {
    small.resume(RacMobyPostRotationResultV1{kRotationMatrix});
  });
  RacMobyPostV1 late_limit(f.actor, f.bindings(), {256U, 256U, 6U});
  late_limit.resume(RacMobyPostRotationResultV1{kRotationMatrix});
  late_limit.resume(RacMobyPostScaleResultV1{});
  unchanged_error(late_limit,
                  [&] { late_limit.resume(RacMobyPostDerivedResultV1{}); });
}

void test_rotation_w_invariant_and_cached_matrix_distinction() {
  Fixture f;
  f.actor.flags = 0U;
  RacMobyPostV1 post(f.actor, f.bindings(), kLimits);
  for (unsigned column = 0U; column < 3U; ++column) {
    for (const auto invalid : {1U, 0x80000001U, 0x3f800000U, 0x7fc12345U}) {
      auto matrix = kRotationMatrix;
      matrix[column][3U] = invalid;
      unchanged_error(
          post, [&] { post.resume(RacMobyPostRotationResultV1{matrix}); });
    }
  }
  auto signed_zero = kRotationMatrix;
  signed_zero[0U][3U] = 0x80000000U;
  signed_zero[2U][3U] = 0x80000000U;
  post.resume(RacMobyPostRotationResultV1{signed_zero});
  expect(post.staged_actor().matrix_bits == signed_zero,
         "zero magnitude does not prove the unknown full ACC zero sign");
  f.actor.flags = 0x100U;
  RacMobyPostV1 cached(f.actor, f.bindings(), kLimits);
  expect(cached.staged_actor().matrix_bits == kMatrix,
         "cached matrix W is a separate source read and must remain raw");
}

void test_reached_shared_read_bytes_must_agree() {
  Fixture f;
  f.actor.flags = 0U;
  f.actor.current_sequence = 1U;
  f.headers[1U].header_reference = f.headers[0U].header_reference;
  RacMobyPostV1 contradiction(f.actor, f.bindings(), kLimits);
  unchanged_error(contradiction, [&] {
    contradiction.resume(RacMobyPostRotationResultV1{kRotationMatrix});
  });
  f.headers[1U].header_bits = f.headers[0U].header_bits;
  RacMobyPostV1 shared(f.actor, f.bindings(), kLimits);
  shared.resume(RacMobyPostRotationResultV1{kRotationMatrix});
  const auto &blend = request<RacMobyPostBlendRequestV1>(shared);
  expect(blend.previous_header_bits == blend.current_header_bits,
         "consistent same-header pointer sharing must remain supported");

  f.actor.flags = 0x100U;
  f.actor.previous_sequence = 255U;
  f.actor.previous_frame = 7U;
  f.headers[1U].header_reference = 0x197080U + 7U * 16U;
  error([&] { RacMobyPostV1 p(f.actor, f.bindings(), kLimits); });
  f.headers[1U].header_bits = f.snapshots[0U].vector_bits;
  RacMobyPostV1 snapshot_alias(f.actor, f.bindings(), kLimits);
  expect(
      request<RacMobyPostBlendRequestV1>(snapshot_alias).current_header_bits ==
          f.snapshots[0U].vector_bits,
      "consistent header/snapshot vector sharing must remain supported");

  // Sequence0's LW address is 400048, inside an aligned header at400040.
  f = Fixture{};
  f.headers[0U].header_reference = 0x400040U;
  error([&] { RacMobyPostV1 p(f.actor, f.bindings(), kLimits); });
  f.headers[0U].header_bits[2U] = 0x400040U;
  RacMobyPostV1 table_alias(f.actor, f.bindings(), kLimits);
  expect(request<RacMobyPostScaleRequestV1>(table_alias).header_bits ==
             f.headers[0U].header_bits,
         "shared model-table word must match the same header bytes");
  // A contradictory bank entry that is never read cannot override cache.
  f.actor.header_cache_key = 0U;
  f.headers[0U].header_bits[2U] ^= 1U;
  RacMobyPostV1 unreached(f.actor, f.bindings(), kLimits);
  expect(request<RacMobyPostScaleRequestV1>(unreached).header_bits ==
             f.actor.cached_header_bits,
         "cache hit must not dereference an unused contradictory header");
}

void test_reached_scale_reference_execution() {
  struct Case {
    std::uint32_t scale;
    RacMobyPostVectorV1 header, expected;
    RacMobyPostVectorV1 position;
    std::array<std::uint32_t, 3U> expected_position;
  };
  const std::array cases{
      Case{0x40000000U,
           {0x3f800000U, 0xbf800000U, 0x3f000000U, 0x40800000U},
           {0x40000000U, 0xc0000000U, 0x3f800000U, 0x41000000U},
           {0x3f800000U, 0xbf800000U, 0x3f000000U, 0x7fc12345U},
           {0x44800000U, 0xc4800000U, 0x44000000U}},
      Case{0xbf800000U,
           {0x00000001U, 0x80000001U, 0x7f800000U, 0xff800000U},
           {0x80000000U, 0U, 0xff800000U, 0x7f800000U},
           {0x80000001U, 0x7f800000U, 0xff800000U, 0xffffffffU},
           {0x80000000U, 0x7fffffffU, 0xffffffffU}},
      // Ordered reference-network discriminator, not a physical fixture:
      // reversed operands would produce42202003, not42202002.
      Case{0x40a02003U,
           {0x41000000U, 0xc1000000U, 0x41000000U, 0xc1000000U},
           {0x42202002U, 0xc2202002U, 0x42202002U, 0xc2202002U},
           {0U, 0U, 0U, 0x3f800000U},
           {0U, 0U, 0U}}};
  for (const auto &test : cases) {
    Fixture f;
    f.actor.scale_bits = test.scale;
    f.actor.position_bits = test.position;
    f.headers[0U].header_bits = test.header;
    RacMobyPostV1 post(f.actor, f.bindings(), kLimits);
    const auto before = post.staged_actor();
    const std::vector<RacMobyPostWriteV1> writes(post.writes().begin(),
                                                 post.writes().end());
    post.evaluate_scale_reference();
    const auto &derived = request<RacMobyPostDerivedRequestV1>(post);
    expect(derived.scaled_header_bits == test.expected &&
               derived.scaled_position_bits == test.expected_position &&
               post.staged_actor() == before &&
               std::vector<RacMobyPostWriteV1>(post.writes().begin(),
                                               post.writes().end()) == writes,
           "actual MUL reference lost operand order, raw domain or untouched "
           "source state");
    unchanged_error(post, [&] { post.evaluate_scale_reference(); });
    unchanged_error(post, [&] { post.resume(RacMobyPostScaleResultV1{}); });
  }
  Fixture f;
  f.actor.flags = 0U;
  RacMobyPostV1 pending_rotation(f.actor, f.bindings(), kLimits);
  unchanged_error(pending_rotation,
                  [&] { pending_rotation.evaluate_scale_reference(); });
}

} // namespace

int main() {
  try {
    test_negative_gate_and_unreached_inputs();
    test_late_sequence1_and_authored_sequence0_cache_order();
    test_cached_equal_ff_and_value_owned_inputs();
    test_regular_and_snapshot_blend_boundaries();
    test_negation_only_xyz_and_typed_responses();
    test_derived_w_counter_and_exact_return_gates();
    test_owned_domains_and_atomic_write_budget();
    test_rotation_w_invariant_and_cached_matrix_distinction();
    test_reached_shared_read_bytes_must_agree();
    test_reached_scale_reference_execution();
    std::cout << "rac_moby_post_tests: PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "rac_moby_post_tests: " << e.what() << '\n';
    return 1;
  }
}
