cmake_minimum_required(VERSION 3.20)
project(GameStream_Core CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows SDK version (we have 10.0.26100.0)
set(CMAKE_SYSTEM_VERSION "10.0.26100.0")

# Windows SDK paths for WinRT.
# Auto-detected from the WindowsSdkDir environment variable (set by vcvarsall.bat).
# Override by passing -DWINDOWS_SDK_PATH=<path> to CMake.
set(WINDOWS_SDK_PATH "" CACHE PATH
    "Windows Kits/10 root (auto-detected from \$ENV{WindowsSdkDir} when empty)")
if(NOT WINDOWS_SDK_PATH)
    if(DEFINED ENV{WindowsSdkDir})
        # Set by vcvarsall.bat / VS Developer Command Prompt
        file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" _detected_sdk_path)
        set(WINDOWS_SDK_PATH "${_detected_sdk_path}" CACHE PATH
            "Windows Kits/10 root" FORCE)
    elseif(EXISTS "C:/Program Files (x86)/Windows Kits/10")
        # Standard x64 host install location
        set(WINDOWS_SDK_PATH "C:/Program Files (x86)/Windows Kits/10" CACHE PATH
            "Windows Kits/10 root" FORCE)
    else()
        message(FATAL_ERROR
            "Windows SDK path not found.\n"
            "Pass -DWINDOWS_SDK_PATH=<C:/Program Files (x86)/Windows Kits/10> to CMake.")
    endif()
endif()
set(WINDOWS_SDK_VERSION "10.0.26100.0")

# ---------------------------------------------------------------------------
# CRT selection — must match libwebrtc.lib which is compiled with /MT.
# All targets in this project (including FetchContent deps spdlog + gtest)
# must use the same CRT to avoid LNK2038 RuntimeLibrary mismatch errors.
# ---------------------------------------------------------------------------
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "" FORCE)

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
# gtest_force_shared_crt OFF: keep /MT consistent with CMAKE_MSVC_RUNTIME_LIBRARY.
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)

enable_testing()

# AMF SDK paths (no default — must be provided by the developer)
set(AMF_SDK_DIR "" CACHE PATH "Path to AMF SDK (e.g. C:/dev/AMF)")
if(NOT AMF_SDK_DIR OR NOT EXISTS "${AMF_SDK_DIR}")
    message(FATAL_ERROR
        "AMF SDK not found.\n"
        "Pass -DAMF_SDK_DIR=<path> pointing to the AMF SDK root (contains amf/public/include).")
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

# Input subsystem headers (Stage 4)
set(INPUT_HEADERS
    include/input_types.h
    include/iinput_sender.h
    include/input_handler.h
)

# Input subsystem sources
set(INPUT_SOURCES
    src/input/input_handler.cpp
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
    tests/input/input_handler_test.cpp
    ${CAPTURE_SOURCES}
    src/capture/wgc_capture.cpp
    ${ENCODER_SOURCES}
    ${INPUT_SOURCES}
    ${CAPTURE_HEADERS}
    ${ENCODER_HEADERS}
    ${WEBRTC_ABSTRACT_HEADERS}
    ${INPUT_HEADERS}
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

# WebRTC paths (no defaults — must be provided by the developer)
set(WEBRTC_ROOT     ""  CACHE PATH "Path to WebRTC source (e.g. E:/dev/src)")
set(WEBRTC_BUILD_DIR "" CACHE PATH "Path to WebRTC build output (e.g. \${WEBRTC_ROOT}/out/Release)")
if(NOT WEBRTC_ROOT OR NOT EXISTS "${WEBRTC_ROOT}")
    message(FATAL_ERROR
        "WebRTC source not found.\n"
        "Pass -DWEBRTC_ROOT=<path> pointing to the depot_tools checkout of WebRTC.")
endif()
if(NOT WEBRTC_BUILD_DIR)
    set(WEBRTC_BUILD_DIR "${WEBRTC_ROOT}/out/Release")
endif()

# WebRTC defines
add_definitions(
    -DWEBRTC_WIN
    -DABSL_ALLOCATOR_NOTHROW=1
    -DWIN32_LEAN_AND_MEAN
    -DNOMINMAX
)

# WebRTC include directories
include_directories(
    ${WEBRTC_ROOT}
    ${WEBRTC_ROOT}/third_party/abseil-cpp
    ${WEBRTC_ROOT}/third_party/libyuv/include
)

# WebRTC system libraries
set(WEBRTC_SYS_LIBS
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
# webrtc_streaming (Static Library)
# ---------------------------------------------------------------------------
add_library(webrtc_streaming STATIC
    src/webrtc/webrtc_host.cpp
    src/webrtc/amf_webrtc_encoder.cpp
    src/webrtc/capture_video_source.cpp
    ${CAPTURE_SOURCES}
    src/capture/wgc_capture.cpp
    ${ENCODER_SOURCES}
    ${WEBRTC_ABSTRACT_HEADERS}
    src/webrtc/amf_webrtc_encoder.h
    src/webrtc/capture_video_source.h
)
target_link_libraries(webrtc_streaming PRIVATE
    spdlog::spdlog
    ${WIN_LIBS}
    ${WINRT_LIBS}
)
target_compile_options(webrtc_streaming PRIVATE
    /W4 /WX /permissive- /bigobj /utf-8
    /await:strict
    # Treat WebRTC, abseil-cpp and libyuv headers as external — suppress their
    # warnings so /WX can be applied to our own code without false positives.
    # NOTE: use /external:I<path> (no space) to avoid CMake deduplicating the
    # flag name and leaving bare paths as spurious "source files" on the cl.exe
    # command line (CMake VS generator de-dups flags that share the same text).
    "/external:I${WEBRTC_ROOT}"
    "/external:I${WEBRTC_ROOT}/third_party/abseil-cpp"
    "/external:I${WEBRTC_ROOT}/third_party/libyuv/include"
    /external:W0
)

# ---------------------------------------------------------------------------
# webrtc_test — Day 12 streaming loop
# ---------------------------------------------------------------------------
add_executable(webrtc_test
    tools/webrtc_test.cpp
)
target_link_libraries(webrtc_test PRIVATE
    webrtc_streaming
    spdlog::spdlog
    ${WIN_LIBS}
    ${WINRT_LIBS}
    "${WEBRTC_BUILD_DIR}/obj/webrtc.lib"
    ${WEBRTC_SYS_LIBS}
    winhttp.lib
    Dbghelp.lib
)
target_compile_options(webrtc_test PRIVATE
    /W4 /WX /permissive- /bigobj /utf-8
    /await:strict
    /EHsc
)

# ---------------------------------------------------------------------------
# input_host_test — Stage 4 isolated input test circuit (no SendInput)
#
# Receives binary input packets from web/input-lab.html over two DataChannels,
# parses them with InputHandler::parse_packet() (static, no side effects),
# and prints a live overlay with latency statistics.
# ---------------------------------------------------------------------------
add_executable(input_host_test
    ${INPUT_SOURCES}
    tools/input_host_test.cpp
    ${INPUT_HEADERS}
    ${WEBRTC_ABSTRACT_HEADERS}
)
target_link_libraries(input_host_test PRIVATE
    webrtc_streaming
    spdlog::spdlog
    ${WIN_LIBS}
    ${WINRT_LIBS}
    "${WEBRTC_BUILD_DIR}/obj/webrtc.lib"
    ${WEBRTC_SYS_LIBS}
    winhttp.lib
    Dbghelp.lib
)
target_compile_options(input_host_test PRIVATE
    /W4 /WX /permissive- /bigobj /utf-8
    /await:strict
    /EHsc
)

# ---------------------------------------------------------------------------
# Common settings
# ---------------------------------------------------------------------------
set_target_properties(
    capture_test fps_loop_test wgc_test
    amf_init_test amf_encode_test amf_pipeline_test
    wgc_encode_test webrtc_test input_host_test
    unit_tests
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# webrtc.lib was compiled with /MT (static CRT, no DLL dependency).
# Our WebRTC-linked targets must use the same CRT to avoid LNK2038 mismatch.
# The other targets (unit_tests etc.) keep /MD as set by gtest_force_shared_crt.
set_property(TARGET webrtc_streaming  PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
set_property(TARGET webrtc_test       PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
set_property(TARGET input_host_test   PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

message(STATUS "GameStream Core - CMake Configuration")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Windows SDK: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "  AMF SDK: ${AMF_SDK_DIR}")
message(STATUS "  WebRTC Root: ${WEBRTC_ROOT}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
