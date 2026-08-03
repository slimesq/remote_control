add_library(RemoteControlServerCore STATIC
    ScreenLockService.cpp
    RemoteControlServerLog.cpp
    RemoteControlServerLog.h
    RemoteControlTransport.cpp
    RemoteControlTransportFileTransfer.cpp
    RemoteControlTransportInternal.h
    RemoteControlTransportProtocol.cpp
    RemoteControlTransportRuntime.cpp
    ScreenLockWindow.cpp
    ScreenLockWindow.ui
    WindowsPlatformIntegration.cpp
    ${PROJECT_SOURCE_DIR}/include/server/ScreenLockService.h
    ${PROJECT_SOURCE_DIR}/include/server/RemoteControlTransport.h
    ${PROJECT_SOURCE_DIR}/include/server/ScreenLockWindow.h
    ${PROJECT_SOURCE_DIR}/include/server/WindowsPlatformIntegration.h
)

target_include_directories(RemoteControlServerCore PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_include_directories(RemoteControlServerCore PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(RemoteControlServerCore PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    RemoteControlCommon
    ws2_32
    mswsock
)

add_executable(RemoteControlServer
    RemoteControlServer.cpp
    ServerMain.cpp
    ServerTrayController.cpp
    ${PROJECT_SOURCE_DIR}/include/server/RemoteControlServer.h
    ${PROJECT_SOURCE_DIR}/include/server/ServerTrayController.h
)

target_include_directories(RemoteControlServer PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(RemoteControlServer PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    RemoteControlServerCore
)
