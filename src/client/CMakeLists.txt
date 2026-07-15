file(GLOB_RECURSE REMOTE_CONTROL_CLIENT_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/client/*.cpp
    ${PROJECT_SOURCE_DIR}/src/client/*.ui
    ${PROJECT_SOURCE_DIR}/include/client/*.h
)

add_executable(RemoteControlClient
    ${REMOTE_CONTROL_CLIENT_SOURCES}
)

target_include_directories(RemoteControlClient PRIVATE
    ${PROJECT_SOURCE_DIR}/include/client
)

target_link_libraries(RemoteControlClient PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    RemoteControlCommon
)
