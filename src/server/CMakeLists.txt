file(GLOB_RECURSE REMOTE_SERVER_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/server/*.cpp
    ${PROJECT_SOURCE_DIR}/src/server/*.ui
    ${PROJECT_SOURCE_DIR}/include/server/*.h
)

qt_add_executable(remote_server_qt
    ${REMOTE_SERVER_SOURCES}
)

target_include_directories(remote_server_qt PRIVATE
    ${PROJECT_SOURCE_DIR}/include/server
)

target_link_libraries(remote_server_qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    remote_common
)
