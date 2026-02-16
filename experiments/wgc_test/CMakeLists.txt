cmake_minimum_required(VERSION 3.20)
project(WGC_Test CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows SDK version
set(CMAKE_SYSTEM_VERSION "10.0.26100.0")

# Find Windows SDK include path
set(WINDOWS_SDK_PATH "C:/Program Files (x86)/Windows Kits/10")
set(WINDOWS_SDK_VERSION "10.0.26100.0")

# Executable
add_executable(wgc_test wgc_test.cpp)

# Enable C++/WinRT
target_compile_options(wgc_test PRIVATE
    /await:strict
    /std:c++20
    /EHsc
    /permissive-
)

# Add WinRT include paths
target_include_directories(wgc_test PRIVATE
    "${WINDOWS_SDK_PATH}/Include/${WINDOWS_SDK_VERSION}/cppwinrt"
    "${WINDOWS_SDK_PATH}/Include/${WINDOWS_SDK_VERSION}/winrt"
)

# Link Windows libraries
target_link_libraries(wgc_test PRIVATE
    d3d11.lib
    dxgi.lib
    windowsapp.lib
    onecore.lib
)

# Set output directory
set_target_properties(wgc_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Enable warnings
if(MSVC)
    target_compile_options(wgc_test PRIVATE /W4 /WX-)
endif()

message(STATUS "WGC Test - CMake Configuration")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Windows SDK: ${CMAKE_SYSTEM_VERSION}")
