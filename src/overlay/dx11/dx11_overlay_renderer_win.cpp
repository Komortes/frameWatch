#include "framewatch/overlay/overlay_renderer.h"
#include "framewatch/overlay/overlay_settings.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"

namespace framewatch {

namespace {

std::filesystem::path ResolveOverlaySettingsPath() {
    if (const char* env = std::getenv("FRAMEWATCH_DX11_OVERLAY_SETTINGS");
        env != nullptr && env[0] != '\0') {
        return env;
    }
    return std::filesystem::path("output/framewatch_dx11_overlay_settings.json");
}

class Dx11ImGuiOverlayRendererWin final : public OverlayRenderer {
  public:
    const char* Name() const noexcept override { return "Dx11ImGuiOverlayRendererWin"; }
    std::string_view Description() const noexcept override { return description_; }

    bool Initialize() override {
        settings_path_ = ResolveOverlaySettingsPath();
        if (const auto loaded = LoadOverlaySettings(settings_path_)) {
            settings_ = *loaded;
        }

        imgui_context_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(imgui_context_);

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 4.0f;
        style.WindowBorderSize = 0.0f;
        style.Colors[ImGuiCol_WindowBg] = {0.0f, 0.0f, 0.0f, static_cast<float>(settings_.panel_opacity)};

        initialized_ = true;
        description_ =
            "ImGui DX11 overlay initialized. F1/F2/F3/F4/F5/F6/F7/F8/F9/F10/F11/F12 control the overlay.";
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
            description_ = "ImGui DX11 overlay is hidden. Press F6 to show.";
            return actions;
        }

        if (present_event.api != GraphicsApi::Dx11 ||
            present_event.native_swap_chain == nullptr) {
            description_ = "ImGui DX11 overlay: no DX11 swap chain in present event.";
            return actions;
        }

        IDXGISwapChain* swap_chain =
            static_cast<IDXGISwapChain*>(present_event.native_swap_chain);
        if (!BindSwapChain(swap_chain)) {
            return actions;
        }

        if (!EnsureRenderTargetView()) {
            return actions;
        }

        ImGui::SetCurrentContext(imgui_context_);
        ImGui_ImplDX11_NewFrame();
        ImGui::GetIO().DisplaySize = {static_cast<float>(backbuffer_width_),
                                      static_cast<float>(backbuffer_height_)};
        ImGui::NewFrame();

        RenderOverlayWindow(snapshot);

        ImGui::Render();
        device_context_->OMSetRenderTargets(1, &render_target_view_, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        ++rendered_frames_;
        description_ = "ImGui DX11 overlay rendering live metrics.";
        return actions;
    }

    void Shutdown() noexcept override {
        try { SaveSettingsIfDirty(); } catch (...) {}
        if (imgui_dx11_initialized_) {
            ImGui::SetCurrentContext(imgui_context_);
            ImGui_ImplDX11_Shutdown();
            imgui_dx11_initialized_ = false;
        }
        ReleaseDx11Resources();
        if (imgui_context_ != nullptr) {
            ImGui::DestroyContext(imgui_context_);
            imgui_context_ = nullptr;
        }
        initialized_ = false;
        description_ = "ImGui DX11 overlay shut down.";
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
        if (ConsumeHotkeyPress(VK_F1, hotkey_show_settings_panel_down_)) {
            settings_.show_settings_panel = !settings_.show_settings_panel;
            settings_dirty_ = true;
        }
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
        if (ConsumeHotkeyPress(VK_F4, hotkey_reset_session_down_)) {
            actions.reset_session = true;
        }
        if (ConsumeHotkeyPress(VK_F5, hotkey_compact_mode_down_)) {
            settings_.compact_mode = !settings_.compact_mode;
            settings_dirty_ = true;
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
        if (ConsumeHotkeyPress(VK_F12, hotkey_show_hints_down_)) {
            settings_.show_hotkey_hints = !settings_.show_hotkey_hints;
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
        }
    }

    bool BindSwapChain(IDXGISwapChain* swap_chain) {
        if (swap_chain == bound_swap_chain_ && device_ != nullptr) {
            return true;
        }

        if (imgui_dx11_initialized_) {
            ImGui::SetCurrentContext(imgui_context_);
            ImGui_ImplDX11_Shutdown();
            imgui_dx11_initialized_ = false;
        }
        ReleaseDx11Resources();

        bound_swap_chain_ = swap_chain;
        bound_swap_chain_->AddRef();

        if (FAILED(bound_swap_chain_->GetDevice(IID_PPV_ARGS(&device_)))) {
            description_ = "ImGui DX11 overlay: failed to get device from swap chain.";
            ReleaseDx11Resources();
            return false;
        }

        device_->GetImmediateContext(&device_context_);
        if (device_context_ == nullptr) {
            description_ = "ImGui DX11 overlay: failed to get device context.";
            ReleaseDx11Resources();
            return false;
        }

        ImGui::SetCurrentContext(imgui_context_);
        ImGui_ImplDX11_Init(device_, device_context_);
        imgui_dx11_initialized_ = true;

        description_ = "ImGui DX11 overlay bound to swap chain.";
        return true;
    }

    bool EnsureRenderTargetView() {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(bound_swap_chain_->GetDesc(&desc))) {
            return false;
        }
        const UINT w = desc.BufferDesc.Width;
        const UINT h = desc.BufferDesc.Height;

        if (render_target_view_ != nullptr && w == backbuffer_width_ && h == backbuffer_height_) {
            return true;
        }

        if (render_target_view_ != nullptr) {
            device_context_->OMSetRenderTargets(0, nullptr, nullptr);
            render_target_view_->Release();
            render_target_view_ = nullptr;
        }

        ID3D11Texture2D* back_buffer = nullptr;
        if (FAILED(bound_swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) {
            return false;
        }

        const HRESULT hr =
            device_->CreateRenderTargetView(back_buffer, nullptr, &render_target_view_);
        back_buffer->Release();

        if (FAILED(hr)) {
            return false;
        }

        backbuffer_width_ = w;
        backbuffer_height_ = h;
        return true;
    }

    ImVec2 ComputeWindowPos(float panel_w, float panel_h) const {
        const float w = static_cast<float>(backbuffer_width_);
        const float h = static_cast<float>(backbuffer_height_);
        constexpr float kMargin = 8.0f;
        switch (settings_.dock_anchor) {
            case OverlayDockAnchor::RightTop:
                return {w - panel_w - kMargin, kMargin};
            case OverlayDockAnchor::RightBottom:
                return {w - panel_w - kMargin, h - panel_h - kMargin};
            case OverlayDockAnchor::LeftTop:
                return {kMargin, kMargin};
            case OverlayDockAnchor::LeftBottom:
                return {kMargin, h - panel_h - kMargin};
        }
        return {kMargin, kMargin};
    }

    void RenderOverlayWindow(const OverlaySnapshot& snapshot) {
        const float panel_width = settings_.show_settings_panel ? 320.0f : 240.0f;
        const float panel_height_guess = settings_.show_settings_panel ? last_panel_height_ + 72.0f
                                                                       : last_panel_height_;
        const ImVec2 pos = ComputeWindowPos(panel_width, panel_height_guess);

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize({panel_width, 0.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(static_cast<float>(settings_.panel_opacity));

        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("##framewatch_overlay", nullptr, kFlags);

        if (!snapshot.status_text.empty()) {
            ImGui::TextColored({1.0f, 1.0f, 0.0f, 1.0f}, "%s", snapshot.status_text.c_str());
        }

        if (settings_.compact_mode) {
            if (!snapshot.stats.empty()) {
                ImGui::Text("%s %s",
                            snapshot.stats.front().label.c_str(),
                            snapshot.stats.front().value.c_str());
            }
            if (snapshot.stats.size() > 4) {
                ImGui::Text("%s %s",
                            snapshot.stats[4].label.c_str(),
                            snapshot.stats[4].value.c_str());
            }
        } else if (settings_.show_sidebar && !snapshot.stats.empty()) {
            for (const auto& stat : snapshot.stats) {
                ImGui::Text("%-12s %s", stat.label.c_str(), stat.value.c_str());
            }
        }

        if (!settings_.compact_mode && settings_.show_graph && !snapshot.graph.empty()) {
            std::vector<float> values;
            values.reserve(snapshot.graph.size());
            for (const auto& pt : snapshot.graph) {
                values.push_back(static_cast<float>(pt.frametime_ms));
            }
            const float max_ms = snapshot.graph_max_ms > 0.0
                                     ? static_cast<float>(snapshot.graph_max_ms)
                                     : 33.3f;
            ImGui::PlotLines("##ft",
                             values.data(),
                             static_cast<int>(values.size()),
                             0,
                             nullptr,
                             0.0f,
                             max_ms,
                             {panel_width - 16.0f, settings_.show_settings_panel ? 72.0f : 60.0f});
        }

        if (settings_.show_settings_panel) {
            ImGui::Separator();
            ImGui::TextDisabled("OVERLAY");
            const int opacity_percent =
                static_cast<int>(ClampOverlayOpacity(settings_.panel_opacity) * 100.0);
            ImGui::Text("Dock: %s", std::string(OverlayDockAnchorName(settings_.dock_anchor)).c_str());
            ImGui::Text("Opacity: %d%%", opacity_percent);
            ImGui::Text("Graph: %s", settings_.show_graph ? "ON" : "OFF");
            ImGui::Text("Stats: %s", settings_.show_sidebar ? "ON" : "OFF");
            ImGui::Text("Compact: %s", settings_.compact_mode ? "ON" : "OFF");
            ImGui::Text("Hints: %s", settings_.show_hotkey_hints ? "ON" : "OFF");
            ImGui::Text("Source: %s", snapshot.graph_label.c_str());
        }

        if (settings_.show_hotkey_hints) {
            ImGui::SetWindowFontScale(0.75f);
            ImGui::TextDisabled("F1 PANEL  F2 BENCH  F3 SAVE  F4 RST  F5 COMPACT");
            ImGui::TextDisabled("F6 OVR  F7 GRAPH  F8 STATS  F9 DOCK  F10/F11 OPACITY  F12 HINTS");
            ImGui::SetWindowFontScale(1.0f);
        }

        last_panel_height_ = ImGui::GetWindowHeight();
        ImGui::End();
    }

    void ReleaseDx11Resources() noexcept {
        if (render_target_view_ != nullptr) {
            render_target_view_->Release();
            render_target_view_ = nullptr;
        }
        if (bound_swap_chain_ != nullptr) {
            bound_swap_chain_->Release();
            bound_swap_chain_ = nullptr;
        }
        if (device_context_ != nullptr) {
            device_context_->Release();
            device_context_ = nullptr;
        }
        if (device_ != nullptr) {
            device_->Release();
            device_ = nullptr;
        }
        backbuffer_width_ = 0;
        backbuffer_height_ = 0;
    }

    bool initialized_{false};
    bool imgui_dx11_initialized_{false};
    ImGuiContext* imgui_context_{nullptr};
    std::string description_{
        "ImGui DX11 overlay compiled. Waiting for the Present detour to bind a swap chain."};
    OverlaySettings settings_{};
    std::filesystem::path settings_path_;
    bool settings_dirty_{false};
    bool hotkey_benchmark_down_{false};
    bool hotkey_export_down_{false};
    bool hotkey_reset_session_down_{false};
    bool hotkey_show_settings_panel_down_{false};
    bool hotkey_compact_mode_down_{false};
    bool hotkey_show_overlay_down_{false};
    bool hotkey_show_graph_down_{false};
    bool hotkey_show_sidebar_down_{false};
    bool hotkey_dock_anchor_down_{false};
    bool hotkey_opacity_down_{false};
    bool hotkey_opacity_up_{false};
    bool hotkey_show_hints_down_{false};
    std::uint64_t rendered_frames_{0};
    IDXGISwapChain* bound_swap_chain_{nullptr};
    ID3D11Device* device_{nullptr};
    ID3D11DeviceContext* device_context_{nullptr};
    ID3D11RenderTargetView* render_target_view_{nullptr};
    UINT backbuffer_width_{0};
    UINT backbuffer_height_{0};
    float last_panel_height_{100.0f};
};

}  // namespace

std::unique_ptr<OverlayRenderer> CreateDx11OverlayRendererWin() {
    return std::make_unique<Dx11ImGuiOverlayRendererWin>();
}

}  // namespace framewatch
