# Pure C++ preprocessing/postprocessing: buildable on host for unit tests.
add_library(inference STATIC
    preprocess.cpp
    postprocess.cpp
)

target_include_directories(inference PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# TensorRT engine wrapper: cross-compile only (no TensorRT on host).
# TRT link requirements live on the consuming executable, not on this static
# library: static-lib PRIVATE libs propagate to every consumer (incl. the
# cross-built host unit tests) but link directories do not, which would leave
# the tests with -lnvinfer/-lcudart and no -L paths.
if(CMAKE_CROSSCOMPILING)
    target_sources(inference PRIVATE tensorrt_engine.cpp)
    target_include_directories(inference PRIVATE
        ${CMAKE_SYSROOT}/usr/include/aarch64-linux-gnu
        ${CMAKE_SYSROOT}/usr/local/cuda/include
    )
endif()

# Target-side smoke executable for the dummy engine.
if(CMAKE_CROSSCOMPILING)
    add_executable(inference_smoke inference_smoke.cpp)
    target_include_directories(inference_smoke PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SYSROOT}/usr/include/aarch64-linux-gnu
        ${CMAKE_SYSROOT}/usr/local/cuda/include
    )
    target_link_directories(inference_smoke PRIVATE
        ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu
        ${CMAKE_SYSROOT}/usr/local/cuda/lib64
        ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/tegra
    )
    # Full TensorRT link closure (Task 1 verified): libnvinfer pulls libcudla
    # and the DLA libs; libcudla needs libcuda; cuBLAS/cuDNN complete the
    # closure. rpath-link lets the linker resolve DT_NEEDED deps (libcuda,
    # libnvdla_*) from the tegra dir without adding them to DT_NEEDED.
    target_link_options(inference_smoke PRIVATE
        -Wl,-rpath-link,${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu
        -Wl,-rpath-link,${CMAKE_SYSROOT}/usr/local/cuda/lib64
        -Wl,-rpath-link,${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/tegra
    )
    target_link_libraries(inference_smoke PRIVATE inference nvinfer cudart cublas -l:libcudnn.so.8)
endif()
