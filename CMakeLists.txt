cmake_minimum_required(VERSION 3.16)
project(viewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

# ============================================================
# libVLC (플랫폼별 경로)
# ============================================================
if(APPLE)
    # macOS: VLC.app에서 가져오기
    set(VLC_DIR "/Applications/VLC.app/Contents/MacOS"
        CACHE PATH "VLC root directory")
    set(_vlc_include_hints "${VLC_DIR}/include")
    set(_vlc_library_hints "${VLC_DIR}/lib")
elseif(WIN32)
    # Windows: VLC SDK 경로 (cmake -DVLC_DIR=C:/vlc-sdk 로 지정)
    if(NOT VLC_DIR)
        set(VLC_DIR "C:/vlc-sdk" CACHE PATH "VLC SDK root directory")
    endif()
    set(_vlc_include_hints
        "${VLC_DIR}/sdk/include"
        "${VLC_DIR}/include")
    set(_vlc_library_hints
        "${VLC_DIR}/sdk/lib"
        "${VLC_DIR}/lib")
endif()

find_path(VLC_INCLUDE_DIR
    NAMES vlc/vlc.h
    HINTS ${_vlc_include_hints}
    REQUIRED)

find_library(VLC_LIBRARY
    NAMES vlc libvlc
    HINTS ${_vlc_library_hints}
    REQUIRED)

message(STATUS "Using libVLC headers: ${VLC_INCLUDE_DIR}")
message(STATUS "Using libVLC library: ${VLC_LIBRARY}")

# ============================================================
# 실행 파일
# ============================================================
set(APP_ICON_MACOS "${CMAKE_SOURCE_DIR}/resources/logo.icns")
set_source_files_properties(${APP_ICON_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
set(APP_ICON_WINDOWS "${CMAKE_SOURCE_DIR}/resources/app.rc")

if(APPLE)
    add_executable(viewer MACOSX_BUNDLE
        src/main.cpp
        src/MainWindow.cpp
        src/VlcWidget.cpp
        src/ConnectionDialog.cpp
        src/StatusBar.cpp
        resources/resources.qrc
        ${APP_ICON_MACOS}
    )
elseif(WIN32)
    add_executable(viewer WIN32
        src/main.cpp
        src/MainWindow.cpp
        src/VlcWidget.cpp
        src/ConnectionDialog.cpp
        src/StatusBar.cpp
        resources/resources.qrc
        ${APP_ICON_WINDOWS}
    )
else()
    add_executable(viewer
        src/main.cpp
        src/MainWindow.cpp
        src/VlcWidget.cpp
        src/ConnectionDialog.cpp
        src/StatusBar.cpp
        resources/resources.qrc
    )
endif()

target_include_directories(viewer PRIVATE ${VLC_INCLUDE_DIR})
target_link_libraries(viewer PRIVATE Qt6::Widgets Qt6::Concurrent ${VLC_LIBRARY})

if(APPLE)
    set_target_properties(viewer PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "영상관리시스템"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.nolsoft.ziilab.viewer"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE "logo.icns"
    )
endif()
