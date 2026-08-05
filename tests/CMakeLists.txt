add_executable(RemoteControlSmokeTests
    SmokeTests.cpp
)

target_link_libraries(RemoteControlSmokeTests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    RemoteControlCommon
)

add_executable(RemoteControlProtocolTests
    ProtocolTests.cpp
)

add_executable(RemoteControlClientWorkerLifecycleTests
    ClientWorkerLifecycleTests.cpp
    ${PROJECT_SOURCE_DIR}/src/client/ControlStreamWorker.cpp
    ${PROJECT_SOURCE_DIR}/src/client/FileDownloadWorker.cpp
    ${PROJECT_SOURCE_DIR}/src/client/RemoteClient.cpp
    ${PROJECT_SOURCE_DIR}/src/client/ScreenStreamWorker.cpp
    ${PROJECT_SOURCE_DIR}/include/client/ControlStreamWorker.h
    ${PROJECT_SOURCE_DIR}/include/client/FileDownloadWorker.h
    ${PROJECT_SOURCE_DIR}/include/client/RemoteClient.h
    ${PROJECT_SOURCE_DIR}/include/client/ScreenStreamWorker.h
)

target_include_directories(RemoteControlClientWorkerLifecycleTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(RemoteControlClientWorkerLifecycleTests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    RemoteControlCommon
)

target_link_libraries(RemoteControlProtocolTests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    RemoteControlCommon
)

add_executable(RemoteControlTransportLifecycleTests
    FakeRemoteControlHostServices.cpp
    FakeRemoteControlHostServices.h
    TransportLifecycleTests.cpp
)

target_link_libraries(RemoteControlTransportLifecycleTests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    RemoteControl::ServerTransport
)

add_executable(RemoteControlConnectionStateTests
    ConnectionStateMachineTests.cpp
)

target_include_directories(RemoteControlConnectionStateTests PRIVATE
    ${PROJECT_SOURCE_DIR}/server_transport
)

target_link_libraries(RemoteControlConnectionStateTests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    RemoteControl::ServerTransport
)

add_executable(RemoteControlTransportResilienceTests
    FakeRemoteControlHostServices.cpp
    FakeRemoteControlHostServices.h
    TransportResilienceTests.cpp
)

target_link_libraries(RemoteControlTransportResilienceTests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    RemoteControlCommon
    RemoteControl::ServerTransport
)

add_test(NAME RemoteControlProtocolTests COMMAND RemoteControlProtocolTests)
add_test(NAME RemoteControlClientWorkerLifecycleTests COMMAND RemoteControlClientWorkerLifecycleTests)
add_test(NAME RemoteControlTransportLifecycleTests COMMAND RemoteControlTransportLifecycleTests)
add_test(NAME RemoteControlConnectionStateTests COMMAND RemoteControlConnectionStateTests)
add_test(NAME RemoteControlTransportResilienceTests COMMAND RemoteControlTransportResilienceTests)

set_tests_properties(RemoteControlTransportLifecycleTests PROPERTIES TIMEOUT 30)
set_tests_properties(RemoteControlClientWorkerLifecycleTests PROPERTIES TIMEOUT 30)
set_tests_properties(RemoteControlTransportResilienceTests PROPERTIES TIMEOUT 30)
