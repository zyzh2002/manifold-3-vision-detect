add_executable(test_yuv_to_rgb
    test_yuv_to_rgb.cpp
    ${CMAKE_SOURCE_DIR}/src/stream/yuv_to_rgb.cpp
)

target_include_directories(test_yuv_to_rgb PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME yuv_to_rgb COMMAND test_yuv_to_rgb)

add_executable(test_mjpeg_framing
    test_mjpeg_framing.cpp
    ${CMAKE_SOURCE_DIR}/src/stream/mjpeg_framing.cpp
)

target_include_directories(test_mjpeg_framing PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME mjpeg_framing COMMAND test_mjpeg_framing)
