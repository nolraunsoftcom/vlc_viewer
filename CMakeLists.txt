cmake_minimum_required(VERSION 3.16)
project(viewer VERSION 0.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network)

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

if(WIN32)
    set(_vlc_library_names libvlc)
else()
    set(_vlc_library_names vlc libvlc)
endif()

find_library(VLC_LIBRARY
    NAMES ${_vlc_library_names}
    HINTS ${_vlc_library_hints}
    REQUIRED)

message(STATUS "Using libVLC headers: ${VLC_INCLUDE_DIR}")
message(STATUS "Using libVLC library: ${VLC_LIBRARY}")

# ============================================================
# 실행 파일
# ============================================================
set(APP_ICON_MACOS "${CMAKE_SOURCE_DIR}/resources/logo.icns")
set_source_files_properties(${APP_ICON_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

if(WIN32)
    set(APP_ICON_WINDOWS_ICON "${CMAKE_SOURCE_DIR}/resources/logo.ico")
    set(APP_ICON_WINDOWS_RC "${CMAKE_BINARY_DIR}/generated/app.rc")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
    configure_file(
        "${CMAKE_SOURCE_DIR}/resources/app.rc.in"
        "${APP_ICON_WINDOWS_RC}"
        @ONLY)
endif()

if(APPLE)
    add_executable(viewer MACOSX_BUNDLE
        src/main.cpp
        src/MainWindow.cpp
        src/VlcWidget.cpp
        src/ConnectionDialog.cpp
        src/ChannelInfoDialog.cpp
        src/StatusBar.cpp
        src/ResourceMonitor.cpp
        src/MediaRelayManager.cpp
        resources/resources.qrc
        ${APP_ICON_MACOS}
    )
elseif(WIN32)
    add_executable(viewer WIN32
        src/main.cpp
        src/MainWindow.cpp
        src/VlcWidget.cpp
        src/ConnectionDialog.cpp
        src/ChannelInfoDialog.cpp
        src/StatusBar.cpp
        src/ResourceMonitor.cpp
        src/MediaRelayManager.cpp
        resources/resources.qrc
        ${APP_ICON_WINDOWS_RC}
    )
else()
    add_executable(viewer
        src/main.cpp
        src/MainWindow.cpp
        src/VlcWidget.cpp
        src/ConnectionDialog.cpp
        src/ChannelInfoDialog.cpp
        src/StatusBar.cpp
        src/ResourceMonitor.cpp
        src/MediaRelayManager.cpp
        resources/resources.qrc
    )
endif()

target_include_directories(viewer PRIVATE ${VLC_INCLUDE_DIR})
target_link_libraries(viewer PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network ${VLC_LIBRARY})

if(APPLE)
    set_target_properties(viewer PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "영상관리시스템"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.nolsoft.ziilab.viewer"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE "logo.icns"
    )
endif()

# ============================================================
# MediaMTX relay runtime bundling
# ============================================================
set(MEDIAMTX_EXECUTABLE "" CACHE FILEPATH "Path to MediaMTX executable to bundle")

if(NOT MEDIAMTX_EXECUTABLE)
    if(WIN32)
        set(_mediamtx_repo_candidate "${CMAKE_SOURCE_DIR}/third_party/mediamtx/mediamtx.exe")
    else()
        set(_mediamtx_repo_candidate "${CMAKE_SOURCE_DIR}/third_party/mediamtx/mediamtx")
    endif()

    if(EXISTS "${_mediamtx_repo_candidate}")
        set(MEDIAMTX_EXECUTABLE "${_mediamtx_repo_candidate}"
            CACHE FILEPATH "Path to MediaMTX executable to bundle" FORCE)
    else()
        find_program(_mediamtx_from_path NAMES mediamtx)
        if(_mediamtx_from_path)
            set(MEDIAMTX_EXECUTABLE "${_mediamtx_from_path}"
                CACHE FILEPATH "Path to MediaMTX executable to bundle" FORCE)
        endif()
    endif()
endif()

if(MEDIAMTX_EXECUTABLE)
    if(WIN32)
        set(_mediamtx_bundle_name "mediamtx.exe")
    else()
        set(_mediamtx_bundle_name "mediamtx")
    endif()

    if(APPLE)
        set(_mediamtx_bundle_dir "$<TARGET_FILE_DIR:viewer>/../Resources")
    else()
        set(_mediamtx_bundle_dir "$<TARGET_FILE_DIR:viewer>")
    endif()

    message(STATUS "Bundling MediaMTX: ${MEDIAMTX_EXECUTABLE}")
    add_custom_target(bundle_mediamtx ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_mediamtx_bundle_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MEDIAMTX_EXECUTABLE}"
            "${_mediamtx_bundle_dir}/${_mediamtx_bundle_name}"
        DEPENDS viewer
        VERBATIM)
else()
    message(STATUS "MediaMTX executable not found; relay binary will not be bundled. Set -DMEDIAMTX_EXECUTABLE=/path/to/mediamtx for product builds.")
endif()
