cmake_minimum_required(VERSION 3.20)
project(D3D11TestRenderer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows SDK version
set(CMAKE_SYSTEM_VERSION "10.0.26100.0")

# Executable
add_executable(test_renderer test_renderer.cpp)

# Link Windows libraries
target_link_libraries(test_renderer PRIVATE
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    user32.lib
    gdi32.lib
)

# Set output directory
set_target_properties(test_renderer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Enable warnings
if(MSVC)
    target_compile_options(test_renderer PRIVATE /W4 /WX-)
endif()

message(STATUS "D3D11 Test Renderer - CMake Configuration")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Windows SDK: ${CMAKE_SYSTEM_VERSION}")
