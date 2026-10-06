#include "openrc/rac_frontend_sound_voice.hpp"

#include "openrc/elf.hpp"
#include "openrc/hash.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>

namespace openrc {
namespace {
void require(bool value, const char* message) {
    if (!value) throw RacFrontendSoundCompileError(message);
}
} // namespace
RacFrontendSoundGainSourceV1 make_rac_frontend_sound_gain_source_v1(std::span<const std::byte> irx) {
    Sha256 sha; sha.update(irx);
    require(hex_digest(sha.finish()) == "d482d7e0eb6fda6f95946f47630e0f35f3f7da89e554c47fdb42f5c5c8f636ed",
        "Frontend voice gain control source revision differs");
    const auto elf = inspect_elf(irx);
    constexpr std::uint32_t address = 0x19144U, bytes = 180U * 4U;
    for (const auto& section : elf.section_headers) {
        if (section.type == 8 || address < section.virtual_address ||
            std::uint64_t{address} + bytes > std::uint64_t{section.virtual_address} + section.size) continue;
        const auto at = section.file_offset + address - section.virtual_address;
        require(std::uint64_t{at} + bytes <= irx.size(), "Frontend pan table has no file owner");
        RacFrontendSoundGainSourceV1 result;
        for (unsigned row = 0; row < 180; ++row) for (unsigned channel = 0; channel < 2; ++channel) {
            const auto offset = at + row * 4U + channel * 2U;
            const auto bits = static_cast<std::uint16_t>(std::to_integer<unsigned>(irx[offset]) |
                (std::to_integer<unsigned>(irx[offset + 1]) << 8));
            result.pan[row][channel] = std::bit_cast<std::int16_t>(bits);
            require(result.pan[row][channel] >= 0, "Frontend pan coefficient has an unqualified sign");
        }
        return result;
    }
    throw RacFrontendSoundCompileError("Frontend pan table is absent");
}
RacFrontendSoundGainV1 evaluate_rac_frontend_sound_gain_v1(
    const RacFrontendSoundGainSourceV1& source, const RacFrontendSoundGainInputV1& input) {
    require(input.descriptor_volume <= 127 && input.tone_volume <= 127 &&
        input.group_volume <= 1024 && input.stereo_mode <= 2,
        "Frontend voice gain arguments exceed the qualified domain");
    // Original 01642c positive-volume path, including the previous pair's
    // source phase-continuity rule rather than a guessed constant-power pan.
    const auto combined = static_cast<std::int32_t>(258U * input.tone_volume * input.descriptor_volume / 127U);
    std::array<std::int32_t, 2> pair{combined, combined};
    if (combined != 0 && input.stereo_mode != 1) {
        auto pan = (std::int64_t{input.pan_degrees} + 90) % 360;
        if (pan < 0) pan += 360;
        const bool rear = pan >= 180;
        const auto& table = source.pan[static_cast<std::size_t>(rear ? pan - 180 : pan)];
        for (unsigned channel = 0; channel < 2; ++channel) {
            require(table[channel] >= 0 && table[channel] <= 16383, "Frontend pan coefficient exceeds source unity");
            pair[channel] = combined * table[rear ? 1U - channel : channel] / 16383;
        }
        const auto old_left = input.previous_panned[0], old_right = input.previous_panned[1];
        if (!rear) {
            int sign = 1;
            if (old_left < 0 && old_right < 0) sign = -1;
            else if ((old_left < 0) != (old_right < 0)) {
                const auto dominant = std::abs(static_cast<int>(old_right)) < std::abs(static_cast<int>(old_left)) ? old_left : old_right;
                sign = dominant < 0 ? -1 : 1;
            }
            pair[0] *= sign; pair[1] *= sign;
        } else if (input.stereo_mode != 2) {
            if ((old_left < 0) != (old_right < 0)) {
                pair[old_left < 0 ? 0 : 1] = -pair[old_left < 0 ? 0 : 1];
            } else {
                const bool invert_right = old_left >= 0 ? pair[1] < pair[0] : pair[0] < pair[1];
                pair[invert_right ? 1 : 0] = -pair[invert_right ? 1 : 0];
            }
        }
    }
    RacFrontendSoundGainV1 result;
    for (unsigned channel = 0; channel < 2; ++channel) {
        require(pair[channel] >= -32768 && pair[channel] <= 32767, "Frontend panned level exceeds signed source range");
        result.panned[channel] = static_cast<std::int16_t>(pair[channel]);
        const auto grouped = std::min(pair[channel], 32766) * static_cast<std::int32_t>(input.group_volume) / 1024;
        // Original 016e40..016e5c uses signed division by 32766 (magic
        // multiplier 0x80020009), not the adjacent maximum envelope level.
        auto curved = static_cast<std::int32_t>(std::int64_t{grouped} * grouped / 32766);
        if (grouped < 0) curved = -curved;
        // The original register write removes bit0 before signed reconstruction.
        const auto bits = static_cast<std::uint16_t>(static_cast<std::uint32_t>(curved) & 0xfffeU);
        result.channel_factors[channel] = std::bit_cast<std::int16_t>(bits);
    }
    return result;
}
AudioGainTableV1 compile_rac_frontend_sound_gain_table_v1(const RacFrontendSoundGainSourceV1& source,
    std::uint32_t fixed_volume,bool variable_descriptor_volume,std::uint32_t group_volume,std::uint32_t stereo_mode) {
    AudioGainTableV1 table;table.state_count=6;table.level_count=128;table.pan_count=360;
    table.initial_state=0;table.gain_denominator=32768;
    constexpr std::array<std::array<std::int16_t,2>,6> representatives{{
        {100,200},{-100,-200},{-200,100},{-100,200},{200,-100},{100,-200}}};
    const auto phase_class=[](const std::array<std::int16_t,2>& pair)->std::uint32_t {
        if(pair[0]>=0 && pair[1]>=0)return 0;
        if(pair[0]<0 && pair[1]<0)return 1;
        const bool left_dominates=std::abs(static_cast<int>(pair[0]))>std::abs(static_cast<int>(pair[1]));
        return pair[0]<0?(left_dominates?2U:3U):(left_dominates?4U:5U);
    };
    table.cells.reserve(table.state_count*table.level_count*table.pan_count);
    for(const auto prior:representatives)for(unsigned level=0;level<128;++level)for(unsigned pan=0;pan<360;++pan){
        const auto result=evaluate_rac_frontend_sound_gain_v1(source,{variable_descriptor_volume?level:fixed_volume,
            variable_descriptor_volume?fixed_volume:level,static_cast<std::int32_t>(pan),prior,group_volume,stereo_mode});
        table.cells.push_back({{result.channel_factors[0],result.channel_factors[1]},phase_class(result.panned)});
    }
    validate_audio_gain_table_v1(table);return table;
}
AudioEnvelopeV1 compile_rac_frontend_sound_envelope_v1(std::uint16_t first, std::uint16_t second) {
    AudioEnvelopeV1 result; result.maximum_level = 32767; result.counter_period = 32768;
    const auto stage = [](unsigned shift, int step, bool decreasing, bool exponential, unsigned target) {
        AudioEnvelopeStageV1 out;
        out.counter_increment = std::max(1U, 32768U >> (shift > 11 ? shift - 11 : 0));
        out.slow_counter_increment = out.counter_increment;
        if (exponential && !decreasing) {
            out.slow_above_level = 24576;
            out.slow_counter_increment = std::max(1U, out.counter_increment / 4);
        }
        const auto delta = step * (1 << (shift < 11 ? 11 - shift : 0));
        if (exponential && decreasing) {
            out.delta_multiplier = delta; out.delta_denominator = 32768;
        } else out.delta_bias = delta;
        out.target = target; out.decreasing = decreasing; return out;
    };
    result.stages[0] = stage((first >> 10) & 31U, 7 - ((first >> 8) & 3U), false, (first & 0x8000U) != 0, 32767);
    result.stages[1] = stage((first >> 4) & 15U, -8, true, true, ((first & 15U) + 1U) * 2048U);
    const bool decreasing = (second & 0x4000U) != 0;
    const auto sustain_step = static_cast<int>((second >> 6) & 3U);
    result.stages[2] = stage((second >> 8) & 31U, decreasing ? -8 + sustain_step : 7 - sustain_step,
        decreasing, (second & 0x8000U) != 0, 0);
    result.stages[3] = stage(second & 31U, -8, true, (second & 0x20U) != 0, 0);
    validate_audio_envelope_v1(result);
    return result;
}
AudioReadAheadV1 compile_rac_frontend_sound_read_ahead_v1(const RacFrontendSoundBankV1& bank, std::uint32_t block_index) {
    require(block_index < bank.audio.blocks.size(), "Frontend finite sound input is absent");
    const auto& block = bank.audio.blocks[block_index];
    require(block.kind == SBlkAudioBlockKind::one_shot && block.content_begin_frame == 1 && block.content_end_frame > 1,
        "Frontend finite sound read-ahead policy has no qualified terminal owner");
    AudioReadAheadV1 result{std::uint64_t{block.content_end_frame} * 28U, 4, 12, 4};
    validate_audio_read_ahead_v1(result); return result;
}
} // namespace openrc
