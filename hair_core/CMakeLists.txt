add_library(hair_core STATIC
    src/HairSimulation.cpp
)

target_include_directories(hair_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
