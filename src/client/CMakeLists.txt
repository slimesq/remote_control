add_executable(RemoteControlClient
    ClientMain.cpp
    ControlStreamWorker.cpp
    FileDownloadWorker.cpp
    MainWindow.cpp
    MainWindow.ui
    RemoteClient.cpp
    RemoteScreenWidget.cpp
    RemoteScreenWindow.cpp
    RemoteScreenWindow.ui
    ScreenStreamWorker.cpp
    ${PROJECT_SOURCE_DIR}/include/client/ControlStreamWorker.h
    ${PROJECT_SOURCE_DIR}/include/client/FileDownloadWorker.h
    ${PROJECT_SOURCE_DIR}/include/client/MainWindow.h
    ${PROJECT_SOURCE_DIR}/include/client/RemoteClient.h
    ${PROJECT_SOURCE_DIR}/include/client/RemoteScreenWidget.h
    ${PROJECT_SOURCE_DIR}/include/client/RemoteScreenWindow.h
    ${PROJECT_SOURCE_DIR}/include/client/ScreenStreamWorker.h
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
