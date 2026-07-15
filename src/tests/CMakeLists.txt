add_executable(RemoteControlSmokeTest
    SmokeTestMain.cpp
)

target_link_libraries(RemoteControlSmokeTest PRIVATE
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

add_test(NAME RemoteControlProtocolTests COMMAND RemoteControlProtocolTests)
