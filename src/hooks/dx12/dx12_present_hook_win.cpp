#ifdef _WIN32

#include "framewatch/hooks/present_hook.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#if defined(FRAMEWATCH_HAS_MINHOOK)
#include <MinHook.h>
#endif

namespace framewatch {

namespace {

// ---------------------------------------------------------------------------
// Dummy DX12 context — used only to extract IDXGISwapChain::Present vtable ptr.
// ---------------------------------------------------------------------------

struct DummyDx12Context {
    HWND              window{nullptr};
    IDXGIFactory4*    factory{nullptr};
    ID3D12Device*     device{nullptr};
    ID3D12CommandQueue* cmd_queue{nullptr};
    IDXGISwapChain1*  swap_chain{nullptr};

    void Reset() noexcept {
        if (swap_chain)  { swap_chain->Release();  swap_chain  = nullptr; }
        if (cmd_queue)   { cmd_queue->Release();   cmd_queue   = nullptr; }
        if (device)      { device->Release();      device      = nullptr; }
        if (factory)     { factory->Release();     factory     = nullptr; }
        if (window)      { DestroyWindow(window);  window      = nullptr; }
    }
};

bool CreateDummyDx12Context(DummyDx12Context& ctx, std::string& error) {
    WNDCLASSW wc{};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FrameWatchMiniDx12DummyWindow";
    // Ignore ERROR_CLASS_ALREADY_EXISTS — safe to call multiple times.
    if (RegisterClassW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = "Failed to register DX12 dummy window class";
        return false;
    }

    ctx.window = CreateWindowExW(0, L"FrameWatchMiniDx12DummyWindow",
                                 L"FrameWatch DX12 Dummy",
                                 WS_OVERLAPPEDWINDOW,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 64, 64,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!ctx.window) {
        error = "Failed to create DX12 dummy window";
        return false;
    }

    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4),
                                   reinterpret_cast<void**>(&ctx.factory)))) {
        error = "CreateDXGIFactory1 failed";
        ctx.Reset();
        return false;
    }

    // D3D12CreateDevice fails gracefully on machines without a DX12 adapter.
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                  __uuidof(ID3D12Device),
                                  reinterpret_cast<void**>(&ctx.device)))) {
        error = "D3D12CreateDevice failed — no DX12-capable adapter on this system";
        ctx.Reset();
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(ctx.device->CreateCommandQueue(
            &qd, __uuidof(ID3D12CommandQueue),
            reinterpret_cast<void**>(&ctx.cmd_queue)))) {
        error = "ID3D12Device::CreateCommandQueue failed";
        ctx.Reset();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width       = 64;
    sd.Height      = 64;
    sd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferCount = 2;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    if (FAILED(ctx.factory->CreateSwapChainForHwnd(
            ctx.cmd_queue, ctx.window, &sd, nullptr, nullptr, &ctx.swap_chain))) {
        error = "IDXGIFactory4::CreateSwapChainForHwnd failed";
        ctx.Reset();
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Dx12PresentHookWin — IDXGISwapChain::Present vtable detour via MinHook.
// ---------------------------------------------------------------------------

class Dx12PresentHookWin final : public PresentHook {
  public:
    HookBackend Backend() const noexcept override { return HookBackend::Dx12; }
    HookState   State()   const noexcept override { return state_; }

    std::string_view Description() const noexcept override { return description_; }

    void SetPresentCallback(PresentCallback cb) override { callback_ = std::move(cb); }

    bool Install() override {
#if !defined(FRAMEWATCH_HAS_MINHOOK)
        state_       = HookState::Unsupported;
        description_ = "DX12 hook compiled without MinHook. Set FRAMEWATCH_MINHOOK_ROOT.";
        return false;
#else
        if (state_ == HookState::Running) return true;

        Dx12PresentHookWin* expected = nullptr;
        if (!active_instance_.compare_exchange_strong(
                expected, this, std::memory_order_acq_rel, std::memory_order_acquire)) {
            state_       = HookState::Error;
            description_ = "Another DX12 Present hook instance is already active.";
            return false;
        }

        void*       present_target = nullptr;
        std::string err;
        if (!ResolvePresentTarget(present_target, err)) {
            active_instance_.store(nullptr, std::memory_order_release);
            // Treat adapter absence as Unsupported, not Error.
            state_       = HookState::Unsupported;
            description_ = std::move(err);
            return false;
        }

        MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
            active_instance_.store(nullptr, std::memory_order_release);
            state_       = HookState::Error;
            description_ = FormatStatus("MH_Initialize failed", s);
            return false;
        }

        s = MH_CreateHook(present_target,
                          reinterpret_cast<LPVOID>(&Dx12PresentHookWin::PresentDetour),
                          reinterpret_cast<LPVOID*>(&original_present_));
        if (s != MH_OK) {
            active_instance_.store(nullptr, std::memory_order_release);
            state_       = HookState::Error;
            description_ = FormatStatus("MH_CreateHook(DX12 Present) failed", s);
            return false;
        }

        s = MH_EnableHook(present_target);
        if (s != MH_OK) {
            MH_RemoveHook(present_target);
            original_present_ = nullptr;
            active_instance_.store(nullptr, std::memory_order_release);
            state_       = HookState::Error;
            description_ = FormatStatus("MH_EnableHook(DX12 Present) failed", s);
            return false;
        }

        hook_target_ = present_target;
        state_       = HookState::Running;
        description_ = "DX12 Present hook installed — IDXGISwapChain::Present (vtable[8]) detouring via MinHook.";
        return true;
#endif
    }

    void Remove() noexcept override {
#if defined(FRAMEWATCH_HAS_MINHOOK)
        // Null active_instance_ first so PresentDetour stops dispatching
        // before the trampoline is freed.
        active_instance_.store(nullptr, std::memory_order_release);

        if (hook_target_) {
            MH_DisableHook(hook_target_);
            MH_RemoveHook(hook_target_);
            hook_target_      = nullptr;
            original_present_ = nullptr;
        }

        state_       = HookState::Ready;
        description_ = "DX12 Present hook removed.";
#else
        state_ = HookState::Unsupported;
#endif
    }

  private:
#if defined(FRAMEWATCH_HAS_MINHOOK)
    using DxgiPresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

    static HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* swap_chain,
                                                    UINT sync_interval,
                                                    UINT flags) {
        if (Dx12PresentHookWin* h = active_instance_.load(std::memory_order_acquire)) {
            h->DispatchPresent(swap_chain, sync_interval, flags);
        }
        return original_present_
                   ? original_present_(swap_chain, sync_interval, flags)
                   : DXGI_ERROR_INVALID_CALL;
    }

    bool ResolvePresentTarget(void*& target, std::string& err) {
        DummyDx12Context ctx;
        if (!CreateDummyDx12Context(ctx, err)) return false;

        // IDXGISwapChain::Present is at vtable slot 8 (same slot as DX11):
        // IUnknown(0-2) + IDXGIObject(3-6) + IDXGIDeviceSubObject(7) + Present(8)
        void** vtable = *reinterpret_cast<void***>(ctx.swap_chain);
        target = vtable[8];
        ctx.Reset();

        if (!target) {
            err = "Failed to resolve IDXGISwapChain::Present from DX12 dummy swap chain";
            return false;
        }
        return true;
    }

    void DispatchPresent(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags) {
        if (!callback_) return;
        PresentEvent ev;
        ev.api              = GraphicsApi::Dx12;
        ev.timestamp        = FrameClock::now();
        ev.native_swap_chain = swap_chain;
        ev.sync_interval    = sync_interval;
        ev.present_flags    = flags;
        callback_(ev);
    }

    static std::string FormatStatus(const char* msg, MH_STATUS s) {
        std::ostringstream ss;
        ss << msg << " (MinHook status " << static_cast<int>(s) << ")";
        return ss.str();
    }

    inline static std::atomic<Dx12PresentHookWin*> active_instance_{nullptr};
    inline static DxgiPresentFn                    original_present_{nullptr};
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
        "DX12 hook backend compiled. Install() will attach to IDXGISwapChain::Present via MinHook."
#else
        "DX12 hook compiled without MinHook support."
#endif
    };
    PresentCallback callback_;
    void*           hook_target_{nullptr};
};

}  // namespace

std::unique_ptr<PresentHook> CreateDx12PresentHookWin() {
    return std::make_unique<Dx12PresentHookWin>();
}

}  // namespace framewatch

#endif  // _WIN32
