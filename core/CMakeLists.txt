cmake_minimum_required(VERSION 3.20)
project(GameStream_Core CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows SDK version (we have 10.0.26100.0)
set(CMAKE_SYSTEM_VERSION "10.0.26100.0")

# Find packages
find_package(spdlog CONFIG)
find_package(fmt CONFIG)

# AMF SDK paths
set(AMF_SDK_DIR "C:/dev/AMF" CACHE PATH "Path to AMF SDK")
if(NOT EXISTS ${AMF_SDK_DIR})
    message(FATAL_ERROR "AMF SDK not found at ${AMF_SDK_DIR}")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${AMF_SDK_DIR}/amf/public/include
)

# Windows libraries
set(WIN_LIBS
    d3d11.lib
    dxgi.lib
    windowscodecs.lib  # WIC for BMP saving
    user32.lib
    kernel32.lib
)

# Header files
set(CAPTURE_HEADERS
    include/result.h
    include/capture_types.h
    include/iframe_capture.h
    include/dxgi_capture.h
)

# Source files for DXGI capture test
set(CAPTURE_TEST_SOURCES
    src/capture/dxgi_capture.cpp
    tools/capture_test.cpp
    ${CAPTURE_HEADERS}
)

# DXGI Capture Test executable
add_executable(capture_test ${CAPTURE_TEST_SOURCES})
target_link_libraries(capture_test PRIVATE ${WIN_LIBS})

if(TARGET spdlog::spdlog)
    target_link_libraries(capture_test PRIVATE spdlog::spdlog)
endif()

if(TARGET fmt::fmt)
    target_link_libraries(capture_test PRIVATE fmt::fmt)
endif()

# Set output directory
set_target_properties(capture_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Enable warnings
if(MSVC)
    target_compile_options(capture_test PRIVATE /W4 /WX-)
endif()

message(STATUS "GameStream Core - CMake Configuration")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Windows SDK: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "  AMF SDK: ${AMF_SDK_DIR}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
