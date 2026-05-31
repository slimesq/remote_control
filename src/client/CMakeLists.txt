file(GLOB_RECURSE REMOTE_CLIENT_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/client/*.cpp
    ${PROJECT_SOURCE_DIR}/src/client/*.ui
    ${PROJECT_SOURCE_DIR}/include/client/*.h
)

qt_add_executable(remote_client_qt
    ${REMOTE_CLIENT_SOURCES}
)

target_include_directories(remote_client_qt PRIVATE
    ${PROJECT_SOURCE_DIR}/include/client
)

target_link_libraries(remote_client_qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    remote_common
)
