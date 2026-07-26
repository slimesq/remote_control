add_library(RemoteControlCommon STATIC
    Packet.cpp
    Protocol.cpp
    ${PROJECT_SOURCE_DIR}/include/common/Packet.h
    ${PROJECT_SOURCE_DIR}/include/common/Protocol.h
)

target_include_directories(RemoteControlCommon PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(RemoteControlCommon PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
)
