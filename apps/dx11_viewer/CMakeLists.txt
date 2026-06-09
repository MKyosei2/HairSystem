set(HAIRGPU_DX11_VIEWER_SHADERS
    DebugLine.hlsl
    HairActiveSet.hlsl
    HairBrickInstanced.hlsl
    HairCpuMesh.hlsl
    HairIndirectArgs.hlsl
    HairLine.hlsl
    HairParticle.hlsl
    HairRibbon.hlsl
    HairSim.hlsl
    HairStrandInstanced.hlsl
    HairTube.hlsl
    HairVoxel.hlsl
    ScalpCoverage.hlsl
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
