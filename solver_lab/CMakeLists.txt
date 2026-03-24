if (WIN32)
    add_executable(solver_lab WIN32
        src/Main.cpp
    )

    target_include_directories(solver_lab PRIVATE
        ${CMAKE_SOURCE_DIR}/hair_core/include
    )

    target_link_libraries(solver_lab PRIVATE
        hair_core
        comctl32
    )
endif()
