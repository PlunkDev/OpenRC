#include "openrc/session_state.hpp"

#include "openrc/hash.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace openrc;
using namespace openrc::game;
constexpr SessionStateLimitsV1 kLimits{8U,    32U,   64U,   4096U, 1024U,
                                       4096U, 1024U, 4096U, 32U};

void expect(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_error(Callback &&callback, const std::string_view fragment = {}) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const SessionStateError &error) {
    expect(std::string_view(error.what()).find(fragment) !=
               std::string_view::npos,
           "Unexpected session-state diagnostic");
    return;
  }
  throw std::runtime_error("Malformed session state or operation was accepted");
}

[[nodiscard]] SessionStateInitialV1 fixture() {
  SessionStateInitialV1 initial;
  initial.schema.identity_key = "session.fixture";
  initial.schema.buffers = {{"spare", 3U}, {"shared", 16U}};
  initial.schema.views = {
      {"word", "shared", SessionStateValueTypeV1::u32, 0U, 4U, 4U},
      {"byte", "shared", SessionStateValueTypeV1::u8, 0U, 16U, 1U},
      {"keys", "shared", SessionStateValueTypeV1::u16, 0U, 4U, 4U},
      {"aux", "shared", SessionStateValueTypeV1::u16, 2U, 4U, 4U},
      {"sliding", "shared", SessionStateValueTypeV1::u16, 1U, 3U, 1U},
  };
  initial.buffers = {
      {"spare", {std::byte{0xfeU}, std::byte{0xedU}, std::byte{0xabU}}},
      {"shared",
       {std::byte{0x10U}, std::byte{0x20U}, std::byte{0x30U}, std::byte{0x40U},
        std::byte{0x50U}, std::byte{0x60U}, std::byte{0x70U}, std::byte{0x80U},
        std::byte{0x90U}, std::byte{0xa0U}, std::byte{0xb0U}, std::byte{0xc0U},
        std::byte{0xd0U}, std::byte{0xe0U}, std::byte{0xf0U},
        std::byte{0x11U}}},
  };
  return initial;
}

void apply(SessionStateV1 &state,
           const std::initializer_list<SessionStateWriteV1> writes) {
  state.apply_batch(std::span(writes.begin(), writes.size()), state.revision());
}

void test_canonicalization_ownership_and_reads() {
  auto initial = fixture();
  const auto before = initial;
  const auto canonical =
      canonicalize_session_state_initial_v1(initial, kLimits);
  expect(initial == before, "Canonicalization mutated caller input");
  expect(canonical.schema.buffers.front().key == "shared" &&
             canonical.schema.views.front().key == "aux" &&
             canonical.buffers.front().buffer_key == "shared",
         "Schema or payload images were not ordered by exact opaque keys");
  auto permuted = initial;
  std::ranges::reverse(permuted.schema.buffers);
  std::ranges::reverse(permuted.schema.views);
  std::ranges::reverse(permuted.buffers);
  expect(canonicalize_session_state_initial_v1(permuted, kLimits) ==
                 canonical &&
             hash_session_state_schema_v1(initial.schema, kLimits) ==
                 hash_session_state_schema_v1(permuted.schema, kLimits) &&
             hash_session_state_initial_v1(initial, kLimits) ==
                 hash_session_state_initial_v1(permuted, kLimits),
         "Declaration order changed canonical data or its digest");
  SessionStateV1 state(initial, kLimits);
  initial.buffers[1U].bytes[0U] = std::byte{0U};
  initial.schema.views.clear();
  expect(state.schema() == canonical.schema && state.revision() == 0U,
         "Runtime retained mutable caller-owned schema storage");
  expect(state.read_u8("byte", 0U) == 0x10U &&
             state.read_u16("keys", 0U) == 0x2010U &&
             state.read_u16("aux", 0U) == 0x4030U &&
             state.read_u32("word", 0U) == 0x40302010U &&
             state.read_u32("word", 3U) == 0x11f0e0d0U &&
             state.read_u16("sliding", 0U) == 0x3020U &&
             state.read_u16("sliding", 1U) == 0x4030U,
         "Unsigned/unaligned/strided views lost canonical little-endian bytes");
  expect(state.buffer_bytes("spare")[2U] == std::byte{0xabU},
         "Unviewed bytes were omitted or zero-filled");
  const SessionStateV1 reordered(permuted, kLimits);
  expect(state.snapshot() == reordered.snapshot() &&
             state.state_sha256() == reordered.state_sha256(),
         "Canonical state depends on initial container order");
  expect_error([&] { static_cast<void>(state.read_u8("missing", 0U)); },
               "unknown view");
  expect_error([&] { static_cast<void>(state.buffer_bytes("missing")); },
               "unknown buffer");
  expect_error([&] { static_cast<void>(state.read_u8("word", 0U)); }, "type");
  expect_error([&] { static_cast<void>(state.read_u16("keys", 4U)); }, "index");
  expect_error([&] { static_cast<void>(state.read_u32("word", UINT64_MAX)); },
               "index");
}

void test_shared_aliases_batch_order_and_revision() {
  SessionStateV1 state(fixture(), kLimits);
  const auto schema_digest = state.schema_sha256();
  const auto before_digest = state.state_sha256();
  apply(state, {{"word", 0U, SessionStateValueTypeV1::u32, 0x11223344U},
                {"keys", 0U, SessionStateValueTypeV1::u16, 0xaabbU},
                {"byte", 1U, SessionStateValueTypeV1::u8, 0xccU},
                {"aux", 0U, SessionStateValueTypeV1::u16, 0xddeeU}});
  expect(
      state.revision() == 1U && state.read_u32("word", 0U) == 0xddeeccbbU &&
          state.read_u16("keys", 0U) == 0xccbbU &&
          state.read_u16("aux", 0U) == 0xddeeU &&
          state.read_u16("sliding", 0U) == 0xeeccU &&
          state.buffer_bytes("shared")[1U] == std::byte{0xccU},
      "Aliasing typed views read independent stale caches or lost write order");
  expect(
      state.schema_sha256() == schema_digest &&
          state.state_sha256() != before_digest,
      "Value update changed schema identity or failed to change state digest");
  apply(state, {{"sliding", 0U, SessionStateValueTypeV1::u16, 0xabcdU},
                {"sliding", 1U, SessionStateValueTypeV1::u16, 0x1234U}});
  expect(state.read_u32("word", 0U) == 0x1234cdbbU && state.revision() == 2U,
         "Within-view overlapping stride did not preserve last-write-wins");

  SessionStateV1 reversed(fixture(), kLimits);
  apply(reversed, {{"aux", 0U, SessionStateValueTypeV1::u16, 0xddeeU},
                   {"byte", 1U, SessionStateValueTypeV1::u8, 0xccU},
                   {"keys", 0U, SessionStateValueTypeV1::u16, 0xaabbU},
                   {"word", 0U, SessionStateValueTypeV1::u32, 0x11223344U}});
  expect(reversed.read_u32("word", 0U) == 0x11223344U,
         "Batch incorrectly sorted writes by view identity");
  const auto before_empty = state.snapshot();
  state.apply_batch({}, state.revision());
  expect(state.snapshot() == before_empty,
         "Empty batch changed bytes or revision");
  expect_error([&] { state.apply_batch({}, state.revision() - 1U); },
               "stale revision");
  apply(state,
        {{"byte", 0U, SessionStateValueTypeV1::u8, state.read_u8("byte", 0U)}});
  expect(state.revision() == 3U,
         "Successful same-value source write omitted its revision");
}

void test_atomic_failure_before_any_buffer_write() {
  auto initial = fixture();
  initial.schema.views.push_back(
      {"spare-byte", "spare", SessionStateValueTypeV1::u8, 0U, 1U, 1U});
  SessionStateV1 state(initial, kLimits);
  const auto before = state.snapshot();
  const auto before_digest = state.state_sha256();
  const std::array failures{
      SessionStateWriteV1{"missing", 0U, SessionStateValueTypeV1::u8, 1U},
      SessionStateWriteV1{"byte", 16U, SessionStateValueTypeV1::u8, 1U},
      SessionStateWriteV1{"word", UINT64_MAX, SessionStateValueTypeV1::u32, 1U},
      SessionStateWriteV1{"keys", 0U, SessionStateValueTypeV1::u32, 1U},
      SessionStateWriteV1{"byte", 0U, SessionStateValueTypeV1::u8, 256U},
      SessionStateWriteV1{"keys", 0U, SessionStateValueTypeV1::u16, 65536U},
      SessionStateWriteV1{"word", 0U, static_cast<SessionStateValueTypeV1>(99U),
                          1U},
      SessionStateWriteV1{std::string(65U, 'x'), 0U,
                          SessionStateValueTypeV1::u8, 1U},
  };
  for (const auto &failure : failures) {
    const std::array batch{
        SessionStateWriteV1{"byte", 0U, SessionStateValueTypeV1::u8, 0xffU},
        SessionStateWriteV1{"spare-byte", 0U, SessionStateValueTypeV1::u8,
                            0xffU},
        failure,
    };
    expect_error([&] { state.apply_batch(batch, 0U); });
    expect(state.snapshot() == before && state.state_sha256() == before_digest,
           "Invalid late batch write left a partial multi-buffer change");
  }
  const std::array valid{
      SessionStateWriteV1{"byte", 0U, SessionStateValueTypeV1::u8, 1U}};
  expect_error([&] { state.apply_batch(valid, 1U); }, "stale revision");
  auto limits = kLimits;
  limits.max_batch_writes = 1U;
  SessionStateV1 bounded(initial, limits);
  const std::array too_many{valid[0U], valid[0U]};
  expect_error([&] { bounded.apply_batch(too_many, 0U); }, "count limit");
  expect(bounded.snapshot() == before, "Over-limit batch changed storage");
}

void test_snapshot_roundtrip_no_reset_and_rejection_rollback() {
  const auto initial = fixture();
  SessionStateV1 state(initial, kLimits);
  apply(state, {{"keys", 1U, SessionStateValueTypeV1::u16, 0xfffeU}});
  const auto saved = state.snapshot();
  const auto saved_digest = state.state_sha256();
  const SessionStateV1 restored(initial.schema, saved, kLimits);
  expect(restored.snapshot() == saved &&
             restored.state_sha256() == saved_digest &&
             restored.read_u16("keys", 1U) == 0xfffeU,
         "Snapshot constructor reset current bytes to initial values");
  apply(state, {{"word", 2U, SessionStateValueTypeV1::u32, 0U}});
  state.restore_snapshot(saved);
  expect(state.snapshot() == saved && state.state_sha256() == saved_digest,
         "In-place snapshot restore lost revision or current state");

  std::vector<SessionStateSnapshotV1> malformed;
  auto changed = saved;
  changed.identity_key = "other.session";
  malformed.push_back(changed);
  changed = saved;
  changed.schema_sha256[0U] ^= std::byte{1U};
  malformed.push_back(changed);
  changed = saved;
  changed.schema_sha256 = {};
  malformed.push_back(changed);
  changed = saved;
  changed.buffers.pop_back();
  malformed.push_back(changed);
  changed = saved;
  changed.buffers[1U] = changed.buffers[0U];
  malformed.push_back(changed);
  changed = saved;
  changed.buffers[1U].buffer_key = "unknown";
  malformed.push_back(changed);
  changed = saved;
  changed.buffers[0U].bytes.pop_back();
  malformed.push_back(changed);
  changed = saved;
  changed.buffers[0U].bytes.push_back(std::byte{0U});
  malformed.push_back(changed);
  changed = saved;
  std::ranges::reverse(changed.buffers);
  malformed.push_back(changed);
  for (const auto &invalid : malformed) {
    expect_error([&] { state.restore_snapshot(invalid); });
    expect(state.snapshot() == saved && state.state_sha256() == saved_digest,
           "Malformed snapshot partially restored bytes/revision");
    expect_error([&] {
      static_cast<void>(SessionStateV1(initial.schema, invalid, kLimits));
    });
  }
  auto other_schema = initial.schema;
  other_schema.views[0U].key = "changed-word";
  expect_error(
      [&] { static_cast<void>(SessionStateV1(other_schema, saved, kLimits)); },
      "schema digest");
  changed = saved;
  changed.revision = UINT64_MAX;
  state.restore_snapshot(changed);
  const auto exhausted = state.snapshot();
  state.apply_batch({}, UINT64_MAX);
  expect_error(
      [&] { apply(state, {{"byte", 0U, SessionStateValueTypeV1::u8, 2U}}); },
      "exhausted");
  expect_error([&] { state.apply_batch({}, UINT64_MAX - 1U); },
               "stale revision");
  expect(state.snapshot() == exhausted,
         "Revision overflow changed current storage");
}

void test_complete_images_limits_and_schema_validation() {
  const auto initial = fixture();
  const auto reject_schema = [](SessionStateSchemaV1 candidate,
                                const SessionStateLimitsV1 limits = kLimits) {
    const auto before = candidate;
    expect_error([&] {
      static_cast<void>(
          canonicalize_session_state_schema_v1(candidate, limits));
    });
    expect(candidate == before, "Invalid schema preflight mutated input");
  };
  auto schema = initial.schema;
  schema.buffers.clear();
  reject_schema(schema);
  schema = initial.schema;
  schema.buffers[0U].byte_count = 0U;
  reject_schema(schema);
  schema = initial.schema;
  schema.buffers[1U].key = schema.buffers[0U].key;
  reject_schema(schema);
  schema = initial.schema;
  schema.views[1U].key = schema.views[0U].key;
  reject_schema(schema);
  schema = initial.schema;
  schema.views[0U].buffer_key = "unknown";
  reject_schema(schema);
  schema = initial.schema;
  schema.views[0U].value_type = static_cast<SessionStateValueTypeV1>(99U);
  reject_schema(schema);
  schema = initial.schema;
  schema.views[0U].element_count = 0U;
  reject_schema(schema);
  schema = initial.schema;
  schema.views[0U].byte_stride = 0U;
  reject_schema(schema);
  schema = initial.schema;
  schema.views[0U].byte_offset = UINT64_MAX;
  reject_schema(schema);
  schema = initial.schema;
  schema.views[0U].byte_offset = 13U;
  reject_schema(schema);
  schema = initial.schema;
  schema.views[0U].byte_stride = UINT64_MAX;
  reject_schema(schema);
  for (const auto &bad_key :
       {std::string{}, std::string{"white space"}, std::string{"line\nbreak"},
        std::string(1U, '\0'), std::string(1U, static_cast<char>(0x80U)),
        std::string(65U, 'x')}) {
    schema = initial.schema;
    schema.identity_key = bad_key;
    reject_schema(schema);
  }
  std::vector<SessionStateLimitsV1> bounded;
  bounded.push_back({});
  auto limits = kLimits;
  limits.max_buffers = 1U;
  bounded.push_back(limits);
  limits = kLimits;
  limits.max_views = 4U;
  bounded.push_back(limits);
  limits = kLimits;
  limits.max_key_bytes = 3U;
  bounded.push_back(limits);
  limits = kLimits;
  limits.max_total_key_bytes = 1U;
  bounded.push_back(limits);
  limits = kLimits;
  limits.max_buffer_bytes = 15U;
  bounded.push_back(limits);
  limits = kLimits;
  limits.max_total_buffer_bytes = 18U;
  bounded.push_back(limits);
  limits = kLimits;
  limits.max_view_elements = 15U;
  bounded.push_back(limits);
  limits = kLimits;
  limits.max_total_view_elements = 30U;
  bounded.push_back(limits);
  limits = kLimits;
  limits.max_batch_writes = 0U;
  bounded.push_back(limits);
  for (const auto &bound : bounded) {
    reject_schema(initial.schema, bound);
  }

  auto huge = initial.schema;
  huge.views.resize(1U);
  huge.views[0U].element_count = UINT64_MAX;
  limits = kLimits;
  limits.max_view_elements = UINT64_MAX;
  limits.max_total_view_elements = UINT64_MAX;
  reject_schema(huge, limits);
  huge = initial.schema;
  huge.buffers[0U].byte_count = UINT64_MAX;
  limits = kLimits;
  limits.max_buffer_bytes = UINT64_MAX;
  limits.max_total_buffer_bytes = UINT64_MAX;
  reject_schema(huge, limits);

  std::vector<SessionStateInitialV1> images;
  auto broken = initial;
  broken.buffers.clear();
  images.push_back(broken);
  broken = initial;
  broken.buffers[1U] = broken.buffers[0U];
  images.push_back(broken);
  broken = initial;
  broken.buffers[0U].buffer_key = "unknown";
  images.push_back(broken);
  broken = initial;
  broken.buffers[0U].bytes.clear();
  images.push_back(broken);
  broken = initial;
  broken.buffers[0U].bytes.push_back(std::byte{0U});
  images.push_back(broken);
  broken = initial;
  broken.buffers[0U].bytes.resize(1025U);
  images.push_back(broken);
  for (const auto &invalid : images) {
    const auto before = invalid;
    expect_error([&] {
      static_cast<void>(
          canonicalize_session_state_initial_v1(invalid, kLimits));
    });
    expect_error([&] { static_cast<void>(SessionStateV1(invalid, kLimits)); });
    expect(invalid == before, "Invalid initial images changed caller storage");
  }

  std::uint64_t schema_keys = initial.schema.identity_key.size();
  for (const auto &buffer : initial.schema.buffers) {
    schema_keys += buffer.key.size();
  }
  for (const auto &view : initial.schema.views) {
    schema_keys += view.key.size() + view.buffer_key.size();
  }
  limits = kLimits;
  limits.max_total_key_bytes = schema_keys;
  static_cast<void>(
      canonicalize_session_state_schema_v1(initial.schema, limits));
  expect_error([&] { static_cast<void>(SessionStateV1(initial, limits)); },
               "aggregate");
  for (const auto &image : initial.buffers) {
    limits.max_total_key_bytes += image.buffer_key.size();
  }
  static_cast<void>(SessionStateV1(initial, limits));

  auto no_views = initial;
  no_views.schema.views.clear();
  SessionStateV1 byte_only(no_views, kLimits);
  expect(byte_only.buffer_bytes("shared").size() == 16U,
         "Explicit canonical storage incorrectly required model/rig/views");
  auto one_element = initial;
  one_element.schema.views = {
      {"last", "shared", SessionStateValueTypeV1::u32, 12U, 1U, UINT64_MAX}};
  expect(SessionStateV1(one_element, kLimits).read_u32("last", 0U) ==
             0x11f0e0d0U,
         "One-element stride overflow check rejected a valid final element");
}

void test_schema_and_value_digest_separation() {
  const auto initial = fixture();
  const auto schema_hash =
      hash_session_state_schema_v1(initial.schema, kLimits);
  const auto initial_hash = hash_session_state_initial_v1(initial, kLimits);
  auto changed = initial;
  changed.buffers[0U].bytes[2U] ^= std::byte{1U};
  expect(hash_session_state_schema_v1(changed.schema, kLimits) == schema_hash &&
             hash_session_state_initial_v1(changed, kLimits) != initial_hash &&
             SessionStateV1(changed, kLimits).state_sha256() !=
                 SessionStateV1(initial, kLimits).state_sha256(),
         "Digest ignored unviewed bytes or confused schema with values");
  changed = initial;
  changed.schema.identity_key = "different.identity";
  expect(hash_session_state_schema_v1(changed.schema, kLimits) != schema_hash,
         "Schema digest ignored stable state identity");
  changed = initial;
  changed.schema.views[0U].byte_stride = 3U;
  expect(hash_session_state_schema_v1(changed.schema, kLimits) != schema_hash,
         "Schema digest ignored view stride");
  changed = initial;
  changed.schema.views[0U].element_count = 3U;
  expect(hash_session_state_schema_v1(changed.schema, kLimits) != schema_hash,
         "Schema digest ignored view count");
  changed = initial;
  changed.schema.views[0U].value_type = SessionStateValueTypeV1::u16;
  expect(hash_session_state_schema_v1(changed.schema, kLimits) != schema_hash,
         "Schema digest ignored scalar width");
  auto saved = SessionStateV1(initial, kLimits).snapshot();
  auto revision_only = saved;
  revision_only.revision = 1U;
  expect(
      SessionStateV1(initial.schema, revision_only, kLimits).state_sha256() !=
          SessionStateV1(initial.schema, saved, kLimits).state_sha256(),
      "Snapshot state digest ignored revision");
}

void test_digest_golden_encoding() {
  SessionStateInitialV1 initial;
  initial.schema.identity_key = "golden";
  initial.schema.buffers = {{"a", 3U}};
  initial.schema.views = {{"v", "a", SessionStateValueTypeV1::u16, 1U, 1U, 2U}};
  initial.buffers = {{"a", {std::byte{1U}, std::byte{2U}, std::byte{3U}}}};
  // Independently calculated from the domain-separated little-endian
  // digest streams, not from a writer/reader roundtrip of this implementation.
  expect(hex_digest(hash_session_state_schema_v1(initial.schema, kLimits)) ==
             "8c1bd841ecbd8a674e1e408d148e5bc36c299f5f5a6d4025b449b762940454f1",
         "Schema SHA-256 canonical encoding changed");
  expect(hex_digest(hash_session_state_initial_v1(initial, kLimits)) ==
             "46ba8f2874666bbf982f3522474be67f71391ce566b3c62f5346ffa954c60199",
         "Initial SHA-256 canonical encoding changed");
  expect(hex_digest(SessionStateV1(initial, kLimits).state_sha256()) ==
             "ebceac0dee4276aa4f0f94317394daf533554c4bf4d189b595d81e69c00d4614",
         "Snapshot SHA-256 canonical encoding changed");
}

} // namespace

int main() {
  try {
    test_canonicalization_ownership_and_reads();
    test_shared_aliases_batch_order_and_revision();
    test_atomic_failure_before_any_buffer_write();
    test_snapshot_roundtrip_no_reset_and_rejection_rollback();
    test_complete_images_limits_and_schema_validation();
    test_schema_and_value_digest_separation();
    test_digest_golden_encoding();
    std::cout << "Session state tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Session state test failure: " << error.what() << '\n';
    return 1;
  }
}
