#include "openrc/rac_frontend_sound_compile.hpp"

#include "openrc/elf.hpp"
#include "openrc/hash.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <string>

namespace openrc {
namespace {

void require(bool condition, const char* message) {
    if (!condition) throw RacFrontendSoundCompileError(message);
}

std::uint32_t word(std::span<const std::byte> bytes, std::size_t offset) {
    require(offset <= bytes.size() && bytes.size() - offset >= 4U,
        "Frontend sound source word exceeds its owner");
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i)
        result |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (i * 8U);
    return result;
}

std::uint16_t half(std::span<const std::byte> bytes, std::size_t offset) {
    require(offset <= bytes.size() && bytes.size() - offset >= 2U,
        "Frontend sound source halfword exceeds its owner");
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[offset]) |
        (std::to_integer<unsigned>(bytes[offset + 1U]) << 8U));
}

void qualify_digest(std::span<const std::byte> bytes, const char* expected) {
    Sha256 sha; sha.update(bytes);
    require(hex_digest(sha.finish()) == expected,
        "Frontend sound control source revision is not qualified");
}

std::size_t source_offset(const ElfReport& report, std::uint32_t address,
                          std::uint32_t size) {
    for (const auto& section : report.section_headers) {
        if (section.type == 8U || address < section.virtual_address) continue;
        const auto relative = static_cast<std::uint64_t>(address) - section.virtual_address;
        if (relative + size <= section.size)
            return static_cast<std::size_t>(section.file_offset + relative);
    }
    throw RacFrontendSoundCompileError("Frontend sound source address has no file owner");
}

} // namespace

RacFrontendSoundBankV1 decode_rac_frontend_sound_bank_v1(
    std::span<const std::byte> bytes) {
    require(bytes.size() >= 0x18U && bytes.size() <= 16U * 1024U * 1024U,
        "Frontend sound bank envelope size exceeds the source profile");
    require(word(bytes, 0) == 3U && word(bytes, 4) == 2U,
        "Frontend sound bank wrapper variant is not qualified");
    RacFrontendSoundBankV1 result;
    auto& bank = result.bank;
    bank.input_size = static_cast<std::uint32_t>(bytes.size());
    bank.sblk_record = {word(bytes, 8), word(bytes, 12)};
    bank.secondary_record = {word(bytes, 16), word(bytes, 20)};
    const auto a = bank.sblk_record.offset, n = bank.sblk_record.size;
    const auto b = bank.secondary_record.offset, m = bank.secondary_record.size;
    require(a == 0x18U && n >= 0x34U && std::uint64_t{a} + n == b &&
        std::uint64_t{b} + m <= bytes.size(), "Frontend sound bank record owners differ");
    require(bytes.size() - (std::size_t{b} + m) < 2048U &&
        std::all_of(bytes.begin() + b + m, bytes.end(),
            [](std::byte value) { return value == std::byte{0}; }),
        "Frontend sound bank sector padding differs");
    require(word(bytes, a) == 0x6b6c4253U && word(bytes, a + 4U) == 1U &&
        word(bytes, a + 8U) == 4U && word(bytes, a + 12U) == 0U &&
        word(bytes, a + 16U) == 0U && word(bytes, a + 48U) == 0U,
        "Frontend sound bank old header variant differs");
    require(word(bytes, a + 40U) == m && word(bytes, a + 44U) == m,
        "Frontend sound bank sample allocation and transfer sizes differ");
    result.source_allocation_hint = word(bytes, a + 36U);
    bank.opaque_a = word(bytes, a + 20U); bank.opaque_b = word(bytes, a + 24U);
    bank.descriptor_table_offset = word(bytes, a + 28U);
    bank.item_data_offset = word(bytes, a + 32U);
    const auto descriptor_count = bank.opaque_a >> 16U;
    require(descriptor_count <= 4096U && bank.descriptor_table_offset == 0x34U,
        "Frontend sound bank descriptor count or table differs");
    bank.descriptor_table_end = 0x34U + descriptor_count * 12U;
    require(bank.descriptor_table_end == bank.item_data_offset && bank.item_data_offset <= n,
        "Frontend sound bank descriptor ownership differs");
    for (std::uint32_t i = 0; i < descriptor_count; ++i) {
        const auto offset = a + 0x34U + i * 12U;
        const auto packed = word(bytes, offset + 4U);
        bank.descriptors.push_back({word(bytes, offset), packed, packed & 0xffffU,
            packed & 0xffff0000U, word(bytes, offset + 8U)});
    }
    bank.item_bytes.assign(bytes.begin() + a + bank.item_data_offset, bytes.begin() + a + n);
    bank.secondary_bytes.assign(bytes.begin() + b, bytes.begin() + b + m);
    result.audio = analyze_sblk_audio_v1(bank, {4096U, 4096U, 16U * 1024U * 1024U});
    return result;
}

RacFrontendSoundSourceV1 make_rac_frontend_sound_source_v1(
    std::span<const std::byte> ee_elf, std::span<const std::byte> sound_irx,
    std::span<const std::byte> libsd_irx) {
    // Exact supported source owners: no original code/data is embedded in the
    // runtime. The hashes bind arithmetic qualification to actual instructions.
    qualify_digest(ee_elf, "17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b");
    qualify_digest(sound_irx, "d482d7e0eb6fda6f95946f47630e0f35f3f7da89e554c47fdb42f5c5c8f636ed");
    qualify_digest(libsd_irx, "89e2322522f30fe5631c2e6c72ef82ac948377f5005331989165a5751297bc10");
    const auto ee = inspect_elf(ee_elf), sound = inspect_elf(sound_irx), libsd = inspect_elf(libsd_irx);
    RacFrontendSoundSourceV1 result;
    for (std::uint32_t i = 0; i < 5; ++i) {
        const auto offset = source_offset(ee, 0x1862e0U + i * 0x20U, 0x20U);
        result.menu_descriptors[i] = half(ee_elf, offset + 0x1aU);
    }
    for (std::uint32_t i = 0; i < 12; ++i)
        result.pitch_semitones[i] = half(libsd_irx, source_offset(libsd, 0x5150U + 2U * i, 2U));
    for (std::uint32_t i = 0; i < 128; ++i)
        result.pitch_fine[i] = half(libsd_irx, source_offset(libsd, 0x5168U + 2U * i, 2U));
    const auto pan = source_offset(sound, 0x19144U + 90U * 4U, 4U);
    result.stereo_center = {std::bit_cast<std::int16_t>(half(sound_irx, pan)),
        std::bit_cast<std::int16_t>(half(sound_irx, pan + 2U))};
    result.default_effects_volume = word(ee_elf, source_offset(ee, 0x15eef0U, 4U));
    result.default_stereo = word(ee_elf, source_offset(ee, 0x15eee8U, 4U)) != 0U;
    return result;
}

RacFrontendSoundVoicePlanV1 compile_rac_frontend_sound_voice_plan_v1(
    const RacFrontendSoundSourceV1& source, const RacFrontendSoundBankV1& owner,
    std::uint32_t variant, std::uint32_t effects_volume, bool stereo) {
    require(variant < source.menu_descriptors.size() && effects_volume <= 1024U,
        "Frontend sound variant or effects volume exceeds the qualified range");
    const auto descriptor_index = source.menu_descriptors[variant];
    require(descriptor_index < owner.bank.descriptors.size(), "Frontend sound descriptor is absent");
    const auto& descriptor = owner.bank.descriptors[descriptor_index];
    require(descriptor.item_count == 1U && descriptor.flags == 0U &&
        (descriptor.type & 0xffffff00U) == 0U && descriptor.type <= 127U &&
        descriptor.data_offset % 40U == 0U && descriptor.data_offset / 40U < owner.audio.items.size(),
        "Frontend sound descriptor is not a qualified fixed menu voice");
    const auto& grain = owner.audio.items[descriptor.data_offset / 40U];
    require(grain.tag == 1U && grain.payload[0] == 0U &&
        (grain.payload[1] & 0xffff00ffU) == 0x42b60000U && grain.payload[2] == 0U &&
        grain.payload[3] == 0x80ff0000U && grain.payload[4] == 0x00009fc0U &&
        grain.payload[6] == 0U && grain.payload[7] == 0U && grain.payload[8] == 0U,
        "Frontend sound grain controls differ from the qualified menu path");
    const auto tone_volume = (grain.payload[1] >> 8U) & 0xffU;
    require(tone_volume <= 127U, "Frontend sound grain requires random volume semantics");
    const auto block = std::find_if(owner.audio.blocks.begin(), owner.audio.blocks.end(),
        [&](const SBlkAudioBlock& candidate) { return candidate.offset == grain.payload[5]; });
    require(block != owner.audio.blocks.end() && block->kind == SBlkAudioBlockKind::one_shot &&
        block->content_begin_frame == 1U, "Frontend sound grain has no qualified one-shot sample owner");
    RacFrontendSoundVoicePlanV1 plan;
    plan.variant = variant; plan.descriptor_index = descriptor_index;
    plan.block_index = static_cast<std::uint32_t>(block - owner.audio.blocks.begin());
    // Original libsd38a0: note60, center74, center-fine66, fine0 ->
    // semitone10, fine66 and shift4. Negative-center 989snd branch returns it.
    const auto product = std::uint32_t{source.pitch_semitones[10]} * source.pitch_fine[66];
    plan.phase_numerator = ((product >> 16U) + 8U) >> 4U;
    require(plan.phase_numerator == 1880U, "Frontend sound source pitch tables differ");
    plan.adsr1 = 0x80ffU; plan.adsr2 = 0x9fc0U;
    plan.group_volume = effects_volume * 8U / 10U;
    plan.source_sample_count_before_end = block->content_end_frame * 28U;
    const auto tone_gain = 127U * 258U * tone_volume / 127U;
    const auto combined = tone_gain * descriptor.type / 127U;
    for (std::size_t channel = 0; channel < 2; ++channel) {
        require(source.stereo_center[channel] >= 0, "Frontend sound center pan phase differs");
        const auto panned = stereo ? combined * static_cast<std::uint32_t>(source.stereo_center[channel]) / 16383U : combined;
        const auto grouped = panned * plan.group_volume / 1024U;
        const auto squared = std::uint64_t{grouped} * grouped / 32766U;
        require(squared < 32768U, "Frontend sound volume would require a signed sweep");
        plan.channel_volume_registers[channel] = static_cast<std::uint16_t>(squared >> 1U);
        plan.channel_volume_factors[channel] = static_cast<std::uint16_t>((squared >> 1U) * 2U);
    }
    return plan;
}

RacFrontendSoundProgramsV1 compile_rac_frontend_sound_programs_v1(
    const RacFrontendSoundBankV1& bank, std::span<const std::byte> sound_irx) {
    qualify_digest(sound_irx, "d482d7e0eb6fda6f95946f47630e0f35f3f7da89e554c47fdb42f5c5c8f636ed");
    require(bank.bank.descriptors.size() == 10 && bank.audio.items.size() == 124,
        "Frontend ambient bank partition is not qualified");
    const auto report = inspect_elf(sound_irx);
    const auto source_half = [&](std::uint32_t address) {
        return half(sound_irx, source_offset(report, address, 2));
    };
    RacFrontendSoundProgramsV1 result;
    auto& control = result.control;
    control.ticks_per_second = 240; control.modulation_tick_divisor = 2;
    control.random.index = word(sound_irx, source_offset(report, 0x18f40, 4));
    control.random.forward_tap = 103;
    for (unsigned i = 0; i < 250; ++i) control.random.words.push_back(source_half(0x18f44 + i * 2));
    const auto down = [](std::int64_t value, std::int32_t denominator) {
        return static_cast<std::int32_t>(value >= 0 ? value / denominator : -((-value + denominator - 1) / denominator));
    };
    for (const auto descriptor_index : {2U, 3U, 8U, 4U, 9U}) {
        const auto& descriptor = bank.bank.descriptors[descriptor_index];
        require(descriptor.flags == ((descriptor_index == 3 || descriptor_index == 8) ? 0x10000U : 0U) &&
            descriptor.item_count > 0 && descriptor.item_count < 128 &&
            descriptor.data_offset % 40 == 0 && (descriptor.type & 0xffffU) <= 127 && (descriptor.type >> 16) < 360,
            "Frontend ambient descriptor control differs");
        const auto first = descriptor.data_offset / 40;
        require(std::uint64_t{first} + descriptor.item_count <= bank.audio.items.size(),
            "Frontend ambient grain owner is absent");
        const auto grains = std::span(bank.audio.items).subspan(first, descriptor.item_count);
        AudioProgramV1 program; program.key = descriptor_index; program.nodes.resize(grains.size());
        const auto signed_half = [](std::uint32_t value, unsigned shift = 0) {
            return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value >> shift));
        };
        const auto label = [&](std::int16_t name) {
            for (std::uint32_t i = 0; i < grains.size(); ++i)
                if (grains[i].tag == 35 && signed_half(grains[i].payload[1]) == name) return i;
            throw RacFrontendSoundCompileError("Frontend ambient branch label is absent");
        };
        for (std::uint32_t i = 0; i < grains.size(); ++i) {
            const auto& grain = grains[i]; auto& node = program.nodes[i];
            require(grain.payload[0] <= 32767, "Frontend ambient delay exceeds signed scheduler domain");
            node.delay_ticks = grain.payload[0];
            node.next = i + 1 < grains.size() ? i + 1 : audio_program_end_v1;
            const auto a = signed_half(grain.payload[1]), b = signed_half(grain.payload[1], 16);
            const auto c = signed_half(grain.payload[2]);
            switch (grain.tag) {
            case 1: {
                // Original start routing clears both wet sends for flags0.
                require((grain.payload[4] >> 16U) == 0U,
                    "Frontend ambient tone requires an unqualified effect route");
                const auto found = std::find_if(bank.audio.blocks.begin(), bank.audio.blocks.end(),
                    [&](const SBlkAudioBlock& block) { return block.offset == grain.payload[5]; });
                require(found != bank.audio.blocks.end(), "Frontend ambient tone has no sample owner");
                const auto index = program.voice_count++;
                result.voices.push_back({descriptor_index, index, i,
                    static_cast<std::uint32_t>(found - bank.audio.blocks.begin()), grain});
                node.action = AudioProgramVoiceV1{index}; break;
            }
            case 4: {
                const auto slot = grain.payload[1] & 255U;
                const auto target = (grain.payload[1] >> 8) & 255U;
                const auto waveform = grain.payload[1] >> 24;
                const auto flags = grain.payload[3] & 255U;
                const auto depth = grain.payload[2] >> 16;
                require(waveform == 1 && (target == 1 || target == 2 || target == 4) &&
                    (flags == 2 || flags == 3), "Frontend ambient modulation family differs");
                AudioProgramOscillatorV1 oscillator;
                oscillator.slot = slot; oscillator.scalar = target == 1 ? 7 : target == 2 ? 5 : 6;
                oscillator.phase_denominator = 65536; oscillator.phase_increment = grain.payload[4] * 2U;
                oscillator.random_initial_phase = true; oscillator.random_phase_mask = 2047;
                const auto amplitude = target == 1 ? (descriptor.type & 0xffffU) * depth / 1024U :
                    target == 2 ? depth * 45U / 256U : depth * 32767U / 1024U;
                for (unsigned p = 0; p < 2048; ++p) {
                    auto value = static_cast<std::int32_t>(std::bit_cast<std::int16_t>(source_half(0x17d60 + p * 2)));
                    if (flags & 1U) value = -value;
                    oscillator.values.push_back(target == 1 ? down(std::int64_t{amplitude} * (value - 32767), 65536) :
                        down(std::int64_t{amplitude} * value, 32768));
                }
                node.action = std::move(oscillator); break;
            }
            case 20: case 21: case 35: node.action = AudioProgramNoopV1{}; break;
            case 22: {
                auto destination = i;
                while (destination && grains[--destination].tag != 21) {}
                require(grains[destination].tag == 21, "Frontend ambient loop start is absent");
                node.next = destination; break;
            }
            case 25: {
                require(a > 1 && b > 0 && c >= 0 && c < a &&
                    std::uint64_t{i} + 1 + static_cast<std::uint64_t>(a) * b < grains.size(),
                    "Frontend ambient random sequence extent differs");
                AudioProgramRandomBranchV1 branch; branch.avoid_previous = true;
                branch.initial_previous = c; branch.shared_previous = true;
                for (int choice = 0; choice < a; ++choice) branch.successors.push_back(i + 1 + choice * b);
                node.action = std::move(branch); break;
            }
            case 26: require(a >= 0, "Frontend ambient random wait is negative");
                node.action = AudioProgramRandomWaitV1{static_cast<std::uint32_t>(a)}; break;
            case 27: node.action = AudioProgramRandomSetV1{4, 32767, 65535, 32767, -32768, a, 100}; break;
            case 30: require(a >= 0 && a < 4, "Frontend ambient local is absent");
                node.action = AudioProgramSetV1{static_cast<std::uint32_t>(a), static_cast<std::int8_t>(b)}; break;
            case 31: require(a >= 0 && a < 4 && b >= -128 && c <= 127 && b <= c,
                    "Frontend ambient random local range differs");
                node.action = AudioProgramRandomSetV1{static_cast<std::uint32_t>(a),
                    static_cast<std::uint32_t>(c - b + 1), 1, 1, b, 1, 1}; break;
            case 32: case 33: require(a >= 0 && a < 4, "Frontend ambient local is absent");
                node.action = AudioProgramAddV1{static_cast<std::uint32_t>(a), grain.tag == 32 ? 1 : -1, -128, 127}; break;
            case 34: require(a >= 0 && a < 4 && b >= 0 && b <= 2 && i + 2 < grains.size(),
                    "Frontend ambient conditional extent differs");
                node.action = AudioProgramCompareV1{static_cast<std::uint32_t>(a),
                    static_cast<AudioProgramComparisonV1>(b), c, i + 1}; node.next = i + 2; break;
            case 36: node.next = label(a); break;
            case 37: {
                require(a <= b, "Frontend ambient random label range is invalid");
                AudioProgramRandomBranchV1 branch;
                for (int name = a; name <= b; ++name) branch.successors.push_back(label(static_cast<std::int16_t>(name)));
                node.action = std::move(branch); break;
            }
            case 40: require(b >= 0 && b < 4, "Frontend ambient local is absent");
                node.action = AudioProgramAddV1{static_cast<std::uint32_t>(b), a, -128, 127}; break;
            case 38: node.action = AudioProgramWaitOwnedV1{}; break;
            case 41: node.action = AudioProgramReleaseV1{}; break;
            case 43: node.action = AudioProgramStopSectionV1{}; break;
            default: throw RacFrontendSoundCompileError("Frontend ambient control primitive is not qualified");
            }
        }
        // Compile structured random blocks to ordinary explicit graph exits.
        // The runtime neither counts source grains nor skips source instructions.
        for (std::uint32_t i = 0; i < grains.size(); ++i) if (grains[i].tag == 25) {
            const auto count = signed_half(grains[i].payload[1]);
            const auto length = signed_half(grains[i].payload[1], 16);
            const auto exit = i + 1 + count * length;
            for (int choice = 0; choice < count; ++choice) {
                auto& last = program.nodes[i + (choice + 1) * length];
                require(last.next == i + 1 + (choice + 1) * length,
                    "Frontend ambient random block has a nonlinear exit");
                last.next = exit;
            }
        }
        // The original explicit owner stop first releases/detaches current
        // voices and only then runs its optional stop section. Preserve both
        // operations in ordinary graph data, including a source tail release.
        auto marker=std::find_if(program.nodes.begin(),program.nodes.end(),[](const auto& node){
            return std::holds_alternative<AudioProgramStopSectionV1>(node.action);});
        std::uint32_t marker_index=0,tail=audio_program_end_v1;
        if(marker==program.nodes.end()) {
            marker_index=static_cast<std::uint32_t>(program.nodes.size());
            AudioProgramNodeV1 node;node.action=AudioProgramStopSectionV1{};program.nodes.push_back(node);
        } else {
            marker_index=static_cast<std::uint32_t>(marker-program.nodes.begin());tail=marker->next;
        }
        program.nodes[marker_index].next=static_cast<std::uint32_t>(program.nodes.size());
        AudioProgramNodeV1 release;release.action=AudioProgramReleaseV1{};release.next=tail;
        program.nodes.push_back(release);
        control.programs.push_back(std::move(program));
    }
    validate_audio_program_bank_v1(control);
    return result;
}

AudioProgramCuesV1 compile_rac_frontend_sound_cues_v1(std::span<const std::byte> ee_elf) {
    qualify_digest(ee_elf,"17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b");
    const auto elf=inspect_elf(ee_elf);
    AudioProgramCuesV1 result;result.voice_bank_resource_id="frontend/audio/ambient-bank";
    result.timeline_resource_id="frontend/background/timeline";result.updates_per_second=50;
    constexpr std::array<std::uint32_t,5> expected_cues{2,3,0,4,1},expected_programs{2,3,8,4,9};
    for(unsigned i=0;i<expected_cues.size();++i){
        const auto at=source_offset(elf,0x1863b0U+i*16U,16);
        const auto first=word(ee_elf,at),last=word(ee_elf,at+4),cue=word(ee_elf,at+8);
        require(cue==expected_cues[i] && first<=last && last<=INT32_MAX,"Frontend scenic cue source row differs");
        const auto definition=source_offset(elf,0x186200U+(cue+2U)*32U,32);
        const auto program=half(ee_elf,definition+26);
        require(program==expected_programs[i],"Frontend scenic cue program binding differs");
        result.cues.push_back({i,program,first,last});
    }
    require(word(ee_elf,source_offset(elf,0x186400U,4))==UINT32_MAX,"Frontend scenic cue table has an unowned tail");
    validate_audio_program_cues_v1(result);return result;
}

} // namespace openrc
