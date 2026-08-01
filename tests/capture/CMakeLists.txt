# Host unit tests for the capture module. The latest-frame slot is pure C++,
# so its sources are compiled directly into the test; liveview_capture.cpp is
# exercised only on the target (PSDK link dependency).
add_executable(test_latest_frame_slot
    test_latest_frame_slot.cpp
    ${CMAKE_SOURCE_DIR}/src/capture/latest_frame_slot.cpp
)

target_include_directories(test_latest_frame_slot PRIVATE ${CMAKE_SOURCE_DIR}/src)

find_package(Threads REQUIRED)
target_link_libraries(test_latest_frame_slot PRIVATE Threads::Threads)

add_test(NAME latest_frame_slot COMMAND test_latest_frame_slot)
