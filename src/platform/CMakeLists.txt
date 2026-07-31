add_library(platform STATIC
    osal.c
    osal_fs.c
    osal_socket.c
    hal_usb_bulk.c
)

target_include_directories(platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/psdk/psdk_lib/include
)

set_target_properties(platform PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# PSDK sample code relies on GNU extensions (sem_timedwait, st_mtim, bzero).
target_compile_definitions(platform PRIVATE
    _GNU_SOURCE
)
