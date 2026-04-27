#include "framewatch/overlay/overlay_renderer.h"
#include "framewatch/overlay/overlay_settings.h"

#include <cfloat>
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

constexpr wchar_t kOverlayWndProcPropertyName[] = L"FrameWatchDx11OverlayRendererInstance";
constexpr float kFallbackDeltaTime = 1.0f / 60.0f;

std::filesystem::path ResolveOverlaySettingsPath() {
    if (const char* env = std::getenv("FRAMEWATCH_DX11_OVERLAY_SETTINGS");
        env != nullptr && env[0] != '\0') {
        return env;
    }
    return std::filesystem::path("output/framewatch_dx11_overlay_settings.json");
}

bool IsMouseMessage(UINT message) noexcept {
    switch (message) {
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
            return true;
        default:
            return false;
    }
}

bool IsKeyboardMessage(UINT message) noexcept {
    switch (message) {
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR:
            return true;
        default:
            return false;
    }
}

float ClientXFromLParam(LPARAM lparam) noexcept {
    return static_cast<float>(static_cast<short>(LOWORD(lparam)));
}

float ClientYFromLParam(LPARAM lparam) noexcept {
    return static_cast<float>(static_cast<short>(HIWORD(lparam)));
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
        style.Colors[ImGuiCol_WindowBg] = {
            0.0f, 0.0f, 0.0f, static_cast<float>(settings_.panel_opacity)};

        initialized_ = true;
        description_ = "ImGui DX11 overlay initialized. F1/F2/F3/F4/F5/F6/F7/F8/F9/F10/F11/F12 "
                       "control the overlay.";
        return true;
    }

    OverlayRenderActions Render(const OverlaySnapshot& snapshot,
                                const PresentEvent& present_event) override {
        if (!initialized_) {
            return {};
        }

        OverlayRenderActions actions = HandleHotkeys();
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

        const bool window_hook_ready = EnsureWindowHookInstalled();

        ImGui::SetCurrentContext(imgui_context_);
        ImGui_ImplDX11_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = {static_cast<float>(backbuffer_width_),
                          static_cast<float>(backbuffer_height_)};
        io.DeltaTime = kFallbackDeltaTime;
        if (!window_hook_ready) {
            UpdateSyntheticInput(io);
        }
        ImGui::NewFrame();

        RenderOverlayWindow(snapshot, actions);
        SaveSettingsIfDirty();

        ImGui::Render();
        device_context_->OMSetRenderTargets(1, &render_target_view_, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        ++rendered_frames_;
        description_ = window_hook_ready
                           ? "ImGui DX11 overlay rendering with WndProc input routing."
                           : "ImGui DX11 overlay rendering with synthetic input fallback.";
        return actions;
    }

    void Shutdown() noexcept override {
        try {
            SaveSettingsIfDirty();
        } catch (...) {
        }
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

    static int DockAnchorToIndex(OverlayDockAnchor anchor) noexcept {
        switch (anchor) {
            case OverlayDockAnchor::RightTop:
                return 0;
            case OverlayDockAnchor::RightBottom:
                return 1;
            case OverlayDockAnchor::LeftTop:
                return 2;
            case OverlayDockAnchor::LeftBottom:
                return 3;
        }
        return 0;
    }

    static OverlayDockAnchor DockAnchorFromIndex(int index) noexcept {
        switch (index) {
            case 0:
                return OverlayDockAnchor::RightTop;
            case 1:
                return OverlayDockAnchor::RightBottom;
            case 2:
                return OverlayDockAnchor::LeftTop;
            default:
                return OverlayDockAnchor::LeftBottom;
        }
    }

    static ImGuiKey MapVirtualKeyToImGuiKey(WPARAM virtual_key,
                                            LPARAM lparam) noexcept {
        switch (virtual_key) {
            case VK_TAB:
                return ImGuiKey_Tab;
            case VK_LEFT:
                return ImGuiKey_LeftArrow;
            case VK_RIGHT:
                return ImGuiKey_RightArrow;
            case VK_UP:
                return ImGuiKey_UpArrow;
            case VK_DOWN:
                return ImGuiKey_DownArrow;
            case VK_PRIOR:
                return ImGuiKey_PageUp;
            case VK_NEXT:
                return ImGuiKey_PageDown;
            case VK_HOME:
                return ImGuiKey_Home;
            case VK_END:
                return ImGuiKey_End;
            case VK_INSERT:
                return ImGuiKey_Insert;
            case VK_DELETE:
                return ImGuiKey_Delete;
            case VK_BACK:
                return ImGuiKey_Backspace;
            case VK_SPACE:
                return ImGuiKey_Space;
            case VK_RETURN:
                return ImGuiKey_Enter;
            case VK_ESCAPE:
                return ImGuiKey_Escape;
            case VK_OEM_7:
                return ImGuiKey_Apostrophe;
            case VK_OEM_COMMA:
                return ImGuiKey_Comma;
            case VK_OEM_MINUS:
                return ImGuiKey_Minus;
            case VK_OEM_PERIOD:
                return ImGuiKey_Period;
            case VK_OEM_2:
                return ImGuiKey_Slash;
            case VK_OEM_1:
                return ImGuiKey_Semicolon;
            case VK_OEM_PLUS:
                return ImGuiKey_Equal;
            case VK_OEM_4:
                return ImGuiKey_LeftBracket;
            case VK_OEM_5:
                return ImGuiKey_Backslash;
            case VK_OEM_6:
                return ImGuiKey_RightBracket;
            case VK_OEM_3:
                return ImGuiKey_GraveAccent;
            case VK_CAPITAL:
                return ImGuiKey_CapsLock;
            case VK_SCROLL:
                return ImGuiKey_ScrollLock;
            case VK_NUMLOCK:
                return ImGuiKey_NumLock;
            case VK_SNAPSHOT:
                return ImGuiKey_PrintScreen;
            case VK_PAUSE:
                return ImGuiKey_Pause;
            case VK_NUMPAD0:
                return ImGuiKey_Keypad0;
            case VK_NUMPAD1:
                return ImGuiKey_Keypad1;
            case VK_NUMPAD2:
                return ImGuiKey_Keypad2;
            case VK_NUMPAD3:
                return ImGuiKey_Keypad3;
            case VK_NUMPAD4:
                return ImGuiKey_Keypad4;
            case VK_NUMPAD5:
                return ImGuiKey_Keypad5;
            case VK_NUMPAD6:
                return ImGuiKey_Keypad6;
            case VK_NUMPAD7:
                return ImGuiKey_Keypad7;
            case VK_NUMPAD8:
                return ImGuiKey_Keypad8;
            case VK_NUMPAD9:
                return ImGuiKey_Keypad9;
            case VK_DECIMAL:
                return ImGuiKey_KeypadDecimal;
            case VK_DIVIDE:
                return ImGuiKey_KeypadDivide;
            case VK_MULTIPLY:
                return ImGuiKey_KeypadMultiply;
            case VK_SUBTRACT:
                return ImGuiKey_KeypadSubtract;
            case VK_ADD:
                return ImGuiKey_KeypadAdd;
            case VK_F1:
                return ImGuiKey_F1;
            case VK_F2:
                return ImGuiKey_F2;
            case VK_F3:
                return ImGuiKey_F3;
            case VK_F4:
                return ImGuiKey_F4;
            case VK_F5:
                return ImGuiKey_F5;
            case VK_F6:
                return ImGuiKey_F6;
            case VK_F7:
                return ImGuiKey_F7;
            case VK_F8:
                return ImGuiKey_F8;
            case VK_F9:
                return ImGuiKey_F9;
            case VK_F10:
                return ImGuiKey_F10;
            case VK_F11:
                return ImGuiKey_F11;
            case VK_F12:
                return ImGuiKey_F12;
            case VK_LSHIFT:
                return ImGuiKey_LeftShift;
            case VK_RSHIFT:
                return ImGuiKey_RightShift;
            case VK_SHIFT: {
                const UINT scancode = (HIWORD(lparam) & 0xFF);
                const UINT mapped = MapVirtualKeyW(scancode, MAPVK_VSC_TO_VK_EX);
                return mapped == VK_RSHIFT ? ImGuiKey_RightShift : ImGuiKey_LeftShift;
            }
            case VK_LCONTROL:
                return ImGuiKey_LeftCtrl;
            case VK_RCONTROL:
                return ImGuiKey_RightCtrl;
            case VK_CONTROL:
                return (HIWORD(lparam) & KF_EXTENDED) != 0 ? ImGuiKey_RightCtrl
                                                           : ImGuiKey_LeftCtrl;
            case VK_LMENU:
                return ImGuiKey_LeftAlt;
            case VK_RMENU:
                return ImGuiKey_RightAlt;
            case VK_MENU:
                return (HIWORD(lparam) & KF_EXTENDED) != 0 ? ImGuiKey_RightAlt
                                                           : ImGuiKey_LeftAlt;
            case VK_LWIN:
                return ImGuiKey_LeftSuper;
            case VK_RWIN:
                return ImGuiKey_RightSuper;
            case VK_APPS:
                return ImGuiKey_Menu;
            default:
                break;
        }

        if (virtual_key >= '0' && virtual_key <= '9') {
            return static_cast<ImGuiKey>(ImGuiKey_0 + (virtual_key - '0'));
        }
        if (virtual_key >= 'A' && virtual_key <= 'Z') {
            return static_cast<ImGuiKey>(ImGuiKey_A + (virtual_key - 'A'));
        }

        return ImGuiKey_None;
    }

    void UpdateModifierKeys(ImGuiIO& io) const {
        io.AddKeyEvent(ImGuiMod_Ctrl, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (GetKeyState(VK_MENU) & 0x8000) != 0);
        io.AddKeyEvent(ImGuiMod_Super,
                       ((GetKeyState(VK_LWIN) & 0x8000) != 0) ||
                           ((GetKeyState(VK_RWIN) & 0x8000) != 0));
    }

    bool EnsureWindowHookInstalled() {
        if (output_window_ == nullptr || IsWindow(output_window_) == 0) {
            return false;
        }
        if (hooked_window_ == output_window_ && original_wnd_proc_ != nullptr) {
            return true;
        }

        RemoveWindowHook();
        SetPropW(output_window_,
                 kOverlayWndProcPropertyName,
                 reinterpret_cast<HANDLE>(this));

        SetLastError(0);
        const LONG_PTR previous = SetWindowLongPtrW(
            output_window_,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&Dx11ImGuiOverlayRendererWin::OverlayWindowProc));
        const DWORD error = GetLastError();
        if (previous == 0 && error != 0) {
            RemovePropW(output_window_, kOverlayWndProcPropertyName);
            description_ = "ImGui DX11 overlay: failed to hook output window WndProc.";
            return false;
        }

        hooked_window_ = output_window_;
        original_wnd_proc_ = reinterpret_cast<WNDPROC>(previous);
        return true;
    }

    void RemoveWindowHook() noexcept {
        if (hooked_window_ == nullptr) {
            return;
        }

        if (original_wnd_proc_ != nullptr) {
            SetWindowLongPtrW(hooked_window_,
                              GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(original_wnd_proc_));
        }
        RemovePropW(hooked_window_, kOverlayWndProcPropertyName);
        original_wnd_proc_ = nullptr;
        hooked_window_ = nullptr;
    }

    static LRESULT CALLBACK OverlayWindowProc(HWND window,
                                              UINT message,
                                              WPARAM wparam,
                                              LPARAM lparam) {
        auto* renderer = reinterpret_cast<Dx11ImGuiOverlayRendererWin*>(
            GetPropW(window, kOverlayWndProcPropertyName));
        if (renderer == nullptr) {
            return DefWindowProcW(window, message, wparam, lparam);
        }
        return renderer->HandleWindowMessage(window, message, wparam, lparam);
    }

    LRESULT HandleWindowMessage(HWND window,
                                UINT message,
                                WPARAM wparam,
                                LPARAM lparam) {
        if (imgui_context_ != nullptr) {
            ImGui::SetCurrentContext(imgui_context_);
            ImGuiIO& io = ImGui::GetIO();
            UpdateModifierKeys(io);

            switch (message) {
                case WM_MOUSEMOVE:
                    io.AddMousePosEvent(ClientXFromLParam(lparam), ClientYFromLParam(lparam));
                    break;
                case WM_MOUSELEAVE:
                    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                    break;
                case WM_LBUTTONDOWN:
                case WM_LBUTTONDBLCLK:
                    io.AddMouseButtonEvent(0, true);
                    break;
                case WM_LBUTTONUP:
                    io.AddMouseButtonEvent(0, false);
                    break;
                case WM_RBUTTONDOWN:
                case WM_RBUTTONDBLCLK:
                    io.AddMouseButtonEvent(1, true);
                    break;
                case WM_RBUTTONUP:
                    io.AddMouseButtonEvent(1, false);
                    break;
                case WM_MBUTTONDOWN:
                case WM_MBUTTONDBLCLK:
                    io.AddMouseButtonEvent(2, true);
                    break;
                case WM_MBUTTONUP:
                    io.AddMouseButtonEvent(2, false);
                    break;
                case WM_XBUTTONDOWN:
                case WM_XBUTTONDBLCLK:
                    io.AddMouseButtonEvent((GET_XBUTTON_WPARAM(wparam) == XBUTTON1) ? 3 : 4, true);
                    break;
                case WM_XBUTTONUP:
                    io.AddMouseButtonEvent((GET_XBUTTON_WPARAM(wparam) == XBUTTON1) ? 3 : 4, false);
                    break;
                case WM_MOUSEWHEEL:
                    io.AddMouseWheelEvent(
                        0.0f,
                        static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                            static_cast<float>(WHEEL_DELTA));
                    break;
                case WM_MOUSEHWHEEL:
                    io.AddMouseWheelEvent(
                        static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                            static_cast<float>(WHEEL_DELTA),
                        0.0f);
                    break;
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYUP: {
                    const bool down = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
                    const ImGuiKey key = MapVirtualKeyToImGuiKey(wparam, lparam);
                    if (key != ImGuiKey_None) {
                        io.AddKeyEvent(key, down);
                    }
                    break;
                }
                case WM_CHAR:
                case WM_SYSCHAR:
                    if (wparam > 0 && wparam < 0x10000) {
                        io.AddInputCharacterUTF16(static_cast<unsigned short>(wparam));
                    }
                    break;
                default:
                    break;
            }

            const bool can_capture = settings_.show_overlay && settings_.show_settings_panel &&
                                     settings_.capture_input_when_panel_open;
            if (can_capture) {
                const bool capture_mouse = IsMouseMessage(message) && io.WantCaptureMouse;
                const bool capture_keyboard =
                    IsKeyboardMessage(message) && (io.WantCaptureKeyboard || io.WantTextInput);
                if (capture_mouse || capture_keyboard) {
                    return 1;
                }
            }
        }

        if (original_wnd_proc_ != nullptr) {
            return CallWindowProcW(original_wnd_proc_, window, message, wparam, lparam);
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    void UpdateSyntheticInput(ImGuiIO& io) const {
        io.MousePos = {-FLT_MAX, -FLT_MAX};
        io.MouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        io.MouseDown[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        io.MouseDown[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

        if (output_window_ == nullptr || IsWindow(output_window_) == 0) {
            return;
        }

        POINT cursor{};
        if (GetCursorPos(&cursor) == 0) {
            return;
        }
        if (ScreenToClient(output_window_, &cursor) == 0) {
            return;
        }
        io.MousePos = {static_cast<float>(cursor.x), static_cast<float>(cursor.y)};
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
        output_window_ = desc.OutputWindow;
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

    void RenderOverlayWindow(const OverlaySnapshot& snapshot, OverlayRenderActions& actions) {
        const float panel_width = settings_.show_settings_panel ? 340.0f : 240.0f;
        const float panel_height_guess = settings_.show_settings_panel
                                             ? last_panel_height_ + 82.0f
                                             : last_panel_height_;
        const ImVec2 pos = ComputeWindowPos(panel_width, panel_height_guess);

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize({panel_width, 0.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(static_cast<float>(settings_.panel_opacity));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;
        if (!settings_.show_settings_panel) {
            flags |= ImGuiWindowFlags_NoInputs;
        }

        ImGui::Begin("##framewatch_overlay", nullptr, flags);

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
            if (ImGui::Button("Benchmark")) {
                actions.toggle_benchmark = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Export")) {
                actions.export_requested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                actions.reset_session = true;
            }

            bool show_graph = settings_.show_graph;
            if (ImGui::Checkbox("Graph", &show_graph)) {
                settings_.show_graph = show_graph;
                settings_dirty_ = true;
            }

            bool show_stats = settings_.show_sidebar;
            if (ImGui::Checkbox("Stats", &show_stats)) {
                settings_.show_sidebar = show_stats;
                settings_dirty_ = true;
            }

            bool compact_mode = settings_.compact_mode;
            if (ImGui::Checkbox("Compact", &compact_mode)) {
                settings_.compact_mode = compact_mode;
                settings_dirty_ = true;
            }

            bool show_hints = settings_.show_hotkey_hints;
            if (ImGui::Checkbox("Hints", &show_hints)) {
                settings_.show_hotkey_hints = show_hints;
                settings_dirty_ = true;
            }

            bool capture_input = settings_.capture_input_when_panel_open;
            if (ImGui::Checkbox("Capture Input", &capture_input)) {
                settings_.capture_input_when_panel_open = capture_input;
                settings_dirty_ = true;
            }

            int dock_index = DockAnchorToIndex(settings_.dock_anchor);
            static const char* kDockAnchorItems[] = {
                "RIGHT TOP", "RIGHT BOTTOM", "LEFT TOP", "LEFT BOTTOM"};
            if (ImGui::Combo("Dock", &dock_index, kDockAnchorItems, 4)) {
                settings_.dock_anchor = DockAnchorFromIndex(dock_index);
                settings_dirty_ = true;
            }

            float opacity = static_cast<float>(settings_.panel_opacity);
            if (ImGui::SliderFloat("Opacity", &opacity, 0.35f, 1.0f, "%.2f")) {
                settings_.panel_opacity = ClampOverlayOpacity(opacity);
                settings_dirty_ = true;
            }

            const int opacity_percent =
                static_cast<int>(ClampOverlayOpacity(settings_.panel_opacity) * 100.0);
            ImGui::Text("Dock: %s",
                        std::string(OverlayDockAnchorName(settings_.dock_anchor)).c_str());
            ImGui::Text("Opacity: %d%%", opacity_percent);
            ImGui::Text("Graph: %s", settings_.show_graph ? "ON" : "OFF");
            ImGui::Text("Stats: %s", settings_.show_sidebar ? "ON" : "OFF");
            ImGui::Text("Compact: %s", settings_.compact_mode ? "ON" : "OFF");
            ImGui::Text("Hints: %s", settings_.show_hotkey_hints ? "ON" : "OFF");
            ImGui::Text("Capture: %s",
                        settings_.capture_input_when_panel_open ? "ON" : "OFF");
            ImGui::Text("Source: %s", snapshot.graph_label.c_str());
        }

        if (settings_.show_hotkey_hints) {
            ImGui::SetWindowFontScale(0.75f);
            ImGui::TextDisabled("F1 PANEL  F2 BENCH  F3 SAVE  F4 RST  F5 COMPACT");
            ImGui::TextDisabled(
                "F6 OVR  F7 GRAPH  F8 STATS  F9 DOCK  F10/F11 OPACITY  F12 HINTS");
            ImGui::SetWindowFontScale(1.0f);
        }

        last_panel_height_ = ImGui::GetWindowHeight();
        ImGui::End();
    }

    void ReleaseDx11Resources() noexcept {
        RemoveWindowHook();

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
        output_window_ = nullptr;
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
    HWND output_window_{nullptr};
    HWND hooked_window_{nullptr};
    WNDPROC original_wnd_proc_{nullptr};
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
