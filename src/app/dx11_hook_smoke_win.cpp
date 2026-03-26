#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "framewatch/hooks/hook_overlay_service.h"

namespace {

using namespace std::chrono_literals;

struct SmokeOptions {
    int frames{180};
    std::filesystem::path csv_path{"output/framewatch_dx11_hook_smoke.csv"};
    std::filesystem::path json_path{"output/framewatch_dx11_hook_smoke.json"};
};

struct Dx11SmokeContext {
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

bool ParseInteger(std::string_view value, int& output) {
    try {
        const int parsed = std::stoi(std::string(value));
        if (parsed <= 0) {
            return false;
        }
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

SmokeOptions ParseArgs(int argc, char** argv) {
    SmokeOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--frames" && (i + 1) < argc) {
            int parsed_frames = options.frames;
            if (ParseInteger(argv[++i], parsed_frames)) {
                options.frames = parsed_frames;
            }
        } else if (arg == "--csv" && (i + 1) < argc) {
            options.csv_path = argv[++i];
        } else if (arg == "--json" && (i + 1) < argc) {
            options.json_path = argv[++i];
        }
    }

    return options;
}

LRESULT CALLBACK SmokeWindowProc(HWND window_handle,
                                 UINT message,
                                 WPARAM wparam,
                                 LPARAM lparam) {
    return DefWindowProcW(window_handle, message, wparam, lparam);
}

bool EnsureSmokeWindowClass() {
    static bool registered = false;
    if (registered) {
        return true;
    }

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = SmokeWindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"FrameWatchMiniDx11SmokeWindow";

    if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    registered = true;
    return true;
}

bool CreateDx11SmokeContext(Dx11SmokeContext& context) {
    if (!EnsureSmokeWindowClass()) {
        return false;
    }

    context.window = CreateWindowExW(0,
                                     L"FrameWatchMiniDx11SmokeWindow",
                                     L"FrameWatch DX11 Hook Smoke",
                                     WS_OVERLAPPEDWINDOW,
                                     CW_USEDEFAULT,
                                     CW_USEDEFAULT,
                                     320,
                                     240,
                                     nullptr,
                                     nullptr,
                                     GetModuleHandleW(nullptr),
                                     nullptr);
    if (context.window == nullptr) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
    swap_chain_desc.BufferDesc.Width = 320;
    swap_chain_desc.BufferDesc.Height = 240;
    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.OutputWindow = context.window;
    swap_chain_desc.Windowed = TRUE;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    constexpr D3D_DRIVER_TYPE kDriverTypes[] = {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
    };

    for (const D3D_DRIVER_TYPE driver_type : kDriverTypes) {
        D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
        const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr,
                                                         driver_type,
                                                         nullptr,
                                                         0,
                                                         kFeatureLevels,
                                                         static_cast<UINT>(std::size(kFeatureLevels)),
                                                         D3D11_SDK_VERSION,
                                                         &swap_chain_desc,
                                                         &context.swap_chain,
                                                         &context.device,
                                                         &feature_level,
                                                         &context.device_context);
        if (SUCCEEDED(hr)) {
            return true;
        }
    }

    context.Reset();
    return false;
}

void PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void PrintMetrics(const framewatch::MetricsSnapshot& snapshot) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "FPS: " << snapshot.current_fps << '\n';
    std::cout << "Average FPS: " << snapshot.average_fps << '\n';
    std::cout << "1% low: " << snapshot.one_percent_low_fps << '\n';
    std::cout << "0.1% low: " << snapshot.point_one_percent_low_fps << '\n';
    std::cout << "Frametime: " << snapshot.latest_frametime_ms << " ms\n";
}

}  // namespace

int main(int argc, char** argv) {
    const SmokeOptions options = ParseArgs(argc, argv);

    auto service = framewatch::CreateHookOverlayService(300);
    if (!service->Initialize()) {
        std::cerr << "DX11 hook initialization failed\n";
        std::cerr << "Hook backend: " << static_cast<int>(service->HookBackendType()) << '\n';
        std::cerr << "Hook status: " << service->HookDescription() << '\n';
        return EXIT_FAILURE;
    }
    service->Runtime().SetExportPaths(options.csv_path, options.json_path);

    Dx11SmokeContext context;
    if (!CreateDx11SmokeContext(context)) {
        std::cerr << "Failed to create DX11 smoke context\n";
        service->Shutdown();
        return EXIT_FAILURE;
    }

    for (int frame_index = 0; frame_index < options.frames; ++frame_index) {
        PumpMessages();
        context.swap_chain->Present(1, 0);
        std::this_thread::sleep_for(16ms);
    }

    framewatch::PerformanceSession& session = service->Runtime().Session();
    const framewatch::MetricsSnapshot metrics = session.LiveMetrics();
    const bool exported = session.ExportPreferred(options.csv_path, options.json_path);

    std::cout << "FrameWatch DX11 hook smoke\n";
    std::cout << "Hook status: " << service->HookDescription() << '\n';
    std::cout << "Renderer: " << service->Runtime().RendererName() << '\n';
    std::cout << "Samples captured: " << session.LiveSampleCount() << '\n';
    PrintMetrics(metrics);
    std::cout << "CSV export: " << (exported ? "ok" : "failed")
              << " -> " << options.csv_path.string() << '\n';
    std::cout << "JSON export: " << (exported ? "ok" : "failed")
              << " -> " << options.json_path.string() << '\n';

    context.Reset();
    service->Shutdown();
    return exported ? EXIT_SUCCESS : EXIT_FAILURE;
}
