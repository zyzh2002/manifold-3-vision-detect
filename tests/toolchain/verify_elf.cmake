# ELF verification script for cross-compiled toolchain smoke tests.
# Usage: cmake -DBINARY=<path> -DREADELF_EXE=<path> -DMAX_GLIBC_VERSION=<version>
#              -DMAX_GLIBCXX_VERSION=<version> -DLANG=<C|CXX> -P verify_elf.cmake

cmake_minimum_required(VERSION 3.21)

foreach(_required_variable BINARY READELF_EXE MAX_GLIBC_VERSION MAX_GLIBCXX_VERSION LANG)
    if(NOT DEFINED ${_required_variable})
        message(FATAL_ERROR "${_required_variable} must be defined")
    endif()
endforeach()

# The expected dynamic dependencies are intrinsic to the smoke-test language: a
# C binary links only libc, while a C++ binary also pulls in libstdc++ and
# libgcc_s. Asserting the exact set catches an accidental static linkage of
# libstdc++/libgcc, which would drop the DT_NEEDED entry without any other
# signal. LANG is a scalar so there is no CMake ;-list argument to marshal.
#
# libm.so.6 is expected in the CXX set because the Phase 5 sysroot now ships the
# device-derived libstdc++.so.6, whose own DT_NEEDED includes libm.so.6; the
# linker propagates that inherited dependency into the binary. libm.so.6 is a
# baseline-satisfied glibc library, so this does not weaken the exact-set
# semantics: the static-libstdc++/libgcc regression is still caught because the
# libstdc++/libgcc_s entries would disappear.
if(LANG STREQUAL "C")
    set(EXPECTED_DEPS "libc.so.6")
elseif(LANG STREQUAL "CXX")
    set(EXPECTED_DEPS "libstdc++.so.6;libm.so.6;libgcc_s.so.1;libc.so.6")
else()
    message(FATAL_ERROR "LANG must be \"C\" or \"CXX\", got: \"${LANG}\"")
endif()

if(NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "BINARY does not exist: ${BINARY}")
endif()
if(NOT EXISTS "${READELF_EXE}")
    message(FATAL_ERROR "READELF_EXE does not exist: ${READELF_EXE}")
endif()

set(VERIFY_OK 1)

# Compare a required symbol version against the target baseline. Versions are
# compared component-wise; the shorter side is padded with zeros so a 2-part
# baseline such as "2.31" still compares correctly against 3-part versions.
function(check_symbol_version symbol_version maximum_version symbol_family)
    string(REPLACE "." ";" _version_parts "${symbol_version}")
    string(REPLACE "." ";" _maximum_parts "${maximum_version}")
    list(LENGTH _version_parts _version_length)
    list(LENGTH _maximum_parts _maximum_length)

    while(_version_length LESS _maximum_length)
        list(APPEND _version_parts 0)
        list(LENGTH _version_parts _version_length)
    endwhile()
    while(_maximum_length LESS _version_length)
        list(APPEND _maximum_parts 0)
        list(LENGTH _maximum_parts _maximum_length)
    endwhile()

    math(EXPR _last_index "${_version_length} - 1")
    foreach(_index RANGE 0 ${_last_index})
        list(GET _version_parts ${_index} _version_component)
        list(GET _maximum_parts ${_index} _maximum_component)
        if(_version_component GREATER _maximum_component)
            message(SEND_ERROR
                "FAIL: ${symbol_family}_${symbol_version} exceeds target baseline ${maximum_version}")
            set(VERIFY_OK 0 PARENT_SCOPE)
            return()
        elseif(_version_component LESS _maximum_component)
            return()
        endif()
    endforeach()
endfunction()

# --- ELF header: class, machine, endianness ---
# Uses the toolchain readelf so the script depends only on READELF_EXE and not
# on a host `file` installation.
execute_process(
    COMMAND ${READELF_EXE} -h "${BINARY}"
    OUTPUT_VARIABLE _ehdr_out
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _ehdr_rc
)
if(NOT _ehdr_rc EQUAL 0)
    message(FATAL_ERROR "readelf -h failed on ${BINARY}")
endif()
if(NOT _ehdr_out MATCHES "Class:[^\n]*ELF64")
    message(SEND_ERROR "FAIL: Expected ELF class ELF64")
    set(VERIFY_OK 0)
endif()
if(NOT _ehdr_out MATCHES "Machine:[^\n]*AArch64")
    message(SEND_ERROR "FAIL: Expected AArch64 machine")
    set(VERIFY_OK 0)
endif()
if(NOT _ehdr_out MATCHES "Data:[^\n]*little endian")
    message(SEND_ERROR "FAIL: Expected little-endian ELF")
    set(VERIFY_OK 0)
endif()

# --- Dynamic interpreter check ---
execute_process(
    COMMAND ${READELF_EXE} -l "${BINARY}"
    OUTPUT_VARIABLE _interp_out
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _interp_out MATCHES "/lib/ld-linux-aarch64\\.so\\.1")
    message(SEND_ERROR "FAIL: Expected interpreter /lib/ld-linux-aarch64.so.1")
    set(VERIFY_OK 0)
endif()

# --- No RPATH / RUNPATH ---
execute_process(
    COMMAND ${READELF_EXE} -d "${BINARY}"
    OUTPUT_VARIABLE _dyn_out
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(FIND "${_dyn_out}" "RPATH" _rpath_pos)
string(FIND "${_dyn_out}" "RUNPATH" _runpath_pos)
if(NOT _rpath_pos EQUAL -1)
    message(SEND_ERROR "FAIL: Binary must not contain RPATH")
    set(VERIFY_OK 0)
endif()
if(NOT _runpath_pos EQUAL -1)
    message(SEND_ERROR "FAIL: Binary must not contain RUNPATH")
    set(VERIFY_OK 0)
endif()

# --- No host x86_64 paths ---
string(FIND "${_dyn_out}" "/usr/lib/x86_64" _x86_pos)
if(NOT _x86_pos EQUAL -1)
    message(SEND_ERROR "FAIL: Host x86_64 path detected in binary")
    set(VERIFY_OK 0)
endif()

# --- Dynamic dependencies ---
# The binary must depend on exactly the expected libraries, no more and no less.
# This catches an accidental static linkage of libstdc++/libgcc, which would
# drop the corresponding DT_NEEDED entry without any other signal.
string(REGEX MATCHALL "NEEDED[^\n]*\\[([^]]+)\\]" _needed_libs "${_dyn_out}")
set(_needed_lib_set "")
foreach(_entry ${_needed_libs})
    string(REGEX MATCH "\\[([^]]+)\\]" _lib_match "${_entry}")
    set(_lib_name "${CMAKE_MATCH_1}")
    list(APPEND _needed_lib_set "${_lib_name}")
endforeach()

foreach(_expected ${EXPECTED_DEPS})
    if(NOT "${_expected}" IN_LIST _needed_lib_set)
        message(SEND_ERROR "FAIL: Expected dynamic dependency missing: ${_expected}")
        set(VERIFY_OK 0)
    endif()
endforeach()

foreach(_needed ${_needed_lib_set})
    if(NOT "${_needed}" IN_LIST EXPECTED_DEPS)
        message(SEND_ERROR "FAIL: Unexpected dynamic dependency: ${_needed}")
        set(VERIFY_OK 0)
    endif()
endforeach()

# --- Symbol version checks ---
execute_process(
    COMMAND ${READELF_EXE} --version-info "${BINARY}"
    OUTPUT_VARIABLE _ver_out
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX MATCHALL "GLIBC_[0-9]+\\.[0-9]+(\\.[0-9]+)?" _glibc_symbols "${_ver_out}")
list(REMOVE_DUPLICATES _glibc_symbols)
foreach(_symbol ${_glibc_symbols})
    string(REGEX REPLACE "^GLIBC_" "" _version "${_symbol}")
    check_symbol_version("${_version}" "${MAX_GLIBC_VERSION}" "GLIBC")
endforeach()

string(REGEX MATCHALL "GLIBCXX_[0-9]+\\.[0-9]+(\\.[0-9]+)?" _glibcxx_symbols "${_ver_out}")
list(REMOVE_DUPLICATES _glibcxx_symbols)
foreach(_symbol ${_glibcxx_symbols})
    string(REGEX REPLACE "^GLIBCXX_" "" _version "${_symbol}")
    check_symbol_version("${_version}" "${MAX_GLIBCXX_VERSION}" "GLIBCXX")
endforeach()

# Result
if(VERIFY_OK)
    message(STATUS "ELF verification PASSED for ${BINARY}")
else()
    message(FATAL_ERROR "ELF verification FAILED for ${BINARY}")
endif()
