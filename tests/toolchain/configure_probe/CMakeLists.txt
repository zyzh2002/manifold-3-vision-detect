cmake_minimum_required(VERSION 3.21)

project(manifold3_configure_probe LANGUAGES C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED systemd)

if(SYSTEMD_LIBRARY_DIRS MATCHES "x86_64")
    message(FATAL_ERROR "Host pkg-config directory leaked into target configuration: ${SYSTEMD_LIBRARY_DIRS}")
endif()
