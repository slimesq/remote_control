qt_add_executable(remote_smoke_test
    smoke_main.cpp
)

target_link_libraries(remote_smoke_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    remote_common
)
