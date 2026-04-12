cmake_minimum_required(VERSION 3.16)
project(viewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

# libVLC (from VLC.app)
set(VLC_INCLUDE_DIR "/Applications/VLC.app/Contents/MacOS/include")
set(VLC_LIB_DIR "/Applications/VLC.app/Contents/MacOS/lib")

add_executable(viewer
    src/main.cpp
    src/MainWindow.cpp
    src/VlcWidget.cpp
    src/ConnectionDialog.cpp
    src/StatusBar.cpp
)

target_include_directories(viewer PRIVATE ${VLC_INCLUDE_DIR})
target_link_directories(viewer PRIVATE ${VLC_LIB_DIR})
target_link_libraries(viewer PRIVATE Qt6::Widgets Qt6::Concurrent vlc)
