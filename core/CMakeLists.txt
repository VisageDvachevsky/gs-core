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

# ---------------------------------------------------------------------------
# Dependencies — self-contained via FetchContent (no system installs needed)
# ---------------------------------------------------------------------------
include(FetchContent)

# spdlog — structured logging (uses its bundled fmt 11.0.2; no external fmt
# dependency to avoid API incompatibilities with fmt >= 11.1 where
# fmt::basic_format_string was moved from base.h to xchar.h).
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# Google Test — unit test framework
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)

enable_testing()

# AMF SDK paths
set(AMF_SDK_DIR "C:/dev/AMF" CACHE PATH "Path to AMF SDK")
if(NOT EXISTS ${AMF_SDK_DIR})
    message(FATAL_ERROR "AMF SDK not found at ${AMF_SDK_DIR}. Install it or override AMF_SDK_DIR.")
endif()

# Include directories (applied globally; unit_tests uses include/ and Windows SDK)
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

# Capture headers
set(CAPTURE_HEADERS
    include/result.h
    include/capture_types.h
    include/iframe_capture.h
    include/dxgi_capture.h
    include/wgc_capture.h
)

# Encoder headers
set(ENCODER_HEADERS
    include/encoder_types.h
    include/iencoder.h
    include/amf_encoder.h
    include/util/ring_buffer.h
)

# Capture sources (shared between several targets)
set(CAPTURE_SOURCES
    src/capture/dxgi_capture.cpp
)

# Encoder sources
set(ENCODER_SOURCES
    src/codec/amf_encoder.cpp
)

# ---------------------------------------------------------------------------
# capture_test — DXGI single-frame capture, saves BMP files
# ---------------------------------------------------------------------------
add_executable(capture_test
    ${CAPTURE_SOURCES}
    tools/capture_test.cpp
    ${CAPTURE_HEADERS}
)
target_link_libraries(capture_test PRIVATE
    ${WIN_LIBS}
    spdlog::spdlog
)
target_compile_options(capture_test PRIVATE /W4 /WX /permissive-)

# ---------------------------------------------------------------------------
# fps_loop_test — DXGI continuous 60 FPS capture loop
# ---------------------------------------------------------------------------
add_executable(fps_loop_test
    ${CAPTURE_SOURCES}
    tools/fps_loop_test.cpp
    ${CAPTURE_HEADERS}
)
target_link_libraries(fps_loop_test PRIVATE
    ${WIN_LIBS}
    spdlog::spdlog
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
)
target_compile_options(wgc_test PRIVATE
    /W4
    /WX
    /permissive-
    /await:strict
    /EHsc
)

# ---------------------------------------------------------------------------
# amf_init_test — Day 6 smoke test: DXGI init -> AMF encoder init
# ---------------------------------------------------------------------------
add_executable(amf_init_test
    ${CAPTURE_SOURCES}
    ${ENCODER_SOURCES}
    tools/amf_init_test.cpp
    ${CAPTURE_HEADERS}
    ${ENCODER_HEADERS}
)
target_link_libraries(amf_init_test PRIVATE
    ${WIN_LIBS}
    spdlog::spdlog
)
target_compile_options(amf_init_test PRIVATE /W4 /WX /permissive-)

# ---------------------------------------------------------------------------
# amf_encode_test — Day 7: DXGI capture → AMF encode → stream.h264
# ---------------------------------------------------------------------------
add_executable(amf_encode_test
    ${CAPTURE_SOURCES}
    ${ENCODER_SOURCES}
    tools/amf_encode_test.cpp
    ${CAPTURE_HEADERS}
    ${ENCODER_HEADERS}
)
target_link_libraries(amf_encode_test PRIVATE
    ${WIN_LIBS}
    spdlog::spdlog
)
target_compile_options(amf_encode_test PRIVATE /W4 /WX /permissive-)

# ---------------------------------------------------------------------------
# amf_pipeline_test — Day 8: threaded capture + encode + RingBuffer writer
# ---------------------------------------------------------------------------
add_executable(amf_pipeline_test
    ${CAPTURE_SOURCES}
    ${ENCODER_SOURCES}
    tools/amf_pipeline_test.cpp
    ${CAPTURE_HEADERS}
    ${ENCODER_HEADERS}
)
target_link_libraries(amf_pipeline_test PRIVATE
    ${WIN_LIBS}
    spdlog::spdlog
)
target_compile_options(amf_pipeline_test PRIVATE /W4 /WX /permissive-)

# ---------------------------------------------------------------------------
# unit_tests — GoogleTest suite: result.h, capture_types.h, encoder types,
#              IEncoder contract, RingBuffer (all edge cases + SPSC stress)
# ---------------------------------------------------------------------------
add_executable(unit_tests
    tests/result_test.cpp
    tests/capture_types_test.cpp
    tests/encoder_types_test.cpp
    tests/iencoder_test.cpp
    tests/util/ring_buffer_test.cpp
    ${CAPTURE_HEADERS}
    ${ENCODER_HEADERS}
)
target_link_libraries(unit_tests PRIVATE
    GTest::gtest_main
    GTest::gmock
    d3d11.lib
    dxgi.lib
)
target_compile_options(unit_tests PRIVATE /W4 /WX /permissive-)

add_test(NAME GameStreamUnitTests COMMAND unit_tests)

# ---------------------------------------------------------------------------
# Common settings
# ---------------------------------------------------------------------------
set_target_properties(
    capture_test fps_loop_test wgc_test
    amf_init_test amf_encode_test amf_pipeline_test
    unit_tests
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "GameStream Core - CMake Configuration")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Windows SDK: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "  AMF SDK: ${AMF_SDK_DIR}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
