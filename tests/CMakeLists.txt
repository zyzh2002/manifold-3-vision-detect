if(CMAKE_CROSSCOMPILING)
    add_subdirectory(toolchain)
endif()

add_subdirectory(capture)
add_subdirectory(core)
add_subdirectory(inference)
add_subdirectory(stream)
add_subdirectory(scripts)
