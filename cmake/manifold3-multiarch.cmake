# Ubuntu multiarch sysroot adaptation for Bootlin GCC.
#
# The Bootlin toolchain (Buildroot-based) is not compiled with --enable-multiarch,
# so it does not search Ubuntu/Debian multiarch subdirectories such as
# usr/lib/aarch64-linux-gnu/ or usr/include/aarch64-linux-gnu/.
#
# This module adds the missing search paths without modifying the sysroot.
#
# C++ stdlib include paths are made explicit so that compile_commands.json contains
# enough information for clangd, even though G++ already knows them internally.

set(MANIFOLD3_MULTIARCH_DIR "aarch64-linux-gnu")

set(MANIFOLD3_MULTIARCH_USR_LIB     "${CMAKE_SYSROOT}/usr/lib/${MANIFOLD3_MULTIARCH_DIR}")
set(MANIFOLD3_MULTIARCH_LIB         "${CMAKE_SYSROOT}/lib/${MANIFOLD3_MULTIARCH_DIR}")
set(MANIFOLD3_MULTIARCH_USR_INCLUDE "${CMAKE_SYSROOT}/usr/include/${MANIFOLD3_MULTIARCH_DIR}")

set(MANIFOLD3_TOOLCHAIN_CXX_INCLUDE
    "${MANIFOLD3_TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu/include/c++/9.3.0")
set(MANIFOLD3_TOOLCHAIN_CXX_ARCH_INCLUDE
    "${MANIFOLD3_TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu/include/c++/9.3.0/aarch64-buildroot-linux-gnu")

set(CMAKE_LIBRARY_ARCHITECTURE "${MANIFOLD3_MULTIARCH_DIR}"
    CACHE STRING "Multiarch library directory name")

# --- Compile flags: multiarch system includes for <bits/*.h> ---
set(CMAKE_C_FLAGS_INIT "-isystem ${MANIFOLD3_MULTIARCH_USR_INCLUDE}")

# --- C++ compile flags: system includes + toolchain C++ stdlib headers ---
set(_cxx_flags
    "-isystem ${MANIFOLD3_MULTIARCH_USR_INCLUDE}"
    " -isystem ${MANIFOLD3_TOOLCHAIN_CXX_INCLUDE}"
    " -isystem ${MANIFOLD3_TOOLCHAIN_CXX_ARCH_INCLUDE}")
string(CONCAT CMAKE_CXX_FLAGS_INIT ${_cxx_flags})

# --- Link flags: crt startup files, library search, rpath resolution ---
set(_link_flags
    "-B${MANIFOLD3_MULTIARCH_USR_LIB}"
    " -L${MANIFOLD3_MULTIARCH_USR_LIB}"
    " -L${MANIFOLD3_MULTIARCH_LIB}"
    " -Wl,--build-id=sha1"
    " -Wl,-rpath-link,${MANIFOLD3_MULTIARCH_USR_LIB}"
    " -Wl,-rpath-link,${MANIFOLD3_MULTIARCH_LIB}")
string(CONCAT _link_flags_str ${_link_flags})
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_link_flags_str}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_link_flags_str}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_link_flags_str}")

# Tegra/CUDA library paths for Phase 5 targets. Not added to global link flags.
# Usage in target that needs CUDA:
#   target_link_directories(your_target PRIVATE "${MANIFOLD3_TEGRA_LIB_DIR}")
set(MANIFOLD3_TEGRA_LIB_DIR "${CMAKE_SYSROOT}/usr/lib/${MANIFOLD3_MULTIARCH_DIR}/tegra")
