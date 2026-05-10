add_library(remote_common STATIC
    Packet.cpp
    Protocol.cpp
)

target_include_directories(remote_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(remote_common PUBLIC
    Qt6::Core
)
