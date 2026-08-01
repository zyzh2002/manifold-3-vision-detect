if(CMAKE_CROSSCOMPILING)
    add_subdirectory(toolchain)
endif()

add_subdirectory(core)
add_subdirectory(inference)
