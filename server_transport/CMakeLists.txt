add_library(RemoteControlServerTransport STATIC)
add_library(RemoteControl::ServerTransport ALIAS RemoteControlServerTransport)

# This target contains no Qt meta-object, resource, or UI declarations.
set_target_properties(RemoteControlServerTransport PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
)

target_sources(RemoteControlServerTransport
    PRIVATE
        src/RemoteControlTransportLog.cpp
        src/RemoteControlTransport.cpp
        src/RemoteControlTransportFileTransfer.cpp
        src/RemoteControlTransportProtocol.cpp
        src/RemoteControlTransportRuntime.cpp
        internal/RemoteControlTransportLog.h
        internal/RemoteControlTransportImpl.h
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/RemoteControlHostServices.h
            include/RemoteControlTransport.h
)

target_include_directories(RemoteControlServerTransport PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(RemoteControlServerTransport
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
        RemoteControlCommon
    PRIVATE
        ws2_32
        mswsock
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    src/RemoteControlTransportLog.cpp
    src/RemoteControlTransport.cpp
    src/RemoteControlTransportFileTransfer.cpp
    src/RemoteControlTransportProtocol.cpp
    src/RemoteControlTransportRuntime.cpp
    internal/RemoteControlTransportLog.h
    internal/RemoteControlTransportImpl.h
    include/RemoteControlHostServices.h
    include/RemoteControlTransport.h
)
