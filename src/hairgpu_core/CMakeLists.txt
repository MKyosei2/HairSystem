add_library(hairgpu_core
    HairMath.cpp
    SampleAsset.cpp
    RenderData.cpp
)

target_include_directories(hairgpu_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
