#ifdef __linux__
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: framewatch_preload_smoke <path/to/libframewatch_preload.so>\n";
        return EXIT_FAILURE;
    }

    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        std::cerr << "dlopen failed: " << dlerror() << '\n';
        return EXIT_FAILURE;
    }

    // Verify the hook symbol is exported.
    void* sym = dlsym(lib, "vkQueuePresentKHR");
    if (!sym) {
        std::cerr << "symbol vkQueuePresentKHR not found: " << dlerror() << '\n';
        dlclose(lib);
        return EXIT_FAILURE;
    }

    std::cout << "preload smoke: vkQueuePresentKHR found at " << sym << '\n';
    dlclose(lib);
    return EXIT_SUCCESS;
}

#else
#include <cstdlib>
int main() { return EXIT_SUCCESS; }
#endif
