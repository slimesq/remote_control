file(GLOB_RECURSE REMOTE_CONTROL_SERVER_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/server/*.cpp
    ${PROJECT_SOURCE_DIR}/src/server/*.ui
    ${PROJECT_SOURCE_DIR}/include/server/*.h
)

add_executable(RemoteControlServer
    ${REMOTE_CONTROL_SERVER_SOURCES}
)

target_include_directories(RemoteControlServer PRIVATE
    ${PROJECT_SOURCE_DIR}/include/server
)

target_link_libraries(RemoteControlServer PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    RemoteControlCommon
)
