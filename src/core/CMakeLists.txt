add_library(core STATIC
    psdk_lifecycle.cpp
    psdk_user_info.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/psdk/psdk_lib/include
)

target_link_libraries(core PUBLIC
    platform
)

# Development credentials. Placeholders fail at runtime with a descriptive
# error; inject real values with -D when deploying.
target_compile_definitions(core PRIVATE
    MANIFOLD3_APP_NAME="${MANIFOLD3_APP_NAME}"
    MANIFOLD3_APP_ID="${MANIFOLD3_APP_ID}"
    MANIFOLD3_APP_KEY="${MANIFOLD3_APP_KEY}"
    MANIFOLD3_APP_LICENSE="${MANIFOLD3_APP_LICENSE}"
    MANIFOLD3_DEVELOPER_ACCOUNT="${MANIFOLD3_DEVELOPER_ACCOUNT}"
    MANIFOLD3_BAUD_RATE="${MANIFOLD3_BAUD_RATE}"
)
