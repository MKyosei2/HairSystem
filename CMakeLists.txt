cmake_minimum_required(VERSION 3.20)
project(HairSystemGPUBase VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HAIRGPU_BUILD_DX11_VIEWER "Build the Windows DX11 sample viewer" ON)

add_subdirectory(src/hairgpu_core)

if(WIN32 AND HAIRGPU_BUILD_DX11_VIEWER)
    add_subdirectory(apps/dx11_viewer)
endif()
