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

# WebRTC abstraction headers (no libwebrtc dependency — interfaces + types only)
set(WEBRTC_ABSTRACT_HEADERS
    include/webrtc_types.h
    include/iwebrtc_observer.h
    include/iwebrtc_host.h
    include/webrtc_host.h
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
# wgc_encode_test — per-window WGC capture + AMF H.264 encode
# ---------------------------------------------------------------------------
add_executable(wgc_encode_test
    ${CAPTURE_SOURCES}
    src/capture/wgc_capture.cpp
    ${ENCODER_SOURCES}
    tools/wgc_encode_test.cpp
    ${CAPTURE_HEADERS}
    ${ENCODER_HEADERS}
)
target_link_libraries(wgc_encode_test PRIVATE
    ${WIN_LIBS}
    ${WINRT_LIBS}
    spdlog::spdlog
)
target_compile_options(wgc_encode_test PRIVATE
    /W4
    /WX
    /permissive-
    /await:strict
    /EHsc
)

# ---------------------------------------------------------------------------
# unit_tests — GoogleTest suite: result.h, capture_types.h, encoder types,
#              IEncoder contract, RingBuffer (all edge cases + SPSC stress),
#              AMFEncoder / DXGICapture / WGCCapture precondition checks,
#              Stage 3: WebRTC abstraction layer (no libwebrtc dependency)
# ---------------------------------------------------------------------------
add_executable(unit_tests
    tests/result_test.cpp
    tests/capture_types_test.cpp
    tests/encoder_types_test.cpp
    tests/iencoder_test.cpp
    tests/util/ring_buffer_test.cpp
    tests/amf_encoder_unit_test.cpp
    tests/dxgi_capture_unit_test.cpp
    tests/wgc_capture_unit_test.cpp
    tests/webrtc_types_test.cpp
    tests/iwebrtc_host_test.cpp
    ${CAPTURE_SOURCES}
    src/capture/wgc_capture.cpp
    ${ENCODER_SOURCES}
    ${CAPTURE_HEADERS}
    ${ENCODER_HEADERS}
    ${WEBRTC_ABSTRACT_HEADERS}
)
target_link_libraries(unit_tests PRIVATE
    GTest::gtest_main
    GTest::gmock
    ${WIN_LIBS}
    ${WINRT_LIBS}
    spdlog::spdlog
)
target_compile_options(unit_tests PRIVATE
    /W4
    /WX
    /permissive-
    /EHsc
    /await:strict
)

add_test(NAME GameStreamUnitTests COMMAND unit_tests)

# ---------------------------------------------------------------------------
# WebRTC integration (optional — set WEBRTC_ROOT and WEBRTC_BUILD_DIR to enable)
#
# Usage:
#   cmake -DWEBRTC_ROOT=E:/dev/src -DWEBRTC_BUILD_DIR=E:/dev/src/out/Release ..
# ---------------------------------------------------------------------------
set(WEBRTC_ROOT     "" CACHE PATH "Path to WebRTC source tree (e.g. E:/dev/src)")
set(WEBRTC_BUILD_DIR "" CACHE PATH "Path to WebRTC build output (e.g. E:/dev/src/out/Release)")

if(WEBRTC_ROOT AND WEBRTC_BUILD_DIR)
    if(NOT EXISTS "${WEBRTC_ROOT}/api/peer_connection_interface.h")
        message(FATAL_ERROR "WEBRTC_ROOT is not a valid WebRTC source tree: ${WEBRTC_ROOT}")
    endif()
    if(NOT EXISTS "${WEBRTC_BUILD_DIR}/obj/webrtc.lib")
        message(FATAL_ERROR "webrtc.lib not found at ${WEBRTC_BUILD_DIR}/obj/webrtc.lib")
    endif()

    # WebRTC sources for GameStream
    set(WEBRTC_GS_SOURCES
        src/webrtc/webrtc_host.cpp
        src/webrtc/amf_webrtc_encoder.cpp
        src/webrtc/capture_video_source.cpp
    )
    set(WEBRTC_GS_HEADERS
        ${WEBRTC_ABSTRACT_HEADERS}
        src/webrtc/amf_webrtc_encoder.h
        src/webrtc/capture_video_source.h
    )

    # Windows system libs required by libwebrtc on Windows
    set(WEBRTC_SYSTEM_LIBS
        secur32.lib
        winmm.lib
        msdmo.lib
        dmoguids.lib
        wmcodecdspuuid.lib
        amstrmid.lib
        strmiids.lib
        Iphlpapi.lib
        dnsapi.lib
        crypt32.lib
    )

    # ---------------------------------------------------------------------------
    # webrtc_streaming — static lib: GameStream WebRTC pipeline
    #   (webrtc_host.cpp + amf_webrtc_encoder.cpp + capture_video_source.cpp)
    # ---------------------------------------------------------------------------
    add_library(webrtc_streaming STATIC
        ${WEBRTC_GS_SOURCES}
        ${CAPTURE_SOURCES}
        src/capture/wgc_capture.cpp
        ${ENCODER_SOURCES}
        ${WEBRTC_GS_HEADERS}
        ${CAPTURE_HEADERS}
        ${ENCODER_HEADERS}
    )

    # WebRTC system headers as SYSTEM to suppress /W4 warnings from them
    target_include_directories(webrtc_streaming SYSTEM PRIVATE
        ${WEBRTC_ROOT}
        ${WEBRTC_ROOT}/third_party/abseil-cpp
        ${WEBRTC_ROOT}/third_party/libyuv/include
    )
    target_include_directories(webrtc_streaming PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src   # for internal headers (src/webrtc/...)
    )

    target_link_libraries(webrtc_streaming PUBLIC
        ${WIN_LIBS}
        ${WINRT_LIBS}
        spdlog::spdlog
        ${WEBRTC_BUILD_DIR}/obj/webrtc.lib
        ${WEBRTC_SYSTEM_LIBS}
    )

    target_compile_options(webrtc_streaming PRIVATE
        /W4
        /WX
        /permissive-
        /EHsc
        /await:strict
        /bigobj                # WebRTC headers produce large object files
        /wd4100                # unreferenced formal parameter (WebRTC internals)
        /wd4702                # unreachable code  (ABSL_ASSERT macros)
    )

    target_compile_definitions(webrtc_streaming PRIVATE
        WEBRTC_WIN
        ABSL_ALLOCATOR_NOTHROW=1
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    # ---------------------------------------------------------------------------
    # webrtc_test — Day 9 DataChannel smoke test (manual SDP copy-paste)
    # ---------------------------------------------------------------------------
    add_executable(webrtc_test
        tools/webrtc_test.cpp
    )
    target_link_libraries(webrtc_test PRIVATE
        webrtc_streaming
        spdlog::spdlog
    )
    target_include_directories(webrtc_test SYSTEM PRIVATE
        ${WEBRTC_ROOT}
        ${WEBRTC_ROOT}/third_party/abseil-cpp
    )
    target_compile_options(webrtc_test PRIVATE
        /W4 /WX /permissive- /EHsc /bigobj
    )
    target_compile_definitions(webrtc_test PRIVATE
        WEBRTC_WIN WIN32_LEAN_AND_MEAN NOMINMAX
    )

    set_target_properties(webrtc_streaming webrtc_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    message(STATUS "  WebRTC: ENABLED")
    message(STATUS "    Root:      ${WEBRTC_ROOT}")
    message(STATUS "    Build dir: ${WEBRTC_BUILD_DIR}")
else()
    message(STATUS "  WebRTC: DISABLED (set -DWEBRTC_ROOT=... -DWEBRTC_BUILD_DIR=... to enable)")
endif()

# ---------------------------------------------------------------------------
# Common settings
# ---------------------------------------------------------------------------
set_target_properties(
    capture_test fps_loop_test wgc_test
    amf_init_test amf_encode_test amf_pipeline_test
    wgc_encode_test
    unit_tests
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "GameStream Core - CMake Configuration")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Windows SDK: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "  AMF SDK: ${AMF_SDK_DIR}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
