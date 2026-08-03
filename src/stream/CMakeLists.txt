# Pure modules: compiled and tested on host and target.
add_library(stream_core STATIC
    yuv_to_rgb.cpp
    mjpeg_framing.cpp
)

target_include_directories(stream_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# Socket + libjpeg streamer: cross build only (host has no libjpeg dev files).
if(CMAKE_CROSSCOMPILING)
    add_library(stream STATIC mjpeg_streamer.cpp)
    target_link_libraries(stream PUBLIC stream_core jpeg)
    target_include_directories(stream PUBLIC ${CMAKE_SOURCE_DIR}/src)
else()
    add_library(stream INTERFACE)
    target_link_libraries(stream INTERFACE stream_core)
endif()
