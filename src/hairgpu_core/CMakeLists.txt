add_library(hairgpu_core
    ActiveSet.cpp
    AssetIO.cpp
    Broadphase.cpp
    Collision.cpp
    Adaptive.cpp
    Authoring.cpp
    Binding.cpp
    BoneAnimation.cpp
    Cards.cpp
    GuideCache.cpp
    HairCache.cpp
    HairMath.cpp
    LOD.cpp
    MeshIO.cpp
    Metrics.cpp
    Pipeline.cpp
    ReducedModel.cpp
    RenderData.cpp
    SampleAsset.cpp
    UsdExport.cpp
    Validation.cpp
    Wig.cpp
)

add_library(HairGpu::core ALIAS hairgpu_core)

target_include_directories(hairgpu_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(hairgpu_core PUBLIC cxx_std_17)

if(HAIRGPU_WITH_ALEMBIC)
    target_compile_definitions(hairgpu_core PUBLIC HAIRGPU_WITH_ALEMBIC=1)
endif()

set_target_properties(hairgpu_core PROPERTIES
    OUTPUT_NAME hairgpu_core
    EXPORT_NAME core
)

if(HAIRGPU_INSTALL)
    install(TARGETS hairgpu_core
        EXPORT HairGpuTargets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )

    install(EXPORT HairGpuTargets
        NAMESPACE HairGpu::
        FILE HairGpuTargets.cmake
        DESTINATION lib/cmake/HairGpu
    )
endif()
