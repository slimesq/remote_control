add_executable(RemoteControlClient
    ClientMain.cpp
    ControlConnectionWorker.cpp
    DownloadWorker.cpp
    MainWindow.cpp
    MainWindow.ui
    RemoteClient.cpp
    WatchConnectionWorker.cpp
    WatchWindow.cpp
    WatchWindow.ui
    ${PROJECT_SOURCE_DIR}/include/client/ControlConnectionWorker.h
    ${PROJECT_SOURCE_DIR}/include/client/DownloadWorker.h
    ${PROJECT_SOURCE_DIR}/include/client/MainWindow.h
    ${PROJECT_SOURCE_DIR}/include/client/RemoteClient.h
    ${PROJECT_SOURCE_DIR}/include/client/WatchConnectionWorker.h
    ${PROJECT_SOURCE_DIR}/include/client/WatchWindow.h
)

target_include_directories(RemoteControlClient PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(RemoteControlClient PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    RemoteControlCommon
)
