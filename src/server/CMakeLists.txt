add_executable(RemoteControlServer
    RemoteControlServer.cpp
    ScreenLockService.cpp
    ScreenLockWindow.cpp
    ScreenLockWindow.ui
    ServerMain.cpp
    ServerTrayController.cpp
    WindowsPlatformIntegration.cpp
    WindowsRemoteControlHostServices.cpp
    ${PROJECT_SOURCE_DIR}/include/server/RemoteControlServer.h
    ${PROJECT_SOURCE_DIR}/include/server/ScreenLockService.h
    ${PROJECT_SOURCE_DIR}/include/server/ScreenLockWindow.h
    ${PROJECT_SOURCE_DIR}/include/server/ServerTrayController.h
    ${PROJECT_SOURCE_DIR}/include/server/WindowsPlatformIntegration.h
    ${PROJECT_SOURCE_DIR}/include/server/internal/WindowsRemoteControlHostServices.h
)

target_include_directories(RemoteControlServer PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(RemoteControlServer PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    RemoteControl::ServerTransport
)
