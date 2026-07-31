# Pure C++ preprocessing/postprocessing: buildable on host for unit tests.
add_library(inference STATIC
    preprocess.cpp
    postprocess.cpp
)

target_include_directories(inference PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# TensorRT engine loading is a cross-only source; see Task 4.
