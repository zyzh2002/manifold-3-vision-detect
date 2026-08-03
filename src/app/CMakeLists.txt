# The application links libpayloadsdk.a, which only ships for AArch64.
# Host builds keep platform/core compilable for future unit tests but skip
# the executable target.
if(NOT CMAKE_CROSSCOMPILING)
    message(STATUS "manifold3_vision_detect: skipped (host build)")
    return()
endif()

add_executable(manifold3_vision_detect main.cpp)

# Generate app.json from the credential cache variables so user_app_id always
# matches the compiled-in MANIFOLD3_APP_ID (a DPK install requirement).
configure_file(app.json.in ${CMAKE_CURRENT_BINARY_DIR}/app.json @ONLY)

# -pthread (not -lpthread): the sysroot libpthread.so is an absolute-path
# symlink, which makes the linker fall back to static libpthread.a; -pthread
# lets the driver resolve the dynamic library correctly.
target_link_options(manifold3_vision_detect PRIVATE -pthread)

# The app references TensorRtEngine directly. The `inference` static library
# carries no link requirements (its TRT deps are only on inference_smoke), so
# the app must resolve the full TensorRT closure itself: libnvinfer pulls
# libcudla and the DLA libs; libcudla needs libcuda; cuBLAS/cuDNN complete the
# closure. rpath-link lets the linker resolve DT_NEEDED deps (libcuda,
# libnvdla_*) from the tegra dir without adding them to DT_NEEDED.
target_link_directories(manifold3_vision_detect PRIVATE
    ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu
    ${CMAKE_SYSROOT}/usr/local/cuda/lib64
    ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/tegra
)
target_link_options(manifold3_vision_detect PRIVATE
    "-Wl,-rpath-link,${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu"
    "-Wl,-rpath-link,${CMAKE_SYSROOT}/usr/local/cuda/lib64"
    "-Wl,-rpath-link,${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/tegra"
)

target_link_libraries(manifold3_vision_detect PRIVATE
    capture
    core
    platform
    inference
    ${CMAKE_SOURCE_DIR}/third_party/psdk/psdk_lib/lib/aarch64-linux-gnu-gcc/libpayloadsdk.a
    nvinfer
    cudart
    cublas
    "-l:libcudnn.so.8"
    m
    dl
)

# Capture-only demo for customer demonstration: MJPEG stream for a browser.
# No TensorRT dependency; links only stream/capture/core/platform + PSDK.
add_executable(stream_demo stream_demo.cpp)
target_link_options(stream_demo PRIVATE -pthread)
target_link_libraries(stream_demo PRIVATE
    stream
    capture
    core
    platform
    ${CMAKE_SOURCE_DIR}/third_party/psdk/psdk_lib/lib/aarch64-linux-gnu-gcc/libpayloadsdk.a
    m
    dl
)
