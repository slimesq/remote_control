add_executable(RemoteControlServer
    CommandService.cpp
    ControlStreamThread.cpp
    FileRequestPool.cpp
    FileRequestWorker.cpp
    LockWindow.cpp
    LockWindow.ui
    PlatformIntegration.cpp
    RemoteServer.cpp
    RemoteSession.cpp
    ServerMain.cpp
    ServerTrayController.cpp
    WatchStreamThread.cpp
    ${PROJECT_SOURCE_DIR}/include/server/CommandService.h
    ${PROJECT_SOURCE_DIR}/include/server/ControlStreamThread.h
    ${PROJECT_SOURCE_DIR}/include/server/FileRequestPool.h
    ${PROJECT_SOURCE_DIR}/include/server/FileRequestWorker.h
    ${PROJECT_SOURCE_DIR}/include/server/LockWindow.h
    ${PROJECT_SOURCE_DIR}/include/server/PlatformIntegration.h
    ${PROJECT_SOURCE_DIR}/include/server/RemoteServer.h
    ${PROJECT_SOURCE_DIR}/include/server/RemoteSession.h
    ${PROJECT_SOURCE_DIR}/include/server/ServerTrayController.h
    ${PROJECT_SOURCE_DIR}/include/server/WatchStreamThread.h
)

target_include_directories(RemoteControlServer PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(RemoteControlServer PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    RemoteControlCommon
)
