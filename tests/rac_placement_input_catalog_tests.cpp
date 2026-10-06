#include "openrc/rac_placement_input_catalog.hpp"
#include "openrc/rac_moby_rotation.hpp"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace openrc;

constexpr std::uint64_t kSourceBytes = 64U * 1024U * 1024U;
constexpr RacPlacementInputCatalogLimitsV1 kLimits{
    65536, 4096, 4096, 256, kSourceBytes,
    {kSourceBytes, kSourceBytes, 255, 255}};

void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void put(std::vector<std::byte> &bytes, std::size_t at, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = std::byte((value >> (8U * i)) & 255U);
}

template<class Function> void rejects(Function function, const char *message) {
  try { function(); }
  catch (const RacPlacementInputCatalogError &) { return; }
  throw std::runtime_error(message);
}

RacGameplayMobyInstanceV1 append_record(RacLevelMobyAssetsV1 &assets, std::uint32_t cls) {
  if (assets.gameplay_source_bytes.empty()) {
    assets.gameplay_source_bytes.resize(0x110);
    put(assets.gameplay_source_bytes, 0x44, 0x100);
  }
  RacGameplayMobyInstanceV1 row;
  row.record_range = {assets.gameplay_source_bytes.size(), 0x78};
  row.class_id = cls; row.scale_bits = 0x3f800000; row.scale = 1;
  row.pvar_index = -1; row.group_index = -1; row.authored_reference_index_bits = UINT32_MAX;
  row.authored_color_words = {0x12345678, 0xabcdef99, 0xffffffff};
  row.admission = {-1, 0x80000101, -9, -7, 31};
  assets.gameplay_source_bytes.resize(assets.gameplay_source_bytes.size() + 0x78);
  const auto at = static_cast<std::size_t>(row.record_range.offset);
  put(assets.gameplay_source_bytes, at, 0x78);
  put(assets.gameplay_source_bytes, at + 4, static_cast<std::uint32_t>(row.admission.selector_index));
  put(assets.gameplay_source_bytes, at + 8, row.admission.policy_bits);
  put(assets.gameplay_source_bytes, at + 12, static_cast<std::uint32_t>(row.admission.key_index));
  put(assets.gameplay_source_bytes, at + 16, static_cast<std::uint32_t>(row.admission.primary_count));
  put(assets.gameplay_source_bytes, at + 20, static_cast<std::uint32_t>(row.admission.alternate_count));
  put(assets.gameplay_source_bytes, at + 0x18, cls);
  put(assets.gameplay_source_bytes, at + 0x1c, row.scale_bits);
  put(assets.gameplay_source_bytes, at + 0x48, UINT32_MAX);
  put(assets.gameplay_source_bytes, at + 0x58, UINT32_MAX);
  put(assets.gameplay_source_bytes, at + 0x74, UINT32_MAX);
  for (unsigned i = 0; i < 3; ++i)
    put(assets.gameplay_source_bytes, at + 0x64 + 4 * i, row.authored_color_words[i]);
  assets.gameplay.static_mobies.push_back(row);
  ++assets.gameplay.static_moby_count;
  put(assets.gameplay_source_bytes, 0x100, assets.gameplay.static_moby_count);
  return row;
}

RacLevelMobyModelV1 model(std::uint32_t cls, bool sequence = true) {
  RacLevelMobyModelV1 result;
  result.class_id = cls;
  result.source_bytes.resize(0x100);
  put(result.source_bytes, 0x24, 0x3f800000);
  put(result.source_bytes, 0x3c, 0x3f800000);
  put(result.source_bytes, 0x44, 0x21);
  result.source_bytes[0xc] = std::byte{1};
  if (sequence) {
    put(result.source_bytes, 0x48, 0x50);
    put(result.source_bytes, 0x50, 0x3e800000);
    result.source_bytes[0x60] = std::byte{1};
    result.source_bytes[0x61] = std::byte{255};
    put(result.source_bytes, 0x6c, 0x80); // Ordinary frame is model-relative.
  }
  return result;
}

RacPlacementClassBindingV1 binding(std::uint32_t cls, std::uint8_t bucket,
                                  std::optional<std::uint32_t> source_model) {
  RacPlacementClassBindingV1 result;
  result.class_id = cls; result.class_table_index = bucket;
  result.callback_reference = 0x12345678;
  result.callback_semantic_key = "test/dispatch/" + std::to_string(bucket);
  result.model_index = source_model;
  if (source_model) result.current_model_flags = 0x801;
  return result;
}

void test_complete_order_and_shared_inputs() {
  RacLevelMobyAssetsV1 assets;
  assets.models.push_back(model(7));
  append_record(assets, 99); append_record(assets, 7); append_record(assets, 7);
  std::vector bindings{binding(7, 8, 0), binding(99, 3, std::nullopt)};
  const auto result = compile_rac_placement_input_catalog_v1(assets, bindings, kLimits);
  expect(result.placements.size() == 3 && result.models.size() == 1,
         "All authored rows and one shared model owner must survive compilation");
  expect(result.placements[0].class_binding_index == 1 && !result.placements[0].model_input_index &&
             result.placements[1].source_ordinal == 1 && result.placements[2].source_ordinal == 2 &&
             result.placements[1].model_input_index == result.placements[2].model_input_index,
         "Catalog must preserve source order and genuine null models");
  expect(result.placements[0].authored.authored_color_words == assets.gameplay.static_mobies[0].authored_color_words &&
             result.placements[0].authored.admission == assets.gameplay.static_mobies[0].admission,
         "Raw color/admission fields must not be clamped or interpreted");
  expect(result.models[0].current_flags == 0x801 && result.models[0].sequence0 &&
             result.models[0].sequence0->frame0_owner_offset == 0x80 &&
             result.models[0].sequence0->frame0_source_word == 0x80 &&
             result.models[0].sequence0->header_bits[0] == 0x3e800000,
         "Current shared flags and actual sequence0/frame0 must be retained");
  auto rooted = assets;
  rooted.gameplay.static_mobies[0].rooted = 1;
  put(rooted.gameplay_source_bytes, 0x110 + 0x4c, 1);
  expect(compile_rac_placement_input_catalog_v1(rooted, bindings, kLimits)
             .placements[0].requires_rooted_query,
         "Nonzero rooted input must remain an unexecuted required query");
  auto missing = bindings; missing.pop_back();
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(assets, missing, kLimits); },
          "Unresolved classes must never become null models");
  auto bad = assets; bad.gameplay.static_mobies[0].mode_bits = 1;
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(bad, bindings, kLimits); },
          "Parsed/source disagreement must fail");
  bad = assets; std::swap(bad.gameplay.static_mobies[0], bad.gameplay.static_mobies[1]);
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(bad, bindings, kLimits); },
          "Reordered records must fail before a catalog is published");
  bad = assets; bad.gameplay.static_mobies.erase(bad.gameplay.static_mobies.begin());
  --bad.gameplay.static_moby_count;
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(bad, bindings, kLimits); },
          "Missing prefix with adjusted parsed count must not form a complete catalog");
  bad = assets; bad.gameplay.static_mobies.pop_back(); --bad.gameplay.static_moby_count;
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(bad, bindings, kLimits); },
          "Missing suffix with adjusted parsed count must not form a complete catalog");
  bad = assets; ++bad.gameplay.spawnable_moby_count;
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(bad, bindings, kLimits); },
          "Parsed spawnable count must bind actual source header");
  auto duplicate = bindings; duplicate.push_back(bindings.front());
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(assets, duplicate, kLimits); },
          "Duplicate registration must fail");
  auto changed = bindings; changed[0].callback_semantic_key.clear();
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(assets, changed, kLimits); },
          "Unresolved callback identity must fail");
  changed = bindings; changed[0].current_model_flags.reset();
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(assets, changed, kLimits); },
          "Missing current flags must not fall back to file defaults");
  changed = bindings; changed[1].model_index = 0; changed[1].current_model_flags = 0x801;
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(assets, changed, kLimits); },
          "A second class may not borrow a different class model through the cache");
  auto low = kLimits; low.max_placements = 2;
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(assets, bindings, low); },
          "Catalog placement bounds must be enforced");
}

void test_external_sequence_slot_resolution() {
  RacLevelMobyAssetsV1 assets;
  assets.models.push_back(model(0, false));
  append_record(assets, 0);
  auto initial = binding(0, 0, 0); initial.apply_external_sequence_table = true;
  std::array bindings{initial};
  assets.level_core_source_bytes.resize(0x280);
  assets.level_core.ratchet_sequence_offsets[0] = 0x200;
  // The sorted extent vector's first element is NOT logical sequence slot0.
  assets.level_core.ratchet_sequences = {{0x100, {0x100, 0x40}}, {0x200, {0x200, 0x40}}};
  put(assets.level_core_source_bytes, 0x200, 0x3f000000);
  assets.level_core_source_bytes[0x210] = std::byte{1};
  assets.level_core_source_bytes[0x211] = std::byte{0x80};
  put(assets.level_core_source_bytes, 0x21c, 0x20); // External frame is sequence-relative.
  auto result = compile_rac_placement_input_catalog_v1(assets, bindings, kLimits);
  expect(result.models[0].sequence0 && result.models[0].sequence0->range.offset == 0x200 &&
             result.models[0].sequence0->frame0_owner_offset == 0x220 &&
             result.models[0].sequence0->sound_byte == 0x80 &&
             result.models[0].sequence0->owner == RacPlacementSequenceOwnerV1::decoded_level_core,
         "External logical slot0 must resolve its actual extent/frame basis");
  auto missing = assets; missing.level_core.ratchet_sequences.pop_back();
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(missing, bindings, kLimits); },
          "Missing external source is unresolved, not null sequence");
  auto malformed = assets; put(malformed.level_core_source_bytes, 0x21c, 0xf0000020);
  rejects([&] { (void)compile_rac_placement_input_catalog_v1(malformed, bindings, kLimits); },
          "Unsupported frame pointer bits must fail closed");
  assets.level_core.ratchet_sequence_offsets[0] = 0;
  assets.models[0] = model(0);
  result = compile_rac_placement_input_catalog_v1(assets, bindings, kLimits);
  expect(result.models[0].sequence0 && result.models[0].sequence0->frame0_owner_offset == 0x80,
         "Zero external word must preserve the actual ordinary sequence");
  assets.models[0] = model(0, false);
  expect(!compile_rac_placement_input_catalog_v1(assets, bindings, kLimits).models[0].sequence0,
         "Genuine ordinary/external null sequence must remain absent");
  std::vector<std::byte> unqualified_overlay(32);
  rejects([&] { (void)resolve_rac_veldin_initial_placement_bindings_v1(assets, unqualified_overlay, kLimits); },
          "Actual producer resolver must reject an unqualified overlay");
}

// Same explicit source policy used by native_game_prepare.cpp. This optional
// route reads user-owned data only; no copyrighted bytes enter the test source.
RacLevelMobyAssetLimitsV1 source_limits() {
  constexpr std::uint64_t pixels = 16U * 1024U * 1024U;
  return {kSourceBytes, kSourceBytes, kSourceBytes, 4096, 65536, 1000000, 1000000,
      {kSourceBytes,kSourceBytes,kSourceBytes,4096,255,4096,4096,4096,255,255},
      {kSourceBytes,65536,1000000,4000000,1000000,16000000,16000000,65536,4000000,4000000},
      {kSourceBytes}, {kSourceBytes,false}, {kSourceBytes,true},
      {{kSourceBytes,4096,4096,4096,1000000,4096,1000000},4096,1000000,1000000},
      {kSourceBytes,kSourceBytes,kSourceBytes,255,4096,4096,pixels,pixels,pixels*4},
      {kSourceBytes,4096,65536,1000000,1000000,1000000,16},4096,65536,1000000,1000000};
}

std::vector<std::byte> read_source(const std::filesystem::path &path) {
  const auto size = std::filesystem::file_size(path);
  expect(size != 0 && size <= kSourceBytes, "Actual file exceeds the source test bound");
  std::vector<std::byte> result(static_cast<std::size_t>(size));
  std::ifstream input(path, std::ios::binary);
  expect(static_cast<bool>(input.read(reinterpret_cast<char *>(result.data()),
             static_cast<std::streamsize>(result.size()))), "Cannot read actual source file");
  return result;
}

void test_actual_source(const std::filesystem::path &image, const std::filesystem::path &overlay_path,
                       const std::optional<std::filesystem::path> &boot_path) {
  const auto overlay = read_source(overlay_path);
  const auto assets = load_rac_level_moby_assets_v1(image, 0, source_limits());
  const auto bindings = resolve_rac_veldin_initial_placement_bindings_v1(assets, overlay, kLimits);
  const auto catalog = compile_rac_placement_input_catalog_v1(assets, bindings, kLimits);
  std::set<std::uint32_t> classes;
  std::uint32_t bound = 0, nulls = 0, headers = 0, external = 0, rooted = 0;
  for (const auto &row : catalog.placements) {
    classes.insert(row.authored.class_id);
    if (row.model_input_index) ++bound; else ++nulls;
    rooted += row.requires_rooted_query ? 1U : 0U;
    expect(row.source_ordinal < assets.gameplay.static_mobies.size() &&
               row.authored.record_range.offset == assets.gameplay.static_mobies[row.source_ordinal].record_range.offset,
           "Actual source order changed");
  }
  for (const auto &entry : catalog.models) if (entry.sequence0) {
    ++headers;
    if (entry.sequence0->owner == RacPlacementSequenceOwnerV1::decoded_level_core) ++external;
  }
  expect(catalog.placements.size() == 296 && classes.size() == 21 && bound == 286 && nulls == 10 &&
             catalog.models.size() == 16 && headers == 16 && external == 1 && rooted == 0,
         "Actual Veldin input catalog census differs from qualified source evidence");
  expect(bindings[83].class_id == 749 && bindings[83].callback_reference != 0 &&
             bindings[106].class_id == 1440 && bindings[106].callback_reference != 0,
         "Actual class registration/callback bindings differ");
  if (boot_path) {
    const auto boot = read_source(*boot_path);
    const RacMobyRotationSourceV1 rotation(boot);
    std::uint32_t evaluated = 0, nonzero = 0;
    std::uint64_t pairs = 0, warnings = 0;
    for (const auto &row : catalog.placements) if (row.model_input_index) {
      const auto &angles = row.authored.rotation_bits;
      // W is not read by source d18; this is not an actor-state seed.
      const auto value = rotation.evaluate({{angles[0], angles[1], angles[2], 0}});
      expect(value.executed_instruction_pairs != 0, "Actual source rotation executed no instructions");
      for (const auto &column : value.rotation.columns)
        expect((column[3] & 0x7fffffffU) == 0, "Actual source rotation returned an invalid W lane");
      pairs += value.executed_instruction_pairs; warnings += value.warnings.size();
      ++evaluated;
      nonzero += angles != std::array<std::uint32_t, 3>{} ? 1U : 0U;
    }
    expect(evaluated == 286 && nonzero == 150, "Actual source rotation input census differs");
    std::cout << "Actual source d18 rotations: evaluated=" << evaluated << " nonzero=" << nonzero
              << " instruction_pairs=" << pairs << " numerical_warnings=" << warnings
              << " spatial_executed=0 construction_executed=0\n";
  }
  std::cout << "Actual placement INPUT catalog: rows=296 used_classes=21 bound=286 null=10 models=16"
               " sequence0_headers=16 external_sequence0=1 rooted_required=0 admission_executed=0"
               " construction_executed=0 world_entered=0\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    test_complete_order_and_shared_inputs();
    test_external_sequence_slot_resolution();
    if (argc != 1) {
      expect((argc == 4 || argc == 5) && std::string(argv[1]) == "--source",
             "Usage: rac_placement_input_catalog_tests [--source ISO OVERLAY [BOOT_ELF]]");
      test_actual_source(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]),
          argc == 5 ? std::optional{std::filesystem::path(argv[4])} : std::nullopt);
    }
    std::cout << "RAC placement input catalog tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "RAC placement input catalog failure: " << error.what() << '\n';
    return 1;
  }
}
