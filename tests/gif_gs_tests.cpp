#include "openrc/gif_gs.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr openrc::GifGsDecodeLimitsV1 kLimits{
    64U,
    4096U,
    64U,
    4096U,
    4096U,
    4096U,
};

void expect(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable>
void expect_decode_error(Callable&& callable, const char* const message) {
    try {
        callable();
    } catch (const openrc::GifGsDecodeError&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] openrc::DvpVuWordV1 known_word(const std::uint32_t bits) {
    return openrc::DvpVuWordV1{bits, 0xffffffffU};
}

[[nodiscard]] openrc::DvpVuVectorV1
known_vector(const std::array<std::uint32_t, 4U>& lanes) {
    openrc::DvpVuVectorV1 result;
    for (std::size_t index = 0U; index < lanes.size(); ++index) {
        result.lanes[index] = known_word(lanes[index]);
    }
    return result;
}

[[nodiscard]] std::uint32_t float_bits(const float value) {
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] openrc::DvpVuVectorV1
packed_st(const float s, const float t, const float q) {
    return known_vector({float_bits(s), float_bits(t), float_bits(q), 0U});
}

[[nodiscard]] openrc::DvpVuVectorV1
packed_rgba(const std::uint8_t r,
            const std::uint8_t g,
            const std::uint8_t b,
            const std::uint8_t a) {
    return known_vector({r, g, b, a});
}

[[nodiscard]] openrc::DvpVuVectorV1
packed_uv(const std::uint16_t u, const std::uint16_t v) {
    return known_vector({u, v, 0U, 0U});
}

[[nodiscard]] openrc::DvpVuVectorV1
packed_xyzf(const std::uint16_t x,
            const std::uint16_t y,
            const std::uint32_t z,
            const std::uint8_t fog,
            const bool adc = false) {
    return known_vector({x,
                         y,
                         (z & 0x00ffffffU) << 4U,
                         (static_cast<std::uint32_t>(fog) << 4U) |
                             (adc ? 0x00008000U : 0U)});
}

[[nodiscard]] openrc::DvpVuVectorV1
packed_xyz(const std::uint16_t x,
           const std::uint16_t y,
           const std::uint32_t z,
           const bool adc = false) {
    return known_vector({x, y, z, adc ? 0x00008000U : 0U});
}

[[nodiscard]] openrc::DvpVuVectorV1
addressed(const std::uint64_t data, const std::uint8_t address) {
    return known_vector({static_cast<std::uint32_t>(data),
                         static_cast<std::uint32_t>(data >> 32U),
                         address,
                         0U});
}

struct TagSpec {
    bool pre = false;
    std::uint16_t prim = 0U;
    std::vector<std::uint8_t> registers;
    std::vector<openrc::DvpVuVectorV1> payload;
};

[[nodiscard]] openrc::DvpVuXgkickEventV1
make_event(const std::vector<TagSpec>& specs) {
    openrc::DvpVuXgkickEventV1 event;
    for (std::size_t tag_index = 0U; tag_index < specs.size(); ++tag_index) {
        const auto& spec = specs[tag_index];
        expect(!spec.registers.empty() && spec.registers.size() <= 16U,
               "synthetic tag register count must be in 1..16");
        expect((spec.payload.size() % spec.registers.size()) == 0U,
               "synthetic tag payload must contain whole loops");
        const auto nloop = spec.payload.size() / spec.registers.size();
        expect(nloop <= 0x7fffU, "synthetic tag loop count is too large");
        const bool eop = tag_index + 1U == specs.size();
        const auto raw_nreg = spec.registers.size() == 16U
                                  ? 0U
                                  : spec.registers.size();
        const auto low64 = static_cast<std::uint64_t>(nloop) |
                           (static_cast<std::uint64_t>(eop) << 15U) |
                           (static_cast<std::uint64_t>(spec.pre) << 46U) |
                           (static_cast<std::uint64_t>(spec.prim & 0x07ffU)
                            << 47U) |
                           (static_cast<std::uint64_t>(raw_nreg) << 60U);

        std::uint64_t high64 = 0U;
        std::array<std::uint8_t, 16U> registers{};
        for (std::size_t index = 0U; index < spec.registers.size(); ++index) {
            expect(spec.registers[index] <= 0x0fU,
                   "synthetic descriptor must fit one nibble");
            registers[index] = spec.registers[index];
            high64 |= static_cast<std::uint64_t>(spec.registers[index])
                      << (index * 4U);
        }

        const auto packet_tag_index =
            static_cast<std::uint64_t>(event.packet_qwords.size());
        event.packet_qwords.push_back(known_vector({
            static_cast<std::uint32_t>(low64),
            static_cast<std::uint32_t>(low64 >> 32U),
            static_cast<std::uint32_t>(high64),
            static_cast<std::uint32_t>(high64 >> 32U),
        }));

        openrc::DvpVuGifTagV1 tag;
        tag.nloop = static_cast<std::uint16_t>(nloop);
        tag.eop = eop;
        tag.pre = spec.pre;
        tag.prim = spec.prim;
        tag.format = 0U;
        tag.register_count = static_cast<std::uint8_t>(spec.registers.size());
        tag.payload_qword_count = spec.payload.size();
        tag.registers = registers;
        tag.registers_known = true;
        event.tags.push_back(openrc::DvpVuXgkickTagV1{
            static_cast<std::uint16_t>(packet_tag_index & 0x03ffU),
            tag,
            packet_tag_index,
        });
        event.packet_qwords.insert(event.packet_qwords.end(),
                                   spec.payload.begin(),
                                   spec.payload.end());
    }
    event.packet_complete = true;
    return event;
}

void test_textured_triangle_list() {
    constexpr auto prim = static_cast<std::uint16_t>(
        3U | (1U << 3U) | (1U << 4U) | (1U << 8U) | (1U << 9U));
    std::vector<openrc::DvpVuVectorV1> payload;
    for (std::uint16_t vertex = 0U; vertex < 3U; ++vertex) {
        payload.push_back(packed_st(static_cast<float>(vertex) + 0.25F,
                                    static_cast<float>(vertex) + 0.5F,
                                    static_cast<float>(vertex) + 2.0F));
        payload.push_back(packed_rgba(static_cast<std::uint8_t>(10U + vertex),
                                      20U,
                                      30U,
                                      0x80U));
        payload.push_back(packed_uv(static_cast<std::uint16_t>(100U + vertex),
                                    static_cast<std::uint16_t>(200U + vertex)));
        payload.push_back(packed_xyzf(static_cast<std::uint16_t>(300U + vertex),
                                      static_cast<std::uint16_t>(400U + vertex),
                                      0x00123400U + vertex,
                                      static_cast<std::uint8_t>(50U + vertex)));
    }

    const auto event = make_event({TagSpec{
        true,
        prim,
        {2U, 1U, 3U, 4U},
        std::move(payload),
    }});
    const auto result =
        openrc::decode_dvp_vu_xgkick_gs_v1(event, kLimits);

    expect(result.register_writes.size() == 12U,
           "triangle register write count mismatch");
    expect(result.vertices.size() == 3U,
           "triangle vertex count mismatch");
    expect(result.primitives.size() == 1U &&
               result.emitted_primitive_count == 1U,
           "triangle primitive count mismatch");
    expect(result.final_primitive.known &&
               result.final_primitive.topology ==
                   openrc::GifGsPrimitiveTopologyV1::triangle_list &&
               !result.final_primitive.attributes_known,
           "PRE primitive state mismatch");

    const auto& vertex = result.vertices[1U];
    expect(vertex.x == 301U && vertex.y == 401U &&
               vertex.z == 0x00123401U && vertex.fog == 51U,
           "packed XYZF2 fields mismatch");
    expect(vertex.texture.s == 1.25F && vertex.texture.t == 1.5F &&
               vertex.texture.q == 3.0F && vertex.texture.u == 101U &&
               vertex.texture.v == 201U,
           "packed texture fields mismatch");
    expect(vertex.color.r == 11U && vertex.color.g == 20U &&
               vertex.color.b == 30U && vertex.color.a == 0x80U,
           "packed RGBA fields mismatch");
    expect(vertex.kick == openrc::GifGsVertexKickV1::submitted,
           "XYZF2 should submit a vertex");
    expect(result.primitives[0U].vertex_indices ==
               std::array<std::uint64_t, 3U>{0U, 1U, 2U},
           "triangle list assembly mismatch");
    expect(result.final_fog == 52U,
           "XYZF2 must retain current fog for later XYZ2 writes");
}

void test_tag_q_reset_and_unknown_fields() {
    auto event = make_event({
        TagSpec{false, 0U, {2U}, {packed_st(2.0F, 3.0F, 7.0F)}},
        TagSpec{true,
                0U,
                {1U, 5U},
                {packed_rgba(1U, 2U, 3U, 4U), packed_xyz(9U, 10U, 11U)}},
    });
    const auto result =
        openrc::decode_dvp_vu_xgkick_gs_v1(event, kLimits);
    expect(result.vertices.size() == 1U &&
               result.vertices[0U].texture.q == 1.0F,
           "a non-empty PACKED GIFtag must reset temporary Q to 1.0");
    expect(result.primitives.size() == 1U &&
               result.primitives[0U].emission ==
                   openrc::GifGsPrimitiveEmissionV1::emitted,
           "point primitive should be emitted");

    event = make_event({TagSpec{
        true,
        0U,
        {5U},
        {packed_xyz(1U, 2U, 3U)},
    }});
    event.packet_qwords[1U].lanes[3U] = {};
    const auto unknown =
        openrc::decode_dvp_vu_xgkick_gs_v1(event, kLimits);
    expect(unknown.vertices[0U].kick ==
               openrc::GifGsVertexKickV1::indeterminate &&
               unknown.primitives[0U].emission ==
                   openrc::GifGsPrimitiveEmissionV1::indeterminate &&
               unknown.indeterminate_primitive_count == 1U,
           "indeterminate ADC must remain explicit");
}

void test_adc_strip_continuation() {
    std::vector<openrc::DvpVuVectorV1> payload;
    payload.push_back(packed_xyz(0U, 0U, 0U, true));
    payload.push_back(packed_xyz(1U, 1U, 1U, true));
    payload.push_back(packed_xyz(2U, 2U, 2U, false));
    payload.push_back(packed_xyz(3U, 3U, 3U, true));
    payload.push_back(packed_xyz(4U, 4U, 4U, false));
    const auto event = make_event({TagSpec{
        true,
        4U,
        {5U},
        std::move(payload),
    }});
    const auto result =
        openrc::decode_dvp_vu_xgkick_gs_v1(event, kLimits);

    expect(result.vertices.size() == 5U && result.primitives.size() == 3U,
           "triangle strip assembly count mismatch");
    expect(result.emitted_primitive_count == 2U &&
               result.suppressed_primitive_count == 1U,
           "ADC strip emission counts mismatch");
    expect(result.primitives[0U].vertex_indices ==
               std::array<std::uint64_t, 3U>{0U, 1U, 2U} &&
               result.primitives[1U].vertex_indices ==
                   std::array<std::uint64_t, 3U>{1U, 2U, 3U} &&
               result.primitives[2U].vertex_indices ==
                   std::array<std::uint64_t, 3U>{2U, 3U, 4U},
           "ADC vertices must participate in strip continuation");
    expect(!result.primitives[0U].strip_winding_reversed &&
               result.primitives[1U].strip_winding_reversed &&
               !result.primitives[2U].strip_winding_reversed,
           "triangle strip winding parity mismatch");
}

void test_addressed_registers() {
    const std::uint64_t rgbaq = 0x3fc00000ULL << 32U |
                                0x44332211ULL;
    const std::uint64_t st =
        static_cast<std::uint64_t>(float_bits(6.0F)) |
        (static_cast<std::uint64_t>(float_bits(7.0F)) << 32U);
    const std::uint64_t xyzf =
        0x0123U | (0x0456ULL << 16U) | (0x00654321ULL << 32U) |
        (0x77ULL << 56U);
    const std::uint64_t xyz =
        0x0789U | (0x0abcULL << 16U) | (0x12345678ULL << 32U);
    const auto event = make_event({TagSpec{
        false,
        0U,
        {0x0eU},
        {
            addressed((1U << 4U) | (1U << 8U), 0x1bU),
            addressed(0U, 0x1aU),
            addressed(1U | (1U << 3U), 0x80U),
            addressed(rgbaq, 0x11U),
            addressed(st, 0x02U),
            addressed(xyzf, 0x04U),
            addressed(xyz, 0x05U),
        },
    }});
    const auto result =
        openrc::decode_dvp_vu_xgkick_gs_v1(event, kLimits);

    expect(result.addressed_writes.size() == 7U &&
               result.addressed_writes[2U].address == 0x80U &&
               result.addressed_writes[2U].dispatched_address == 0x00U,
           "A+D address dispatch mismatch");
    expect(result.final_primitive.known &&
               result.final_primitive.topology ==
                   openrc::GifGsPrimitiveTopologyV1::line_list &&
               result.final_primitive.attributes_known &&
               result.final_primitive.primitive_attributes_selected == false &&
               result.final_primitive.raw_prmode ==
                   ((1U << 4U) | (1U << 8U)) &&
               !result.final_primitive.gouraud_shading &&
               result.final_primitive.texture_mapping &&
               result.final_primitive.fixed_texture_coordinates,
           "A+D PRIM was not applied");
    expect(result.vertices.size() == 2U && result.primitives.size() == 1U,
           "A+D geometry was not assembled");
    expect(result.vertices[0U].x == 0x0123U &&
               result.vertices[0U].y == 0x0456U &&
               result.vertices[0U].z == 0x00654321U &&
               result.vertices[0U].fog == 0x77U,
           "A+D XYZF2 fields mismatch");
    expect(result.vertices[1U].x == 0x0789U &&
               result.vertices[1U].y == 0x0abcU &&
               result.vertices[1U].z == 0x12345678U &&
               result.vertices[1U].fog == 0x77U,
           "A+D XYZ2 fields mismatch");
    expect(result.vertices[1U].texture.s == 6.0F &&
               result.vertices[1U].texture.t == 7.0F &&
               result.vertices[1U].texture.q == 1.5F &&
               result.vertices[1U].color.r == 0x11U &&
               result.vertices[1U].color.g == 0x22U &&
               result.vertices[1U].color.b == 0x33U &&
               result.vertices[1U].color.a == 0x44U,
           "A+D ST/RGBAQ fields mismatch");
}

void test_nop_unsupported_and_unresolved_address() {
    auto unknown_address = addressed(0U, 0U);
    unknown_address.lanes[2U].known_mask = 0xffffff00U;
    const auto event = make_event({TagSpec{
        true,
        0U,
        {0x0fU, 0x06U, 0x0eU, 0x0eU},
        {known_vector({0U, 0U, 0U, 0U}),
         known_vector({1U, 2U, 3U, 4U}),
         addressed(0U, 0x40U),
         unknown_address},
    }});
    const auto result =
        openrc::decode_dvp_vu_xgkick_gs_v1(event, kLimits);
    expect(result.register_writes.size() == 4U &&
               result.unsupported_register_write_count == 1U &&
               result.unresolved_addressed_write_count == 1U &&
               !result.final_primitive.known,
           "NOP/unsupported/unresolved accounting mismatch");
}

void test_xgkick_stream_state_and_aggregate_limits() {
    const auto regular_xyz = [](const std::uint16_t x,
                                const std::uint16_t y,
                                const std::uint32_t z) {
        return static_cast<std::uint64_t>(x) |
               (static_cast<std::uint64_t>(y) << 16U) |
               (static_cast<std::uint64_t>(z) << 32U);
    };
    const auto first = make_event({TagSpec{
        false,
        0U,
        {0x0eU},
        {
            addressed(4U, 0x00U),
            addressed(regular_xyz(10U, 20U, 30U), 0x05U),
            addressed(regular_xyz(11U, 21U, 31U), 0x05U),
        },
    }});
    const auto second = make_event({TagSpec{
        false,
        0U,
        {0x05U},
        {packed_xyz(12U, 22U, 32U)},
    }});
    const std::array events{first, second};
    const auto result = openrc::decode_dvp_vu_xgkick_gs_stream_v1(
        std::span<const openrc::DvpVuXgkickEventV1>{events}, kLimits);

    expect(result.register_writes.size() == 4U &&
               result.register_writes[0U].event_index == 0U &&
               result.register_writes[2U].event_index == 0U &&
               result.register_writes[3U].event_index == 1U,
           "stream register provenance mismatch");
    expect(result.vertices.size() == 3U &&
               result.vertices[0U].event_index == 0U &&
               result.vertices[1U].event_index == 0U &&
               result.vertices[2U].event_index == 1U,
           "stream vertex provenance mismatch");
    expect(result.primitives.size() == 1U &&
               result.primitives[0U].completion_event_index == 1U &&
               result.primitives[0U].vertex_indices ==
                   std::array<std::uint64_t, 3U>{0U, 1U, 2U},
           "pending strip did not complete across XGKICK events");

    auto limits = kLimits;
    limits.max_xgkick_events = 1U;
    expect_decode_error(
        [&] {
            (void)openrc::decode_dvp_vu_xgkick_gs_stream_v1(
                std::span<const openrc::DvpVuXgkickEventV1>{events}, limits);
        },
        "aggregate XGKICK event limit must be enforced");
    limits = kLimits;
    limits.max_tags = 1U;
    expect_decode_error(
        [&] {
            (void)openrc::decode_dvp_vu_xgkick_gs_stream_v1(
                std::span<const openrc::DvpVuXgkickEventV1>{events}, limits);
        },
        "aggregate tag limit must be enforced");
    limits = kLimits;
    limits.max_packet_qwords = 5U;
    expect_decode_error(
        [&] {
            (void)openrc::decode_dvp_vu_xgkick_gs_stream_v1(
                std::span<const openrc::DvpVuXgkickEventV1>{events}, limits);
        },
        "aggregate packet qword limit must be enforced");
    limits = kLimits;
    limits.max_register_writes = 3U;
    expect_decode_error(
        [&] {
            (void)openrc::decode_dvp_vu_xgkick_gs_stream_v1(
                std::span<const openrc::DvpVuXgkickEventV1>{events}, limits);
        },
        "aggregate register write limit must be enforced");

    const auto reset = make_event({TagSpec{
        false,
        0U,
        {0x0eU},
        {addressed(4U, 0x00U),
         addressed(regular_xyz(12U, 22U, 32U), 0x05U)},
    }});
    const std::array reset_events{first, reset};
    const auto reset_result = openrc::decode_dvp_vu_xgkick_gs_stream_v1(
        std::span<const openrc::DvpVuXgkickEventV1>{reset_events}, kLimits);
    expect(reset_result.vertices.size() == 3U &&
               reset_result.primitives.empty(),
           "every PRIM write must reset pending primitive assembly");
}

void test_zero_loop_and_special_register_semantics() {
    auto zero_loop = make_event({TagSpec{
        true,
        0x03ffU,
        {0x0fU},
        {},
    }});
    zero_loop.tags[0U].tag.format = 2U;
    zero_loop.tags[0U].tag.register_count = 0U;
    zero_loop.tags[0U].tag.registers_known = false;
    zero_loop.packet_qwords[0U].lanes[2U] = {};
    zero_loop.packet_qwords[0U].lanes[3U] = {};
    const auto empty =
        openrc::decode_dvp_vu_xgkick_gs_v1(zero_loop, kLimits);
    expect(empty.register_writes.empty() && !empty.final_primitive.known,
           "zero-loop tag must ignore PRE/PRIM/FLG/NREG/REGS");

    const auto xyzf3 = make_event({TagSpec{
        true,
        0U,
        {0x0cU},
        {known_vector({0x04560123U, 0x77654321U, 0xdeadbeefU, 0xcafebabeU})},
    }});
    const auto xyzf3_result =
        openrc::decode_dvp_vu_xgkick_gs_v1(xyzf3, kLimits);
    expect(xyzf3_result.vertices.size() == 1U &&
               xyzf3_result.vertices[0U].source_descriptor ==
                   openrc::GifGsRegisterDescriptorV1::xyzf3 &&
               xyzf3_result.vertices[0U].x == 0x0123U &&
               xyzf3_result.vertices[0U].y == 0x0456U &&
               xyzf3_result.vertices[0U].z == 0x00654321U &&
               xyzf3_result.vertices[0U].fog == 0x77U &&
               xyzf3_result.vertices[0U].kick ==
                   openrc::GifGsVertexKickV1::suppressed,
           "XYZF3 must use lower-64-bit layout without a drawing kick");

    auto partial_address = addressed(0U, 0U);
    partial_address.lanes[2U].known_mask = 0xffffff7fU;
    const auto alias = make_event({TagSpec{
        false,
        0U,
        {0x0eU},
        {partial_address},
    }});
    const auto alias_result =
        openrc::decode_dvp_vu_xgkick_gs_v1(alias, kLimits);
    expect(!alias_result.addressed_writes[0U].address.has_value() &&
               alias_result.addressed_writes[0U].dispatched_address == 0U &&
               alias_result.unresolved_addressed_write_count == 0U &&
               alias_result.final_primitive.known,
           "A+D dispatch must only require known address bits 0..6");

    auto unknown_address = addressed(0U, 0U);
    unknown_address.lanes[2U].known_mask = 0xffffff00U;
    const auto q_latch = make_event({TagSpec{
        true,
        0U,
        {0x02U, 0x0eU, 0x01U, 0x05U},
        {packed_st(1.0F, 2.0F, 5.0F),
         unknown_address,
         packed_rgba(1U, 2U, 3U, 4U),
         packed_xyz(1U, 2U, 3U)},
    }});
    const auto q_result =
        openrc::decode_dvp_vu_xgkick_gs_v1(q_latch, kLimits);
    expect(q_result.vertices.size() == 1U &&
               q_result.vertices[0U].texture.q == 5.0F,
           "unresolved A+D must not alter the GIF temporary Q latch");
}

void test_raster_context_state_across_xgkick_events() {
    const auto xy_offset = [](const std::uint16_t ofx,
                              const std::uint16_t ofy,
                              const std::uint16_t reserved_low,
                              const std::uint16_t reserved_high) {
        return static_cast<std::uint64_t>(ofx) |
               (static_cast<std::uint64_t>(reserved_low) << 16U) |
               (static_cast<std::uint64_t>(ofy) << 32U) |
               (static_cast<std::uint64_t>(reserved_high) << 48U);
    };
    const auto scissor = [](const std::uint16_t x0,
                            const std::uint16_t x1,
                            const std::uint16_t y0,
                            const std::uint16_t y1) {
        return static_cast<std::uint64_t>(x0 & 0x07ffU) |
               (0x1fULL << 11U) |
               (static_cast<std::uint64_t>(x1 & 0x07ffU) << 16U) |
               (0x1fULL << 27U) |
               (static_cast<std::uint64_t>(y0 & 0x07ffU) << 32U) |
               (0x1fULL << 43U) |
               (static_cast<std::uint64_t>(y1 & 0x07ffU) << 48U) |
               (0x1fULL << 59U);
    };

    constexpr auto old_xy1 = 0xa5a556785a5a1234ULL;
    const auto first = make_event({TagSpec{
        false,
        0U,
        {0x0eU},
        {
            addressed(1U, 0x1aU),
            addressed(0U, 0x00U),
            addressed(old_xy1, 0x18U),
            addressed(scissor(1U, 100U, 2U, 200U), 0x40U),
            addressed(xy_offset(0x3333U, 0x4444U, 0x1357U, 0x2468U),
                      0x19U),
            addressed(scissor(11U, 111U, 22U, 222U), 0x41U),
        },
    }});

    const auto new_xy1 = xy_offset(0x7777U, 0x8888U, 0xabcdU, 0xef01U);
    const auto second = make_event({TagSpec{
        false,
        0U,
        {0x05U, 0x0eU, 0x0eU, 0x05U, 0x0eU, 0x0eU, 0x05U},
        {
            packed_xyz(10U, 20U, 30U),
            addressed(new_xy1, 0x18U),
            addressed(scissor(3U, 300U, 4U, 400U), 0x40U),
            packed_xyz(11U, 21U, 31U),
            addressed(0U, 0x1aU),
            addressed(1U << 9U, 0x1bU),
            packed_xyz(12U, 22U, 32U),
        },
    }});
    const std::array events{first, second};
    const auto result = openrc::decode_dvp_vu_xgkick_gs_stream_v1(
        std::span<const openrc::DvpVuXgkickEventV1>{events}, kLimits);

    expect(result.vertices.size() == 3U && result.primitives.size() == 3U,
           "raster-state point stream count mismatch");
    const auto& first_raster = result.vertices[0U].raster;
    expect(first_raster.selected_context_index == 0U &&
               first_raster.context.xy_offset.has_value() &&
               first_raster.context.scissor.has_value(),
           "event-one raster state did not carry into event two");
    expect(first_raster.context.xy_offset->ofx == 0x1234U &&
               first_raster.context.xy_offset->ofy == 0x5678U &&
               first_raster.context.xy_offset->raw_low.bits ==
                   static_cast<std::uint32_t>(old_xy1) &&
               first_raster.context.xy_offset->raw_high.bits ==
                   static_cast<std::uint32_t>(old_xy1 >> 32U),
           "XYOFFSET_1 fields or reserved raw bits mismatch");
    expect(first_raster.context.scissor->scax0 == 1U &&
               first_raster.context.scissor->scax1 == 100U &&
               first_raster.context.scissor->scay0 == 2U &&
               first_raster.context.scissor->scay1 == 200U,
           "SCISSOR_1 field layout mismatch");

    const auto& second_raster = result.vertices[1U].raster;
    expect(second_raster.selected_context_index == 0U &&
               second_raster.context.xy_offset->ofx == 0x7777U &&
               second_raster.context.xy_offset->ofy == 0x8888U &&
               second_raster.context.scissor->scax0 == 3U &&
               second_raster.context.scissor->scax1 == 300U &&
               second_raster.context.scissor->scay0 == 4U &&
               second_raster.context.scissor->scay1 == 400U,
           "updated context-one raster state mismatch");
    expect(result.vertices[0U].raster == result.primitives[0U].raster &&
               result.vertices[1U].raster == result.primitives[1U].raster &&
               result.vertices[0U].raster != result.vertices[1U].raster,
           "vertex/primitive raster snapshots were retroactively changed");

    const auto& third_raster = result.vertices[2U].raster;
    expect(third_raster.selected_context_index == 1U &&
               third_raster.context.xy_offset->ofx == 0x3333U &&
               third_raster.context.xy_offset->ofy == 0x4444U &&
               third_raster.context.scissor->scax0 == 11U &&
               third_raster.context.scissor->scax1 == 111U &&
               third_raster.context.scissor->scay0 == 22U &&
               third_raster.context.scissor->scay1 == 222U &&
               result.final_raster == third_raster,
           "effective PRMODE context-two selection mismatch");
    expect(result.final_raster_contexts[0U].xy_offset->raw_low.bits ==
               static_cast<std::uint32_t>(new_xy1) &&
               result.final_raster_contexts[1U].xy_offset->ofx == 0x3333U &&
               result.unsupported_register_write_count == 0U,
           "typed raster writes were not retained in final state");
}

void test_partial_raster_fields_and_unresolved_context() {
    auto partial_xy = addressed(0x246813579abc4321ULL, 0x18U);
    partial_xy.lanes[1U].known_mask = 0xffff0000U;
    auto partial_scissor = addressed(
        static_cast<std::uint64_t>(7U) | (70ULL << 16U) |
            (8ULL << 32U) | (80ULL << 48U),
        0x40U);
    partial_scissor.lanes[0U].known_mask &= ~(0x07ffU << 16U);
    auto unknown_prmodecont = addressed(0U, 0x1aU);
    unknown_prmodecont.lanes[0U].known_mask &= ~1U;

    const auto event = make_event({TagSpec{
        false,
        0U,
        {0x0eU, 0x0eU, 0x0eU, 0x0eU, 0x05U, 0x0eU, 0x05U},
        {
            addressed(1U, 0x1aU),
            addressed(0U, 0x00U),
            partial_xy,
            partial_scissor,
            packed_xyz(1U, 2U, 3U),
            unknown_prmodecont,
            packed_xyz(4U, 5U, 6U),
        },
    }});
    const auto result =
        openrc::decode_dvp_vu_xgkick_gs_v1(event, kLimits);

    expect(result.vertices.size() == 2U &&
               result.vertices[0U].raster.selected_context_index == 0U &&
               !result.vertices[1U].raster.selected_context_index.has_value(),
           "indeterminate effective PRIM.CTXT must remain explicit");
    const auto& xy = *result.vertices[0U].raster.context.xy_offset;
    expect(xy.ofx == 0x4321U && !xy.ofy.has_value() &&
               xy.raw_high.bits == partial_xy.lanes[1U].bits &&
               xy.raw_high.known_mask == partial_xy.lanes[1U].known_mask,
           "partial XYOFFSET field masks were not preserved");
    const auto& sc = *result.vertices[0U].raster.context.scissor;
    expect(sc.scax0 == 7U && !sc.scax1.has_value() &&
               sc.scay0 == 8U && sc.scay1 == 80U &&
               sc.raw_low.known_mask == partial_scissor.lanes[0U].known_mask,
           "partial SCISSOR field masks were not preserved");
    expect(!result.final_raster.selected_context_index.has_value() &&
               !result.final_raster.context.xy_offset.has_value() &&
               !result.final_raster.context.scissor.has_value(),
           "final unresolved raster selection must not imply a context");
}

void test_limits_and_malformed_framing() {
    const auto triangle = make_event({TagSpec{
        true,
        3U,
        {5U},
        {packed_xyz(0U, 0U, 0U),
         packed_xyz(1U, 1U, 1U),
         packed_xyz(2U, 2U, 2U)},
    }});

    auto limits = kLimits;
    limits.max_vertices = 2U;
    expect_decode_error(
        [&] { (void)openrc::decode_dvp_vu_xgkick_gs_v1(triangle, limits); },
        "vertex limit must be enforced");
    limits = kLimits;
    limits.max_register_writes = 2U;
    expect_decode_error(
        [&] { (void)openrc::decode_dvp_vu_xgkick_gs_v1(triangle, limits); },
        "register write limit must be enforced");
    limits = kLimits;
    limits.max_packet_qwords = 3U;
    expect_decode_error(
        [&] { (void)openrc::decode_dvp_vu_xgkick_gs_v1(triangle, limits); },
        "packet limit must be enforced");
    limits = kLimits;
    limits.max_primitives = 1U;
    const auto points = make_event({TagSpec{
        true,
        0U,
        {5U},
        {packed_xyz(0U, 0U, 0U), packed_xyz(1U, 1U, 1U)},
    }});
    expect_decode_error(
        [&] { (void)openrc::decode_dvp_vu_xgkick_gs_v1(points, limits); },
        "primitive limit must be enforced");

    limits = kLimits;
    limits.max_tags = 1U;
    const auto two_tags = make_event({
        TagSpec{false, 0U, {0x0fU}, {known_vector({0U, 0U, 0U, 0U})}},
        TagSpec{false, 0U, {0x0fU}, {known_vector({0U, 0U, 0U, 0U})}},
    });
    expect_decode_error(
        [&] { (void)openrc::decode_dvp_vu_xgkick_gs_v1(two_tags, limits); },
        "tag limit must be enforced");

    limits = kLimits;
    limits.max_tags = 0U;
    expect_decode_error(
        [&] { (void)openrc::decode_dvp_vu_xgkick_gs_v1(triangle, limits); },
        "zero limits must be rejected");

    auto malformed = triangle;
    malformed.packet_complete = false;
    expect_decode_error(
        [&] { (void)openrc::decode_dvp_vu_xgkick_gs_v1(malformed, kLimits); },
        "incomplete packet must be rejected");
    malformed = triangle;
    malformed.tags[0U].tag.format = 1U;
    expect_decode_error(
        [&] { (void)openrc::decode_dvp_vu_xgkick_gs_v1(malformed, kLimits); },
        "non-PACKED tag must be rejected");
    malformed = triangle;
    malformed.tags[0U].tag.registers_known = false;
    expect_decode_error(
        [&] { (void)openrc::decode_dvp_vu_xgkick_gs_v1(malformed, kLimits); },
        "indeterminate descriptors must be rejected");
    malformed = triangle;
    malformed.tags[0U].packet_qword_index = 1U;
    expect_decode_error(
        [&] { (void)openrc::decode_dvp_vu_xgkick_gs_v1(malformed, kLimits); },
        "non-contiguous tags must be rejected");
    malformed = triangle;
    malformed.packet_qwords.push_back(known_vector({0U, 0U, 0U, 0U}));
    expect_decode_error(
        [&] { (void)openrc::decode_dvp_vu_xgkick_gs_v1(malformed, kLimits); },
        "trailing packet qwords must be rejected");
    malformed = triangle;
    malformed.tags[0U].tag.registers[0U] = 4U;
    expect_decode_error(
        [&] { (void)openrc::decode_dvp_vu_xgkick_gs_v1(malformed, kLimits); },
        "descriptor metadata mismatch must be rejected");
}

} // namespace

int main() {
    try {
        test_textured_triangle_list();
        test_tag_q_reset_and_unknown_fields();
        test_adc_strip_continuation();
        test_addressed_registers();
        test_nop_unsupported_and_unresolved_address();
        test_xgkick_stream_state_and_aggregate_limits();
        test_zero_loop_and_special_register_semantics();
        test_raster_context_state_across_xgkick_events();
        test_partial_raster_fields_and_unresolved_context();
        test_limits_and_malformed_framing();
        std::cout << "gif_gs_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "gif_gs_tests: " << error.what() << '\n';
        return 1;
    }
}
