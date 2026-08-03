set(REMOTE_CONTROL_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/client/ControlStreamWorker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/client/FileDownloadWorker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/client/MainWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/client/RemoteClient.h
    ${CMAKE_CURRENT_SOURCE_DIR}/client/RemoteScreenWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/client/ScreenStreamWorker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/Packet.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/Protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/ScreenLockService.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/RemoteControlTransport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/ScreenLockWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/WindowsPlatformIntegration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/RemoteControlServer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/ServerTrayController.h
)

add_custom_target(RemoteControlHeaders SOURCES
    ${REMOTE_CONTROL_PUBLIC_HEADERS}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${REMOTE_CONTROL_PUBLIC_HEADERS}
)
