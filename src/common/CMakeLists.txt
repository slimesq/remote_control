file(GLOB_RECURSE REMOTE_COMMON_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/common/*.cpp
    ${PROJECT_SOURCE_DIR}/include/common/*.h
)

add_library(remote_common STATIC
    ${REMOTE_COMMON_SOURCES}
)

target_include_directories(remote_common PUBLIC
    ${PROJECT_SOURCE_DIR}/include/common
)

target_link_libraries(remote_common PUBLIC
    Qt6::Core
)
