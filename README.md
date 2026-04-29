# ZiiLab Viewer

RTSP 기반 멀티채널 영상 관제 시스템

## 기능

- 멀티채널 RTSP 스트림 동시 재생 (최대 20채널)
- 동적 그리드 레이아웃 (컬럼 수 선택: Auto/1~5)
- 채널 추가/삭제/저장 (자동 복원)
- 전체화면 탭 (더블클릭 또는 우클릭)
- 자동 재접속 (최대 30회, 5초 간격)
- 스냅샷 캡처 (PNG)
- 실시간 로그 (레벨별 색상)
- 상태 바 (Connected, Bitrate, FPS, Dropped)

## 기술 스택

- **C++ 17** + **Qt 6** (GUI 프레임워크)
- **libVLC 3.x** (영상 디코딩/렌더링)
- **CMake** (빌드 시스템)

---

## 사전 요구사항

### macOS

```bash
brew install cmake qt@6
brew install --cask vlc
```

### Windows

1. **Qt 6** - https://www.qt.io/download-qt-installer 에서 설치
   - 설치 시 "Desktop" 컴포넌트 선택
   - 설치 경로 기억 (예: `C:\Qt\6.11.0\msvc2022_64`)

2. **VLC** - https://www.videolan.org/vlc/ 에서 설치
   - 64bit 버전 설치
   - SDK 다운로드: https://get.videolan.org/vlc/last/win64/
   - `vlc-3.x.x-win64.7z` 안의 `sdk/` 폴더 필요

3. **CMake** - https://cmake.org/download/
4. **Visual Studio 2022** (또는 Build Tools) - C++ 개발 도구 포함

### Linux (Ubuntu/Debian)

```bash
sudo apt install build-essential cmake qt6-base-dev libvlc-dev vlc
```

---

## 빌드

### macOS

```bash
cd /path/to/viewer
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew
make -j$(sysctl -n hw.ncpu)
```

실행:
```bash
./viewer
```

#### macOS 앱 번들 (.app) 만들기

```bash
# 빌드 후
/opt/homebrew/bin/macdeployqt viewer.app

# VLC 라이브러리 복사
mkdir -p viewer.app/Contents/Frameworks
cp /Applications/VLC.app/Contents/MacOS/lib/libvlc.dylib viewer.app/Contents/Frameworks/
cp /Applications/VLC.app/Contents/MacOS/lib/libvlccore.dylib viewer.app/Contents/Frameworks/
cp -r /Applications/VLC.app/Contents/MacOS/plugins viewer.app/Contents/Frameworks/plugins

# install_name_tool로 경로 수정
install_name_tool -change @rpath/libvlc.5.dylib @executable_path/../Frameworks/libvlc.dylib viewer.app/Contents/MacOS/viewer
install_name_tool -change @rpath/libvlccore.9.dylib @executable_path/../Frameworks/libvlccore.dylib viewer.app/Contents/MacOS/viewer
```

#### DMG 패키징

```bash
# hdiutil로 DMG 생성
hdiutil create -volname "ZiiLab Viewer" -srcfolder viewer.app -ov -format UDZO ZiiLabViewer.dmg
```

### Windows

```powershell
# Visual Studio Developer Command Prompt에서 실행
cd C:\path\to\viewer
mkdir build && cd build

cmake .. -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.11.0\msvc2022_64 ^
  -DVLC_INCLUDE_DIR=C:\vlc-sdk\include ^
  -DVLC_LIB_DIR=C:\vlc-sdk\lib

cmake --build . --config Release
```

실행 전 DLL 복사:
```powershell
# Qt DLL 배포
C:\Qt\6.11.0\msvc2022_64\bin\windeployqt.exe Release\viewer.exe

# VLC 런타임 전체 복사 (루트 DLL + plugins + 필요 지원 디렉터리)
copy C:\vlc-sdk\*.dll Release\
copy C:\vlc-sdk\plugins.dat Release\
xcopy C:\vlc-sdk\plugins Release\plugins\ /E
if (Test-Path C:\vlc-sdk\lua) { xcopy C:\vlc-sdk\lua Release\lua\ /E /I /Y }
if (Test-Path C:\vlc-sdk\locale) { xcopy C:\vlc-sdk\locale Release\locale\ /E /I /Y }
```

### Linux

```bash
cd /path/to/viewer
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

실행:
```bash
./viewer
```

---

## 크로스 플랫폼 빌드 참고

CMakeLists.txt에서 VLC 경로를 플랫폼별로 분기하려면:

```cmake
# macOS
if(APPLE)
    set(VLC_INCLUDE_DIR "/Applications/VLC.app/Contents/MacOS/include")
    set(VLC_LIB_DIR "/Applications/VLC.app/Contents/MacOS/lib")
# Windows
elseif(WIN32)
    set(VLC_INCLUDE_DIR "C:/vlc-sdk/include")
    set(VLC_LIB_DIR "C:/vlc-sdk/lib")
# Linux
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VLC REQUIRED libvlc)
endif()
```

---

## 프로젝트 구조

```
viewer/
├── CMakeLists.txt              # 빌드 설정
├── README.md
├── src/
│   ├── main.cpp                # 앱 진입점, VLC 초기화
│   ├── MainWindow.h/cpp        # 메인 창 (사이드바, 그리드, 패널)
│   ├── VlcWidget.h/cpp         # VLC 플레이어 위젯 (재생, 재접속, 스냅샷)
│   ├── ConnectionDialog.h/cpp  # 채널 추가 다이얼로그
│   └── StatusBar.h/cpp         # 하단 상태 바
├── build/                      # 빌드 산출물 (gitignore)
└── .gitignore
```

## 설정 파일

- `~/.ziilab/channels.json` — 채널 목록 (자동 저장/로드)
- `~/.ziilab/snapshots/` — 스냅샷 저장 경로

## 운영 문서

- [VOXL2 RTSP 네트워크 접근 가이드](docs/voxl2-rtsp-network-access.md)

---

## 개발

```bash
# 소스 수정 후 (build 폴더에서)
make && ./viewer

# CMakeLists.txt 수정 시
cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew && make
```
