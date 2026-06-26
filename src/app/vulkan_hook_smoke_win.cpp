#ifdef _WIN32
#include <cstdlib>
#include <iostream>

#include "framewatch/hooks/vulkan/vulkan_present_hook_win.h"

int main() {
    framewatch::VulkanPresentHookWin hook;

    bool installed = hook.Install();
    const framewatch::HookState state = hook.State();

    if (installed) {
        std::cout << "Vulkan hook: installed — " << hook.Description() << '\n';
        hook.Remove();
        std::cout << "Vulkan hook: removed\n";
        return EXIT_SUCCESS;
    }

    // Graceful failure: Vulkan not on this machine or MinHook not present.
    if (state == framewatch::HookState::Unsupported) {
        std::cout << "Vulkan hook: unsupported — " << hook.Description() << '\n';
        return EXIT_SUCCESS;
    }

    std::cerr << "Vulkan hook: error — " << hook.Description() << '\n';
    return EXIT_FAILURE;
}

#else
#include <cstdlib>
int main() { return EXIT_SUCCESS; }
#endif
