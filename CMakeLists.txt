cmake_minimum_required(VERSION 3.21)

project(manifold3_vision_detect
    LANGUAGES C CXX
    VERSION 0.1.0
    DESCRIPTION "Manifold 3 vision detection application"
)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Project-wide language standards ---
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# Development credential defaults; override with -D on the command line or in
# CMakePresets. These feed src/core/manifold3_app_info.h.
if(NOT DEFINED MANIFOLD3_APP_NAME)
    set(MANIFOLD3_APP_NAME "your_app_name" CACHE STRING "PSDK application name")
endif()
if(NOT DEFINED MANIFOLD3_APP_ID)
    set(MANIFOLD3_APP_ID "your_app_id" CACHE STRING "PSDK application ID")
endif()
if(NOT DEFINED MANIFOLD3_APP_KEY)
    set(MANIFOLD3_APP_KEY "your_app_key" CACHE STRING "PSDK application key")
endif()
if(NOT DEFINED MANIFOLD3_APP_LICENSE)
    set(MANIFOLD3_APP_LICENSE "your_app_license" CACHE STRING "PSDK application license")
endif()
if(NOT DEFINED MANIFOLD3_DEVELOPER_ACCOUNT)
    set(MANIFOLD3_DEVELOPER_ACCOUNT "your_developer_account" CACHE STRING "PSDK developer account")
endif()
if(NOT DEFINED MANIFOLD3_BAUD_RATE)
    set(MANIFOLD3_BAUD_RATE "460800" CACHE STRING "PSDK baud rate")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Target system: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Sysroot: ${CMAKE_SYSROOT}")

add_subdirectory(src)
add_subdirectory(tests)
