add_library(capture STATIC
    latest_frame_slot.cpp
    liveview_capture.cpp
)

target_link_libraries(capture PUBLIC
    core
)
