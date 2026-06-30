#ifdef __linux__
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>

namespace {

bool check_symbol(void* lib, const char* name) {
    void* sym = dlsym(lib, name);
    if (!sym) {
        std::cerr << "symbol " << name << " not found: " << dlerror() << '\n';
        return false;
    }
    std::cout << "preload smoke: " << name << " @ " << sym << '\n';
    return true;
}

}  // namespace

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

    bool ok = true;
    ok &= check_symbol(lib, "vkQueuePresentKHR");
    ok &= check_symbol(lib, "eglSwapBuffers");
    ok &= check_symbol(lib, "glXSwapBuffers");

    dlclose(lib);
    if (!ok) return EXIT_FAILURE;

    std::cout << "preload smoke PASSED\n";
    return EXIT_SUCCESS;
}

#else
#include <cstdlib>
int main() { return EXIT_SUCCESS; }
#endif
