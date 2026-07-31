add_library(capture STATIC
    liveview_capture.cpp
)

target_link_libraries(capture PUBLIC
    core
)
