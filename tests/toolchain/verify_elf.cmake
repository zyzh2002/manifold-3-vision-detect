# ELF verification script for cross-compiled toolchain smoke tests.
# Usage: cmake -DBINARY=<path> -DREADELF_EXE=<path> -DMAX_GLIBC_VERSION=<version>
#              -DMAX_GLIBCXX_VERSION=<version> -P verify_elf.cmake

cmake_minimum_required(VERSION 3.21)

foreach(_required_variable BINARY READELF_EXE MAX_GLIBC_VERSION MAX_GLIBCXX_VERSION)
    if(NOT DEFINED ${_required_variable})
        message(FATAL_ERROR "${_required_variable} must be defined")
    endif()
endforeach()

if(NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "BINARY does not exist: ${BINARY}")
endif()
if(NOT EXISTS "${READELF_EXE}")
    message(FATAL_ERROR "READELF_EXE does not exist: ${READELF_EXE}")
endif()

set(VERIFY_OK 1)

find_program(FILE_EXE file REQUIRED)

function(check_symbol_version symbol_version maximum_version symbol_family)
    string(REPLACE "." ";" _version_parts "${symbol_version}")
    string(REPLACE "." ";" _maximum_parts "${maximum_version}")
    list(LENGTH _version_parts _version_length)
    list(LENGTH _maximum_parts _maximum_length)

    while(_version_length LESS 3)
        list(APPEND _version_parts 0)
        list(LENGTH _version_parts _version_length)
    endwhile()
    while(_maximum_length LESS 3)
        list(APPEND _maximum_parts 0)
        list(LENGTH _maximum_parts _maximum_length)
    endwhile()

    foreach(_index RANGE 0 2)
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

# --- Architecture check ---
execute_process(
    COMMAND ${FILE_EXE} -b "${BINARY}"
    OUTPUT_VARIABLE _file_out
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _file_rc
)
if(NOT _file_rc EQUAL 0)
    message(FATAL_ERROR "file command failed on ${BINARY}")
endif()
string(FIND "${_file_out}" "ARM aarch64" _arch_pos)
if(_arch_pos LESS 0)
    message(SEND_ERROR "FAIL: Expected AArch64 ELF64, got: ${_file_out}")
    set(VERIFY_OK 0)
endif()
if(NOT _file_out MATCHES "LSB")
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
string(REGEX MATCHALL "NEEDED[^\n]*\\[([^]]+)\\]" _needed_libs "${_dyn_out}")
set(_found_libc OFF)
set(_found_libstdcxx OFF)
set(_found_libgcc_s OFF)
foreach(_entry ${_needed_libs})
    string(REGEX MATCH "\\[([^]]+)\\]" _lib_match "${_entry}")
    set(_lib_name "${CMAKE_MATCH_1}")
    if(_lib_name STREQUAL "libc.so.6")
        set(_found_libc ON)
    elseif(_lib_name STREQUAL "libstdc++.so.6")
        set(_found_libstdcxx ON)
    elseif(_lib_name STREQUAL "libgcc_s.so.1")
        set(_found_libgcc_s ON)
    elseif(NOT _lib_name STREQUAL "")
        message(SEND_ERROR "FAIL: Unexpected dynamic dependency: ${_lib_name}")
        set(VERIFY_OK 0)
    endif()
endforeach()
if(NOT _found_libc)
    message(SEND_ERROR "FAIL: libc.so.6 must be in DT_NEEDED")
    set(VERIFY_OK 0)
endif()

# --- Symbol version checks ---
execute_process(
    COMMAND ${READELF_EXE} --version-info "${BINARY}"
    OUTPUT_VARIABLE _ver_out
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX MATCHALL "GLIBC_[0-9]+\\.[0-9]+" _glibc_symbols "${_ver_out}")
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
