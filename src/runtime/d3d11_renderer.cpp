#include "d3d11_renderer.hpp"

#include "generated/wireframe_ps_dxbc.hpp"
#include "generated/wireframe_vs_dxbc.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace openrc::runtime {
namespace {

using Microsoft::WRL::ComPtr;

struct alignas(16) FitConstants {
    std::array<float, 2U> scale{};
    std::array<float, 2U> offset{};
};

static_assert(sizeof(FitConstants) == 16U);

constexpr float kPi = 3.14159265358979323846F;
constexpr float kDefaultVerticalFov = kPi / 3.0F;
constexpr float kMaximumPitch = 89.0F * kPi / 180.0F;

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] Vec3 operator+(const Vec3 a, const Vec3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] Vec3 operator-(const Vec3 a, const Vec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vec3 operator*(const Vec3 value, const float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] float dot(const Vec3 a, const Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Vec3 cross(const Vec3 a, const Vec3 b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] Vec3 normalized(const Vec3 value) noexcept {
    const auto length_squared = dot(value, value);
    if (!(length_squared > 0.0F) || !std::isfinite(length_squared)) {
        return {};
    }
    return value * (1.0F / std::sqrt(length_squared));
}

[[nodiscard]] Vec3 as_vec3(const SceneVertex3dV1& vertex) noexcept {
    return {vertex.x, vertex.y, vertex.z};
}

[[nodiscard]] std::string hresult_message(
    const char* const operation,
    const HRESULT result) {
    std::ostringstream stream;
    stream << operation << " failed (HRESULT 0x" << std::hex
           << std::uppercase << std::setw(8) << std::setfill('0')
           << static_cast<std::uint32_t>(result) << ')';
    return stream.str();
}

void require_success(const HRESULT result, const char* const operation) {
    if (FAILED(result)) {
        throw std::runtime_error(hresult_message(operation, result));
    }
}

[[nodiscard]] UINT checked_buffer_size(
    const std::size_t item_count,
    const std::size_t item_size,
    const char* const description) {
    if (item_count == 0U || item_size == 0U ||
        item_count >
            static_cast<std::size_t>(std::numeric_limits<UINT>::max()) /
                item_size) {
        throw std::runtime_error(
            std::string(description) +
            " is empty or exceeds the D3D11 buffer-size limit");
    }
    return static_cast<UINT>(item_count * item_size);
}

} // namespace

struct D3d11Renderer::Implementation {
    explicit Implementation(HWND native_window,
                            const SceneGeometryV1& geometry)
        : Implementation(native_window, geometry, nullptr) {}

    Implementation(HWND native_window,
                   const SceneGeometryV1& geometry,
                   const SceneGeometry3dV1& source_geometry)
        : Implementation(native_window, geometry, &source_geometry) {}

    Implementation(HWND native_window,
                   const SceneGeometryV1& geometry,
                   const SceneGeometry3dV1* const source_geometry)
        : window(native_window),
          minimum_x(geometry.minimum_x),
          maximum_x(geometry.maximum_x),
          minimum_y(geometry.minimum_y),
          maximum_y(geometry.maximum_y) {
        if (window == nullptr) {
            throw std::invalid_argument(
                "The D3D11 renderer requires a valid window handle");
        }
        if (geometry.vertices.empty() ||
            geometry.triangle_indices.empty()) {
            throw std::invalid_argument(
                "The D3D11 renderer requires non-empty triangle geometry");
        }
        if (geometry.triangle_indices.size() >
            static_cast<std::size_t>(std::numeric_limits<UINT>::max())) {
            throw std::runtime_error(
                "The D3D11 index count exceeds the 32-bit draw limit");
        }
        raster_index_count =
            static_cast<UINT>(geometry.triangle_indices.size());

        if (source_geometry != nullptr) {
            initialize_source_geometry(*source_geometry);
        }

        create_device_and_swap_chain();
        create_pipeline();
        create_raster_geometry_buffers(geometry);
        if (source_geometry != nullptr) {
            create_source_geometry_buffers(*source_geometry);
        }
        create_render_target();

        RECT client_rectangle{};
        if (GetClientRect(window, &client_rectangle) == FALSE) {
            throw std::runtime_error(
                "GetClientRect failed while initializing the D3D11 renderer");
        }
        const auto client_width =
            std::max<LONG>(0, client_rectangle.right - client_rectangle.left);
        const auto client_height =
            std::max<LONG>(0, client_rectangle.bottom - client_rectangle.top);
        set_dimensions(static_cast<std::uint32_t>(client_width),
                       static_cast<std::uint32_t>(client_height));
    }

    void initialize_source_geometry(const SceneGeometry3dV1& geometry) {
        if (geometry.vertices.empty() || geometry.triangle_indices.empty()) {
            throw std::invalid_argument(
                "The D3D11 source-space renderer requires non-empty triangle geometry");
        }
        if (geometry.triangle_indices.size() >
            static_cast<std::size_t>(std::numeric_limits<UINT>::max())) {
            throw std::runtime_error(
                "The D3D11 source-space index count exceeds the 32-bit draw limit");
        }
        for (const auto index : geometry.triangle_indices) {
            if (index >= geometry.vertices.size()) {
                throw std::invalid_argument(
                    "The D3D11 source-space geometry contains an invalid index");
            }
        }

        source_vertices = geometry.vertices;
        projected_vertices.resize(source_vertices.size());
        for (std::size_t index = 0U; index < source_vertices.size(); ++index) {
            projected_vertices[index].rgba = source_vertices[index].rgba;
        }
        source_index_count =
            static_cast<UINT>(geometry.triangle_indices.size());
        has_source_geometry = true;
        show_source_geometry = true;

        orbit_target = {
            (geometry.minimum_x + geometry.maximum_x) * 0.5F,
            (geometry.minimum_y + geometry.maximum_y) * 0.5F,
            (geometry.minimum_z + geometry.maximum_z) * 0.5F,
        };
        orbit_radius = 0.0F;
        for (const auto& vertex : source_vertices) {
            const auto relative = as_vec3(vertex) - orbit_target;
            orbit_radius = std::max(orbit_radius, std::sqrt(dot(relative, relative)));
        }
        if (!(orbit_radius > 0.0F) || !std::isfinite(orbit_radius)) {
            orbit_radius = 1.0F;
        }
    }

    void create_device_and_swap_chain() {
        DXGI_SWAP_CHAIN_DESC swap_chain_description{};
        swap_chain_description.BufferDesc.Format =
            DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_chain_description.SampleDesc.Count = 1U;
        swap_chain_description.SampleDesc.Quality = 0U;
        swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_description.BufferCount = 2U;
        swap_chain_description.OutputWindow = window;
        swap_chain_description.Windowed = TRUE;
        swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr std::array<D3D_FEATURE_LEVEL, 3U> kFeatureLevels{
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
        constexpr UINT kDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        auto result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            kDeviceFlags,
            kFeatureLevels.data(),
            static_cast<UINT>(kFeatureLevels.size()),
            D3D11_SDK_VERSION,
            &swap_chain_description,
            swap_chain.GetAddressOf(),
            device.GetAddressOf(),
            &feature_level,
            context.GetAddressOf());
        const auto hardware_result = result;
        if (FAILED(result)) {
            swap_chain.Reset();
            context.Reset();
            device.Reset();
            result = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                kDeviceFlags,
                kFeatureLevels.data(),
                static_cast<UINT>(kFeatureLevels.size()),
                D3D11_SDK_VERSION,
                &swap_chain_description,
                swap_chain.GetAddressOf(),
                device.GetAddressOf(),
                &feature_level,
                context.GetAddressOf());
        }
        if (FAILED(result)) {
            throw std::runtime_error(
                hresult_message(
                    "D3D11 hardware-device creation", hardware_result) +
                "; " + hresult_message("D3D11 WARP-device creation", result));
        }
    }

    void create_pipeline() {
        require_success(
            device->CreateVertexShader(
                g_openrc_wireframe_vs,
                sizeof(g_openrc_wireframe_vs),
                nullptr,
                vertex_shader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader");
        require_success(
            device->CreatePixelShader(
                g_openrc_wireframe_ps,
                sizeof(g_openrc_wireframe_ps),
                nullptr,
                pixel_shader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader");

        constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2U> kInputElements{{
            {"POSITION",
             0U,
             DXGI_FORMAT_R32G32_FLOAT,
             0U,
             0U,
             D3D11_INPUT_PER_VERTEX_DATA,
             0U},
            {"COLOR",
             0U,
             DXGI_FORMAT_R8G8B8A8_UNORM,
             0U,
             static_cast<UINT>(offsetof(SceneVertexV1, rgba)),
             D3D11_INPUT_PER_VERTEX_DATA,
             0U},
        }};
        require_success(
            device->CreateInputLayout(
                kInputElements.data(),
                static_cast<UINT>(kInputElements.size()),
                g_openrc_wireframe_vs,
                sizeof(g_openrc_wireframe_vs),
                input_layout.GetAddressOf()),
            "ID3D11Device::CreateInputLayout");

        D3D11_RASTERIZER_DESC rasterizer_description{};
        rasterizer_description.FillMode = D3D11_FILL_WIREFRAME;
        rasterizer_description.CullMode = D3D11_CULL_NONE;
        rasterizer_description.DepthClipEnable = TRUE;
        rasterizer_description.AntialiasedLineEnable = TRUE;
        require_success(
            device->CreateRasterizerState(
                &rasterizer_description, rasterizer_state.GetAddressOf()),
            "ID3D11Device::CreateRasterizerState");

        D3D11_DEPTH_STENCIL_DESC depth_description{};
        depth_description.DepthEnable = FALSE;
        depth_description.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depth_description.DepthFunc = D3D11_COMPARISON_ALWAYS;
        require_success(
            device->CreateDepthStencilState(
                &depth_description, depth_state.GetAddressOf()),
            "ID3D11Device::CreateDepthStencilState");

        D3D11_BUFFER_DESC constant_description{};
        constant_description.ByteWidth = sizeof(FitConstants);
        constant_description.Usage = D3D11_USAGE_DEFAULT;
        constant_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        require_success(
            device->CreateBuffer(
                &constant_description, nullptr, fit_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(fit constants)");
    }

    void create_raster_geometry_buffers(const SceneGeometryV1& geometry) {
        D3D11_BUFFER_DESC vertex_description{};
        vertex_description.ByteWidth = checked_buffer_size(
            geometry.vertices.size(), sizeof(SceneVertexV1), "Vertex buffer");
        vertex_description.Usage = D3D11_USAGE_IMMUTABLE;
        vertex_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertex_data{};
        vertex_data.pSysMem = geometry.vertices.data();
        require_success(
            device->CreateBuffer(
                &vertex_description,
                &vertex_data,
                raster_vertex_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(raster vertices)");

        D3D11_BUFFER_DESC index_description{};
        index_description.ByteWidth = checked_buffer_size(
            geometry.triangle_indices.size(),
            sizeof(std::uint32_t),
            "Index buffer");
        index_description.Usage = D3D11_USAGE_IMMUTABLE;
        index_description.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA index_data{};
        index_data.pSysMem = geometry.triangle_indices.data();
        require_success(
            device->CreateBuffer(
                &index_description,
                &index_data,
                raster_index_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(raster indices)");
    }

    void create_source_geometry_buffers(const SceneGeometry3dV1& geometry) {
        D3D11_BUFFER_DESC vertex_description{};
        vertex_description.ByteWidth = checked_buffer_size(
            projected_vertices.size(),
            sizeof(SceneVertexV1),
            "Projected source vertex buffer");
        vertex_description.Usage = D3D11_USAGE_DYNAMIC;
        vertex_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertex_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        require_success(
            device->CreateBuffer(
                &vertex_description,
                nullptr,
                source_vertex_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(projected source vertices)");

        D3D11_BUFFER_DESC index_description{};
        index_description.ByteWidth = checked_buffer_size(
            geometry.triangle_indices.size(),
            sizeof(std::uint32_t),
            "Source index buffer");
        index_description.Usage = D3D11_USAGE_IMMUTABLE;
        index_description.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA index_data{};
        index_data.pSysMem = geometry.triangle_indices.data();
        require_success(
            device->CreateBuffer(
                &index_description,
                &index_data,
                source_index_buffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer(source indices)");
    }

    void create_render_target() {
        ComPtr<ID3D11Texture2D> back_buffer;
        require_success(
            swap_chain->GetBuffer(
                0U,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(back_buffer.GetAddressOf())),
            "IDXGISwapChain::GetBuffer");
        require_success(
            device->CreateRenderTargetView(
                back_buffer.Get(), nullptr, render_target.GetAddressOf()),
            "ID3D11Device::CreateRenderTargetView");
    }

    [[nodiscard]] float aspect_ratio() const noexcept {
        if (width == 0U || height == 0U) {
            return 16.0F / 9.0F;
        }
        return static_cast<float>(width) / static_cast<float>(height);
    }

    void reset_camera() noexcept {
        if (!has_source_geometry) {
            return;
        }
        orbit_yaw = 45.0F * kPi / 180.0F;
        orbit_pitch = 25.0F * kPi / 180.0F;

        const auto half_vertical = kDefaultVerticalFov * 0.5F;
        const auto half_horizontal = std::atan(
            std::tan(half_vertical) * std::max(aspect_ratio(), 0.01F));
        const auto limiting_half_fov =
            std::max(0.01F, std::min(half_vertical, half_horizontal));
        orbit_distance = std::max(
            orbit_radius * 1.05F,
            orbit_radius * 1.10F / std::sin(limiting_half_fov));
        camera_initialized = true;
        projected_vertices_dirty = true;
    }

    void orbit(const float yaw_delta, const float pitch_delta) noexcept {
        if (!has_source_geometry || !std::isfinite(yaw_delta) ||
            !std::isfinite(pitch_delta)) {
            return;
        }
        orbit_yaw = std::remainder(orbit_yaw + yaw_delta, 2.0F * kPi);
        orbit_pitch = std::clamp(
            orbit_pitch + pitch_delta, -kMaximumPitch, kMaximumPitch);
        projected_vertices_dirty = true;
    }

    void zoom(const float wheel_steps) noexcept {
        if (!has_source_geometry || !std::isfinite(wheel_steps)) {
            return;
        }
        const auto scale = std::exp(-0.15F * wheel_steps);
        orbit_distance = std::clamp(
            orbit_distance * scale,
            orbit_radius * 1.05F,
            orbit_radius * 100.0F);
        projected_vertices_dirty = true;
    }

    void toggle_view_mode() noexcept {
        if (has_source_geometry) {
            show_source_geometry = !show_source_geometry;
        }
    }

    [[nodiscard]] bool is_3d_view() const noexcept {
        return has_source_geometry && show_source_geometry;
    }

    void update_projected_vertices() {
        if (!has_source_geometry || !projected_vertices_dirty || width == 0U ||
            height == 0U) {
            return;
        }
        if (!camera_initialized) {
            reset_camera();
        }

        const auto cosine_pitch = std::cos(orbit_pitch);
        const Vec3 target_to_eye{
            cosine_pitch * std::cos(orbit_yaw),
            cosine_pitch * std::sin(orbit_yaw),
            std::sin(orbit_pitch),
        };
        const auto eye = orbit_target + target_to_eye * orbit_distance;
        const auto forward = normalized(orbit_target - eye);
        constexpr Vec3 kDebugUp{0.0F, 0.0F, 1.0F};
        const auto right = normalized(cross(forward, kDebugUp));
        const auto camera_up = normalized(cross(right, forward));

        const auto tangent_half_vertical =
            std::tan(kDefaultVerticalFov * 0.5F);
        const auto tangent_half_horizontal =
            tangent_half_vertical * aspect_ratio();
        const auto minimum_camera_z = orbit_radius * 0.001F;

        for (std::size_t index = 0U; index < source_vertices.size(); ++index) {
            const auto relative = as_vec3(source_vertices[index]) - eye;
            const auto camera_z = std::max(dot(relative, forward), minimum_camera_z);
            projected_vertices[index].x =
                dot(relative, right) /
                (camera_z * tangent_half_horizontal);
            projected_vertices[index].y =
                dot(relative, camera_up) /
                (camera_z * tangent_half_vertical);
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        require_success(
            context->Map(
                source_vertex_buffer.Get(),
                0U,
                D3D11_MAP_WRITE_DISCARD,
                0U,
                &mapped),
            "ID3D11DeviceContext::Map(projected source vertices)");
        std::memcpy(
            mapped.pData,
            projected_vertices.data(),
            projected_vertices.size() * sizeof(SceneVertexV1));
        context->Unmap(source_vertex_buffer.Get(), 0U);
        projected_vertices_dirty = false;
    }

    void set_dimensions(const std::uint32_t new_width,
                        const std::uint32_t new_height) {
        width = new_width;
        height = new_height;
        if (width == 0U || height == 0U) {
            return;
        }

        constexpr float kPreferredMarginPixels = 32.0F;
        const auto margin = std::min(
            kPreferredMarginPixels,
            0.1F * static_cast<float>(std::min(width, height)));
        const auto drawable_width = std::max(
            1.0F, static_cast<float>(width) - 2.0F * margin);
        const auto drawable_height = std::max(
            1.0F, static_cast<float>(height) - 2.0F * margin);
        const auto scene_width = std::max(1.0F, maximum_x - minimum_x);
        const auto scene_height = std::max(1.0F, maximum_y - minimum_y);
        const auto pixels_per_scene_unit = std::min(
            drawable_width / scene_width, drawable_height / scene_height);
        const auto center_x = (minimum_x + maximum_x) * 0.5F;
        const auto center_y = (minimum_y + maximum_y) * 0.5F;

        fit_constants.scale = {
            2.0F * pixels_per_scene_unit / static_cast<float>(width),
            2.0F * pixels_per_scene_unit / static_cast<float>(height),
        };
        fit_constants.offset = {
            -center_x * fit_constants.scale[0U],
            -center_y * fit_constants.scale[1U],
        };
        if (has_source_geometry) {
            if (!camera_initialized) {
                reset_camera();
            }
            projected_vertices_dirty = true;
        }
    }

    void resize(const std::uint32_t new_width,
                const std::uint32_t new_height) {
        if (new_width == 0U || new_height == 0U) {
            set_dimensions(0U, 0U);
            return;
        }
        if (new_width == width && new_height == height && render_target) {
            return;
        }

        context->OMSetRenderTargets(0U, nullptr, nullptr);
        render_target.Reset();
        require_success(
            swap_chain->ResizeBuffers(
                0U, new_width, new_height, DXGI_FORMAT_UNKNOWN, 0U),
            "IDXGISwapChain::ResizeBuffers");
        create_render_target();
        set_dimensions(new_width, new_height);
    }

    void render() {
        if (width == 0U || height == 0U || !render_target) {
            return;
        }

        const auto render_source = is_3d_view();
        if (render_source) {
            update_projected_vertices();
        }

        constexpr std::array<float, 4U> kClearColor{
            5.0F / 255.0F,
            8.0F / 255.0F,
            18.0F / 255.0F,
            1.0F,
        };
        constexpr FitConstants kSourceFitConstants{
            {1.0F, 1.0F},
            {0.0F, 0.0F},
        };
        const auto& active_fit_constants =
            render_source ? kSourceFitConstants : fit_constants;
        context->UpdateSubresource(
            fit_buffer.Get(), 0U, nullptr, &active_fit_constants, 0U, 0U);
        context->ClearRenderTargetView(render_target.Get(), kClearColor.data());

        ID3D11RenderTargetView* const render_targets[] = {
            render_target.Get()};
        context->OMSetRenderTargets(1U, render_targets, nullptr);
        context->OMSetDepthStencilState(depth_state.Get(), 0U);

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0F;
        viewport.MaxDepth = 1.0F;
        context->RSSetViewports(1U, &viewport);
        context->RSSetState(rasterizer_state.Get());

        constexpr UINT kStride = sizeof(SceneVertexV1);
        constexpr UINT kOffset = 0U;
        ID3D11Buffer* const vertex_buffers[] = {
            render_source ? source_vertex_buffer.Get() :
                            raster_vertex_buffer.Get()};
        context->IASetInputLayout(input_layout.Get());
        context->IASetVertexBuffers(
            0U, 1U, vertex_buffers, &kStride, &kOffset);
        context->IASetIndexBuffer(
            render_source ? source_index_buffer.Get() : raster_index_buffer.Get(),
            DXGI_FORMAT_R32_UINT,
            0U);
        context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11Buffer* const constant_buffers[] = {fit_buffer.Get()};
        context->VSSetShader(vertex_shader.Get(), nullptr, 0U);
        context->VSSetConstantBuffers(0U, 1U, constant_buffers);
        context->PSSetShader(pixel_shader.Get(), nullptr, 0U);
        context->DrawIndexed(
            render_source ? source_index_count : raster_index_count, 0U, 0);

        const auto result = swap_chain->Present(1U, 0U);
        if (result == DXGI_STATUS_OCCLUDED) {
            return;
        }
        if (result == DXGI_ERROR_DEVICE_REMOVED ||
            result == DXGI_ERROR_DEVICE_RESET) {
            throw std::runtime_error(
                hresult_message(
                    "The D3D11 device was removed",
                    device->GetDeviceRemovedReason()));
        }
        require_success(result, "IDXGISwapChain::Present");
    }

    HWND window = nullptr;
    float minimum_x = 0.0F;
    float maximum_x = 0.0F;
    float minimum_y = 0.0F;
    float maximum_y = 0.0F;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    UINT raster_index_count = 0U;
    UINT source_index_count = 0U;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;
    FitConstants fit_constants{};
    std::vector<SceneVertex3dV1> source_vertices;
    std::vector<SceneVertexV1> projected_vertices;
    Vec3 orbit_target{};
    float orbit_radius = 1.0F;
    float orbit_yaw = 0.0F;
    float orbit_pitch = 0.0F;
    float orbit_distance = 1.0F;
    bool has_source_geometry = false;
    bool show_source_geometry = false;
    bool camera_initialized = false;
    bool projected_vertices_dirty = false;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap_chain;
    ComPtr<ID3D11RenderTargetView> render_target;
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    ComPtr<ID3D11InputLayout> input_layout;
    ComPtr<ID3D11RasterizerState> rasterizer_state;
    ComPtr<ID3D11DepthStencilState> depth_state;
    ComPtr<ID3D11Buffer> raster_vertex_buffer;
    ComPtr<ID3D11Buffer> raster_index_buffer;
    ComPtr<ID3D11Buffer> source_vertex_buffer;
    ComPtr<ID3D11Buffer> source_index_buffer;
    ComPtr<ID3D11Buffer> fit_buffer;
};

D3d11Renderer::D3d11Renderer(HWND window,
                             const SceneGeometryV1& geometry)
    : implementation_(std::make_unique<Implementation>(window, geometry)) {}

D3d11Renderer::D3d11Renderer(
    HWND window,
    const SceneGeometryV1& raster_geometry,
    const SceneGeometry3dV1& source_geometry)
    : implementation_(std::make_unique<Implementation>(
          window, raster_geometry, source_geometry)) {}

D3d11Renderer::~D3d11Renderer() = default;

D3d11Renderer::D3d11Renderer(D3d11Renderer&&) noexcept = default;

D3d11Renderer&
D3d11Renderer::operator=(D3d11Renderer&&) noexcept = default;

void D3d11Renderer::resize(const std::uint32_t width,
                           const std::uint32_t height) {
    implementation_->resize(width, height);
}

void D3d11Renderer::render() {
    implementation_->render();
}

void D3d11Renderer::orbit(
    const float yaw_delta,
    const float pitch_delta) {
    implementation_->orbit(yaw_delta, pitch_delta);
}

void D3d11Renderer::zoom(const float wheel_steps) {
    implementation_->zoom(wheel_steps);
}

void D3d11Renderer::reset_camera() {
    implementation_->reset_camera();
}

void D3d11Renderer::toggle_view_mode() {
    implementation_->toggle_view_mode();
}

bool D3d11Renderer::is_3d_view() const noexcept {
    return implementation_->is_3d_view();
}

} // namespace openrc::runtime
