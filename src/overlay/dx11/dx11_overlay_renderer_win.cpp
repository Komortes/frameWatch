#include "framewatch/overlay/overlay_renderer.h"
#include "framewatch/overlay/overlay_settings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

namespace framewatch {

namespace {

constexpr UINT kGlyphWidth = 5;
constexpr UINT kGlyphHeight = 7;
constexpr float kGlyphSpacing = 1.0F;
constexpr std::size_t kMaxStatRows = 5;
constexpr std::size_t kInitialVertexCapacity = 16'384;

struct Color {
    float r;
    float g;
    float b;
    float a;
};

struct OverlayVertex {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
};

using GlyphBitmap = std::array<std::uint8_t, kGlyphHeight>;

std::string FormatHResult(HRESULT result) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << static_cast<std::uint32_t>(result);
    return stream.str();
}

void ReleaseCom(IUnknown*& object) noexcept {
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

template <typename T>
void ReleaseCom(T*& object) noexcept {
    IUnknown* unknown = object;
    ReleaseCom(unknown);
    object = nullptr;
}

GlyphBitmap GlyphForCharacter(char character) {
    switch (character) {
        case 'A':
            return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        case 'B':
            return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
        case 'C':
            return {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
        case 'D':
            return {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
        case 'E':
            return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
        case 'F':
            return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
        case 'G':
            return {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
        case 'H':
            return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        case 'I':
            return {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
        case 'J':
            return {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E};
        case 'K':
            return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
        case 'L':
            return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
        case 'M':
            return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
        case 'N':
            return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
        case 'O':
            return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        case 'P':
            return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
        case 'Q':
            return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
        case 'R':
            return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
        case 'S':
            return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
        case 'T':
            return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
        case 'U':
            return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        case 'V':
            return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
        case 'W':
            return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
        case 'X':
            return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
        case 'Y':
            return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
        case 'Z':
            return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
        case '0':
            return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
        case '1':
            return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
        case '2':
            return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
        case '3':
            return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
        case '4':
            return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
        case '5':
            return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
        case '6':
            return {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
        case '7':
            return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
        case '8':
            return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
        case '9':
            return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};
        case '.':
            return {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06};
        case ':':
            return {0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00};
        case '-':
            return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
        case '%':
            return {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13};
        case '^':
            return {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00};
        case '/':
            return {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00};
        case ' ':
        default:
            return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }
}

bool IsRenderableGlyph(char character) {
    if (character == ' ') {
        return true;
    }

    const GlyphBitmap glyph = GlyphForCharacter(character);
    return std::any_of(glyph.begin(), glyph.end(), [](std::uint8_t row) { return row != 0; });
}

std::string ToOverlayText(std::string_view text) {
    std::string result;
    result.reserve(text.size());

    for (const char character : text) {
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        result.push_back(IsRenderableGlyph(upper) ? upper : ' ');
    }

    return result;
}

float MeasureTextWidth(std::string_view text, float scale) {
    if (text.empty()) {
        return 0.0F;
    }

    return ((static_cast<float>(text.size()) * (static_cast<float>(kGlyphWidth) + kGlyphSpacing)) -
            kGlyphSpacing) *
           scale;
}

std::string FitTextToWidth(std::string_view text, float scale, float max_width) {
    std::string prepared = ToOverlayText(text);
    if (prepared.empty() || max_width <= 0.0F) {
        return {};
    }

    if (MeasureTextWidth(prepared, scale) <= max_width) {
        return prepared;
    }

    while (!prepared.empty() && MeasureTextWidth(prepared + "...", scale) > max_width) {
        prepared.pop_back();
    }

    if (prepared.empty()) {
        return {};
    }

    return prepared + "...";
}

float ToNdcX(float pixel_x, float surface_width) {
    return (pixel_x / surface_width) * 2.0F - 1.0F;
}

float ToNdcY(float pixel_y, float surface_height) {
    return 1.0F - (pixel_y / surface_height) * 2.0F;
}

OverlayVertex MakeVertex(float pixel_x,
                         float pixel_y,
                         const Color& color,
                         float surface_width,
                         float surface_height) {
    return OverlayVertex{
        ToNdcX(pixel_x, surface_width),
        ToNdcY(pixel_y, surface_height),
        color.r,
        color.g,
        color.b,
        color.a,
    };
}

void AddRectangle(std::vector<OverlayVertex>& triangles,
                  float x,
                  float y,
                  float width,
                  float height,
                  const Color& color,
                  float surface_width,
                  float surface_height) {
    if (width <= 0.0F || height <= 0.0F) {
        return;
    }

    const float x1 = x + width;
    const float y1 = y + height;

    triangles.push_back(MakeVertex(x, y, color, surface_width, surface_height));
    triangles.push_back(MakeVertex(x1, y, color, surface_width, surface_height));
    triangles.push_back(MakeVertex(x, y1, color, surface_width, surface_height));
    triangles.push_back(MakeVertex(x1, y, color, surface_width, surface_height));
    triangles.push_back(MakeVertex(x1, y1, color, surface_width, surface_height));
    triangles.push_back(MakeVertex(x, y1, color, surface_width, surface_height));
}

void AddLine(std::vector<OverlayVertex>& lines,
             float x0,
             float y0,
             float x1,
             float y1,
             const Color& color,
             float surface_width,
             float surface_height) {
    lines.push_back(MakeVertex(x0, y0, color, surface_width, surface_height));
    lines.push_back(MakeVertex(x1, y1, color, surface_width, surface_height));
}

void AddText(std::vector<OverlayVertex>& triangles,
             float x,
             float y,
             std::string_view text,
             float scale,
             const Color& color,
             float surface_width,
             float surface_height) {
    float cursor_x = x;
    const float pixel_step = scale;
    const float advance = (static_cast<float>(kGlyphWidth) + kGlyphSpacing) * scale;

    for (const char character : text) {
        const GlyphBitmap glyph = GlyphForCharacter(character);
        for (std::size_t row = 0; row < glyph.size(); ++row) {
            for (UINT column = 0; column < kGlyphWidth; ++column) {
                if ((glyph[row] & (1U << (kGlyphWidth - 1U - column))) == 0) {
                    continue;
                }

                AddRectangle(triangles,
                             cursor_x + static_cast<float>(column) * pixel_step,
                             y + static_cast<float>(row) * pixel_step,
                             pixel_step,
                             pixel_step,
                             color,
                             surface_width,
                             surface_height);
            }
        }

        cursor_x += advance;
    }
}

bool CompileShader(std::string_view source,
                   const char* entry_point,
                   const char* target,
                   ID3DBlob** shader_blob,
                   std::string& error) {
    ID3DBlob* compiled_blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    const HRESULT result = D3DCompile(source.data(),
                                      source.size(),
                                      "framewatch_dx11_overlay",
                                      nullptr,
                                      nullptr,
                                      entry_point,
                                      target,
                                      flags,
                                      0,
                                      &compiled_blob,
                                      &error_blob);
    if (FAILED(result)) {
        error = "Failed to compile DX11 overlay shader (" + FormatHResult(result) + ")";
        if (error_blob != nullptr && error_blob->GetBufferPointer() != nullptr) {
            error += ": ";
            error.append(static_cast<const char*>(error_blob->GetBufferPointer()),
                         error_blob->GetBufferSize());
        }
        ReleaseCom(error_blob);
        ReleaseCom(compiled_blob);
        return false;
    }

    ReleaseCom(error_blob);
    *shader_blob = compiled_blob;
    return true;
}

Color WithAlphaScale(Color color, float alpha_scale) {
    color.a = std::clamp(color.a * alpha_scale, 0.0F, 1.0F);
    return color;
}

bool IsRightAnchored(OverlayDockAnchor anchor) noexcept {
    return anchor == OverlayDockAnchor::RightTop || anchor == OverlayDockAnchor::RightBottom;
}

bool IsBottomAnchored(OverlayDockAnchor anchor) noexcept {
    return anchor == OverlayDockAnchor::RightBottom || anchor == OverlayDockAnchor::LeftBottom;
}

std::filesystem::path ResolveOverlaySettingsPath() {
    if (const char* override_path = std::getenv("FRAMEWATCH_DX11_OVERLAY_SETTINGS");
        override_path != nullptr && override_path[0] != '\0') {
        return override_path;
    }

    return std::filesystem::path("output/framewatch_dx11_overlay_settings.json");
}

struct D3d11StateBackup {
    ID3D11RenderTargetView* render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* depth_stencil_view{nullptr};
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT viewport_count{0};
    ID3D11BlendState* blend_state{nullptr};
    FLOAT blend_factor[4]{};
    UINT sample_mask{0xFFFFFFFFU};
    ID3D11RasterizerState* rasterizer_state{nullptr};
    ID3D11DepthStencilState* depth_stencil_state{nullptr};
    UINT stencil_ref{0};
    ID3D11InputLayout* input_layout{nullptr};
    D3D11_PRIMITIVE_TOPOLOGY primitive_topology{D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED};
    ID3D11Buffer* vertex_buffer{nullptr};
    UINT vertex_stride{0};
    UINT vertex_offset{0};
    ID3D11Buffer* index_buffer{nullptr};
    DXGI_FORMAT index_format{DXGI_FORMAT_UNKNOWN};
    UINT index_offset{0};
    ID3D11VertexShader* vertex_shader{nullptr};
    ID3D11PixelShader* pixel_shader{nullptr};

    void Capture(ID3D11DeviceContext* context) {
        context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                    render_target_views,
                                    &depth_stencil_view);
        viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        context->RSGetViewports(&viewport_count, viewports);
        context->OMGetBlendState(&blend_state, blend_factor, &sample_mask);
        context->RSGetState(&rasterizer_state);
        context->OMGetDepthStencilState(&depth_stencil_state, &stencil_ref);
        context->IAGetInputLayout(&input_layout);
        context->IAGetPrimitiveTopology(&primitive_topology);
        context->IAGetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &vertex_offset);
        context->IAGetIndexBuffer(&index_buffer, &index_format, &index_offset);
        context->VSGetShader(&vertex_shader, nullptr, nullptr);
        context->PSGetShader(&pixel_shader, nullptr, nullptr);
    }

    void Restore(ID3D11DeviceContext* context) noexcept {
        context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                    render_target_views,
                                    depth_stencil_view);
        if (viewport_count > 0) {
            context->RSSetViewports(viewport_count, viewports);
        }
        context->OMSetBlendState(blend_state, blend_factor, sample_mask);
        context->RSSetState(rasterizer_state);
        context->OMSetDepthStencilState(depth_stencil_state, stencil_ref);
        context->IASetInputLayout(input_layout);
        context->IASetPrimitiveTopology(primitive_topology);
        context->IASetVertexBuffers(0, 1, &vertex_buffer, &vertex_stride, &vertex_offset);
        context->IASetIndexBuffer(index_buffer, index_format, index_offset);
        context->VSSetShader(vertex_shader, nullptr, 0);
        context->PSSetShader(pixel_shader, nullptr, 0);

        for (ID3D11RenderTargetView*& render_target_view : render_target_views) {
            ReleaseCom(render_target_view);
        }
        ReleaseCom(depth_stencil_view);
        ReleaseCom(blend_state);
        ReleaseCom(rasterizer_state);
        ReleaseCom(depth_stencil_state);
        ReleaseCom(input_layout);
        ReleaseCom(vertex_buffer);
        ReleaseCom(index_buffer);
        ReleaseCom(vertex_shader);
        ReleaseCom(pixel_shader);
    }
};

class Dx11OverlayRendererWin final : public OverlayRenderer {
  public:
    const char* Name() const noexcept override { return "Dx11OverlayRendererWin"; }

    std::string_view Description() const noexcept override { return description_; }

    bool Initialize() override {
        initialized_ = true;
        settings_path_ = ResolveOverlaySettingsPath();
        if (const auto loaded = LoadOverlaySettings(settings_path_)) {
            settings_ = *loaded;
        }
        description_ =
            "DX11 overlay renderer initialized. F6/F7/F8/F9/F10/F11 control the live overlay.";
        rendered_frames_ = 0;
        return true;
    }

    OverlayRenderActions Render(const OverlaySnapshot& snapshot,
                                const PresentEvent& present_event) override {
        if (!initialized_) {
            return {};
        }

        const OverlayRenderActions actions = HandleHotkeys();
        SaveSettingsIfDirty();

        if (!settings_.show_overlay) {
            description_ =
                "DX11 overlay is hidden. Press F6 to show it again and keep feeding live metrics.";
            return actions;
        }

        if (present_event.api != GraphicsApi::Dx11 || present_event.native_swap_chain == nullptr) {
            description_ =
                "DX11 overlay renderer is active, but the latest present event did not provide a DX11 swap chain.";
            return actions;
        }

        IDXGISwapChain* swap_chain =
            static_cast<IDXGISwapChain*>(present_event.native_swap_chain);
        if (!BindSwapChain(swap_chain)) {
            return actions;
        }

        if (!EnsureRenderTarget()) {
            return actions;
        }

        std::vector<OverlayVertex> triangles;
        std::vector<OverlayVertex> lines;
        BuildOverlayGeometry(snapshot, triangles, lines);
        if (triangles.empty() && lines.empty()) {
            return actions;
        }

        D3d11StateBackup state_backup;
        state_backup.Capture(device_context_);

        ID3D11RenderTargetView* render_target_view = render_target_view_;
        device_context_->OMSetRenderTargets(1, &render_target_view, nullptr);

        D3D11_VIEWPORT viewport{};
        viewport.TopLeftX = 0.0F;
        viewport.TopLeftY = 0.0F;
        viewport.Width = static_cast<float>(backbuffer_width_);
        viewport.Height = static_cast<float>(backbuffer_height_);
        viewport.MinDepth = 0.0F;
        viewport.MaxDepth = 1.0F;
        device_context_->RSSetViewports(1, &viewport);
        device_context_->OMSetBlendState(blend_state_, nullptr, 0xFFFFFFFFU);
        device_context_->RSSetState(rasterizer_state_);
        device_context_->OMSetDepthStencilState(depth_stencil_state_, 0);
        device_context_->IASetInputLayout(input_layout_);
        device_context_->VSSetShader(vertex_shader_, nullptr, 0);
        device_context_->PSSetShader(pixel_shader_, nullptr, 0);

        bool draw_ok = true;
        draw_ok = DrawVertices(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, triangles) && draw_ok;
        draw_ok = DrawVertices(D3D11_PRIMITIVE_TOPOLOGY_LINELIST, lines) && draw_ok;

        state_backup.Restore(device_context_);

        if (!draw_ok) {
            return actions;
        }

        ++rendered_frames_;
        description_ = "DX11 overlay renderer is drawing a live geometry overlay into the active swap chain.";
        return actions;
    }

    void Shutdown() noexcept override {
        SaveSettingsIfDirty();
        ResetDx11Resources();
        initialized_ = false;
        rendered_frames_ = 0;
        description_ = "DX11 overlay renderer shut down.";
    }

  private:
    bool ConsumeHotkeyPress(int virtual_key, bool& previous_down) noexcept {
        const bool down = (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
        const bool pressed = down && !previous_down;
        previous_down = down;
        return pressed;
    }

    OverlayRenderActions HandleHotkeys() {
        OverlayRenderActions actions;
        if (ConsumeHotkeyPress(VK_F6, hotkey_show_overlay_down_)) {
            settings_.show_overlay = !settings_.show_overlay;
            settings_dirty_ = true;
        }
        if (ConsumeHotkeyPress(VK_F2, hotkey_benchmark_down_)) {
            actions.toggle_benchmark = true;
        }
        if (ConsumeHotkeyPress(VK_F3, hotkey_export_down_)) {
            actions.export_requested = true;
        }
        if (ConsumeHotkeyPress(VK_F7, hotkey_show_graph_down_)) {
            settings_.show_graph = !settings_.show_graph;
            settings_dirty_ = true;
        }
        if (ConsumeHotkeyPress(VK_F8, hotkey_show_sidebar_down_)) {
            settings_.show_sidebar = !settings_.show_sidebar;
            settings_dirty_ = true;
        }
        if (ConsumeHotkeyPress(VK_F9, hotkey_dock_anchor_down_)) {
            settings_.dock_anchor = CycleOverlayDockAnchor(settings_.dock_anchor);
            settings_dirty_ = true;
        }
        if (ConsumeHotkeyPress(VK_F10, hotkey_opacity_down_)) {
            AdjustOverlayOpacity(settings_, -0.08);
            settings_dirty_ = true;
        }
        if (ConsumeHotkeyPress(VK_F11, hotkey_opacity_up_)) {
            AdjustOverlayOpacity(settings_, 0.08);
            settings_dirty_ = true;
        }
        return actions;
    }

    void SaveSettingsIfDirty() {
        if (!settings_dirty_) {
            return;
        }

        if (SaveOverlaySettings(settings_, settings_path_)) {
            settings_dirty_ = false;
            return;
        }

        description_ = "DX11 overlay renderer could not persist settings to " +
                       settings_path_.string() + ".";
    }

    bool BindSwapChain(IDXGISwapChain* swap_chain) {
        if (swap_chain == bound_swap_chain_ && device_ != nullptr && device_context_ != nullptr) {
            return true;
        }

        ResetDx11Resources();

        bound_swap_chain_ = swap_chain;
        bound_swap_chain_->AddRef();

        const HRESULT device_result = bound_swap_chain_->GetDevice(IID_PPV_ARGS(&device_));
        if (FAILED(device_result)) {
            description_ =
                "DX11 overlay renderer could not resolve the swap-chain device (" +
                FormatHResult(device_result) + ").";
            ResetDx11Resources();
            return false;
        }

        device_->GetImmediateContext(&device_context_);
        if (device_context_ == nullptr) {
            description_ = "DX11 overlay renderer could not resolve the device context.";
            ResetDx11Resources();
            return false;
        }

        if (!CreatePipelineResources()) {
            ResetDx11Resources();
            return false;
        }

        description_ = "DX11 overlay renderer bound to the active swap chain.";
        return true;
    }

    bool CreatePipelineResources() {
        static constexpr std::string_view kShaderSource = R"(
struct VsInput {
    float2 position : POSITION;
    float4 color : COLOR0;
};

struct PsInput {
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

PsInput VSMain(VsInput input) {
    PsInput output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.color = input.color;
    return output;
}

float4 PSMain(PsInput input) : SV_Target {
    return input.color;
}
)";

        ID3DBlob* vertex_shader_blob = nullptr;
        ID3DBlob* pixel_shader_blob = nullptr;
        std::string error;

        const bool vertex_ok =
            CompileShader(kShaderSource, "VSMain", "vs_4_0", &vertex_shader_blob, error);
        if (!vertex_ok) {
            description_ = error;
            return false;
        }

        const bool pixel_ok =
            CompileShader(kShaderSource, "PSMain", "ps_4_0", &pixel_shader_blob, error);
        if (!pixel_ok) {
            description_ = error;
            ReleaseCom(vertex_shader_blob);
            return false;
        }

        HRESULT result = device_->CreateVertexShader(vertex_shader_blob->GetBufferPointer(),
                                                     vertex_shader_blob->GetBufferSize(),
                                                     nullptr,
                                                     &vertex_shader_);
        if (FAILED(result)) {
            description_ = "DX11 overlay renderer failed to create the vertex shader (" +
                           FormatHResult(result) + ").";
            ReleaseCom(vertex_shader_blob);
            ReleaseCom(pixel_shader_blob);
            return false;
        }

        result = device_->CreatePixelShader(pixel_shader_blob->GetBufferPointer(),
                                            pixel_shader_blob->GetBufferSize(),
                                            nullptr,
                                            &pixel_shader_);
        if (FAILED(result)) {
            description_ = "DX11 overlay renderer failed to create the pixel shader (" +
                           FormatHResult(result) + ").";
            ReleaseCom(vertex_shader_blob);
            ReleaseCom(pixel_shader_blob);
            return false;
        }

        constexpr D3D11_INPUT_ELEMENT_DESC kInputLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        result = device_->CreateInputLayout(kInputLayout,
                                            static_cast<UINT>(std::size(kInputLayout)),
                                            vertex_shader_blob->GetBufferPointer(),
                                            vertex_shader_blob->GetBufferSize(),
                                            &input_layout_);
        ReleaseCom(vertex_shader_blob);
        ReleaseCom(pixel_shader_blob);
        if (FAILED(result)) {
            description_ = "DX11 overlay renderer failed to create the input layout (" +
                           FormatHResult(result) + ").";
            return false;
        }

        D3D11_BUFFER_DESC vertex_buffer_desc{};
        vertex_buffer_desc.ByteWidth =
            static_cast<UINT>(kInitialVertexCapacity * sizeof(OverlayVertex));
        vertex_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
        vertex_buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertex_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        result = device_->CreateBuffer(&vertex_buffer_desc, nullptr, &vertex_buffer_);
        if (FAILED(result)) {
            description_ = "DX11 overlay renderer failed to create the vertex buffer (" +
                           FormatHResult(result) + ").";
            return false;
        }
        vertex_buffer_capacity_ = kInitialVertexCapacity;

        D3D11_BLEND_DESC blend_desc{};
        blend_desc.RenderTarget[0].BlendEnable = TRUE;
        blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        result = device_->CreateBlendState(&blend_desc, &blend_state_);
        if (FAILED(result)) {
            description_ = "DX11 overlay renderer failed to create the blend state (" +
                           FormatHResult(result) + ").";
            return false;
        }

        D3D11_RASTERIZER_DESC rasterizer_desc{};
        rasterizer_desc.FillMode = D3D11_FILL_SOLID;
        rasterizer_desc.CullMode = D3D11_CULL_NONE;
        rasterizer_desc.DepthClipEnable = TRUE;
        result = device_->CreateRasterizerState(&rasterizer_desc, &rasterizer_state_);
        if (FAILED(result)) {
            description_ = "DX11 overlay renderer failed to create the rasterizer state (" +
                           FormatHResult(result) + ").";
            return false;
        }

        D3D11_DEPTH_STENCIL_DESC depth_stencil_desc{};
        depth_stencil_desc.DepthEnable = FALSE;
        depth_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depth_stencil_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        depth_stencil_desc.StencilEnable = FALSE;
        result = device_->CreateDepthStencilState(&depth_stencil_desc, &depth_stencil_state_);
        if (FAILED(result)) {
            description_ = "DX11 overlay renderer failed to create the depth-stencil state (" +
                           FormatHResult(result) + ").";
            return false;
        }

        return true;
    }

    bool EnsureRenderTarget() {
        ID3D11Texture2D* current_backbuffer = nullptr;
        const HRESULT result = bound_swap_chain_->GetBuffer(0, IID_PPV_ARGS(&current_backbuffer));
        if (FAILED(result)) {
            description_ = "DX11 overlay renderer could not access the swap-chain back buffer (" +
                           FormatHResult(result) + ").";
            ReleaseRenderTarget();
            return false;
        }

        bool recreate_render_target = render_target_view_ == nullptr;
        if (backbuffer_texture_ == nullptr || current_backbuffer != backbuffer_texture_) {
            recreate_render_target = true;
        }

        D3D11_TEXTURE2D_DESC backbuffer_desc{};
        current_backbuffer->GetDesc(&backbuffer_desc);
        backbuffer_width_ = backbuffer_desc.Width;
        backbuffer_height_ = backbuffer_desc.Height;

        if (recreate_render_target) {
            ReleaseRenderTarget();
            backbuffer_texture_ = current_backbuffer;
            current_backbuffer = nullptr;

            const HRESULT render_target_result =
                device_->CreateRenderTargetView(backbuffer_texture_, nullptr, &render_target_view_);
            if (FAILED(render_target_result)) {
                description_ =
                    "DX11 overlay renderer failed to create the back-buffer render target view (" +
                    FormatHResult(render_target_result) + ").";
                ReleaseRenderTarget();
                return false;
            }
        }

        ReleaseCom(current_backbuffer);
        return render_target_view_ != nullptr;
    }

    void ReleaseRenderTarget() noexcept {
        ReleaseCom(render_target_view_);
        ReleaseCom(backbuffer_texture_);
        backbuffer_width_ = 0;
        backbuffer_height_ = 0;
    }

    void ResetDx11Resources() noexcept {
        ReleaseRenderTarget();
        ReleaseCom(vertex_buffer_);
        vertex_buffer_capacity_ = 0;
        ReleaseCom(input_layout_);
        ReleaseCom(vertex_shader_);
        ReleaseCom(pixel_shader_);
        ReleaseCom(blend_state_);
        ReleaseCom(rasterizer_state_);
        ReleaseCom(depth_stencil_state_);
        ReleaseCom(device_context_);
        ReleaseCom(device_);
        ReleaseCom(bound_swap_chain_);
    }

    bool EnsureVertexBufferCapacity(std::size_t vertex_count) {
        if (vertex_count <= vertex_buffer_capacity_) {
            return true;
        }

        ReleaseCom(vertex_buffer_);

        vertex_buffer_capacity_ = vertex_buffer_capacity_ == 0
                                      ? kInitialVertexCapacity
                                      : vertex_buffer_capacity_;
        while (vertex_buffer_capacity_ < vertex_count) {
            vertex_buffer_capacity_ *= 2;
        }

        D3D11_BUFFER_DESC vertex_buffer_desc{};
        vertex_buffer_desc.ByteWidth =
            static_cast<UINT>(vertex_buffer_capacity_ * sizeof(OverlayVertex));
        vertex_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
        vertex_buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertex_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        const HRESULT result = device_->CreateBuffer(&vertex_buffer_desc, nullptr, &vertex_buffer_);
        if (FAILED(result)) {
            description_ = "DX11 overlay renderer failed to grow the vertex buffer (" +
                           FormatHResult(result) + ").";
            vertex_buffer_capacity_ = 0;
            return false;
        }

        return true;
    }

    bool DrawVertices(D3D11_PRIMITIVE_TOPOLOGY topology,
                      const std::vector<OverlayVertex>& vertices) {
        if (vertices.empty()) {
            return true;
        }

        if (!EnsureVertexBufferCapacity(vertices.size())) {
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped_resource{};
        const HRESULT map_result =
            device_context_->Map(vertex_buffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
        if (FAILED(map_result)) {
            description_ = "DX11 overlay renderer failed to map the vertex buffer (" +
                           FormatHResult(map_result) + ").";
            return false;
        }

        std::memcpy(mapped_resource.pData,
                    vertices.data(),
                    vertices.size() * sizeof(OverlayVertex));
        device_context_->Unmap(vertex_buffer_, 0);

        const UINT stride = sizeof(OverlayVertex);
        const UINT offset = 0;
        device_context_->IASetVertexBuffers(0, 1, &vertex_buffer_, &stride, &offset);
        device_context_->IASetPrimitiveTopology(topology);
        device_context_->Draw(static_cast<UINT>(vertices.size()), 0);
        return true;
    }

    void BuildOverlayGeometry(const OverlaySnapshot& snapshot,
                              std::vector<OverlayVertex>& triangles,
                              std::vector<OverlayVertex>& lines) const {
        if (backbuffer_width_ < 280 || backbuffer_height_ < 180) {
            return;
        }

        const float surface_width = static_cast<float>(backbuffer_width_);
        const float surface_height = static_cast<float>(backbuffer_height_);

        const float panel_alpha_scale = static_cast<float>(settings_.panel_opacity);
        const float text_alpha_scale =
            static_cast<float>(0.45 + settings_.panel_opacity * 0.55);

        const Color panel_color = WithAlphaScale(Color{0.05F, 0.07F, 0.10F, 0.78F}, panel_alpha_scale);
        const Color panel_border =
            WithAlphaScale(Color{0.30F, 0.64F, 0.91F, 0.95F}, text_alpha_scale);
        const Color graph_background =
            WithAlphaScale(Color{0.08F, 0.11F, 0.15F, 0.85F}, panel_alpha_scale);
        const Color graph_grid =
            WithAlphaScale(Color{0.22F, 0.27F, 0.34F, 0.55F}, text_alpha_scale);
        const Color graph_line =
            WithAlphaScale(Color{0.22F, 0.84F, 0.97F, 1.00F}, text_alpha_scale);
        const Color frame_budget_line =
            WithAlphaScale(Color{0.98F, 0.69F, 0.21F, 0.75F}, text_alpha_scale);
        const Color label_color =
            WithAlphaScale(Color{0.79F, 0.85F, 0.93F, 0.94F}, text_alpha_scale);
        const Color value_color =
            WithAlphaScale(Color{0.98F, 0.99F, 1.00F, 1.00F}, text_alpha_scale);
        const Color accent_color =
            WithAlphaScale(Color{0.22F, 0.84F, 0.97F, 0.94F}, text_alpha_scale);
        const Color footer_color =
            WithAlphaScale(Color{0.71F, 0.77F, 0.85F, 0.88F}, text_alpha_scale);

        const bool show_stats = settings_.show_sidebar;
        const bool show_graph = settings_.show_graph;
        const bool compact_mode = !show_stats && !show_graph;

        const float panel_margin = 20.0F;
        const float panel_padding = 14.0F;
        const float accent_height = 3.0F;
        const float stats_area_height = show_stats ? 104.0F : 22.0F;
        const float graph_gap = show_graph ? 12.0F : 0.0F;
        const float graph_height = show_graph ? (show_stats ? 104.0F : 134.0F) : 0.0F;
        const float footer_height = compact_mode ? 0.0F : 18.0F;
        const float compact_width = 180.0F;
        const float compact_height = 44.0F;

        const float requested_panel_width = compact_mode ? compact_width : 400.0F;
        const float requested_panel_height =
            compact_mode ? compact_height
                         : accent_height + 14.0F + stats_area_height + graph_gap + graph_height +
                               footer_height + 14.0F;

        const float panel_width =
            std::min(requested_panel_width, surface_width - panel_margin * 2.0F);
        const float panel_height =
            std::min(requested_panel_height, surface_height - panel_margin * 2.0F);
        if (panel_width <= 200.0F || panel_height <= 140.0F) {
            if (!compact_mode) {
                return;
            }
        }

        float panel_x = panel_margin;
        float panel_y = panel_margin;
        if (IsRightAnchored(settings_.dock_anchor)) {
            panel_x = surface_width - panel_margin - panel_width;
        }
        if (IsBottomAnchored(settings_.dock_anchor)) {
            panel_y = surface_height - panel_margin - panel_height;
        }

        const float graph_x = panel_x + panel_padding;
        const float graph_y = panel_y + accent_height + 14.0F + stats_area_height + graph_gap;
        const float graph_width = panel_width - panel_padding * 2.0F;

        triangles.reserve(12'000);
        lines.reserve(2'048);

        AddRectangle(triangles,
                     panel_x,
                     panel_y,
                     panel_width,
                     panel_height,
                     panel_color,
                     surface_width,
                     surface_height);
        AddRectangle(triangles,
                     panel_x,
                     panel_y,
                     panel_width,
                     accent_height,
                     accent_color,
                     surface_width,
                     surface_height);

        AddLine(lines,
                panel_x,
                panel_y + panel_height,
                panel_x + panel_width,
                panel_y + panel_height,
                panel_border,
                surface_width,
                surface_height);
        AddLine(lines,
                panel_x + panel_width,
                panel_y,
                panel_x + panel_width,
                panel_y + panel_height,
                panel_border,
                surface_width,
                surface_height);

        const auto stat_value = [&](std::size_t index) -> std::string_view {
            return index < snapshot.stats.size() ? snapshot.stats[index].value : std::string_view("0.0");
        };

        if (compact_mode) {
            const std::string compact_label = FitTextToWidth("FPS", 2.0F, 48.0F);
            const std::string compact_value = FitTextToWidth(stat_value(0), 2.0F, panel_width - 82.0F);
            AddText(triangles,
                    panel_x + panel_padding,
                    panel_y + 16.0F,
                    compact_label,
                    2.0F,
                    label_color,
                    surface_width,
                    surface_height);
            AddText(triangles,
                    panel_x + panel_width - panel_padding - MeasureTextWidth(compact_value, 2.0F),
                    panel_y + 16.0F,
                    compact_value,
                    2.0F,
                    accent_color,
                    surface_width,
                    surface_height);
            return;
        }

        const float label_scale = 2.0F;
        const float value_scale = 2.0F;
        const float row_spacing = 18.0F;
        const float label_x = panel_x + panel_padding;

        if (show_stats) {
            const float label_max_width = panel_width * 0.44F;
            const float value_max_width =
                panel_width - (panel_padding * 2.0F) - label_max_width - 8.0F;
            const std::size_t stat_count = std::min(snapshot.stats.size(), kMaxStatRows);

            for (std::size_t index = 0; index < stat_count; ++index) {
                const float row_y = panel_y + 14.0F + static_cast<float>(index) * row_spacing;
                const std::string label =
                    FitTextToWidth(snapshot.stats[index].label, label_scale, label_max_width);
                const std::string value =
                    FitTextToWidth(snapshot.stats[index].value, value_scale, value_max_width);

                AddText(triangles,
                        label_x,
                        row_y,
                        label,
                        label_scale,
                        label_color,
                        surface_width,
                        surface_height);

                const float value_width = MeasureTextWidth(value, value_scale);
                const float value_x =
                    std::max(label_x + label_max_width + 8.0F,
                             panel_x + panel_width - panel_padding - value_width);
                AddText(triangles,
                        value_x,
                        row_y,
                        value,
                        value_scale,
                        index == 0 ? accent_color : value_color,
                        surface_width,
                        surface_height);
            }
        } else {
            const std::string fps_label = FitTextToWidth("FPS", 2.0F, 48.0F);
            const std::string fps_value = FitTextToWidth(stat_value(0), 2.0F, 86.0F);
            const std::string ft_label = FitTextToWidth("FT", 2.0F, 36.0F);
            const std::string ft_value = FitTextToWidth(stat_value(4), 2.0F, 118.0F);

            AddText(triangles,
                    label_x,
                    panel_y + 14.0F,
                    fps_label,
                    2.0F,
                    label_color,
                    surface_width,
                    surface_height);
            AddText(triangles,
                    label_x + 42.0F,
                    panel_y + 14.0F,
                    fps_value,
                    2.0F,
                    accent_color,
                    surface_width,
                    surface_height);
            AddText(triangles,
                    panel_x + panel_width * 0.52F,
                    panel_y + 14.0F,
                    ft_label,
                    2.0F,
                    label_color,
                    surface_width,
                    surface_height);
            AddText(triangles,
                    panel_x + panel_width - panel_padding - MeasureTextWidth(ft_value, 2.0F),
                    panel_y + 14.0F,
                    ft_value,
                    2.0F,
                    value_color,
                    surface_width,
                    surface_height);
        }

        if (show_graph) {
            AddRectangle(triangles,
                         graph_x,
                         graph_y,
                         graph_width,
                         graph_height,
                         graph_background,
                         surface_width,
                         surface_height);

            AddLine(lines,
                    graph_x,
                    graph_y,
                    graph_x + graph_width,
                    graph_y,
                    graph_grid,
                    surface_width,
                    surface_height);
            AddLine(lines,
                    graph_x,
                    graph_y + graph_height,
                    graph_x + graph_width,
                    graph_y + graph_height,
                    graph_grid,
                    surface_width,
                    surface_height);
            AddLine(lines,
                    graph_x,
                    graph_y,
                    graph_x,
                    graph_y + graph_height,
                    graph_grid,
                    surface_width,
                    surface_height);
            AddLine(lines,
                    graph_x + graph_width,
                    graph_y,
                    graph_x + graph_width,
                    graph_y + graph_height,
                    graph_grid,
                    surface_width,
                    surface_height);

            for (int grid_index = 1; grid_index <= 3; ++grid_index) {
                const float grid_y =
                    graph_y + (graph_height * static_cast<float>(grid_index) / 4.0F);
                AddLine(lines,
                        graph_x,
                        grid_y,
                        graph_x + graph_width,
                        grid_y,
                        graph_grid,
                        surface_width,
                        surface_height);
            }
        }

        if (show_graph && !snapshot.graph.empty()) {
            const double graph_range = std::max(0.001, snapshot.graph_max_ms - snapshot.graph_min_ms);
            const auto add_graph_reference_line = [&](double frametime_ms, const Color& color) {
                if (frametime_ms < snapshot.graph_min_ms || frametime_ms > snapshot.graph_max_ms) {
                    return;
                }

                const double normalized =
                    std::clamp((frametime_ms - snapshot.graph_min_ms) / graph_range, 0.0, 1.0);
                const float line_y =
                    graph_y + graph_height - static_cast<float>(normalized) * graph_height;
                AddLine(lines,
                        graph_x,
                        line_y,
                        graph_x + graph_width,
                        line_y,
                        color,
                        surface_width,
                        surface_height);
            };

            add_graph_reference_line(16.6667, frame_budget_line);
            add_graph_reference_line(33.3333, Color{0.82F, 0.34F, 0.29F, 0.60F});

            for (std::size_t index = 1; index < snapshot.graph.size(); ++index) {
                const OverlayGraphPoint& previous = snapshot.graph[index - 1];
                const OverlayGraphPoint& current = snapshot.graph[index];

                const float x0 = graph_x + static_cast<float>(previous.x) * graph_width;
                const float y0 = graph_y + graph_height -
                                 static_cast<float>(std::clamp(previous.y, 0.0, 1.0)) * graph_height;
                const float x1 = graph_x + static_cast<float>(current.x) * graph_width;
                const float y1 = graph_y + graph_height -
                                 static_cast<float>(std::clamp(current.y, 0.0, 1.0)) * graph_height;

                AddLine(lines, x0, y0, x1, y1, graph_line, surface_width, surface_height);
            }

            const OverlayGraphPoint& last_point = snapshot.graph.back();
            const float marker_x = graph_x + static_cast<float>(last_point.x) * graph_width;
            const float marker_y = graph_y + graph_height -
                                   static_cast<float>(std::clamp(last_point.y, 0.0, 1.0)) * graph_height;
            AddRectangle(triangles,
                         marker_x - 2.0F,
                         marker_y - 2.0F,
                         4.0F,
                         4.0F,
                         graph_line,
                         surface_width,
                         surface_height);
        }

        const int opacity_percent =
            static_cast<int>(std::lround(ClampOverlayOpacity(settings_.panel_opacity) * 100.0));
        const std::string footer_left = FitTextToWidth(
            snapshot.graph_label + " " + std::string(OverlayDockAnchorName(settings_.dock_anchor)) +
                " " + std::to_string(opacity_percent) + "%",
            1.0F,
            panel_width * 0.46F);
        const std::string footer_right = FitTextToWidth(
            "F2 BENCH F3 SAVE F6 OVR F7 GR F8 ST F9 DOCK F10- F11+",
            1.0F,
            panel_width - (panel_padding * 2.0F) - MeasureTextWidth(footer_left, 1.0F) - 10.0F);

        const float footer_y = panel_y + panel_height - panel_padding - 7.0F;
        AddText(triangles,
                panel_x + panel_padding,
                footer_y,
                footer_left,
                1.0F,
                footer_color,
                surface_width,
                surface_height);
        AddText(triangles,
                panel_x + panel_width - panel_padding - MeasureTextWidth(footer_right, 1.0F),
                footer_y,
                footer_right,
                1.0F,
                footer_color,
                surface_width,
                surface_height);
    }

    bool initialized_{false};
    std::string description_{
        "DX11 overlay renderer compiled. Waiting for the Present detour to bind a swap chain."};
    OverlaySettings settings_{};
    std::filesystem::path settings_path_;
    bool settings_dirty_{false};
    bool hotkey_benchmark_down_{false};
    bool hotkey_export_down_{false};
    bool hotkey_show_overlay_down_{false};
    bool hotkey_show_graph_down_{false};
    bool hotkey_show_sidebar_down_{false};
    bool hotkey_dock_anchor_down_{false};
    bool hotkey_opacity_down_{false};
    bool hotkey_opacity_up_{false};
    std::uint64_t rendered_frames_{0};
    IDXGISwapChain* bound_swap_chain_{nullptr};
    ID3D11Device* device_{nullptr};
    ID3D11DeviceContext* device_context_{nullptr};
    ID3D11Texture2D* backbuffer_texture_{nullptr};
    ID3D11RenderTargetView* render_target_view_{nullptr};
    ID3D11VertexShader* vertex_shader_{nullptr};
    ID3D11PixelShader* pixel_shader_{nullptr};
    ID3D11InputLayout* input_layout_{nullptr};
    ID3D11Buffer* vertex_buffer_{nullptr};
    std::size_t vertex_buffer_capacity_{0};
    ID3D11BlendState* blend_state_{nullptr};
    ID3D11RasterizerState* rasterizer_state_{nullptr};
    ID3D11DepthStencilState* depth_stencil_state_{nullptr};
    UINT backbuffer_width_{0};
    UINT backbuffer_height_{0};
};

}  // namespace

std::unique_ptr<OverlayRenderer> CreateDx11OverlayRendererWin() {
    return std::make_unique<Dx11OverlayRendererWin>();
}

}  // namespace framewatch
