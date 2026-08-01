# Host tests for scripts/extend_sysroot_from_device.sh using fake ssh/scp
# fixtures; no device access needed.
add_test(NAME sysroot_extend_script
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_extend_sysroot_from_device.sh
)
