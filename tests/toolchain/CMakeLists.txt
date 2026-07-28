add_executable(toolchain_smoke_c toolchain_smoke_c.c)
add_executable(toolchain_smoke_cpp toolchain_smoke_cpp.cpp)

set(_target_libstdcxx "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/libstdc++.so.6")
if(NOT EXISTS "${_target_libstdcxx}")
    message(FATAL_ERROR "Target libstdc++ not found: ${_target_libstdcxx}")
endif()

execute_process(
    COMMAND "${CMAKE_READELF}" --version-info "${_target_libstdcxx}"
    OUTPUT_VARIABLE _target_libstdcxx_versions
    RESULT_VARIABLE _target_libstdcxx_readelf_result
)
if(NOT _target_libstdcxx_readelf_result EQUAL 0)
    message(FATAL_ERROR "Failed to inspect target libstdc++ symbol versions")
endif()

string(REGEX MATCHALL "GLIBCXX_[0-9]+\\.[0-9]+(\\.[0-9]+)?"
    _target_glibcxx_symbols "${_target_libstdcxx_versions}")
list(REMOVE_DUPLICATES _target_glibcxx_symbols)
set(MAX_TARGET_GLIBCXX_VERSION "0.0.0")
foreach(_symbol ${_target_glibcxx_symbols})
    string(REGEX REPLACE "^GLIBCXX_" "" _version "${_symbol}")
    if(_version VERSION_GREATER MAX_TARGET_GLIBCXX_VERSION)
        set(MAX_TARGET_GLIBCXX_VERSION "${_version}")
    endif()
endforeach()

if(MAX_TARGET_GLIBCXX_VERSION STREQUAL "0.0.0")
    message(FATAL_ERROR "No GLIBCXX symbol versions found in target libstdc++")
endif()

# Static ELF verification: registered as CTest tests so they run via `ctest`
add_test(NAME toolchain_smoke_c_elf
    COMMAND ${CMAKE_COMMAND}
        -DBINARY=$<TARGET_FILE:toolchain_smoke_c>
        -DREADELF_EXE=${CMAKE_READELF}
        -DMAX_GLIBC_VERSION=2.31
        -DMAX_GLIBCXX_VERSION=${MAX_TARGET_GLIBCXX_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_elf.cmake
)
set_tests_properties(toolchain_smoke_c_elf PROPERTIES
    LABELS "toolchain;static"
)

add_test(NAME toolchain_smoke_cpp_elf
    COMMAND ${CMAKE_COMMAND}
        -DBINARY=$<TARGET_FILE:toolchain_smoke_cpp>
        -DREADELF_EXE=${CMAKE_READELF}
        -DMAX_GLIBC_VERSION=2.31
        -DMAX_GLIBCXX_VERSION=${MAX_TARGET_GLIBCXX_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_elf.cmake
)
set_tests_properties(toolchain_smoke_cpp_elf PROPERTIES
    LABELS "toolchain;static"
)

# Convenience target that runs both verifications after build
add_custom_target(verify_toolchain_smoke
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L toolchain
    DEPENDS toolchain_smoke_c toolchain_smoke_cpp
    COMMENT "Verifying AArch64 ELF properties of toolchain smoke tests"
)
