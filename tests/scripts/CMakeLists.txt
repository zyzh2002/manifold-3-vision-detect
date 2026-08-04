# Host tests for scripts/extend_sysroot_from_device.sh using fake ssh/scp
# fixtures; no device access needed.
add_test(NAME sysroot_extend_script
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_extend_sysroot_from_device.sh
)

# Host tests for scripts/check_inference_sysroot.sh using the fake sysroot
# fixture builder; no device access needed.
add_test(NAME sysroot_checker
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_check_inference_sysroot.sh
)

# Host tests for scripts/fetch_model.sh using a fake git fixture; no HF access
# or device needed.
add_test(NAME fetch_model_script
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_fetch_model.sh
)
