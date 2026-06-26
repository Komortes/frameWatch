cmake_minimum_required(VERSION 3.21)

project(FrameWatchMini VERSION 0.1.0 LANGUAGES CXX)

option(FRAMEWATCH_ENABLE_DX11_HOOK "Build the DX11 hook scaffold on Windows" ON)
option(FRAMEWATCH_ENABLE_VULKAN_HOOK "Build the Vulkan vkQueuePresentKHR hook scaffold on Windows" ON)
option(FRAMEWATCH_ENABLE_MINHOOK "Enable MinHook-backed Present detouring when MinHook is available" ON)
option(FRAMEWATCH_BUILD_WINDOWS_BOOTSTRAP "Build the injected DX11 overlay DLL and injector on Windows" ON)
option(FRAMEWATCH_BUILD_DEMO "Build the CLI MVP demo" ON)
option(FRAMEWATCH_BUILD_DEBUG_WINDOW "Build the SDL2 debug window if SDL2 is available" ON)
option(FRAMEWATCH_BUILD_TESTS "Build unit tests" ON)
option(FRAMEWATCH_SANITIZE_ADDRESS "Enable AddressSanitizer + UBSan (GCC/Clang only)" OFF)
option(FRAMEWATCH_SANITIZE_THREAD "Enable ThreadSanitizer (GCC/Clang only)" OFF)

if(FRAMEWATCH_SANITIZE_ADDRESS AND FRAMEWATCH_SANITIZE_THREAD)
    message(FATAL_ERROR "FRAMEWATCH_SANITIZE_ADDRESS and FRAMEWATCH_SANITIZE_THREAD are mutually exclusive")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    if(FRAMEWATCH_SANITIZE_ADDRESS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
        message(STATUS "FrameWatch: AddressSanitizer + UBSan enabled")
    endif()
    if(FRAMEWATCH_SANITIZE_THREAD)
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
        add_link_options(-fsanitize=thread)
        message(STATUS "FrameWatch: ThreadSanitizer enabled")
    endif()
endif()

set(FRAMEWATCH_MINHOOK_ROOT "" CACHE PATH "Optional MinHook installation root")

add_library(framewatch_core
    src/core/frametime_tracker.cpp
    src/core/metrics_engine.cpp
    src/core/session_logger.cpp
    src/session/performance_session.cpp
    src/exporter/csv_exporter.cpp
    src/exporter/json_exporter.cpp
    src/hooks/hook_overlay_service.cpp
    src/hooks/present_hook.cpp
    src/overlay/overlay_model.cpp
    src/overlay/overlay_settings.cpp
    src/overlay/overlay_runtime.cpp
    src/overlay/overlay_renderer_stub.cpp
    src/ipc/ipc_server.cpp
)

target_include_directories(framewatch_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_compile_features(framewatch_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(framewatch_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(framewatch_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32 AND FRAMEWATCH_ENABLE_DX11_HOOK)
    target_sources(framewatch_core PRIVATE
        src/hooks/dx11/dx11_present_hook_win.cpp
        src/overlay/dx11/dx11_overlay_renderer_win.cpp
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(framewatch_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
    )
    target_compile_definitions(framewatch_core PRIVATE
        FRAMEWATCH_HAS_DX11_HOOK=1
        FRAMEWATCH_HAS_DX11_OVERLAY=1
    )
    target_link_libraries(framewatch_core PUBLIC d3d11 dxgi d3dcompiler)

    if(MSVC)
        set_source_files_properties(
            third_party/imgui/imgui.cpp
            third_party/imgui/imgui_draw.cpp
            third_party/imgui/imgui_tables.cpp
            third_party/imgui/imgui_widgets.cpp
            third_party/imgui/backends/imgui_impl_dx11.cpp
            PROPERTIES COMPILE_OPTIONS "/W0"
        )
    endif()

    if(FRAMEWATCH_ENABLE_MINHOOK)
        find_path(FRAMEWATCH_MINHOOK_INCLUDE_DIR
            NAMES MinHook.h
            HINTS
                ${FRAMEWATCH_MINHOOK_ROOT}
                ${FRAMEWATCH_MINHOOK_ROOT}/include
        )
        find_library(FRAMEWATCH_MINHOOK_LIBRARY
            NAMES MinHook minhook libMinHook.x64 libMinHook.x86
            HINTS
                ${FRAMEWATCH_MINHOOK_ROOT}
                ${FRAMEWATCH_MINHOOK_ROOT}/lib
                ${FRAMEWATCH_MINHOOK_ROOT}/lib64
        )

        if(FRAMEWATCH_MINHOOK_INCLUDE_DIR AND FRAMEWATCH_MINHOOK_LIBRARY)
            target_include_directories(framewatch_core PRIVATE ${FRAMEWATCH_MINHOOK_INCLUDE_DIR})
            target_link_libraries(framewatch_core PRIVATE ${FRAMEWATCH_MINHOOK_LIBRARY})
            target_compile_definitions(framewatch_core PRIVATE FRAMEWATCH_HAS_MINHOOK=1)
            message(STATUS "FrameWatch: MinHook enabled from ${FRAMEWATCH_MINHOOK_LIBRARY}")
        else()
            message(STATUS "FrameWatch: MinHook not found, DX11 Present detour stays disabled")
        endif()
    endif()
endif()

if(WIN32 AND FRAMEWATCH_ENABLE_VULKAN_HOOK)
    target_sources(framewatch_core PRIVATE
        src/hooks/vulkan/vulkan_present_hook_win.cpp
    )
    target_compile_definitions(framewatch_core PRIVATE FRAMEWATCH_HAS_VULKAN_HOOK=1)
    message(STATUS "FrameWatch: Vulkan hook scaffold enabled")
endif()

if(APPLE)
    find_library(FRAMEWATCH_APPLICATION_SERVICES_FRAMEWORK ApplicationServices REQUIRED)
    target_sources(framewatch_core PRIVATE src/platform/window_targeting_macos.cpp)
    target_link_libraries(framewatch_core PRIVATE ${FRAMEWATCH_APPLICATION_SERVICES_FRAMEWORK})
else()
    target_sources(framewatch_core PRIVATE src/platform/window_targeting_stub.cpp)
endif()

if(FRAMEWATCH_BUILD_DEMO)
    add_executable(framewatch_demo src/app/main.cpp)
    target_link_libraries(framewatch_demo PRIVATE framewatch_core)
    target_compile_features(framewatch_demo PRIVATE cxx_std_20)
endif()

if(WIN32 AND FRAMEWATCH_ENABLE_VULKAN_HOOK)
    add_executable(framewatch_vulkan_hook_smoke src/app/vulkan_hook_smoke_win.cpp)
    target_link_libraries(framewatch_vulkan_hook_smoke PRIVATE framewatch_core)
    target_compile_features(framewatch_vulkan_hook_smoke PRIVATE cxx_std_20)
endif()

if(WIN32 AND FRAMEWATCH_ENABLE_DX11_HOOK)
    add_executable(framewatch_dx11_hook_smoke src/app/dx11_hook_smoke_win.cpp)
    target_link_libraries(framewatch_dx11_hook_smoke PRIVATE framewatch_core d3d11 dxgi d3dcompiler)
    target_compile_features(framewatch_dx11_hook_smoke PRIVATE cxx_std_20)

    if(FRAMEWATCH_BUILD_WINDOWS_BOOTSTRAP)
        add_library(framewatch_dx11_overlay SHARED src/bootstrap/injected_runtime_win.cpp)
        target_link_libraries(framewatch_dx11_overlay PRIVATE framewatch_core)
        target_include_directories(framewatch_dx11_overlay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_compile_definitions(framewatch_dx11_overlay PRIVATE FRAMEWATCH_BUILD_INJECTED_RUNTIME=1)
        target_compile_features(framewatch_dx11_overlay PRIVATE cxx_std_20)

        add_executable(framewatch_injector src/app/framewatch_injector_win.cpp)
        target_include_directories(framewatch_injector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(framewatch_injector PRIVATE user32)
        target_compile_features(framewatch_injector PRIVATE cxx_std_20)
    endif()
endif()

set(FRAMEWATCH_SDL2_TARGET "")

if(FRAMEWATCH_BUILD_DEBUG_WINDOW)
    find_package(SDL2 CONFIG QUIET)

    if(TARGET SDL2::SDL2)
        set(FRAMEWATCH_SDL2_TARGET SDL2::SDL2)
        message(STATUS "FrameWatch: using SDL2 package target")
    else()
        find_path(FRAMEWATCH_SDL2_INCLUDE_DIR
            NAMES SDL.h
            PATH_SUFFIXES SDL2
            HINTS
                /opt/homebrew/include
                /usr/local/include
        )
        find_library(FRAMEWATCH_SDL2_LIBRARY
            NAMES SDL2
            HINTS
                /opt/homebrew/lib
                /usr/local/lib
        )

        if(FRAMEWATCH_SDL2_INCLUDE_DIR AND FRAMEWATCH_SDL2_LIBRARY)
            add_library(framewatch_sdl2 INTERFACE)
            target_include_directories(framewatch_sdl2 INTERFACE ${FRAMEWATCH_SDL2_INCLUDE_DIR})
            target_link_libraries(framewatch_sdl2 INTERFACE ${FRAMEWATCH_SDL2_LIBRARY})
            add_library(FrameWatch::SDL2 ALIAS framewatch_sdl2)
            set(FRAMEWATCH_SDL2_TARGET FrameWatch::SDL2)
            message(STATUS "FrameWatch: using SDL2 from ${FRAMEWATCH_SDL2_LIBRARY}")
        endif()
    endif()

    if(FRAMEWATCH_SDL2_TARGET)
        add_executable(framewatch_debug_window
            src/app/debug_window_main.cpp
            src/app/debug_window/bitmap_font.cpp
            src/app/debug_window/renderer.cpp
            src/app/debug_window/targeting.cpp
            src/app/debug_window/ui_panels.cpp
        )
        target_link_libraries(framewatch_debug_window PRIVATE framewatch_core ${FRAMEWATCH_SDL2_TARGET})
        target_compile_features(framewatch_debug_window PRIVATE cxx_std_20)
    else()
        message(STATUS "FrameWatch: SDL2 not found, skipping framewatch_debug_window target")
    endif()
endif()

include(CTest)

if(BUILD_TESTING AND FRAMEWATCH_BUILD_TESTS)
    find_package(Catch2 3 QUIET)
    if(NOT TARGET Catch2::Catch2WithMain)
        include(FetchContent)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.7.1
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(Catch2)
    endif()

    if(TARGET Catch2::Catch2WithMain)
        add_executable(framewatch_tests tests/metrics_tests.cpp)
        target_link_libraries(framewatch_tests PRIVATE framewatch_core Catch2::Catch2WithMain)
        target_compile_features(framewatch_tests PRIVATE cxx_std_20)
        include(Catch)
        catch_discover_tests(framewatch_tests)
    else()
        message(STATUS "FrameWatch: Catch2 unavailable — skipping unit tests")
    endif()
endif()
