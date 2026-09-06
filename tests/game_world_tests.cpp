#include "openrc/game_world.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void expect(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Callback>
void expect_world_error(Callback &&callback, const std::string &message) {
  try {
    callback();
  } catch (const openrc::game::GameWorldError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_session_level_sequence_and_snapshot() {
  using namespace openrc::game;

  GameSessionV1 session(0x123456789abcdef0U);
  static_assert(noexcept(session.level_instance_sequence()));
  expect(session.level_instance_sequence() == 0U,
         "an unloaded session already has a committed level instance");
  const auto first =
      session.request_level(7U, 3U, LevelRequestReasonV1::new_game);
  expect(first.sequence == 0U && first.level_id == 7U &&
             first.spawn_point_id == 3U &&
             session.pending_level_request() == first,
         "the first level request is not ordered or retained while pending");

  WorldV1 world;
  expect_world_error(
      [&] {
        static_cast<void>(session.request_level(
            12U, std::nullopt, LevelRequestReasonV1::transition));
      },
      "a second level request was issued while one remained pending");
  auto altered_first = first;
  altered_first.level_id = 8U;
  expect_world_error([&] { world.load_level(session, altered_first); },
                     "an altered copy of a pending request was committed");
  world.load_level(session, first);
  expect(world.active_level() == ActiveLevelV1{7U, 3U, 1U} &&
             session.active_level_id() == 7U &&
             session.active_spawn_point_id() == 3U &&
             session.level_instance_sequence() == 1U,
         "the first active level identity is wrong");

  const auto second = session.request_level(12U, std::nullopt,
                                            LevelRequestReasonV1::transition);
  world.load_level(session, second);
  expect(world.active_level() == ActiveLevelV1{12U, std::nullopt, 2U} &&
             session.snapshot() ==
                 GameSessionSnapshotV1{
                     0x123456789abcdef0U,
                     0U,
                     2U,
                     2U,
                     std::nullopt,
                     2U,
                     12U,
                     std::nullopt,
                     std::nullopt,
                 },
         "the persistent session snapshot lost a level transition");

  const GameSessionV1 restored(session.snapshot());
  expect(restored.snapshot() == session.snapshot() &&
             restored.level_instance_sequence() == 2U,
         "a game-session snapshot did not round-trip exactly");
}

void test_entity_lifetime_and_stable_ids() {
  using namespace openrc::game;

  GameSessionV1 session;
  WorldV1 world;
  world.load_level(
      session, session.request_level(2U, 9U, LevelRequestReasonV1::new_game));

  WorldEntityDefinitionV1 definition;
  definition.archetype_id = 55U;
  definition.transform.position = {1.0F, 2.0F, 3.0F};
  const auto first = world.spawn_entity(definition);
  definition.archetype_id = 77U;
  const auto second = world.spawn_entity(definition);
  expect(first.slot == 0U && first.generation == 1U && second.slot == 1U &&
             world.entity_count() == 2U && world.slot_count() == 2U,
         "world entity creation is not deterministic");
  expect(world.entity_at_slot(0U) == world.find_entity(first) &&
             world.find_entity(first)->archetype_id == 55U,
         "stable slot iteration disagrees with entity lookup");

  expect(world.destroy_entity(first) && !world.destroy_entity(first) &&
             world.find_entity(first) == nullptr,
         "a stale entity generation remained valid");
  const auto reused = world.spawn_entity(definition);
  expect(reused.slot == 0U && reused.generation == 2U &&
             reused.level_instance_sequence == first.level_instance_sequence,
         "the lowest free entity slot was not reused with a new generation");

  const auto transition = session.request_level(
      18U, std::nullopt, LevelRequestReasonV1::transition);
  world.load_level(session, transition);
  expect(world.entity_count() == 0U && world.find_entity(reused) == nullptr,
         "an entity ID survived replacement of its level instance");
  const auto next_level_entity = world.spawn_entity(definition);
  expect(next_level_entity.slot == 0U &&
             next_level_entity.level_instance_sequence !=
                 reused.level_instance_sequence,
         "a new level reused an indistinguishable entity ID");
}

void test_transform_and_unloaded_world_validation() {
  using namespace openrc::game;

  WorldV1 world;
  expect_world_error([&] { static_cast<void>(world.spawn_entity({})); },
                     "an entity spawned without a loaded level");

  GameSessionV1 session;
  world.load_level(
      session,
      session.request_level(0U, std::nullopt, LevelRequestReasonV1::developer));
  WorldEntityDefinitionV1 invalid;
  invalid.transform.position[0U] = std::numeric_limits<float>::quiet_NaN();
  expect_world_error([&] { static_cast<void>(world.spawn_entity(invalid)); },
                     "a non-finite entity transform was accepted");
  invalid = {};
  invalid.transform.rotation = {};
  expect_world_error([&] { static_cast<void>(world.spawn_entity(invalid)); },
                     "a zero entity rotation was accepted");
  invalid = {};
  invalid.transform.scale[1U] = 0.0F;
  expect_world_error([&] { static_cast<void>(world.spawn_entity(invalid)); },
                     "a zero entity scale was accepted");

  world.unload_level();
  expect(!world.active_level(), "unload_level retained its active level");
}

void test_replay_tick_validation() {
  using namespace openrc::game;

  GameSessionV1 session;
  GameInputCommandV1 command;
  command.tick_index = 0U;
  command.held_buttons = game_button_mask_v1(GameButtonV1::jump);
  session.commit_simulation_tick(command);
  expect(session.next_tick_index() == 1U,
         "a committed simulation tick did not advance the session");
  expect_world_error([&] { session.commit_simulation_tick(command); },
                     "a duplicate replay tick was accepted");

  command.tick_index = 2U;
  expect_world_error([&] { session.commit_simulation_tick(command); },
                     "a skipped replay tick was accepted");
  command.tick_index = 1U;
  command.held_buttons = 0x80000000U;
  expect_world_error(
      [&] { session.commit_simulation_tick(command); },
      "an invalid replay input command was accepted by the session");
}

void test_snapshot_validation() {
  using namespace openrc::game;

  GameSessionSnapshotV1 invalid;
  invalid.next_level_commit_sequence = 1U;
  expect_world_error([&] { GameSessionV1 session(invalid); },
                     "a session snapshot omitted an unresolved request");
  invalid = {};
  invalid.level_instance_sequence = 1U;
  expect_world_error([&] { GameSessionV1 session(invalid); },
                     "a session snapshot omitted its active level");
  invalid = {};
  invalid.next_level_request_sequence = 1U;
  invalid.next_level_commit_sequence = 1U;
  expect_world_error(
      [&] { GameSessionV1 session(invalid); },
      "a session snapshot accepted inconsistent level-instance numbering");
  invalid = {};
  invalid.active_spawn_point_id = 4U;
  expect_world_error([&] { GameSessionV1 session(invalid); },
                     "a session snapshot accepted a spawn without a level");

  GameSessionV1 session(9U);
  const auto pending =
      session.request_level(4U, 8U, LevelRequestReasonV1::checkpoint_restart);
  const GameSessionV1 restored(session.snapshot());
  expect(restored.pending_level_request() == pending,
         "a pending level request did not survive snapshot restoration");

  GameSessionV1 invalid_request_session;
  expect_world_error(
      [&] {
        static_cast<void>(invalid_request_session.request_level(
            1U, std::nullopt, static_cast<LevelRequestReasonV1>(255U)));
      },
      "an unknown level-request reason was accepted");
}

void test_prepared_persistent_state_survives_level_lifecycle() {
  using namespace openrc;
  using namespace openrc::game;
  constexpr SessionStateLimitsV1 limits{2U,   8U,  64U,  1024U, 64U,
                                        128U, 64U, 256U, 16U};
  SessionStateInitialV1 initial;
  initial.schema.identity_key = "test.session/progress";
  initial.schema.buffers = {{"progress", 8U}};
  initial.schema.views = {
      {"word", "progress", SessionStateValueTypeV1::u32, 0U, 2U, 4U},
      {"key", "progress", SessionStateValueTypeV1::u16, 0U, 1U, 2U},
      {"high-key-byte", "progress", SessionStateValueTypeV1::u8, 1U, 1U, 1U},
  };
  initial.buffers = {
      {"progress",
       {std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78},
        std::byte{0x9a}, std::byte{0xbc}, std::byte{0xde}, std::byte{0xf0}}}};
  const auto source_copy = initial;
  GameSessionV1 session(99U, initial, limits);
  const std::array writes{
      SessionStateWriteV1{"key", 0U, SessionStateValueTypeV1::u16, 0xabcdU},
      SessionStateWriteV1{"high-key-byte", 0U, SessionStateValueTypeV1::u8,
                          0x11U},
  };
  session.apply_persistent_state_writes(writes, 0U);
  expect(session.persistent_state()->read_u32("word", 0U) == 0x785611cdU &&
             initial == source_copy,
         "session-owned aliases did not share bytes or mutated the prepared "
         "initial data");
  const auto persistent = session.snapshot().persistent_state;
  WorldV1 world;
  for (const auto reason :
       {LevelRequestReasonV1::new_game, LevelRequestReasonV1::transition,
        LevelRequestReasonV1::checkpoint_restart}) {
    world.load_level(session, session.request_level(7U, 0U, reason));
    expect(session.snapshot().persistent_state == persistent,
           "a level request/replacement replayed persistent initial values");
  }
  world.unload_level();
  expect(session.snapshot().persistent_state == persistent,
         "world unload erased session progress");
  const auto pending =
      session.request_level(9U, 2U, LevelRequestReasonV1::transition);
  const auto snapshot = session.snapshot();
  GameSessionV1 restored(snapshot, initial.schema, limits);
  expect(restored.level_instance_sequence() == snapshot.level_instance_sequence,
         "metadata-only level identity disagrees with persistent snapshot");
  expect(restored.snapshot() == snapshot &&
             restored.pending_level_request() == pending,
         "persistent snapshot restoration changed bytes/revision or level "
         "metadata");
  expect_world_error([&] { GameSessionV1 missing_schema(snapshot); },
                     "persistent snapshot invented its trusted schema");
  auto wrong_schema = initial.schema;
  wrong_schema.views[1].byte_offset = 2U;
  expect_world_error(
      [&] { GameSessionV1 wrong(snapshot, wrong_schema, limits); },
      "persistent snapshot accepted another field layout");
  auto truncated = snapshot;
  truncated.persistent_state->buffers[0].bytes.pop_back();
  expect_world_error(
      [&] { GameSessionV1 wrong(truncated, initial.schema, limits); },
      "persistent snapshot implicitly filled missing bytes");
  expect_world_error(
      [&] { restored.apply_persistent_state_writes(writes, 0U); },
      "stale persistent writes were accepted by session owner");
  expect(restored.snapshot() == snapshot,
         "failed persistent writes changed session metadata/state");
  restored.apply_persistent_state_writes(
      std::array{
          SessionStateWriteV1{"key", 0U, SessionStateValueTypeV1::u16, 0U}},
      1U);
  expect(session.snapshot() == snapshot && restored.snapshot() != snapshot,
         "restored session shared mutable storage with its original");
  const GameSessionV1 fresh(99U, initial, limits);
  expect(fresh.persistent_state()->read_u32("word", 0U) == 0x78563412U &&
             fresh.persistent_state()->revision() == 0U,
         "explicit new session inherited old mutable progress");
  GameSessionV1 absent;
  expect(!absent.persistent_state() && !absent.snapshot().persistent_state,
         "no-contract session manufactured default progress");
  expect_world_error([&] { absent.apply_persistent_state_writes({}, 0U); },
                     "session without a contract accepted a state operation");
  expect_world_error(
      [&] { GameSessionV1 wrong(absent.snapshot(), initial.schema, limits); },
      "schema-only restoration manufactured a missing state image");
}

} // namespace

int main() {
  try {
    test_session_level_sequence_and_snapshot();
    test_entity_lifetime_and_stable_ids();
    test_transform_and_unloaded_world_validation();
    test_replay_tick_validation();
    test_snapshot_validation();
    test_prepared_persistent_state_survives_level_lifecycle();
    std::cout << "game_world_tests: ok\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "game_world_tests: " << error.what() << '\n';
    return 1;
  }
}
