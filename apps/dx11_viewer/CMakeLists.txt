add_executable(hairgpu_dx11_viewer WIN32
    src/AppMain.cpp
    src/GpuHairViewer.cpp
    src/GpuHairViewer.h
)

target_include_directories(hairgpu_dx11_viewer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(hairgpu_dx11_viewer PRIVATE
    HAIRGPU_VIEWER_SHADER_DIR=L"${CMAKE_CURRENT_SOURCE_DIR}/shaders/"
)

target_link_libraries(hairgpu_dx11_viewer PRIVATE
    hairgpu_core
    d3d11
    dxgi
    d3dcompiler
)

add_custom_command(TARGET hairgpu_dx11_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/HairSim.hlsl
        $<TARGET_FILE_DIR:hairgpu_dx11_viewer>/HairSim.hlsl
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/HairRibbon.hlsl
        $<TARGET_FILE_DIR:hairgpu_dx11_viewer>/HairRibbon.hlsl
)
