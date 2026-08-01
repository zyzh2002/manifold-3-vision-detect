# Host unit tests for the pure-C++ inference pieces. The TensorRT engine
# wrapper (Task 4) is exercised only on the target.
add_executable(test_inference_preprocess test_preprocess.cpp)
target_link_libraries(test_inference_preprocess PRIVATE inference)

add_executable(test_inference_postprocess test_postprocess.cpp)
target_link_libraries(test_inference_postprocess PRIVATE inference)

# The TensorRT name/shape/dtype enforcement in tensorrt_engine.cpp is
# cross-compile-only; the contract header and DecodeSyntheticSeg are pure C++
# and covered here.
add_executable(test_inference_synthetic_engine_contract test_synthetic_engine_contract.cpp)
target_link_libraries(test_inference_synthetic_engine_contract PRIVATE inference)

add_test(NAME inference_preprocess COMMAND test_inference_preprocess)
add_test(NAME inference_postprocess COMMAND test_inference_postprocess)
add_test(NAME inference_synthetic_engine_contract COMMAND test_inference_synthetic_engine_contract)
