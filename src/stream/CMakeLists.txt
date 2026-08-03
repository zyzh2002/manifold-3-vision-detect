# Pure C++ conversion helpers: buildable on host for unit tests.
add_library(stream STATIC
    yuv_to_rgb.cpp
)

target_include_directories(stream PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
