#ifdef _WIN32

#include <atomic>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include "framewatch/hooks/present_hook.h"

// Factory function defined in src/hooks/dx12/dx12_present_hook_win.cpp
namespace framewatch {
std::unique_ptr<PresentHook> CreateDx12PresentHookWin();
}

// ---------------------------------------------------------------------------
// Minimal DX12 context for exercising the hooked swap chain.
// ---------------------------------------------------------------------------

namespace {

struct Dx12SmokeContext {
    HWND               window{nullptr};
    IDXGIFactory4*     factory{nullptr};
    ID3D12Device*      device{nullptr};
    ID3D12CommandQueue* cmd_queue{nullptr};
    IDXGISwapChain1*   swap_chain{nullptr};

    void Reset() noexcept {
        if (swap_chain)  { swap_chain->Release();  swap_chain  = nullptr; }
        if (cmd_queue)   { cmd_queue->Release();   cmd_queue   = nullptr; }
        if (device)      { device->Release();      device      = nullptr; }
        if (factory)     { factory->Release();     factory     = nullptr; }
        if (window)      { DestroyWindow(window);  window      = nullptr; }
    }
};

bool CreateDx12SmokeContext(Dx12SmokeContext& ctx) {
    WNDCLASSW wc{};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FrameWatchDx12SmokeWindow";
    RegisterClassW(&wc);

    ctx.window = CreateWindowExW(0, L"FrameWatchDx12SmokeWindow",
                                 L"FrameWatch DX12 Hook Smoke",
                                 WS_OVERLAPPEDWINDOW,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 320, 240,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!ctx.window) return false;

    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4),
                                   reinterpret_cast<void**>(&ctx.factory)))) {
        ctx.Reset(); return false;
    }

    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                  __uuidof(ID3D12Device),
                                  reinterpret_cast<void**>(&ctx.device)))) {
        ctx.Reset(); return false;
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(ctx.device->CreateCommandQueue(
            &qd, __uuidof(ID3D12CommandQueue),
            reinterpret_cast<void**>(&ctx.cmd_queue)))) {
        ctx.Reset(); return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width       = 320;
    sd.Height      = 240;
    sd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferCount = 2;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    if (FAILED(ctx.factory->CreateSwapChainForHwnd(
            ctx.cmd_queue, ctx.window, &sd, nullptr, nullptr, &ctx.swap_chain))) {
        ctx.Reset(); return false;
    }

    return true;
}

}  // namespace

int main() {
    auto hook = framewatch::CreateDx12PresentHookWin();

    std::atomic<int> callback_count{0};
    hook->SetPresentCallback([&](const framewatch::PresentEvent&) {
        ++callback_count;
    });

    if (!hook->Install()) {
        if (hook->State() == framewatch::HookState::Unsupported) {
            std::cout << "FrameWatch DX12 hook smoke: DX12 not available on this system — skipping\n";
            std::cout << "Reason: " << hook->Description() << '\n';
            return EXIT_SUCCESS;
        }
        std::cerr << "FrameWatch DX12 hook smoke: Install() failed\n";
        std::cerr << "Reason: " << hook->Description() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "Hook installed: " << hook->Description() << '\n';

    Dx12SmokeContext ctx;
    if (!CreateDx12SmokeContext(ctx)) {
        hook->Remove();
        std::cerr << "Failed to create DX12 smoke context\n";
        return EXIT_FAILURE;
    }

    // DXGI_PRESENT_TEST (0x1) — test present: does not swap buffers,
    // so no render targets are required.  The detour fires regardless.
    for (int i = 0; i < 10; ++i) {
        ctx.swap_chain->Present(0, DXGI_PRESENT_TEST);
    }

    ctx.Reset();
    hook->Remove();

    const int captured = callback_count.load();
    std::cout << "Frames captured by DX12 hook: " << captured << '\n';

    if (captured == 0) {
        std::cerr << "ERROR: hook fired 0 times — detour may not be attached\n";
        return EXIT_FAILURE;
    }

    std::cout << "DX12 hook smoke PASSED\n";
    return EXIT_SUCCESS;
}

#else

#include <cstdlib>
int main() { return EXIT_SUCCESS; }

#endif  // _WIN32
