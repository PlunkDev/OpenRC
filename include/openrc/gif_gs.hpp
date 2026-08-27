#pragma once

#include "openrc/dvp_vu_execute.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace openrc {

enum class GifGsRegisterDescriptorV1 : std::uint8_t {
    prim = 0x00U,
    rgbaq = 0x01U,
    st = 0x02U,
    uv = 0x03U,
    xyzf2 = 0x04U,
    xyz2 = 0x05U,
    tex0_1 = 0x06U,
    tex0_2 = 0x07U,
    clamp_1 = 0x08U,
    clamp_2 = 0x09U,
    fog = 0x0aU,
    reserved = 0x0bU,
    xyzf3 = 0x0cU,
    xyz3 = 0x0dU,
    address_data = 0x0eU,
    nop = 0x0fU,
};

enum class GifGsPrimitiveTopologyV1 : std::uint8_t {
    point = 0U,
    line_list = 1U,
    line_strip = 2U,
    triangle_list = 3U,
    triangle_strip = 4U,
    triangle_fan = 5U,
    sprite = 6U,
    invalid = 7U,
};

enum class GifGsVertexKickV1 : std::uint8_t {
    submitted = 0U,
    suppressed,
    indeterminate,
};

enum class GifGsPrimitiveEmissionV1 : std::uint8_t {
    emitted = 0U,
    suppressed,
    indeterminate,
};

struct GifGsPrimitiveStateV1 {
    bool known = false;
    // Raw PRIM always supplies topology. The remaining flags below are the
    // effective attributes selected by PRMODECONT and are meaningful only
    // when attributes_known is true.
    std::uint16_t raw = 0U;
    GifGsPrimitiveTopologyV1 topology = GifGsPrimitiveTopologyV1::invalid;
    bool attributes_known = false;
    std::optional<bool> primitive_attributes_selected;
    std::optional<std::uint16_t> raw_prmode;
    bool gouraud_shading = false;
    bool texture_mapping = false;
    bool fogging = false;
    bool alpha_blending = false;
    bool antialiasing = false;
    bool fixed_texture_coordinates = false;
    bool context_two = false;
    bool fixed_fragment_control = false;

    [[nodiscard]] bool operator==(const GifGsPrimitiveStateV1&) const =
        default;
};

struct GifGsTextureStateV1 {
    std::optional<float> s;
    std::optional<float> t;
    std::optional<float> q;
    std::optional<std::uint16_t> u;
    std::optional<std::uint16_t> v;

    [[nodiscard]] bool operator==(const GifGsTextureStateV1&) const = default;
};

struct GifGsColorStateV1 {
    std::optional<std::uint8_t> r;
    std::optional<std::uint8_t> g;
    std::optional<std::uint8_t> b;
    std::optional<std::uint8_t> a;

    [[nodiscard]] bool operator==(const GifGsColorStateV1&) const = default;
};

// Typed forms of the two per-context raster registers currently consumed by
// the decoder. The original 64-bit register value is retained as two masked
// words so reserved and indeterminate bits are never discarded.
struct GifGsXyOffsetStateV1 {
    DvpVuWordV1 raw_low;
    DvpVuWordV1 raw_high;
    std::optional<std::uint16_t> ofx;
    std::optional<std::uint16_t> ofy;

    [[nodiscard]] bool operator==(const GifGsXyOffsetStateV1&) const =
        default;
};

struct GifGsScissorStateV1 {
    DvpVuWordV1 raw_low;
    DvpVuWordV1 raw_high;
    std::optional<std::uint16_t> scax0;
    std::optional<std::uint16_t> scax1;
    std::optional<std::uint16_t> scay0;
    std::optional<std::uint16_t> scay1;

    [[nodiscard]] bool operator==(const GifGsScissorStateV1&) const =
        default;
};

struct GifGsRasterContextStateV1 {
    std::optional<GifGsXyOffsetStateV1> xy_offset;
    std::optional<GifGsScissorStateV1> scissor;

    [[nodiscard]] bool operator==(const GifGsRasterContextStateV1&) const =
        default;
};

struct GifGsRasterSnapshotV1 {
    // 0 selects XYOFFSET_1/SCISSOR_1 and 1 selects their _2 counterparts.
    // An empty value explicitly means that effective PRIM.CTXT could not be
    // resolved; in that case context remains empty as well.
    std::optional<std::uint8_t> selected_context_index;
    GifGsRasterContextStateV1 context;

    [[nodiscard]] bool operator==(const GifGsRasterSnapshotV1&) const =
        default;
};

struct GifGsRegisterWriteV1 {
    std::uint64_t event_index = 0U;
    std::uint64_t tag_index = 0U;
    std::uint64_t loop_index = 0U;
    std::uint8_t register_index = 0U;
    std::uint64_t packet_qword_index = 0U;
    GifGsRegisterDescriptorV1 descriptor = GifGsRegisterDescriptorV1::nop;
    DvpVuVectorV1 payload;
};

struct GifGsAddressedWriteV1 {
    std::uint64_t register_write_index = 0U;
    std::optional<std::uint8_t> address;
    // The GS dispatch table ignores address bit 7.
    std::optional<std::uint8_t> dispatched_address;
    DvpVuWordV1 data_low;
    DvpVuWordV1 data_high;
};

struct GifGsVertexV1 {
    std::uint64_t register_write_index = 0U;
    std::uint64_t event_index = 0U;
    GifGsRegisterDescriptorV1 source_descriptor =
        GifGsRegisterDescriptorV1::xyz2;
    std::optional<std::uint8_t> addressed_register;
    std::optional<std::uint16_t> x;
    std::optional<std::uint16_t> y;
    std::optional<std::uint32_t> z;
    std::optional<std::uint8_t> fog;
    std::optional<bool> adc;
    GifGsVertexKickV1 kick = GifGsVertexKickV1::indeterminate;
    GifGsTextureStateV1 texture;
    GifGsColorStateV1 color;
    GifGsPrimitiveStateV1 primitive;
    GifGsRasterSnapshotV1 raster;
};

struct GifGsPrimitiveV1 {
    std::uint64_t completion_event_index = 0U;
    GifGsPrimitiveTopologyV1 topology = GifGsPrimitiveTopologyV1::invalid;
    GifGsPrimitiveStateV1 state;
    std::array<std::uint64_t, 3U> vertex_indices{};
    std::uint8_t vertex_count = 0U;
    // Arrival-order strip vertices alternate their effective winding.
    bool strip_winding_reversed = false;
    GifGsPrimitiveEmissionV1 emission =
        GifGsPrimitiveEmissionV1::indeterminate;
    GifGsRasterSnapshotV1 raster;
};

struct GifGsDecodeLimitsV1 {
    std::uint64_t max_tags = 0U;
    std::uint64_t max_packet_qwords = 0U;
    std::uint64_t max_register_writes = 0U;
    std::uint64_t max_vertices = 0U;
    std::uint64_t max_primitives = 0U;
    std::uint64_t max_xgkick_events = 0U;
};

struct GifGsDecodeResultV1 {
    std::vector<GifGsRegisterWriteV1> register_writes;
    std::vector<GifGsAddressedWriteV1> addressed_writes;
    // One entry is retained for every XYZ write, including ADC-suppressed
    // writes because those writes still advance strip/fan assembly.
    std::vector<GifGsVertexV1> vertices;
    // One entry is retained for every completed topology group. Its emission
    // says whether the last vertex actually kicked that primitive.
    std::vector<GifGsPrimitiveV1> primitives;
    GifGsPrimitiveStateV1 final_primitive;
    GifGsTextureStateV1 final_texture;
    GifGsColorStateV1 final_color;
    std::optional<std::uint8_t> final_fog;
    std::uint64_t emitted_primitive_count = 0U;
    std::uint64_t suppressed_primitive_count = 0U;
    std::uint64_t indeterminate_primitive_count = 0U;
    std::uint64_t unassembled_vertex_count = 0U;
    std::uint64_t unsupported_register_write_count = 0U;
    std::uint64_t unresolved_addressed_write_count = 0U;
    std::array<GifGsRasterContextStateV1, 2U> final_raster_contexts;
    GifGsRasterSnapshotV1 final_raster;
};

class GifGsDecodeError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Decodes one complete, bounded PACKED-mode PATH1 packet captured by XGKICK.
// This is a convenience wrapper over the stream decoder and applies all
// limits, including max_xgkick_events, to its one-element stream.
[[nodiscard]] GifGsDecodeResultV1
decode_dvp_vu_xgkick_gs_v1(const DvpVuXgkickEventV1& event,
                           GifGsDecodeLimitsV1 limits);

// Decodes an ordered sequence of complete XGKICK events as one continuous GS
// stream. GIFtag and descriptor framing are cross-checked against each copied
// packet. GS attributes and pending primitive assembly carry across event
// boundaries, while every resource limit applies to the aggregate result.
[[nodiscard]] GifGsDecodeResultV1 decode_dvp_vu_xgkick_gs_stream_v1(
    std::span<const DvpVuXgkickEventV1> events,
    GifGsDecodeLimitsV1 limits);

} // namespace openrc
