add_executable(hairgpu_cli
    src/Main.cpp
)

target_link_libraries(hairgpu_cli
    PRIVATE
        hairgpu_core
)

target_include_directories(hairgpu_cli
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(hairgpu_cli PRIVATE cxx_std_17)

if(HAIRGPU_INSTALL)
    install(TARGETS hairgpu_cli
        RUNTIME DESTINATION bin
    )
endif()
