set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    MANIFOLD3_TOOLCHAIN_DIR
    MANIFOLD3_SYSROOT
    MANIFOLD3_ALLOW_UNVERIFIED_SYSROOT
)

# --- Environment variable / cache variable resolution ---
# Accept MANIFOLD3_TOOLCHAIN_DIR and MANIFOLD3_SYSROOT as CMake cache variables
# (via -D or CMakePresets), falling back to environment variables.

if(NOT DEFINED MANIFOLD3_TOOLCHAIN_DIR)
    if(DEFINED ENV{MANIFOLD3_TOOLCHAIN_DIR})
        set(MANIFOLD3_TOOLCHAIN_DIR "$ENV{MANIFOLD3_TOOLCHAIN_DIR}"
            CACHE PATH "Bootlin GCC 9.3.0 aarch64 toolchain directory")
    else()
        message(FATAL_ERROR
            "MANIFOLD3_TOOLCHAIN_DIR is not set. "
            "Set it via -D, CMakePresets, or environment variable. "
            "See docs/build-environment.md for setup instructions.")
    endif()
endif()

if(NOT DEFINED MANIFOLD3_SYSROOT)
    if(DEFINED ENV{MANIFOLD3_SYSROOT})
        set(MANIFOLD3_SYSROOT "$ENV{MANIFOLD3_SYSROOT}"
            CACHE PATH "Jetson Linux r35.5.0 target sysroot")
    else()
        message(FATAL_ERROR
            "MANIFOLD3_SYSROOT is not set. "
            "Set it via -D, CMakePresets, or environment variable. "
            "See docs/build-environment.md for setup instructions.")
    endif()
endif()

if(NOT DEFINED MANIFOLD3_ALLOW_UNVERIFIED_SYSROOT)
    set(MANIFOLD3_ALLOW_UNVERIFIED_SYSROOT OFF CACHE BOOL
        "Allow a sysroot whose Jetson Linux r35.5.0 identity cannot be verified")
endif()

if(NOT IS_DIRECTORY "${MANIFOLD3_TOOLCHAIN_DIR}")
    message(FATAL_ERROR "MANIFOLD3_TOOLCHAIN_DIR does not exist: ${MANIFOLD3_TOOLCHAIN_DIR}")
endif()
if(NOT IS_DIRECTORY "${MANIFOLD3_SYSROOT}")
    message(FATAL_ERROR "MANIFOLD3_SYSROOT does not exist: ${MANIFOLD3_SYSROOT}")
endif()

# --- Compiler identity ---
set(TOOLCHAIN_BIN_DIR "${MANIFOLD3_TOOLCHAIN_DIR}/bin")
set(TOOLCHAIN_TRIPLE "aarch64-linux")

if(NOT EXISTS "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-gcc")
    message(FATAL_ERROR
        "Compiler not found: ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-gcc. "
        "Check MANIFOLD3_TOOLCHAIN_DIR points to a valid NVIDIA Bootlin GCC 9.3.0 toolchain.")
endif()
if(NOT EXISTS "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-g++")
    message(FATAL_ERROR "C++ compiler not found: ${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-g++")
endif()

execute_process(
    COMMAND "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-gcc" -dumpversion
    OUTPUT_VARIABLE _gcc_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _gcc_version_result
)
if(NOT _gcc_version_result EQUAL 0 OR NOT _gcc_version STREQUAL "9.3.0")
    message(FATAL_ERROR
        "Expected GCC 9.3.0 but found: ${_gcc_version}. "
        "Check MANIFOLD3_TOOLCHAIN_DIR points to the NVIDIA Bootlin GCC 9.3.0 toolchain.")
endif()

execute_process(
    COMMAND "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-gcc" -dumpmachine
    OUTPUT_VARIABLE _gcc_machine
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT _gcc_machine STREQUAL "aarch64-buildroot-linux-gnu")
    message(FATAL_ERROR
        "Expected target machine 'aarch64-buildroot-linux-gnu' but got: ${_gcc_machine}")
endif()

set(CMAKE_C_COMPILER   "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-g++")
set(CMAKE_AR           "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-ar"      CACHE FILEPATH "")
set(CMAKE_STRIP        "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-strip"   CACHE FILEPATH "")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-objcopy" CACHE FILEPATH "")
set(CMAKE_OBJDUMP      "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-objdump" CACHE FILEPATH "")
set(CMAKE_RANLIB       "${TOOLCHAIN_BIN_DIR}/${TOOLCHAIN_TRIPLE}-ranlib"  CACHE FILEPATH "")

set(CMAKE_SYSROOT "${MANIFOLD3_SYSROOT}")

# --- Validate Phase 2 base sysroot key files ---
set(_sysroot_required_files
    "lib/ld-linux-aarch64.so.1"
    "usr/lib/aarch64-linux-gnu/crt1.o"
    "usr/lib/aarch64-linux-gnu/crti.o"
    "usr/lib/aarch64-linux-gnu/crtn.o"
    "usr/include/aarch64-linux-gnu/bits/libc-header-start.h"
)
foreach(_f ${_sysroot_required_files})
    if(NOT EXISTS "${MANIFOLD3_SYSROOT}/${_f}")
        message(FATAL_ERROR "Required sysroot file missing: ${MANIFOLD3_SYSROOT}/${_f}")
    endif()
endforeach()

set(_sysroot_identity_error "")
if(EXISTS "${MANIFOLD3_SYSROOT}/etc/nv_tegra_release")
    file(STRINGS "${MANIFOLD3_SYSROOT}/etc/nv_tegra_release" _tegra_release_line LIMIT_COUNT 1)
    if(NOT _tegra_release_line MATCHES "R35.*REVISION:[ \t]*5\\.0")
        set(_sysroot_identity_error
            "sysroot /etc/nv_tegra_release does not match expected r35.5.0 baseline. Got: ${_tegra_release_line}")
    endif()
else()
    set(_sysroot_identity_error
        "sysroot /etc/nv_tegra_release not found; cannot confirm r35.5.0 identity")
endif()

if(NOT _sysroot_identity_error STREQUAL "")
    if(MANIFOLD3_ALLOW_UNVERIFIED_SYSROOT)
        message(WARNING "${_sysroot_identity_error}")
    else()
        message(FATAL_ERROR
            "${_sysroot_identity_error}. Set MANIFOLD3_ALLOW_UNVERIFIED_SYSROOT=ON only for a measured, "
            "documented target sysroot.")
    endif()
endif()

# --- Multiarch + Toolchain adaptation ---
include(${CMAKE_CURRENT_LIST_DIR}/manifold3-multiarch.cmake)

# --- Root path isolation (must precede find_program) ---
set(CMAKE_FIND_ROOT_PATH "${MANIFOLD3_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# --- pkg-config isolation (host tool, target .pc files) ---
find_program(PKG_CONFIG_EXECUTABLE pkg-config NO_CMAKE_FIND_ROOT_PATH)
if(PKG_CONFIG_EXECUTABLE)
    set(_target_pkg_config_dirs
        "${MANIFOLD3_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig"
        "${MANIFOLD3_SYSROOT}/usr/lib/pkgconfig"
        "${MANIFOLD3_SYSROOT}/usr/share/pkgconfig"
    )
    list(JOIN _target_pkg_config_dirs ":" _target_pkg_config_libdir)

    set(ENV{PKG_CONFIG} "${PKG_CONFIG_EXECUTABLE}")
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${MANIFOLD3_SYSROOT}")
    set(ENV{PKG_CONFIG_LIBDIR} "${_target_pkg_config_libdir}")
    set(ENV{PKG_CONFIG_PATH} "")
    set(ENV{PKG_CONFIG_DIR} "")
endif()
