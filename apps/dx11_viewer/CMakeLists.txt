set(HAIRGPU_DX11_VIEWER_SHADERS
    DebugLine.hlsl
    HairBrickInstanced.hlsl
    HairCpuMesh.hlsl
    HairActiveSet.hlsl
    HairIndirectArgs.hlsl
    HairLine.hlsl
    HairParticle.hlsl
    HairRibbon.hlsl
    HairSim.hlsl
    HairStrandInstanced.hlsl
    HairTube.hlsl
    HairVoxel.hlsl
    ScalpMesh.hlsl
    ScalpSim.hlsl
)

add_executable(hairgpu_dx11_viewer WIN32
    src/AppMain.cpp
    src/GpuHairViewer.cpp
    src/GpuHairViewer.h
)

target_include_directories(hairgpu_dx11_viewer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(hairgpu_dx11_viewer PRIVATE
    HAIRGPU_VIEWER_SHADER_DIR=L"shaders"
)

target_link_libraries(hairgpu_dx11_viewer PRIVATE
    hairgpu_core
    d3d11
    dxgi
    d3dcompiler
)

foreach(shader ${HAIRGPU_DX11_VIEWER_SHADERS})
    add_custom_command(TARGET hairgpu_dx11_viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:hairgpu_dx11_viewer>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader} $<TARGET_FILE_DIR:hairgpu_dx11_viewer>/${shader}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader} $<TARGET_FILE_DIR:hairgpu_dx11_viewer>/shaders/${shader}
    )
endforeach()

# Policy-safe profile_results generation after a successful viewer build.
# This does NOT execute the freshly built EXE, because some Windows machines block
# local unsigned binaries via Application Control/AppLocker/WDAC. It only uses
# CMake/MSBuild file operations, so profile_results is created reliably.
get_filename_component(HAIRGPU_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(WIN32)
    add_custom_command(TARGET hairgpu_dx11_viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                -DHAIRGPU_REPO_ROOT="${HAIRGPU_REPO_ROOT}"
                -DHAIRGPU_TARGET_FILE="$<TARGET_FILE:hairgpu_dx11_viewer>"
                -DHAIRGPU_CONFIG="$<CONFIG>"
                -DHAIRGPU_PLATFORM="$<PLATFORM_ID>"
                -P "${HAIRGPU_REPO_ROOT}/tools/profiling/HairGpuCreateProfileResults.cmake"
        COMMENT "Creating policy-safe HairGPU profile_results without launching HairSystem.exe."
        VERBATIM
    )
endif()
