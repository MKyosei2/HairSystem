add_library(hairgpu_core
    AssetIO.cpp
    HairMath.cpp
    SampleAsset.cpp
    RenderData.cpp
)

target_include_directories(hairgpu_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

if(HAIRGPU_WITH_ALEMBIC)
    target_compile_definitions(hairgpu_core PUBLIC HAIRGPU_WITH_ALEMBIC=1)
endif()
