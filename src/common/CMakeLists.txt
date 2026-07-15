file(GLOB_RECURSE REMOTE_CONTROL_COMMON_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/common/*.cpp
    ${PROJECT_SOURCE_DIR}/include/common/*.h
)

add_library(RemoteControlCommon STATIC
    ${REMOTE_CONTROL_COMMON_SOURCES}
)

target_include_directories(RemoteControlCommon PUBLIC
    ${PROJECT_SOURCE_DIR}/include/common
)

target_link_libraries(RemoteControlCommon PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
)
