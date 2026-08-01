# Host unit tests for the PSDK credential filling logic.
add_executable(test_psdk_user_info test_psdk_user_info.cpp)
target_link_libraries(test_psdk_user_info PRIVATE core)

add_test(NAME psdk_user_info COMMAND test_psdk_user_info)
