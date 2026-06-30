/**
 * FrameWatch Linux LD_PRELOAD hook
 *
 * Intercepts vkQueuePresentKHR by sitting between the application and the
 * Vulkan loader.  No Vulkan SDK required — Vulkan types are defined inline
 * from the stable 1.0 ABI.
 *
 * Usage:
 *   LD_PRELOAD=/path/to/libframewatch_preload.so ./your_vulkan_game
 *
 * The hook connects to the IPC socket at /tmp/framewatch-live.sock and
 * streams newline-delimited JSON:
 *   {"frame":42,"ts":1.234567,"ft_ms":16.6667,"fps":60.000}
 *
 * If the socket is absent or the connection drops, the hook runs silently
 * with zero overhead (the socket fd is checked before every send).
 */

// _GNU_SOURCE injected by CMake target_compile_definitions (required for RTLD_NEXT)
#include <dlfcn.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

// ── Minimal Vulkan ABI (stable since Vulkan 1.0, no SDK needed) ──────────────

typedef struct VkQueue_T*        VkQueue;
typedef struct VkSwapchainKHR_T* VkSwapchainKHR;
typedef struct VkSemaphore_T*    VkSemaphore;
typedef int32_t                  VkResult;

struct VkPresentInfoKHR {
    uint32_t            sType;
    const void*         pNext;
    uint32_t            waitSemaphoreCount;
    const VkSemaphore*  pWaitSemaphores;
    uint32_t            swapchainCount;
    const VkSwapchainKHR* pSwapchains;
    const uint32_t*     pImageIndices;
    VkResult*           pResults;
};

#define VK_SUCCESS 0

using PFN_vkQueuePresentKHR = VkResult (*)(VkQueue, const VkPresentInfoKHR*);

// ── Minimal EGL / GLX ABI (opaque pointers — no SDK needed) ──────────────────

// EGL: opaque handles per the EGL 1.5 ABI (void* is ABI-stable).
typedef void*        EGLDisplay;
typedef void*        EGLSurface;
#define EGL_TRUE     1u
using PFN_eglSwapBuffers = unsigned int (*)(EGLDisplay, EGLSurface);

// GLX: Display is an Xlib opaque struct; GLXDrawable is XID (unsigned long).
typedef void*        XlibDisplay;
typedef unsigned long GLXDrawable;
using PFN_glXSwapBuffers = void (*)(XlibDisplay*, GLXDrawable);

// ── Internal state ────────────────────────────────────────────────────────────

static PFN_vkQueuePresentKHR g_real_present  = nullptr;
static PFN_eglSwapBuffers    g_real_egl_swap = nullptr;
static PFN_glXSwapBuffers    g_real_glx_swap = nullptr;
static int                   g_ipc_fd        = -1;
static std::atomic<uint64_t> g_frame_index{0};

// Protects g_ipc_fd and g_last_ts from concurrent present calls.
static std::mutex g_mutex;
static double     g_last_ts = 0.0;

static constexpr const char kEndpoint[] = "/tmp/framewatch-live.sock";

// ── Helpers ───────────────────────────────────────────────────────────────────

static double mono_time_sec() noexcept {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

static void ipc_connect() noexcept {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return;
    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, kEndpoint, sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        g_ipc_fd = fd;
    } else {
        close(fd);
    }
}

// ── Library lifecycle ─────────────────────────────────────────────────────────

__attribute__((constructor))
static void fw_preload_init() noexcept {
    g_real_present  = reinterpret_cast<PFN_vkQueuePresentKHR>(
        dlsym(RTLD_NEXT, "vkQueuePresentKHR"));
    g_real_egl_swap = reinterpret_cast<PFN_eglSwapBuffers>(
        dlsym(RTLD_NEXT, "eglSwapBuffers"));
    g_real_glx_swap = reinterpret_cast<PFN_glXSwapBuffers>(
        dlsym(RTLD_NEXT, "glXSwapBuffers"));
    ipc_connect();
}

__attribute__((destructor))
static void fw_preload_fini() noexcept {
    if (g_ipc_fd >= 0) {
        close(g_ipc_fd);
        g_ipc_fd = -1;
    }
}

// ── Shared frame-timing + IPC logic ──────────────────────────────────────────

static void record_frame() noexcept {
    const uint64_t frame = g_frame_index.load(std::memory_order_relaxed);
    const double   now   = mono_time_sec();

    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_ipc_fd >= 0 && g_last_ts > 0.0) {
        const double ft_ms = (now - g_last_ts) * 1000.0;
        const double fps   = (ft_ms > 0.0) ? 1000.0 / ft_ms : 0.0;

        char buf[128];
        int n = snprintf(buf, sizeof(buf),
            "{\"frame\":%" PRIu64 ",\"ts\":%.6f,\"ft_ms\":%.4f,\"fps\":%.3f}\n",
            frame, now, ft_ms, fps);

        if (n > 0 && n < static_cast<int>(sizeof(buf))) {
            if (write(g_ipc_fd, buf, static_cast<size_t>(n)) < 0) {
                close(g_ipc_fd);
                g_ipc_fd = -1;
            }
        }
    }

    g_last_ts = now;
}

// ── Hooks ─────────────────────────────────────────────────────────────────────

extern "C" VkResult vkQueuePresentKHR(VkQueue                 queue,
                                       const VkPresentInfoKHR* pInfo) {
    ++g_frame_index;
    record_frame();
    if (g_real_present) return g_real_present(queue, pInfo);
    return VK_SUCCESS;
}

extern "C" unsigned int eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    ++g_frame_index;
    record_frame();
    if (g_real_egl_swap) return g_real_egl_swap(dpy, surf);
    return EGL_TRUE;
}

extern "C" void glXSwapBuffers(XlibDisplay* dpy, GLXDrawable drawable) {
    ++g_frame_index;
    record_frame();
    if (g_real_glx_swap) g_real_glx_swap(dpy, drawable);
}
