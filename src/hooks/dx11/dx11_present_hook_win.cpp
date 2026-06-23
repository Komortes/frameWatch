#include "framewatch/hooks/present_hook.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#if defined(FRAMEWATCH_HAS_MINHOOK)
#include <MinHook.h>
#endif

namespace framewatch {

namespace {

std::string FormatHResult(HRESULT result) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase
           << static_cast<std::uint32_t>(result);
    return stream.str();
}

#if defined(FRAMEWATCH_HAS_MINHOOK)
std::string FormatMinHookStatus(const char* message, MH_STATUS status) {
    std::ostringstream stream;
    stream << message << " (MinHook status " << static_cast<int>(status) << ")";
    return stream.str();
}

LRESULT CALLBACK DummyWindowProc(HWND window_handle,
                                 UINT message,
                                 WPARAM wparam,
                                 LPARAM lparam) {
    return DefWindowProcW(window_handle, message, wparam, lparam);
}

bool EnsureDummyWindowClass(std::string& error) {
    static bool registered = false;
    if (registered) {
        return true;
    }

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DummyWindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"FrameWatchMiniDx11DummyWindow";

    if (RegisterClassW(&window_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = "Failed to register DX11 dummy window class";
        return false;
    }

    registered = true;
    return true;
}

struct DummyDx11Context {
    HWND window{nullptr};
    IDXGISwapChain* swap_chain{nullptr};
    ID3D11Device* device{nullptr};
    ID3D11DeviceContext* device_context{nullptr};

    void Reset() noexcept {
        if (device_context != nullptr) {
            device_context->Release();
            device_context = nullptr;
        }
        if (swap_chain != nullptr) {
            swap_chain->Release();
            swap_chain = nullptr;
        }
        if (device != nullptr) {
            device->Release();
            device = nullptr;
        }
        if (window != nullptr) {
            DestroyWindow(window);
            window = nullptr;
        }
    }
};

bool CreateDummyDx11Context(DummyDx11Context& context, std::string& error) {
    if (!EnsureDummyWindowClass(error)) {
        return false;
    }

    context.window = CreateWindowExW(0,
                                     L"FrameWatchMiniDx11DummyWindow",
                                     L"FrameWatch DX11 Dummy",
                                     WS_OVERLAPPEDWINDOW,
                                     CW_USEDEFAULT,
                                     CW_USEDEFAULT,
                                     64,
                                     64,
                                     nullptr,
                                     nullptr,
                                     GetModuleHandleW(nullptr),
                                     nullptr);
    if (context.window == nullptr) {
        error = "Failed to create DX11 dummy window";
        return false;
    }

    DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
    swap_chain_desc.BufferDesc.Width = 64;
    swap_chain_desc.BufferDesc.Height = 64;
    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.OutputWindow = context.window;
    swap_chain_desc.Windowed = TRUE;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr std::array<D3D_FEATURE_LEVEL, 3> kFeatureLevels{
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    constexpr std::array<D3D_DRIVER_TYPE, 2> kDriverTypes{
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
    };

    for (const D3D_DRIVER_TYPE driver_type : kDriverTypes) {
        D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
        const HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr,
                                                             driver_type,
                                                             nullptr,
                                                             0,
                                                             kFeatureLevels.data(),
                                                             static_cast<UINT>(kFeatureLevels.size()),
                                                             D3D11_SDK_VERSION,
                                                             &swap_chain_desc,
                                                             &context.swap_chain,
                                                             &context.device,
                                                             &feature_level,
                                                             &context.device_context);
        if (SUCCEEDED(result)) {
            return true;
        }
    }

    error = "Failed to create DX11 dummy swap chain";
    context.Reset();
    return false;
}
#endif

class Dx11PresentHookWin final : public PresentHook {
  public:
    HookBackend Backend() const noexcept override { return HookBackend::Dx11; }

    HookState State() const noexcept override { return state_; }

    std::string_view Description() const noexcept override { return description_; }

    void SetPresentCallback(PresentCallback callback) override { callback_ = std::move(callback); }

    bool Install() override {
#if !defined(FRAMEWATCH_HAS_MINHOOK)
        state_ = HookState::Unsupported;
        description_ =
            "DX11 backend compiled, but MinHook was not found. Set FRAMEWATCH_MINHOOK_ROOT or install MinHook to enable Present detouring.";
        return false;
#else
        if (state_ == HookState::Running) {
            return true;
        }

        // Only one hook instance may be installed per process because PresentDetour
        // is a static function that dispatches through active_instance_.
        Dx11PresentHookWin* expected_null = nullptr;
        if (!active_instance_.compare_exchange_strong(
                expected_null, this, std::memory_order_acq_rel, std::memory_order_acquire)) {
            state_ = HookState::Error;
            description_ =
                "Another DX11 Present hook instance is already active. Only one hook may be installed per process.";
            return false;
        }

        void* present_target = nullptr;
        std::string error;
        if (!ResolvePresentTarget(present_target, error)) {
            active_instance_.store(nullptr, std::memory_order_release);
            state_ = HookState::Error;
            description_ = std::move(error);
            return false;
        }

        const MH_STATUS init_status = MH_Initialize();
        if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
            active_instance_.store(nullptr, std::memory_order_release);
            state_ = HookState::Error;
            description_ = FormatMinHookStatus("Failed to initialize MinHook", init_status);
            return false;
        }

        const MH_STATUS create_status =
            MH_CreateHook(present_target,
                          reinterpret_cast<LPVOID>(&Dx11PresentHookWin::PresentDetour),
                          reinterpret_cast<LPVOID*>(&original_present_));
        if (create_status != MH_OK) {
            active_instance_.store(nullptr, std::memory_order_release);
            state_ = HookState::Error;
            description_ = FormatMinHookStatus("Failed to create DX11 Present hook", create_status);
            return false;
        }

        const MH_STATUS enable_status = MH_EnableHook(present_target);
        if (enable_status != MH_OK) {
            MH_RemoveHook(present_target);
            // MH_RemoveHook frees the trampoline that original_present_ pointed to;
            // clear it so PresentDetour cannot call through a freed pointer.
            original_present_ = nullptr;
            active_instance_.store(nullptr, std::memory_order_release);
            state_ = HookState::Error;
            description_ = FormatMinHookStatus("Failed to enable DX11 Present hook", enable_status);
            return false;
        }

        hook_target_ = present_target;
        state_ = HookState::Running;
        description_ =
            "DX11 Present hook installed. Live swap-chain presents now feed the frametime runtime.";
        return true;
#endif
    }

    void Remove() noexcept override {
#if defined(FRAMEWATCH_HAS_MINHOOK)
        // Null active_instance_ first so PresentDetour stops dispatching before
        // we disable and free the hook trampoline.
        active_instance_.store(nullptr, std::memory_order_release);

        if (hook_target_ != nullptr) {
            MH_DisableHook(hook_target_);
            MH_RemoveHook(hook_target_);
            hook_target_ = nullptr;
            // Trampoline is now freed; clear our copy so it cannot be called.
            original_present_ = nullptr;
        }

        state_ = HookState::Ready;
        description_ = "DX11 Present hook removed.";
#else
        state_ = HookState::Unsupported;
#endif
    }

  private:
#if defined(FRAMEWATCH_HAS_MINHOOK)
    using DxgiPresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,
                                                      UINT,
                                                      UINT);

    static HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* swap_chain,
                                                   UINT sync_interval,
                                                   UINT present_flags) {
        if (Dx11PresentHookWin* hook =
                active_instance_.load(std::memory_order_acquire)) {
            hook->DispatchPresent(swap_chain, sync_interval, present_flags);
        }

        return original_present_ != nullptr
                   ? original_present_(swap_chain, sync_interval, present_flags)
                   : DXGI_ERROR_INVALID_CALL;
    }

    bool ResolvePresentTarget(void*& present_target, std::string& error) {
        DummyDx11Context context;
        if (!CreateDummyDx11Context(context, error)) {
            return false;
        }

        void** vtable = *reinterpret_cast<void***>(context.swap_chain);
        // IDXGISwapChain::Present is at vtable slot 8:
        // IUnknown(0-2) + IDXGIObject(3-6) + IDXGIDeviceSubObject(7) + Present(8)
        present_target = vtable[8];
        context.Reset();

        if (present_target == nullptr) {
            error = "Failed to resolve IDXGISwapChain::Present from the DX11 dummy swap chain";
            return false;
        }

        return true;
    }

    void DispatchPresent(IDXGISwapChain* swap_chain,
                         std::uint32_t sync_interval,
                         std::uint32_t present_flags) {
        if (!callback_) {
            return;
        }

        PresentEvent present_event;
        present_event.api = GraphicsApi::Dx11;
        present_event.timestamp = FrameClock::now();
        present_event.native_swap_chain = swap_chain;
        present_event.sync_interval = sync_interval;
        present_event.present_flags = present_flags;
        callback_(present_event);
    }

    inline static std::atomic<Dx11PresentHookWin*> active_instance_{nullptr};
    inline static DxgiPresentFn original_present_{nullptr};
#endif

    HookState state_{
#if defined(FRAMEWATCH_HAS_MINHOOK)
        HookState::Ready
#else
        HookState::Unsupported
#endif
    };
    std::string description_{
#if defined(FRAMEWATCH_HAS_MINHOOK)
        "DX11 hook backend compiled. Install() will attach to IDXGISwapChain::Present through MinHook."
#else
        "DX11 backend compiled without MinHook support."
#endif
    };
    PresentCallback callback_;
    void* hook_target_{nullptr};
};

}  // namespace

std::unique_ptr<PresentHook> CreateDx11PresentHookWin() {
    return std::make_unique<Dx11PresentHookWin>();
}

}  // namespace framewatch
