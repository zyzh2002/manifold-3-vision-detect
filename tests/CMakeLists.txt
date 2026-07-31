if(CMAKE_CROSSCOMPILING)
    add_subdirectory(toolchain)
endif()

add_subdirectory(inference)
