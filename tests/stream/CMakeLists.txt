add_executable(test_yuv_to_rgb
    test_yuv_to_rgb.cpp
    ${CMAKE_SOURCE_DIR}/src/stream/yuv_to_rgb.cpp
)

target_include_directories(test_yuv_to_rgb PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME yuv_to_rgb COMMAND test_yuv_to_rgb)
