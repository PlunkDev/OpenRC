#include "openrc/gif_gs.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace openrc {
namespace {

constexpr std::uint32_t kFullKnown = 0xffffffffU;

[[noreturn]] void fail(const std::string& message) {
    throw GifGsDecodeError(message);
}

[[nodiscard]] std::uint64_t checked_add(const std::uint64_t left,
                                        const std::uint64_t right,
                                        const char* const description) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        fail(std::string("Integer overflow while calculating ") +
             description);
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const description) {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        fail(std::string("Integer overflow while calculating ") +
             description);
    }
    return left * right;
}

[[nodiscard]] bool fully_known(const DvpVuWordV1 value) noexcept {
    return value.known_mask == kFullKnown;
}

[[nodiscard]] std::optional<std::uint32_t>
extract_field(const DvpVuWordV1 word,
              const std::uint32_t shift,
              const std::uint32_t width) noexcept {
    const auto value_mask =
        width == 32U ? kFullKnown : ((1U << width) - 1U);
    const auto field_mask = value_mask << shift;
    if ((word.known_mask & field_mask) != field_mask) {
        return std::nullopt;
    }
    return (word.bits >> shift) & value_mask;
}

[[nodiscard]] std::optional<float>
extract_float(const DvpVuWordV1 word) noexcept {
    if (!fully_known(word)) {
        return std::nullopt;
    }
    return std::bit_cast<float>(word.bits);
}

[[nodiscard]] GifGsPrimitiveStateV1
make_primitive_state(const std::optional<std::uint16_t> raw) noexcept {
    GifGsPrimitiveStateV1 result;
    if (!raw.has_value()) {
        return result;
    }

    result.known = true;
    result.raw = static_cast<std::uint16_t>(*raw & 0x07ffU);
    result.topology =
        static_cast<GifGsPrimitiveTopologyV1>(result.raw & 0x07U);
    return result;
}

[[nodiscard]] GifGsPrimitiveEmissionV1
primitive_emission(const GifGsVertexKickV1 kick) noexcept {
    switch (kick) {
    case GifGsVertexKickV1::submitted:
        return GifGsPrimitiveEmissionV1::emitted;
    case GifGsVertexKickV1::suppressed:
        return GifGsPrimitiveEmissionV1::suppressed;
    case GifGsVertexKickV1::indeterminate:
        return GifGsPrimitiveEmissionV1::indeterminate;
    }
    return GifGsPrimitiveEmissionV1::indeterminate;
}

class Decoder final {
public:
    explicit Decoder(const GifGsDecodeLimitsV1 limits) : limits_(limits) {}

    void begin_event(const std::uint64_t event_index) noexcept {
        current_event_index_ = event_index;
    }

    void begin_tag(const DvpVuGifTagV1& tag) {
        if (tag.nloop == 0U) {
            return;
        }
        if (tag.pre) {
            apply_primitive(tag.prim);
        }
        // GIF initializes its temporary packed-mode Q to 1.0 for every
        // non-empty tag. RGBA copies this temporary value into RGBAQ.
        temporary_q_ = 1.0F;
    }

    void process_write(const std::uint64_t tag_index,
                       const std::uint64_t loop_index,
                       const std::uint8_t register_index,
                       const std::uint64_t packet_qword_index,
                       const GifGsRegisterDescriptorV1 descriptor,
                       const DvpVuVectorV1& payload) {
        const auto write_index =
            static_cast<std::uint64_t>(result_.register_writes.size());
        result_.register_writes.push_back(GifGsRegisterWriteV1{
            current_event_index_,
            tag_index,
            loop_index,
            register_index,
            packet_qword_index,
            descriptor,
            payload,
        });

        switch (descriptor) {
        case GifGsRegisterDescriptorV1::prim:
            apply_primitive(extract_primitive(payload.lanes[0U]));
            break;
        case GifGsRegisterDescriptorV1::rgbaq:
            process_packed_rgba(payload);
            break;
        case GifGsRegisterDescriptorV1::st:
            process_packed_st(payload);
            break;
        case GifGsRegisterDescriptorV1::uv:
            process_packed_uv(payload);
            break;
        case GifGsRegisterDescriptorV1::xyzf2:
            process_packed_xyzf(payload,
                                write_index,
                                descriptor,
                                false);
            break;
        case GifGsRegisterDescriptorV1::xyz2:
            process_packed_xyz(payload, write_index, descriptor, false);
            break;
        case GifGsRegisterDescriptorV1::fog:
            result_.final_fog = extract_u8(payload.lanes[3U], 4U);
            break;
        case GifGsRegisterDescriptorV1::xyzf3:
            process_regular_xyzf(payload,
                                 write_index,
                                 descriptor,
                                 std::nullopt,
                                 true);
            break;
        case GifGsRegisterDescriptorV1::xyz3:
            process_regular_xyz(payload,
                                write_index,
                                descriptor,
                                std::nullopt,
                                true);
            break;
        case GifGsRegisterDescriptorV1::address_data:
            process_addressed(payload, write_index);
            break;
        case GifGsRegisterDescriptorV1::nop:
            break;
        case GifGsRegisterDescriptorV1::tex0_1:
        case GifGsRegisterDescriptorV1::tex0_2:
        case GifGsRegisterDescriptorV1::clamp_1:
        case GifGsRegisterDescriptorV1::clamp_2:
        case GifGsRegisterDescriptorV1::reserved:
            ++result_.unsupported_register_write_count;
            break;
        }
    }

    [[nodiscard]] GifGsDecodeResultV1 finish() && {
        result_.final_raster = raster_snapshot();
        return std::move(result_);
    }

private:
    [[nodiscard]] static std::optional<std::uint16_t>
    extract_primitive(const DvpVuWordV1 word) noexcept {
        const auto value = extract_field(word, 0U, 11U);
        return value.has_value()
                   ? std::optional<std::uint16_t>{
                         static_cast<std::uint16_t>(*value)}
                   : std::nullopt;
    }

    [[nodiscard]] static std::optional<std::uint8_t>
    extract_u8(const DvpVuWordV1 word, const std::uint32_t shift) noexcept {
        const auto value = extract_field(word, shift, 8U);
        return value.has_value()
                   ? std::optional<std::uint8_t>{
                         static_cast<std::uint8_t>(*value)}
                   : std::nullopt;
    }

    [[nodiscard]] static std::optional<std::uint16_t>
    extract_u16(const DvpVuWordV1 word,
                const std::uint32_t shift) noexcept {
        const auto value = extract_field(word, shift, 16U);
        return value.has_value()
                   ? std::optional<std::uint16_t>{
                         static_cast<std::uint16_t>(*value)}
                   : std::nullopt;
    }

    [[nodiscard]] static std::optional<std::uint16_t>
    extract_u14(const DvpVuWordV1 word,
                const std::uint32_t shift) noexcept {
        const auto value = extract_field(word, shift, 14U);
        return value.has_value()
                   ? std::optional<std::uint16_t>{
                         static_cast<std::uint16_t>(*value)}
                   : std::nullopt;
    }

    [[nodiscard]] static std::optional<std::uint16_t>
    extract_u11(const DvpVuWordV1 word,
                const std::uint32_t shift) noexcept {
        const auto value = extract_field(word, shift, 11U);
        return value.has_value()
                   ? std::optional<std::uint16_t>{
                         static_cast<std::uint16_t>(*value)}
                   : std::nullopt;
    }

    [[nodiscard]] static std::optional<bool>
    extract_bit(const DvpVuWordV1 word,
                const std::uint32_t shift) noexcept {
        const auto value = extract_field(word, shift, 1U);
        return value.has_value() ? std::optional<bool>{*value != 0U}
                                 : std::nullopt;
    }

    void apply_primitive(const std::optional<std::uint16_t> raw) {
        assembly_vertices_.clear();
        result_.final_primitive = make_primitive_state(raw);
        refresh_primitive_attributes();
    }

    void refresh_primitive_attributes() {
        auto& primitive = result_.final_primitive;
        primitive.attributes_known = false;
        primitive.primitive_attributes_selected =
            primitive_attributes_selected_;
        primitive.raw_prmode = raw_prmode_;
        primitive.gouraud_shading = false;
        primitive.texture_mapping = false;
        primitive.fogging = false;
        primitive.alpha_blending = false;
        primitive.antialiasing = false;
        primitive.fixed_texture_coordinates = false;
        primitive.context_two = false;
        primitive.fixed_fragment_control = false;

        std::optional<std::uint16_t> effective;
        if (primitive.known && primitive_attributes_selected_ == true) {
            effective = primitive.raw;
        } else if (primitive.known &&
                   primitive_attributes_selected_ == false &&
                   raw_prmode_.has_value()) {
            effective = *raw_prmode_;
        }
        if (!effective.has_value()) {
            return;
        }

        primitive.attributes_known = true;
        primitive.gouraud_shading = (*effective & (1U << 3U)) != 0U;
        primitive.texture_mapping = (*effective & (1U << 4U)) != 0U;
        primitive.fogging = (*effective & (1U << 5U)) != 0U;
        primitive.alpha_blending = (*effective & (1U << 6U)) != 0U;
        primitive.antialiasing = (*effective & (1U << 7U)) != 0U;
        primitive.fixed_texture_coordinates =
            (*effective & (1U << 8U)) != 0U;
        primitive.context_two = (*effective & (1U << 9U)) != 0U;
        primitive.fixed_fragment_control =
            (*effective & (1U << 10U)) != 0U;
        if (primitive.topology == GifGsPrimitiveTopologyV1::point ||
            primitive.topology == GifGsPrimitiveTopologyV1::sprite) {
            primitive.gouraud_shading = false;
            primitive.antialiasing = false;
        }
    }

    void process_packed_rgba(const DvpVuVectorV1& payload) {
        result_.final_color.r = extract_u8(payload.lanes[0U], 0U);
        result_.final_color.g = extract_u8(payload.lanes[1U], 0U);
        result_.final_color.b = extract_u8(payload.lanes[2U], 0U);
        result_.final_color.a = extract_u8(payload.lanes[3U], 0U);
        result_.final_texture.q = temporary_q_;
    }

    void process_packed_st(const DvpVuVectorV1& payload) {
        result_.final_texture.s = extract_float(payload.lanes[0U]);
        result_.final_texture.t = extract_float(payload.lanes[1U]);
        temporary_q_ = extract_float(payload.lanes[2U]);
    }

    void process_packed_uv(const DvpVuVectorV1& payload) {
        result_.final_texture.u = extract_u14(payload.lanes[0U], 0U);
        result_.final_texture.v = extract_u14(payload.lanes[1U], 0U);
    }

    void process_packed_xyzf(const DvpVuVectorV1& payload,
                             const std::uint64_t write_index,
                             const GifGsRegisterDescriptorV1 descriptor,
                             const bool always_suppressed) {
        const auto adc = extract_bit(payload.lanes[3U], 15U);
        auto kick = GifGsVertexKickV1::indeterminate;
        if (always_suppressed) {
            kick = GifGsVertexKickV1::suppressed;
        } else if (adc.has_value()) {
            kick = *adc ? GifGsVertexKickV1::suppressed
                        : GifGsVertexKickV1::submitted;
        }

        const auto fog = extract_u8(payload.lanes[3U], 4U);
        append_vertex(GifGsVertexV1{
            write_index,
            current_event_index_,
            descriptor,
            std::nullopt,
            extract_u16(payload.lanes[0U], 0U),
            extract_u16(payload.lanes[1U], 0U),
            extract_field(payload.lanes[2U], 4U, 24U),
            fog,
            adc,
            kick,
            {},
            {},
            {},
            {},
        });
        result_.final_fog = fog;
    }

    void process_packed_xyz(const DvpVuVectorV1& payload,
                            const std::uint64_t write_index,
                            const GifGsRegisterDescriptorV1 descriptor,
                            const bool always_suppressed) {
        const auto adc = extract_bit(payload.lanes[3U], 15U);
        auto kick = GifGsVertexKickV1::indeterminate;
        if (always_suppressed) {
            kick = GifGsVertexKickV1::suppressed;
        } else if (adc.has_value()) {
            kick = *adc ? GifGsVertexKickV1::suppressed
                        : GifGsVertexKickV1::submitted;
        }

        append_vertex(GifGsVertexV1{
            write_index,
            current_event_index_,
            descriptor,
            std::nullopt,
            extract_u16(payload.lanes[0U], 0U),
            extract_u16(payload.lanes[1U], 0U),
            fully_known(payload.lanes[2U])
                ? std::optional<std::uint32_t>{payload.lanes[2U].bits}
                : std::nullopt,
            result_.final_fog,
            adc,
            kick,
            {},
            {},
            {},
            {},
        });
    }

    void process_addressed(const DvpVuVectorV1& payload,
                           const std::uint64_t write_index) {
        const auto address_value = extract_field(payload.lanes[2U], 0U, 8U);
        const auto dispatched_value =
            extract_field(payload.lanes[2U], 0U, 7U);
        const auto address =
            address_value.has_value()
                ? std::optional<std::uint8_t>{
                      static_cast<std::uint8_t>(*address_value)}
                : std::nullopt;
        const auto dispatched =
            dispatched_value.has_value()
                ? std::optional<std::uint8_t>{
                      static_cast<std::uint8_t>(*dispatched_value)}
                : std::nullopt;
        result_.addressed_writes.push_back(GifGsAddressedWriteV1{
            write_index,
            address,
            dispatched,
            payload.lanes[0U],
            payload.lanes[1U],
        });

        if (!dispatched.has_value()) {
            ++result_.unresolved_addressed_write_count;
            apply_primitive(std::nullopt);
            result_.final_texture = {};
            result_.final_color = {};
            result_.final_fog.reset();
            primitive_attributes_selected_.reset();
            raw_prmode_.reset();
            invalidate_raster_contexts();
            refresh_primitive_attributes();
            return;
        }

        switch (*dispatched) {
        case 0x00U:
            apply_primitive(extract_primitive(payload.lanes[0U]));
            break;
        case 0x01U:
        case 0x11U:
            process_regular_rgbaq(payload);
            break;
        case 0x02U:
            result_.final_texture.s = extract_float(payload.lanes[0U]);
            result_.final_texture.t = extract_float(payload.lanes[1U]);
            break;
        case 0x03U:
            result_.final_texture.u = extract_u14(payload.lanes[0U], 0U);
            result_.final_texture.v = extract_u14(payload.lanes[0U], 16U);
            break;
        case 0x04U:
        case 0x0cU:
            process_regular_xyzf(payload,
                                 write_index,
                                 GifGsRegisterDescriptorV1::address_data,
                                 *dispatched,
                                 *dispatched == 0x0cU);
            break;
        case 0x05U:
        case 0x0dU:
            process_regular_xyz(payload,
                                write_index,
                                GifGsRegisterDescriptorV1::address_data,
                                *dispatched,
                                *dispatched == 0x0dU);
            break;
        case 0x0aU:
            result_.final_fog = extract_u8(payload.lanes[1U], 24U);
            break;
        case 0x0fU:
            break;
        case 0x1aU:
            primitive_attributes_selected_ =
                extract_bit(payload.lanes[0U], 0U);
            refresh_primitive_attributes();
            break;
        case 0x1bU: {
            const auto value = extract_field(payload.lanes[0U], 0U, 11U);
            raw_prmode_ =
                value.has_value()
                    ? std::optional<std::uint16_t>{
                          static_cast<std::uint16_t>(*value)}
                    : std::nullopt;
            refresh_primitive_attributes();
            break;
        }
        case 0x18U:
        case 0x19U:
            result_.final_raster_contexts[*dispatched - 0x18U].xy_offset =
                decode_xy_offset(payload);
            break;
        case 0x40U:
        case 0x41U:
            result_.final_raster_contexts[*dispatched - 0x40U].scissor =
                decode_scissor(payload);
            break;
        default:
            // All addressed writes remain available to later GS-state work.
            ++result_.unsupported_register_write_count;
            break;
        }
    }

    [[nodiscard]] static GifGsXyOffsetStateV1
    decode_xy_offset(const DvpVuVectorV1& payload) noexcept {
        return GifGsXyOffsetStateV1{
            payload.lanes[0U],
            payload.lanes[1U],
            extract_u16(payload.lanes[0U], 0U),
            extract_u16(payload.lanes[1U], 0U),
        };
    }

    [[nodiscard]] static GifGsScissorStateV1
    decode_scissor(const DvpVuVectorV1& payload) noexcept {
        return GifGsScissorStateV1{
            payload.lanes[0U],
            payload.lanes[1U],
            extract_u11(payload.lanes[0U], 0U),
            extract_u11(payload.lanes[0U], 16U),
            extract_u11(payload.lanes[1U], 0U),
            extract_u11(payload.lanes[1U], 16U),
        };
    }

    void invalidate_raster_contexts() noexcept {
        for (auto& context : result_.final_raster_contexts) {
            context.xy_offset = GifGsXyOffsetStateV1{};
            context.scissor = GifGsScissorStateV1{};
        }
    }

    [[nodiscard]] std::optional<std::uint8_t>
    effective_raster_context_index() const noexcept {
        if (!primitive_attributes_selected_.has_value()) {
            return std::nullopt;
        }
        if (*primitive_attributes_selected_) {
            if (!result_.final_primitive.known) {
                return std::nullopt;
            }
            return static_cast<std::uint8_t>(
                (result_.final_primitive.raw >> 9U) & 1U);
        }
        if (!raw_prmode_.has_value()) {
            return std::nullopt;
        }
        return static_cast<std::uint8_t>((*raw_prmode_ >> 9U) & 1U);
    }

    [[nodiscard]] GifGsRasterSnapshotV1 raster_snapshot() const {
        GifGsRasterSnapshotV1 snapshot;
        snapshot.selected_context_index = effective_raster_context_index();
        if (snapshot.selected_context_index.has_value()) {
            snapshot.context = result_.final_raster_contexts[
                *snapshot.selected_context_index];
        }
        return snapshot;
    }

    void process_regular_rgbaq(const DvpVuVectorV1& payload) {
        result_.final_color.r = extract_u8(payload.lanes[0U], 0U);
        result_.final_color.g = extract_u8(payload.lanes[0U], 8U);
        result_.final_color.b = extract_u8(payload.lanes[0U], 16U);
        result_.final_color.a = extract_u8(payload.lanes[0U], 24U);
        result_.final_texture.q = extract_float(payload.lanes[1U]);
    }

    void process_regular_xyzf(const DvpVuVectorV1& payload,
                              const std::uint64_t write_index,
                              const GifGsRegisterDescriptorV1 descriptor,
                              const std::optional<std::uint8_t> address,
                              const bool suppressed) {
        const auto fog = extract_u8(payload.lanes[1U], 24U);
        append_vertex(GifGsVertexV1{
            write_index,
            current_event_index_,
            descriptor,
            address,
            extract_u16(payload.lanes[0U], 0U),
            extract_u16(payload.lanes[0U], 16U),
            extract_field(payload.lanes[1U], 0U, 24U),
            fog,
            std::nullopt,
            suppressed ? GifGsVertexKickV1::suppressed
                       : GifGsVertexKickV1::submitted,
            {},
            {},
            {},
            {},
        });
        result_.final_fog = fog;
    }

    void process_regular_xyz(const DvpVuVectorV1& payload,
                             const std::uint64_t write_index,
                             const GifGsRegisterDescriptorV1 descriptor,
                             const std::optional<std::uint8_t> address,
                             const bool suppressed) {
        append_vertex(GifGsVertexV1{
            write_index,
            current_event_index_,
            descriptor,
            address,
            extract_u16(payload.lanes[0U], 0U),
            extract_u16(payload.lanes[0U], 16U),
            fully_known(payload.lanes[1U])
                ? std::optional<std::uint32_t>{payload.lanes[1U].bits}
                : std::nullopt,
            result_.final_fog,
            std::nullopt,
            suppressed ? GifGsVertexKickV1::suppressed
                       : GifGsVertexKickV1::submitted,
            {},
            {},
            {},
            {},
        });
    }

    void append_vertex(GifGsVertexV1 vertex) {
        if (result_.vertices.size() >= limits_.max_vertices) {
            fail("GIF/GS vertex limit exceeded");
        }
        vertex.texture = result_.final_texture;
        vertex.color = result_.final_color;
        vertex.primitive = result_.final_primitive;
        vertex.raster = raster_snapshot();
        result_.vertices.push_back(std::move(vertex));
        const auto vertex_index =
            static_cast<std::uint64_t>(result_.vertices.size() - 1U);
        assemble_vertex(vertex_index);
    }

    void assemble_vertex(const std::uint64_t vertex_index) {
        const auto& vertex =
            result_.vertices[static_cast<std::size_t>(vertex_index)];
        if (!result_.final_primitive.known ||
            result_.final_primitive.topology ==
                GifGsPrimitiveTopologyV1::invalid) {
            ++result_.unassembled_vertex_count;
            return;
        }

        assembly_vertices_.push_back(vertex_index);
        const auto count = assembly_vertices_.size();
        GifGsPrimitiveV1 primitive;
        primitive.completion_event_index = vertex.event_index;
        primitive.topology = result_.final_primitive.topology;
        primitive.state = result_.final_primitive;
        primitive.raster = vertex.raster;
        bool complete = false;

        switch (primitive.topology) {
        case GifGsPrimitiveTopologyV1::point:
            primitive.vertex_indices[0U] = assembly_vertices_[count - 1U];
            primitive.vertex_count = 1U;
            complete = true;
            break;
        case GifGsPrimitiveTopologyV1::line_list:
        case GifGsPrimitiveTopologyV1::sprite:
            if ((count % 2U) == 0U) {
                primitive.vertex_indices[0U] =
                    assembly_vertices_[count - 2U];
                primitive.vertex_indices[1U] =
                    assembly_vertices_[count - 1U];
                primitive.vertex_count = 2U;
                complete = true;
            }
            break;
        case GifGsPrimitiveTopologyV1::line_strip:
            if (count >= 2U) {
                primitive.vertex_indices[0U] =
                    assembly_vertices_[count - 2U];
                primitive.vertex_indices[1U] =
                    assembly_vertices_[count - 1U];
                primitive.vertex_count = 2U;
                complete = true;
            }
            break;
        case GifGsPrimitiveTopologyV1::triangle_list:
            if ((count % 3U) == 0U) {
                primitive.vertex_indices[0U] =
                    assembly_vertices_[count - 3U];
                primitive.vertex_indices[1U] =
                    assembly_vertices_[count - 2U];
                primitive.vertex_indices[2U] =
                    assembly_vertices_[count - 1U];
                primitive.vertex_count = 3U;
                complete = true;
            }
            break;
        case GifGsPrimitiveTopologyV1::triangle_strip:
            if (count >= 3U) {
                primitive.vertex_indices[0U] =
                    assembly_vertices_[count - 3U];
                primitive.vertex_indices[1U] =
                    assembly_vertices_[count - 2U];
                primitive.vertex_indices[2U] =
                    assembly_vertices_[count - 1U];
                primitive.vertex_count = 3U;
                primitive.strip_winding_reversed = ((count - 3U) & 1U) != 0U;
                complete = true;
            }
            break;
        case GifGsPrimitiveTopologyV1::triangle_fan:
            if (count >= 3U) {
                primitive.vertex_indices[0U] = assembly_vertices_[0U];
                primitive.vertex_indices[1U] =
                    assembly_vertices_[count - 2U];
                primitive.vertex_indices[2U] =
                    assembly_vertices_[count - 1U];
                primitive.vertex_count = 3U;
                complete = true;
            }
            break;
        case GifGsPrimitiveTopologyV1::invalid:
            break;
        }

        if (!complete) {
            return;
        }
        if (result_.primitives.size() >= limits_.max_primitives) {
            fail("GIF/GS primitive limit exceeded");
        }
        primitive.emission = primitive_emission(vertex.kick);
        switch (primitive.emission) {
        case GifGsPrimitiveEmissionV1::emitted:
            ++result_.emitted_primitive_count;
            break;
        case GifGsPrimitiveEmissionV1::suppressed:
            ++result_.suppressed_primitive_count;
            break;
        case GifGsPrimitiveEmissionV1::indeterminate:
            ++result_.indeterminate_primitive_count;
            break;
        }
        result_.primitives.push_back(std::move(primitive));
    }

    GifGsDecodeLimitsV1 limits_;
    GifGsDecodeResultV1 result_;
    std::uint64_t current_event_index_ = 0U;
    std::optional<bool> primitive_attributes_selected_;
    std::optional<std::uint16_t> raw_prmode_;
    std::optional<float> temporary_q_;
    std::vector<std::uint64_t> assembly_vertices_;
};

void validate_limits(const GifGsDecodeLimitsV1 limits) {
    if (limits.max_tags == 0U || limits.max_packet_qwords == 0U ||
        limits.max_register_writes == 0U || limits.max_vertices == 0U ||
        limits.max_primitives == 0U || limits.max_xgkick_events == 0U) {
        fail("GIF/GS decode limits must all be non-zero");
    }
}

[[nodiscard]] std::uint64_t
validate_packet(const DvpVuXgkickEventV1& event,
                const GifGsDecodeLimitsV1 limits) {
    if (!event.packet_complete) {
        fail("GIF/GS decoder requires a complete XGKICK packet");
    }
    if (event.encountered_indeterminate_tag) {
        fail("Complete XGKICK packet reports an indeterminate GIFtag");
    }
    if (event.tags.empty()) {
        fail("Complete XGKICK packet contains no GIFtags");
    }
    if (event.tags.size() > limits.max_tags) {
        fail("GIF/GS tag limit exceeded");
    }
    if (event.packet_qwords.size() > limits.max_packet_qwords) {
        fail("GIF/GS packet qword limit exceeded");
    }

    std::uint64_t cursor = 0U;
    std::uint64_t total_register_writes = 0U;
    for (std::size_t index = 0U; index < event.tags.size(); ++index) {
        const auto& record = event.tags[index];
        const auto& tag = record.tag;
        if (record.packet_qword_index != cursor ||
            cursor >= event.packet_qwords.size()) {
            fail("XGKICK GIFtag qword indices are not contiguous");
        }
        if (tag.eop != (index + 1U == event.tags.size())) {
            fail("XGKICK GIFtag EOP framing is inconsistent");
        }

        const auto& raw =
            event.packet_qwords[static_cast<std::size_t>(cursor)];
        if (!fully_known(raw.lanes[0U]) || !fully_known(raw.lanes[1U])) {
            fail("Copied GIFtag control fields are indeterminate");
        }
        const auto low64 = static_cast<std::uint64_t>(raw.lanes[0U].bits) |
                           (static_cast<std::uint64_t>(raw.lanes[1U].bits)
                            << 32U);
        if (tag.nloop != (low64 & 0x7fffU) ||
            tag.eop != (((low64 >> 15U) & 1U) != 0U)) {
            fail("GIFtag loop/EOP metadata does not match its copied qword");
        }

        if (tag.nloop == 0U) {
            if (tag.payload_qword_count != 0U) {
                fail("Zero-loop GIFtag must not contain payload qwords");
            }
            cursor = checked_add(cursor, 1U, "zero-loop GIFtag qword");
            continue;
        }

        if (tag.format != 0U) {
            fail("GIF/GS decoder currently accepts PACKED-mode tags only");
        }
        if (!tag.registers_known) {
            fail("PACKED GIFtag has indeterminate REGS descriptors");
        }
        if (tag.register_count == 0U || tag.register_count > 16U) {
            fail("PACKED GIFtag register count is outside 1..16");
        }
        if (!fully_known(raw.lanes[2U]) ||
            !fully_known(raw.lanes[3U])) {
            fail("Copied PACKED GIFtag REGS fields are indeterminate");
        }

        const auto expected_payload = checked_multiply(
            tag.nloop, tag.register_count, "PACKED GIFtag payload qwords");
        if (tag.payload_qword_count != expected_payload) {
            fail("PACKED GIFtag payload count is inconsistent");
        }
        total_register_writes = checked_add(total_register_writes,
                                            expected_payload,
                                            "GIF/GS register writes");
        if (total_register_writes > limits.max_register_writes) {
            fail("GIF/GS register write limit exceeded");
        }

        const auto tag_end = checked_add(
            checked_add(cursor, 1U, "GIFtag qword"),
            expected_payload,
            "GIFtag end");
        if (tag_end > event.packet_qwords.size()) {
            fail("PACKED GIFtag payload exceeds the copied XGKICK packet");
        }
        const auto high64 = static_cast<std::uint64_t>(raw.lanes[2U].bits) |
                            (static_cast<std::uint64_t>(raw.lanes[3U].bits)
                             << 32U);
        const auto raw_nreg =
            static_cast<std::uint8_t>((low64 >> 60U) & 0x0fU);
        const auto decoded_nreg = raw_nreg == 0U ? 16U : raw_nreg;
        if (tag.pre != (((low64 >> 46U) & 1U) != 0U) ||
            tag.prim != ((low64 >> 47U) & 0x07ffU) ||
            tag.format != ((low64 >> 58U) & 0x03U) ||
            tag.register_count != decoded_nreg) {
            fail("GIFtag metadata does not match its copied qword");
        }
        for (std::size_t register_index = 0U; register_index < 16U;
             ++register_index) {
            const auto descriptor = static_cast<std::uint8_t>(
                (high64 >> (register_index * 4U)) & 0x0fU);
            if (tag.registers[register_index] != descriptor) {
                fail("GIFtag REGS metadata does not match its copied qword");
            }
        }
        cursor = tag_end;
    }

    if (cursor != event.packet_qwords.size()) {
        fail("XGKICK packet contains trailing qwords after EOP");
    }
    return total_register_writes;
}

} // namespace

GifGsDecodeResultV1
decode_dvp_vu_xgkick_gs_v1(const DvpVuXgkickEventV1& event,
                           const GifGsDecodeLimitsV1 limits) {
    return decode_dvp_vu_xgkick_gs_stream_v1(
        std::span<const DvpVuXgkickEventV1>{&event, 1U}, limits);
}

GifGsDecodeResultV1 decode_dvp_vu_xgkick_gs_stream_v1(
    const std::span<const DvpVuXgkickEventV1> events,
    const GifGsDecodeLimitsV1 limits) {
    validate_limits(limits);
    if (events.empty()) {
        fail("GIF/GS stream must contain at least one XGKICK event");
    }
    if (events.size() > limits.max_xgkick_events) {
        fail("GIF/GS XGKICK event limit exceeded");
    }

    std::uint64_t total_writes = 0U;
    std::uint64_t total_tags = 0U;
    std::uint64_t total_packet_qwords = 0U;
    for (const auto& event : events) {
        total_writes = checked_add(total_writes,
                                   validate_packet(event, limits),
                                   "stream register writes");
        total_tags = checked_add(total_tags,
                                 event.tags.size(),
                                 "stream GIFtags");
        total_packet_qwords = checked_add(total_packet_qwords,
                                          event.packet_qwords.size(),
                                          "stream packet qwords");
        if (total_writes > limits.max_register_writes) {
            fail("GIF/GS aggregate register write limit exceeded");
        }
        if (total_tags > limits.max_tags) {
            fail("GIF/GS aggregate tag limit exceeded");
        }
        if (total_packet_qwords > limits.max_packet_qwords) {
            fail("GIF/GS aggregate packet qword limit exceeded");
        }
    }

    Decoder decoder{limits};
    for (std::size_t event_index = 0U; event_index < events.size();
         ++event_index) {
        const auto& event = events[event_index];
        decoder.begin_event(event_index);
        for (std::size_t tag_index = 0U; tag_index < event.tags.size();
             ++tag_index) {
            const auto& record = event.tags[tag_index];
            const auto& tag = record.tag;
            decoder.begin_tag(tag);
            for (std::uint64_t loop_index = 0U; loop_index < tag.nloop;
                 ++loop_index) {
                for (std::uint8_t register_index = 0U;
                     register_index < tag.register_count;
                     ++register_index) {
                    const auto payload_offset = checked_add(
                        checked_multiply(loop_index,
                                         tag.register_count,
                                         "PACKED payload offset"),
                        register_index,
                        "PACKED payload offset");
                    const auto packet_index = checked_add(
                        checked_add(record.packet_qword_index,
                                    1U,
                                    "PACKED payload start"),
                        payload_offset,
                        "PACKED payload qword index");
                    const auto descriptor =
                        static_cast<GifGsRegisterDescriptorV1>(
                            tag.registers[register_index]);
                    decoder.process_write(
                        tag_index,
                        loop_index,
                        register_index,
                        packet_index,
                        descriptor,
                        event.packet_qwords[static_cast<std::size_t>(
                            packet_index)]);
                }
            }
        }
    }

    auto result = std::move(decoder).finish();
    if (result.register_writes.size() != total_writes) {
        fail("Internal GIF/GS register write count mismatch");
    }
    return result;
}

} // namespace openrc
