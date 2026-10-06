#include "openrc/rac_placement_input_catalog.hpp"

#include "openrc/hash.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <set>

namespace openrc {
namespace {

void require(bool condition, const char *message) {
  if (!condition) throw RacPlacementInputCatalogError(message);
}

std::span<const std::byte> range(std::span<const std::byte> bytes,
                               std::uint64_t begin, std::uint64_t count) {
  require(begin <= bytes.size() && count <= bytes.size() - begin,
          "Placement input source range leaves its owner");
  return bytes.subspan(static_cast<std::size_t>(begin), static_cast<std::size_t>(count));
}

std::uint32_t word(std::span<const std::byte> bytes, std::uint64_t offset) {
  const auto part = range(bytes, offset, 4);
  std::uint32_t result = 0;
  for (unsigned i = 0; i < 4; ++i)
    result |= std::to_integer<std::uint32_t>(part[i]) << (8U * i);
  return result;
}

std::uint8_t byte(std::span<const std::byte> bytes, std::uint64_t offset) {
  return std::to_integer<std::uint8_t>(range(bytes, offset, 1).front());
}

void validate_limits(const RacPlacementInputCatalogLimitsV1 &limits) {
  require(limits.max_placements && limits.max_classes && limits.max_models &&
              limits.max_key_bytes && limits.max_source_bytes &&
              limits.sequence.max_input_bytes && limits.sequence.max_sequence_bytes &&
              limits.sequence.max_frames && limits.sequence.max_trigger_words,
          "Placement input catalog requires explicit nonzero limits");
}

void validate_assets(const RacLevelMobyAssetsV1 &assets,
                     const RacPlacementInputCatalogLimitsV1 &limits) {
  validate_limits(limits);
  require(assets.gameplay.static_moby_count == assets.gameplay.static_mobies.size() &&
              assets.gameplay.static_mobies.size() <= limits.max_placements &&
              assets.models.size() <= limits.max_models &&
              assets.level_core.moby_classes.size() <= limits.max_classes &&
              !assets.gameplay_source_bytes.empty() &&
              assets.gameplay_source_bytes.size() <= limits.max_source_bytes &&
              assets.level_core_source_bytes.size() <= limits.max_source_bytes,
          "Placement input catalog exceeds its source/count limits");
  const auto raw = std::span<const std::byte>(assets.gameplay_source_bytes);
  const auto block = std::uint64_t{word(raw, 0x44)};
  const auto header = range(raw, block, 16);
  require(word(header, 0) != 0 && word(header, 0) == assets.gameplay.static_moby_count &&
              word(header, 4) == assets.gameplay.spawnable_moby_count &&
              word(header, 8) == 0 && word(header, 12) == 0 &&
              assets.gameplay.static_mobies.front().record_range.offset == block + 16,
          "Placement catalog does not cover its actual source Moby header/count/start");
  static_cast<void>(range(raw, block + 16,
      std::uint64_t{assets.gameplay.static_moby_count} * kRacGameplayMobyRecordBytesV1));
}

void validate_authored(const RacGameplayMobyInstanceV1 &record,
                       std::span<const std::byte> gameplay) {
  require(record.record_range.size == kRacGameplayMobyRecordBytesV1,
          "Placement input has a different authored record envelope");
  const auto bytes = range(gameplay, record.record_range.offset, record.record_range.size);
  const auto equal = [&](std::uint32_t offset, std::uint32_t expected) {
    require(word(bytes, offset) == expected,
            "Placement input parsed field differs from actual authored bytes");
  };
  equal(0, kRacGameplayMobyRecordBytesV1);
  equal(4, static_cast<std::uint32_t>(record.admission.selector_index));
  equal(8, record.admission.policy_bits);
  equal(12, static_cast<std::uint32_t>(record.admission.key_index));
  equal(16, static_cast<std::uint32_t>(record.admission.primary_count));
  equal(20, static_cast<std::uint32_t>(record.admission.alternate_count));
  equal(0x18, record.class_id); equal(0x1c, record.scale_bits);
  equal(0x20, record.draw_distance_raw);
  equal(0x24, static_cast<std::uint32_t>(record.update_distance));
  for (unsigned i = 0; i < 3; ++i) {
    equal(0x30 + 4 * i, record.position_bits[i]);
    equal(0x3c + 4 * i, record.rotation_bits[i]);
    equal(0x64 + 4 * i, record.authored_color_words[i]);
    require(std::bit_cast<std::uint32_t>(record.position[i]) == record.position_bits[i] &&
                std::bit_cast<std::uint32_t>(record.rotation[i]) == record.rotation_bits[i],
            "Placement input transform bits differ from parsed values");
  }
  equal(0x48, static_cast<std::uint32_t>(record.group_index));
  equal(0x4c, static_cast<std::uint32_t>(record.rooted));
  equal(0x50, record.rooted_distance_bits);
  equal(0x58, static_cast<std::uint32_t>(record.pvar_index));
  equal(0x5c, static_cast<std::uint32_t>(record.occlusion));
  equal(0x60, record.mode_bits);
  equal(0x70, static_cast<std::uint32_t>(record.light_index));
  equal(0x74, record.authored_reference_index_bits);
  require(std::bit_cast<std::uint32_t>(record.scale) == record.scale_bits &&
              std::bit_cast<std::uint32_t>(record.rooted_distance) == record.rooted_distance_bits,
          "Placement input scalar bits differ from parsed values");
  if (record.pvar_index == -1)
    require(record.pvar_data_range.size == 0, "Absent placement PVar has a byte owner");
  else {
    require(record.pvar_index >= 0 && record.pvar_data_range.size != 0,
            "Placement PVar owner is unresolved");
    static_cast<void>(range(gameplay, record.pvar_data_range.offset, record.pvar_data_range.size));
  }
}

RacRatchetSequenceRangeV1 ordinary_sequence_range(const RacMobyClassV1 &model) {
  require(!model.sequence_offsets.empty() && model.sequence_offsets[0] != 0,
          "Ordinary placement sequence0 has no occupied slot");
  const auto begin = model.sequence_offsets[0];
  auto end = model.input_bytes;
  const auto boundary = [&](std::uint64_t candidate) {
    if (candidate > begin && candidate <= model.input_bytes) end = std::min(end, candidate);
  };
  for (const auto offset : model.sequence_offsets) boundary(offset);
  for (const auto offset : std::array<std::uint64_t, 8>{model.packet_table_offset,
           model.collision_offset, model.skeleton_offset, model.common_translation_offset,
           model.joint_metadata_offset, model.gif_usage_offset,
           model.sound_definitions_offset, model.shadow_range.offset}) boundary(offset);
  if (model.bangles_offset_qwords) boundary(std::uint64_t{model.bangles_offset_qwords} * 16);
  require(end > begin, "Placement sequence0 has no bounded envelope");
  return {begin, end - begin};
}

RacPlacementModelInputV1 compile_model(const RacLevelMobyAssetsV1 &assets,
    const RacPlacementClassBindingV1 &binding, RacPlacementInputCatalogLimitsV1 limits) {
  require(binding.model_index && *binding.model_index < assets.models.size() &&
              binding.current_model_flags.has_value(),
          "Placement bound model/current flags are unresolved");
  const auto &source = assets.models[*binding.model_index];
  require(source.class_id == binding.class_id && !source.source_bytes.empty() &&
              source.source_bytes.size() <= limits.max_source_bytes,
          "Placement model binding differs from its source owner");
  const auto bytes = std::span<const std::byte>(source.source_bytes);
  RacMobyClassV1 model;
  try {
    model = parse_rac_moby_class_v1(bytes, {limits.max_source_bytes,
        source.source == RacLevelMobyModelSourceV1::shared_gadget});
  } catch (const RacMobyClassError &error) {
    throw RacPlacementInputCatalogError("Placement model: " + std::string(error.what()));
  }
  RacPlacementModelInputV1 result;
  result.source_model_index = *binding.model_index;
  result.source_sha256 = prepared_content_sha256_v1(bytes);
  result.current_flags = *binding.current_model_flags;
  result.scale_bits = model.scale_bits;
  result.spatial_offset = model.collision_offset;
  result.auxiliary_word = word(bytes, 0x40);
  result.byte_06 = byte(bytes, 6); result.byte_0c = byte(bytes, 12);
  result.byte_0e = byte(bytes, 14); result.byte_0f = byte(bytes, 15);

  std::optional<RacRatchetSequenceRangeV1> sequence_range;
  auto owner = RacPlacementSequenceOwnerV1::model_source;
  auto sequence_bytes = bytes;
  if (model.sequence_count != 0 && binding.apply_external_sequence_table) {
    require(binding.class_table_index == 0 && binding.class_id == 0,
            "External placement sequence replacement has a different registered owner");
    const auto offset = assets.level_core.ratchet_sequence_offsets[0];
    if (offset != 0) {
      const auto &extents = assets.level_core.ratchet_sequences;
      const RacLevelCoreRatchetSequenceV1 *found = nullptr;
      for (const auto &extent : extents) if (extent.asset_offset == offset) {
        require(!found, "Placement external sequence0 has duplicate owner extents");
        found = &extent;
      }
      require(found && found->asset_range.offset == offset && found->asset_range.size != 0,
              "Placement external sequence0 is unresolved");
      sequence_range = {found->asset_range.offset, found->asset_range.size};
      sequence_bytes = assets.level_core_source_bytes;
      owner = RacPlacementSequenceOwnerV1::decoded_level_core;
    }
  }
  if (!sequence_range && !model.sequence_offsets.empty() && model.sequence_offsets[0] != 0)
    sequence_range = ordinary_sequence_range(model);
  if (!sequence_range) return result; // Actual null pointer, not a failed parse.

  try {
    const auto sequence = owner == RacPlacementSequenceOwnerV1::decoded_level_core
        ? parse_rac_ratchet_sequence_v1(sequence_bytes, *sequence_range, limits.sequence)
        : parse_rac_moby_sequence_v1(sequence_bytes, *sequence_range, limits.sequence);
    require(!sequence.frames.empty(), "Placement sequence0 has no supported actual frame0");
    result.sequence0 = RacPlacementSequenceInputV1{owner, *sequence_range,
        sequence.frames.front().source_offset, sequence.frames.front().packed_offset_word,
        sequence.opaque_prefix_words, sequence.frame_count, sequence.continuous_sound_id,
        sequence.trigger_count, prepared_content_sha256_v1(sequence.encoded_bytes)};
  } catch (const RacRatchetSequenceError &error) {
    throw RacPlacementInputCatalogError("Placement sequence0: " + std::string(error.what()));
  }
  return result;
}

std::span<const std::byte> overlay_range(std::span<const std::byte> overlay,
                                       std::uint32_t address, std::uint32_t count) {
  std::size_t cursor = 0;
  std::optional<std::span<const std::byte>> found;
  while (cursor < overlay.size()) {
    const auto destination = word(overlay, cursor);
    const auto bytes = word(overlay, cursor + 4);
    const auto payload = range(overlay, cursor + 16, bytes);
    if (address >= destination && std::uint64_t{address} + count <= std::uint64_t{destination} + bytes) {
      require(!found, "Placement overlay input has overlapping source owners");
      found = range(payload, address - destination, count);
    }
    cursor += 16 + bytes;
  }
  require(found.has_value(), "Placement overlay does not supply the required producer input");
  return *found;
}

} // namespace

RacPlacementInputCatalogV1 compile_rac_placement_input_catalog_v1(
    const RacLevelMobyAssetsV1 &assets,
    std::span<const RacPlacementClassBindingV1> bindings,
    RacPlacementInputCatalogLimitsV1 limits) {
  validate_assets(assets, limits);
  require(bindings.size() <= limits.max_classes, "Placement class binding count exceeds its limit");
  std::map<std::uint32_t, std::uint32_t> by_class;
  std::set<std::uint8_t> buckets;
  for (std::size_t i = 0; i < bindings.size(); ++i) {
    const auto &binding = bindings[i];
    require(by_class.emplace(binding.class_id, static_cast<std::uint32_t>(i)).second &&
                buckets.insert(binding.class_table_index).second,
            "Placement class registration has duplicate class or bucket identities");
    require(binding.callback_semantic_key.size() <= limits.max_key_bytes &&
                ((binding.callback_reference == 0) == binding.callback_semantic_key.empty()),
            "Placement callback identity is absent or differs from its null reference");
    for (const auto character : binding.callback_semantic_key)
      require(character >= '!' && character <= '~', "Placement callback key has invalid bytes");
    require(binding.model_index.has_value() == binding.current_model_flags.has_value(),
            "Placement null model and current flags disagree");
    require(!binding.model_index || *binding.model_index < assets.models.size(),
            "Placement model binding leaves its source model table");
    require(!binding.model_index || assets.models[*binding.model_index].class_id == binding.class_id,
            "Placement model identity does not match its class binding");
    require(!binding.apply_external_sequence_table ||
                (binding.model_index && binding.class_table_index == 0 && binding.class_id == 0),
            "External placement sequence binding lacks its actual bucket0 model");
  }
  RacPlacementInputCatalogV1 result;
  result.level_id = assets.level_id;
  result.spawnable_count = assets.gameplay.spawnable_moby_count;
  result.gameplay_source_sha256 = prepared_content_sha256_v1(assets.gameplay_source_bytes);
  result.classes.assign(bindings.begin(), bindings.end());
  result.placements.reserve(assets.gameplay.static_mobies.size());
  std::map<std::uint32_t, std::uint32_t> compiled_models;
  std::optional<std::uint64_t> next_offset;
  for (const auto &record : assets.gameplay.static_mobies) {
    validate_authored(record, assets.gameplay_source_bytes);
    require(!next_offset || record.record_range.offset == *next_offset,
            "Placement catalog source records are reordered or incomplete");
    next_offset = record.record_range.offset + record.record_range.size;
    const auto binding_it = by_class.find(record.class_id);
    require(binding_it != by_class.end(), "Placement class has no resolved registration binding");
    const auto &binding = bindings[binding_it->second];
    std::optional<std::uint32_t> model_input;
    if (binding.model_index) {
      const auto known = compiled_models.find(*binding.model_index);
      if (known == compiled_models.end()) {
        model_input = static_cast<std::uint32_t>(result.models.size());
        result.models.push_back(compile_model(assets, binding, limits));
        compiled_models.emplace(*binding.model_index, *model_input);
      } else {
        model_input = known->second;
        require(result.models[*model_input].current_flags == *binding.current_model_flags,
                "Placement shared model has inconsistent current flags");
      }
    }
    result.placements.push_back({static_cast<std::uint32_t>(result.placements.size()),
        binding_it->second, model_input, record, record.rooted != 0});
  }
  return result;
}

std::vector<RacPlacementClassBindingV1> resolve_rac_veldin_initial_placement_bindings_v1(
    const RacLevelMobyAssetsV1 &assets, std::span<const std::byte> overlay,
    RacPlacementInputCatalogLimitsV1 limits) {
  validate_assets(assets, limits);
  require(overlay.size() <= limits.max_source_bytes && assets.level_id == 0 &&
              hex_digest(prepared_content_sha256_v1(overlay)) ==
                  "922fd06e1ac6c717cc7832a3b8ac7be5aaf178ebe1b7982ae4cd294af1876c79",
          "Initial placement registration requires the qualified Veldin overlay");
  require(word(overlay_range(overlay, 0x160080, 4), 0) == 0,
          "Initial placement registration counter is not zero in installed source");
  const auto &entries = assets.level_core.moby_classes;
  require(entries.size() == 125 && assets.level_core.header.moby_classes.count == entries.size() &&
              assets.level_core.header.moby_classes.offset == 0xe0 &&
              assets.gameplay.moby_class_ids.size() == entries.size() && entries.front().class_id == 0,
          "Initial placement class table does not match the qualified registration order");
  const auto dispatch = overlay_range(overlay, 0x1ea680, 0x504);
  std::size_t sentinel = 0;
  while (sentinel < dispatch.size() && word(dispatch, sentinel) != UINT32_MAX) sentinel += 12;
  require(sentinel < dispatch.size(), "Initial placement dispatch table has no bounded sentinel");
  std::vector<RacPlacementClassBindingV1> result;
  result.reserve(entries.size());
  std::set<std::uint32_t> seen;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto &entry = entries[i];
    const auto class_id = static_cast<std::uint32_t>(entry.class_id);
    require(class_id < 0x800 && seen.insert(class_id).second &&
                assets.gameplay.moby_class_ids[i] == class_id &&
                entry.table_entry_range.offset == 0xe0 + i * 32 && entry.table_entry_range.size == 32,
            "Initial placement class registration input is inconsistent");
    auto selected = std::size_t{0};
    while (selected < sentinel && word(dispatch, selected) != class_id) selected += 12;
    RacPlacementClassBindingV1 binding;
    binding.class_id = class_id; binding.class_table_index = static_cast<std::uint8_t>(i);
    binding.callback_reference = word(dispatch, selected + 4);
    binding.callback_table_reference = word(dispatch, selected + 8);
    if (binding.callback_reference)
      binding.callback_semantic_key = "rac1/level/0/dispatch/" + std::to_string(selected / 12) + "/update";
    if (entry.asset_offset != 0) {
      require(entry.asset_range.offset == entry.asset_offset && entry.asset_range.size != 0,
              "Initial placement local model range is unresolved");
      const auto actual = range(assets.level_core_source_bytes, entry.asset_range.offset, entry.asset_range.size);
      for (std::size_t m = 0; m < assets.models.size(); ++m) if (assets.models[m].class_id == class_id) {
        require(!binding.model_index && assets.models[m].source == RacLevelMobyModelSourceV1::local_level_core &&
                    std::ranges::equal(actual, assets.models[m].source_bytes),
                "Initial placement model does not match its actual local source owner");
        binding.model_index = static_cast<std::uint32_t>(m);
      }
      require(binding.model_index.has_value(), "Initial placement local model is missing, not null");
      binding.current_model_flags = static_cast<std::uint16_t>(word(actual, 0x44));
      binding.apply_external_sequence_table = i == 0;
    } else {
      require(entry.asset_range.size == 0, "Initial placement null model has a conflicting local range");
      // MOVZ at244f30 passes real zero to241350. Later240398 only registers
      // previously unregistered classes; a decoded gadget is not evidence of
      // a replacement of this existing registry slot.
    }
    result.push_back(std::move(binding));
  }
  return result;
}

} // namespace openrc
