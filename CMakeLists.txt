cmake_minimum_required(VERSION 3.16)
project(viewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

# ============================================================
# libVLC (플랫폼별 경로)
# ============================================================
if(APPLE)
    # macOS: VLC.app에서 가져오기
    set(VLC_INCLUDE_DIR "/Applications/VLC.app/Contents/MacOS/include"
        CACHE PATH "VLC include directory")
    set(VLC_LIB_DIR "/Applications/VLC.app/Contents/MacOS/lib"
        CACHE PATH "VLC library directory")
elseif(WIN32)
    # Windows: VLC SDK 경로 (cmake -DVLC_DIR=C:/vlc-sdk 로 지정)
    if(NOT VLC_DIR)
        set(VLC_DIR "C:/vlc-sdk" CACHE PATH "VLC SDK root directory")
    endif()
    set(VLC_INCLUDE_DIR "${VLC_DIR}/sdk/include"
        CACHE PATH "VLC include directory")
    set(VLC_LIB_DIR "${VLC_DIR}/sdk/lib"
        CACHE PATH "VLC library directory")
endif()

# ============================================================
# 실행 파일
# ============================================================
add_executable(viewer
    src/main.cpp
    src/MainWindow.cpp
    src/VlcWidget.cpp
    src/ConnectionDialog.cpp
    src/StatusBar.cpp
)

target_include_directories(viewer PRIVATE ${VLC_INCLUDE_DIR})
target_link_directories(viewer PRIVATE ${VLC_LIB_DIR})

if(WIN32)
    target_link_libraries(viewer PRIVATE Qt6::Widgets Qt6::Concurrent libvlc)
else()
    target_link_libraries(viewer PRIVATE Qt6::Widgets Qt6::Concurrent vlc)
endif()
