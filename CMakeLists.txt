cmake_minimum_required(VERSION 3.21)

project(RemoteControlQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_standard_project_setup()

add_subdirectory(src/common)
add_subdirectory(src/client)
add_subdirectory(src/server)
add_subdirectory(src/tests)
