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

target_link_libraries(manifold3_vision_detect PRIVATE
    core
    platform
    ${CMAKE_SOURCE_DIR}/third_party/psdk/psdk_lib/lib/aarch64-linux-gnu-gcc/libpayloadsdk.a
    m
    dl
)
