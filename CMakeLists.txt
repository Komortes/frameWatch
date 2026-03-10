cmake_minimum_required(VERSION 3.21)

project(FrameWatchMini VERSION 0.1.0 LANGUAGES CXX)

option(FRAMEWATCH_ENABLE_DX11_HOOK "Build the DX11 hook scaffold on Windows" ON)
option(FRAMEWATCH_BUILD_DEMO "Build the CLI MVP demo" ON)
option(FRAMEWATCH_BUILD_TESTS "Build unit tests" ON)

add_library(framewatch_core
    src/core/frametime_tracker.cpp
    src/core/metrics_engine.cpp
    src/core/session_logger.cpp
    src/exporter/csv_exporter.cpp
    src/exporter/json_exporter.cpp
    src/hooks/present_hook.cpp
    src/overlay/overlay_model.cpp
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
    target_sources(framewatch_core PRIVATE src/hooks/dx11/dx11_present_hook_win.cpp)
    target_compile_definitions(framewatch_core PRIVATE FRAMEWATCH_HAS_DX11_HOOK=1)
endif()

if(FRAMEWATCH_BUILD_DEMO)
    add_executable(framewatch_demo src/app/main.cpp)
    target_link_libraries(framewatch_demo PRIVATE framewatch_core)
    target_compile_features(framewatch_demo PRIVATE cxx_std_20)
endif()

include(CTest)

if(BUILD_TESTING AND FRAMEWATCH_BUILD_TESTS)
    add_executable(framewatch_tests tests/metrics_tests.cpp)
    target_link_libraries(framewatch_tests PRIVATE framewatch_core)
    target_compile_features(framewatch_tests PRIVATE cxx_std_20)
    add_test(NAME framewatch_tests COMMAND framewatch_tests)
endif()
