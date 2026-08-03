add_executable(RemoteControlSmokeTests
    SmokeTestMain.cpp
)

target_link_libraries(RemoteControlSmokeTests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    RemoteControlCommon
)

add_executable(RemoteControlProtocolTests
    ProtocolTestMain.cpp
)

target_link_libraries(RemoteControlProtocolTests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    RemoteControlCommon
)

add_executable(RemoteControlTransportLifecycleTests
    TransportLifecycleTestMain.cpp
)

target_link_libraries(RemoteControlTransportLifecycleTests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    RemoteControlServerCore
)

add_executable(RemoteControlConnectionStateTests
    ConnectionStateMachineTestMain.cpp
)

target_include_directories(RemoteControlConnectionStateTests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(RemoteControlConnectionStateTests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    RemoteControlServerCore
)

add_executable(RemoteControlTransportResilienceTests
    TransportResilienceTestMain.cpp
)

target_link_libraries(RemoteControlTransportResilienceTests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    RemoteControlCommon
    RemoteControlServerCore
)

add_test(NAME RemoteControlProtocolTests COMMAND RemoteControlProtocolTests)
add_test(NAME RemoteControlTransportLifecycleTests COMMAND RemoteControlTransportLifecycleTests)
add_test(NAME RemoteControlConnectionStateTests COMMAND RemoteControlConnectionStateTests)
add_test(NAME RemoteControlTransportResilienceTests COMMAND RemoteControlTransportResilienceTests)

set_tests_properties(RemoteControlTransportLifecycleTests PROPERTIES TIMEOUT 30)
set_tests_properties(RemoteControlTransportResilienceTests PROPERTIES TIMEOUT 30)
