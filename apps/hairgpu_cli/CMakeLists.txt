add_executable(hairgpu_cli
    src/main.cpp
)

target_link_libraries(hairgpu_cli PRIVATE hairgpu_core)
target_include_directories(hairgpu_cli PRIVATE ${CMAKE_SOURCE_DIR}/include)
