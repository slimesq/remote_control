qt_add_executable(remote_client_qt
    main.cpp
    MainWindow.cpp
    MainWindow.h
    RemoteClient.cpp
    RemoteClient.h
    WatchWindow.cpp
    WatchWindow.h
)

target_link_libraries(remote_client_qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    remote_common
)
