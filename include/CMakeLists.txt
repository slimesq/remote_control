set(REMOTE_CONTROL_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/client/ControlConnectionWorker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/client/DownloadWorker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/client/MainWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/client/RemoteClient.h
    ${CMAKE_CURRENT_SOURCE_DIR}/client/WatchConnectionWorker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/client/WatchWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/Packet.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/Protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/CommandService.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/ControlStreamThread.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/FileRequestPool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/FileRequestWorker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/LockWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/PlatformIntegration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/RemoteServer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/RemoteSession.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/ServerTrayController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/server/WatchStreamThread.h
)

add_custom_target(RemoteControlHeaders SOURCES
    ${REMOTE_CONTROL_PUBLIC_HEADERS}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${REMOTE_CONTROL_PUBLIC_HEADERS}
)
