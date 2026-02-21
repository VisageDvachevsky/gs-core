cmake_minimum_required(VERSION 3.20)
project(GameStream_Core CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows SDK version (we have 10.0.26100.0)
set(CMAKE_SYSTEM_VERSION "10.0.26100.0")

# Windows SDK paths for WinRT
set(WINDOWS_SDK_PATH "C:/Program Files (x86)/Windows Kits/10")
set(WINDOWS_SDK_VERSION "10.0.26100.0")

# Find packages — REQUIRED: missing dependencies fail fast with a clear message
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

# AMF SDK paths
set(AMF_SDK_DIR "C:/dev/AMF" CACHE PATH "Path to AMF SDK")
if(NOT EXISTS ${AMF_SDK_DIR})
    message(FATAL_ERROR "AMF SDK not found at ${AMF_SDK_DIR}")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${AMF_SDK_DIR}/amf/public/include
    "${WINDOWS_SDK_PATH}/Include/${WINDOWS_SDK_VERSION}/cppwinrt"
    "${WINDOWS_SDK_PATH}/Include/${WINDOWS_SDK_VERSION}/winrt"
)

# Windows libraries
set(WIN_LIBS
    d3d11.lib
    dxgi.lib
    windowscodecs.lib  # WIC for BMP saving
    user32.lib
    kernel32.lib
)

# WinRT libraries (for WGC)
set(WINRT_LIBS
    windowsapp.lib
    onecore.lib
)

# Header files
set(CAPTURE_HEADERS
    include/result.h
    include/capture_types.h
    include/iframe_capture.h
    include/dxgi_capture.h
    include/wgc_capture.h
)

# ---------------------------------------------------------------------------
# capture_test — DXGI single-frame capture, saves BMP files
# ---------------------------------------------------------------------------
add_executable(capture_test
    src/capture/dxgi_capture.cpp
    tools/capture_test.cpp
    ${CAPTURE_HEADERS}
)
target_link_libraries(capture_test PRIVATE
    ${WIN_LIBS}
    spdlog::spdlog
    fmt::fmt
)
target_compile_options(capture_test PRIVATE /W4 /WX /permissive-)

# ---------------------------------------------------------------------------
# fps_loop_test — DXGI continuous 60 FPS capture loop
# ---------------------------------------------------------------------------
add_executable(fps_loop_test
    src/capture/dxgi_capture.cpp
    tools/fps_loop_test.cpp
    ${CAPTURE_HEADERS}
)
target_link_libraries(fps_loop_test PRIVATE
    ${WIN_LIBS}
    spdlog::spdlog
    fmt::fmt
)
target_compile_options(fps_loop_test PRIVATE /W4 /WX /permissive-)

# ---------------------------------------------------------------------------
# wgc_test — WGC per-window capture loop
# ---------------------------------------------------------------------------
add_executable(wgc_test
    src/capture/wgc_capture.cpp
    tools/wgc_test.cpp
    ${CAPTURE_HEADERS}
)
target_link_libraries(wgc_test PRIVATE
    ${WIN_LIBS}
    ${WINRT_LIBS}
    spdlog::spdlog
    fmt::fmt
)
target_compile_options(wgc_test PRIVATE
    /W4
    /WX
    /permissive-
    /await:strict
    /EHsc
)

# ---------------------------------------------------------------------------
# Common settings
# ---------------------------------------------------------------------------
set_target_properties(capture_test fps_loop_test wgc_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "GameStream Core - CMake Configuration")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Windows SDK: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "  AMF SDK: ${AMF_SDK_DIR}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
