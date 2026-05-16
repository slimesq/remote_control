qt_add_executable(remote_server_qt
    main.cpp
    RemoteServer.cpp
    RemoteServer.h
    RemoteSession.cpp
    RemoteSession.h
    CommandService.cpp
    CommandService.h
    LockWindow.cpp
    LockWindow.h
    LockWindow.ui
    PlatformIntegration.cpp
    PlatformIntegration.h
    ServerTrayController.cpp
    ServerTrayController.h
)

target_link_libraries(remote_server_qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    remote_common
)
