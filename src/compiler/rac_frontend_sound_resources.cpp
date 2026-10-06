#include "openrc/rac_frontend_sound_resources.hpp"

#include "openrc/hash.hpp"
#include "openrc/rac_frontend_sound_pcm.hpp"
#include "openrc/rac_frontend_sound_bank.hpp"
#include "openrc/rac_startup.hpp"
#include "openrc/wad_bundle.hpp"

#include <fstream>
#include <string>

namespace openrc {
namespace {

void require(bool condition, const char* message) {
    if (!condition) throw RacFrontendSoundCompileError(message);
}

std::vector<std::byte> read_range(const std::filesystem::path& image,
    std::uint64_t offset, std::uint64_t count, std::uint64_t limit) {
    const auto image_bytes = std::filesystem::file_size(image);
    require(count != 0U && count <= limit && offset <= image_bytes &&
        count <= image_bytes - offset, "Frontend sound ISO range exceeds its owner");
    std::vector<std::byte> bytes(static_cast<std::size_t>(count));
    std::ifstream input(image, std::ios::binary);
    input.seekg(static_cast<std::streamoff>(offset));
    require(static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()))), "Frontend sound ISO read failed");
    return bytes;
}

std::uint32_t word(std::span<const std::byte> bytes, std::size_t offset) {
    require(offset <= bytes.size() && bytes.size() - offset >= 4U,
        "Frontend sound TOC word exceeds its owner");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
    return value;
}

void qualify(std::span<const std::byte> bytes, const char* expected) {
    Sha256 hash; hash.update(bytes);
    require(hex_digest(hash.finish()) == expected,
        "Frontend sound ISO source revision is not qualified");
}

} // namespace

namespace {
std::vector<LevelPackageResourceV1> compile_frontend_sound_resources(
    const std::filesystem::path& image, std::span<const std::byte> boot_executable, bool ambient) {
    constexpr std::uint64_t sector_bytes = 2048U;
    const auto toc = read_range(image, 1500U * sector_bytes, 0x2960U, 0x2960U);
    qualify(toc, "10b5950d0c5c4271f40640f5ae1ce7750bfcdc942459814a6f2865be7ba252c4");
    // This source row is outside the gameplay extent catalog. It is a raw
    // resident SBlk envelope, whereas +12c0 owns the compressed module bundle.
    const auto bank_offset = std::uint64_t{word(toc, 0x14e0U)} * sector_bytes;
    const auto bank_size = std::uint64_t{word(toc, 0x14e4U)} * sector_bytes;
    const auto bank_bytes = read_range(image, bank_offset, bank_size, 16U * 1024U * 1024U);
    qualify(bank_bytes, "b628ffb8de1e230eaa84c124e778fc621153e86e6f7e9276e26c66515ec356ba");
    const auto modules = read_rac_startup_wad_v1(image, 0x12c0U,
        16U * 1024U * 1024U, 64U * 1024U * 1024U);
    const auto bundle = parse_wad_bundle_v1(modules.decoded_bytes);
    const auto module = [&](std::size_t index) {
        const auto& slot = bundle.slots.at(index);
        require(slot.kind == WadBundleRecordKind::elf,
            "Frontend sound module slot is not an ELF owner");
        return std::span<const std::byte>(modules.decoded_bytes).subspan(slot.offset, slot.size);
    };
    const auto source = make_rac_frontend_sound_source_v1(boot_executable, module(21U), module(20U));
    require(source.default_effects_volume == 1024U && source.default_stereo,
        "Frontend sound startup settings differ from the compiled profile");
    const auto bank = decode_rac_frontend_sound_bank_v1(bank_bytes);
    const std::vector<LevelPackageProvenanceV1> provenance{
        {LevelPackageProvenanceKindV1::iso_range, "rac1/frontend-sound-bank",
            bank_offset, bank_size, prepared_content_sha256_v1(bank_bytes)},
        {LevelPackageProvenanceKindV1::iso_range, "rac1/iop-module-bundle",
            modules.source_byte_offset, modules.source_bytes.size(),
            prepared_content_sha256_v1(modules.source_bytes)},
        {LevelPackageProvenanceKindV1::prepared_resource, "rac1/boot-executable",
            0U, boot_executable.size(), prepared_content_sha256_v1(boot_executable)},
        {LevelPackageProvenanceKindV1::generated,
            ambient ? "compiler/rac-frontend-ambient-resources-v1-effects1024-stereo" :
                "compiler/rac-frontend-sound-resources-v1-effects1024-stereo", 0U, 0U, {}}};
    std::vector<LevelPackageResourceV1> result;
    if(ambient) {
        const auto prepared=compile_rac_frontend_ambient_bank_v1(source,bank,module(21U));
        const auto append=[&](std::string id,std::string type,std::vector<std::byte> payload){
            LevelPackageResourceV1 resource;resource.resource_id=std::move(id);resource.type_id=std::move(type);
            resource.schema_version=1;resource.provenance=provenance;resource.payload=std::move(payload);
            resource.payload_sha256=prepared_content_sha256_v1(resource.payload);result.push_back(std::move(resource));};
        result.reserve(20);
        append(prepared.voices.program_resource_id,"openrc.audio-program",encode_audio_program_bank_v1(prepared.program));
        append("frontend/audio/ambient-bank","openrc.audio-voice-bank",encode_audio_voice_bank_v1(prepared.voices));
        append("frontend/audio/ambient-cues","openrc.audio-program-cues",encode_audio_program_cues_v1(compile_rac_frontend_sound_cues_v1(boot_executable)));
        for(std::size_t i=0;i<prepared.streams.size();++i)
            append(prepared.voices.stream_resource_ids[i],"openrc.audio-stream",encode_audio_stream_v1(prepared.streams[i]));
        for(std::size_t i=0;i<prepared.gains.size();++i)
            append(prepared.voices.gain_resource_ids[i],"openrc.audio-gain-table",encode_audio_gain_table_v1(prepared.gains[i]));
        require(result.size()==20,"Frontend ambient resource partition differs");return result;
    }
    result.reserve(source.menu_descriptors.size());
    for (std::uint32_t variant = 0; variant < source.menu_descriptors.size(); ++variant) {
        const auto plan = compile_rac_frontend_sound_voice_plan_v1(source, bank, variant,
            source.default_effects_volume, source.default_stereo);
        LevelPackageResourceV1 resource;
        resource.resource_id = "frontend/audio/variant-" + std::to_string(variant);
        resource.type_id = "openrc.audio-clip";
        resource.schema_version = 1U;
        resource.provenance = provenance;
        resource.payload = encode_audio_clip_v1(compile_rac_frontend_sound_pcm_v1(bank, plan));
        resource.payload_sha256 = prepared_content_sha256_v1(resource.payload);
        result.push_back(std::move(resource));
    }
    return result;
}
} // namespace

std::vector<LevelPackageResourceV1> compile_rac_frontend_sound_resources_v1(
    const std::filesystem::path& image,std::span<const std::byte> boot_executable) {
    return compile_frontend_sound_resources(image,boot_executable,false);
}
std::vector<LevelPackageResourceV1> compile_rac_frontend_ambient_resources_v1(
    const std::filesystem::path& image,std::span<const std::byte> boot_executable) {
    return compile_frontend_sound_resources(image,boot_executable,true);
}

} // namespace openrc
