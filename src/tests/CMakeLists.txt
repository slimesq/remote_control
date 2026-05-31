file(GLOB_RECURSE REMOTE_SMOKE_TEST_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/tests/*.cpp
)

qt_add_executable(remote_smoke_test
    ${REMOTE_SMOKE_TEST_SOURCES}
)

target_link_libraries(remote_smoke_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    remote_common
)
