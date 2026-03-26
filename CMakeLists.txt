cmake_minimum_required(VERSION 3.21)

project(FrameWatchMini VERSION 0.1.0 LANGUAGES CXX)

option(FRAMEWATCH_ENABLE_DX11_HOOK "Build the DX11 hook scaffold on Windows" ON)
option(FRAMEWATCH_ENABLE_MINHOOK "Enable MinHook-backed DX11 Present detouring when MinHook is available" ON)
option(FRAMEWATCH_BUILD_DEMO "Build the CLI MVP demo" ON)
option(FRAMEWATCH_BUILD_DEBUG_WINDOW "Build the SDL2 debug window if SDL2 is available" ON)
option(FRAMEWATCH_BUILD_TESTS "Build unit tests" ON)

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
)

target_include_directories(framewatch_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
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
    )
    target_compile_definitions(framewatch_core PRIVATE
        FRAMEWATCH_HAS_DX11_HOOK=1
        FRAMEWATCH_HAS_DX11_OVERLAY=1
    )
    target_link_libraries(framewatch_core PUBLIC d3d11 dxgi d3dcompiler)

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

if(WIN32 AND FRAMEWATCH_ENABLE_DX11_HOOK)
    add_executable(framewatch_dx11_hook_smoke src/app/dx11_hook_smoke_win.cpp)
    target_link_libraries(framewatch_dx11_hook_smoke PRIVATE framewatch_core d3d11 dxgi d3dcompiler)
    target_compile_features(framewatch_dx11_hook_smoke PRIVATE cxx_std_20)
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
        add_executable(framewatch_debug_window src/app/debug_window_main.cpp)
        target_link_libraries(framewatch_debug_window PRIVATE framewatch_core ${FRAMEWATCH_SDL2_TARGET})
        target_compile_features(framewatch_debug_window PRIVATE cxx_std_20)
    else()
        message(STATUS "FrameWatch: SDL2 not found, skipping framewatch_debug_window target")
    endif()
endif()

include(CTest)

if(BUILD_TESTING AND FRAMEWATCH_BUILD_TESTS)
    add_executable(framewatch_tests tests/metrics_tests.cpp)
    target_link_libraries(framewatch_tests PRIVATE framewatch_core)
    target_compile_features(framewatch_tests PRIVATE cxx_std_20)
    add_test(NAME framewatch_tests COMMAND framewatch_tests)
endif()
