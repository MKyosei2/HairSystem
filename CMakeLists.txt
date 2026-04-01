cmake_minimum_required(VERSION 3.20)
project(HairSystemGPUBase VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HAIRGPU_BUILD_DX11_VIEWER "Build the Windows DX11 sample viewer" ON)
option(HAIRGPU_WITH_ALEMBIC "Enable Alembic SDK integration points" OFF)
option(HAIRGPU_INSTALL "Enable install/export targets" ON)

add_subdirectory(src/hairgpu_core)

if(WIN32 AND HAIRGPU_BUILD_DX11_VIEWER)
    add_subdirectory(apps/dx11_viewer)
endif()

add_subdirectory(apps/hairgpu_cli)

if(HAIRGPU_INSTALL)
    include(CMakePackageConfigHelpers)

    install(DIRECTORY include/ DESTINATION include)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/HairGpuConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/HairGpuConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/HairGpuConfig.cmake"
        INSTALL_DESTINATION lib/cmake/HairGpu
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/HairGpuConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/HairGpuConfigVersion.cmake"
        DESTINATION lib/cmake/HairGpu
    )
endif()
