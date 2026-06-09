# MediaMTX relay 전환 가이드

작성일: 2026-06-09

## 결정

제품형 구조는 MediaMTX relay를 사용한다.

목표는 VOXL2 Mini의 `voxl-streamer`에 직접 붙는 RTSP client 수를 장비당 1개로 고정하고, preview, 녹화, 전체화면, 향후 원격 클라이언트는 MediaMTX 뒤에서 처리하는 것이다.

```text
VOXL2 Mini
  rtsp://169.254.4.1:8900/live
        |
        | upstream RTSP 1개
        v
MediaMTX
  rtsp://127.0.0.1:8554/voxl1
        |
        +--> viewer grid preview
        +--> viewer fullscreen
        +--> recording
        +--> future remote/web clients
```

## 왜 MediaMTX인가

현재 viewer는 같은 VOXL URL에 여러 RTSP client를 만든다.

```text
grid preview       -> VOXL RTSP 1개
recording          -> VOXL RTSP 1개 추가
fullscreen         -> VOXL RTSP 1개 추가
external ffprobe   -> VOXL RTSP 1개 추가
```

이 구조는 실제 `voxl-streamer` 로그의 `total clients: 5`, `total clients: 6`, `New frame rejected` 패턴과 맞물린다. 제품형에서는 VOXL을 stream source로만 두고, client fan-out은 우리 쪽 relay가 맡아야 한다.

MediaMTX는 RTSP source URL을 pull해서 path로 restream할 수 있고, path별 `rtspTransport`, `sourceOnDemand`, recording 설정을 제공한다.

## 1차 prototype

우선 viewer 코드를 바꾸지 않고 relay만 붙인다.

### 1. MediaMTX 설치

현재 개발 머신에는 `mediamtx`가 설치되어 있지 않다. 공식 release binary를 받아 실행하거나, Docker로 실행한다.

권장 prototype은 standalone binary다. Docker는 macOS에서 host network 동작이 Linux와 달라 RTSP/UDP 검증이 번거로울 수 있다.

설치 기준은 현재 번들된 MediaMTX `v1.19.0`로 잡는다. 이 문서와 `ops/mediamtx/mediamtx.yml`은 `v1.19.0`에서 동작하는 설정 키를 기준으로 하되, 불필요한 프로토콜 설정은 최소화한다.

### 제품 번들링 기준

제품형에서는 사용자 PC에 MediaMTX 설치를 요구하지 않는다. `mediamtx` 실행 파일을 viewer 패키지 안에 포함하고, viewer가 내부 `MediaRelayManager`로 실행한다.

권장 배치:

```text
macOS:   viewer.app/Contents/Resources/mediamtx
Windows: viewer.exe 옆 mediamtx.exe
Linux:   viewer 옆 mediamtx
```

CMake는 아래 순서로 MediaMTX 실행 파일을 찾고 빌드 산출물에 복사한다.

```text
1. -DMEDIAMTX_EXECUTABLE=/path/to/mediamtx
2. repo: third_party/mediamtx/mediamtx 또는 mediamtx.exe
3. PATH의 mediamtx
```

제품 빌드 예:

```bash
cmake .. \
  -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DMEDIAMTX_EXECUTABLE=/path/to/mediamtx
cmake --build .
```

`MediaRelayManager`는 런타임에서 `ZIILAB_MEDIAMTX`, PATH, 앱 번들 내부 경로 순으로 실행 파일을 찾는다. 제품 배포본은 앱 번들 내부 경로에서 찾아지는 상태가 정상이다.

### 2. 설정 파일

repo에 prototype 설정을 추가했다.

```text
ops/mediamtx/mediamtx.yml
```

핵심 설정:

```yaml
api: true
apiAddress: 127.0.0.1:9997

rtsp: true
rtspTransports: [tcp]
rtspAddress: :8554

rtmp: false
hls: false
webrtc: false
srt: false
moq: false

paths:
  voxl1:
    source: rtsp://169.254.4.1:8900/live
    sourceOnDemand: false
    rtspTransport: tcp
    record: false
```

> 변경 이력: upstream(`VOXL -> MediaMTX`)은 초기 `udp` 기본값에서 `tcp`로 전환했다(앱 v0.0.1).
> UDP에서 RTSP 세션 keepalive/RTP 흐름이 끊겨 ~20-30초 주기로 stream 이 teardown 되던
> 현상과, 손실 구간 매크로블로킹(깨짐)을 줄이기 위함이다. 저지연이 더 중요한 프로파일에서는
> `udp` 로 되돌릴 수 있다.

`sourceOnDemand: false`는 MediaMTX가 시작될 때 VOXL upstream을 바로 붙잡게 한다. 제품형에서 source 상태를 명확히 감시하기 쉽다. 배터리/무선 자원을 아껴야 하는 프로파일은 나중에 `true`를 검토한다.

열리는 포트:

- `8554/tcp`: viewer가 접속하는 RTSP server. localhost 구간이라 TCP 재전송 지연의 영향이 사실상 없고, LibVLC 렌더링 안정성이 더 좋다.
- `9997/tcp`: viewer 내부 관리용 Control API, localhost 바인딩

주의: viewer와 MediaMTX 사이는 localhost TCP로 둔다. VOXL upstream도 path의 `rtspTransport: tcp`로 둔다(앱 v0.0.1 기준). 즉 제품 기본값은 `VOXL -> MediaMTX: TCP`, `MediaMTX -> viewer: TCP(localhost)` 조합이다. UDP는 손실 구간에서 RTP가 끊겨 stream teardown/깨짐을 유발했다. 저지연 우선 프로파일에서만 `udp` 재검토.

### 3. 실행

```bash
cd /Users/ieonsang/nolsoft/ziilab/viewer
mediamtx ops/mediamtx/mediamtx.yml
```

다른 터미널에서 확인:

```bash
ffprobe -v error \
  -rtsp_transport tcp \
  -timeout 5000000 \
  -show_entries stream=codec_name,profile,width,height,r_frame_rate,avg_frame_rate \
  -of default=noprint_wrappers=1 \
  rtsp://127.0.0.1:8554/voxl1
```

MediaMTX path 상태 확인:

```bash
curl http://127.0.0.1:9997/v3/paths/list
curl http://127.0.0.1:9997/v3/rtspsessions/list
```

현재 VOXL 설정 기준 예상 결과:

```text
codec_name=hevc
profile=Main
width=640
height=480
r_frame_rate=30/1
avg_frame_rate=30/1
```

### 4. viewer 연결 URL 변경

prototype에서는 viewer 채널 URL만 바꾼다.

```text
기존: rtsp://169.254.4.1:8900/live
변경: rtsp://127.0.0.1:8554/voxl1
```

이 단계에서는 viewer가 녹화/전체화면 때 여러 RTSP client를 만들어도 모두 MediaMTX에 붙는다. VOXL에는 MediaMTX 한 client만 붙는다.

## viewer 코드 변경 계획

### Step 1. 수동 relay URL 사용

목표: MediaMTX 효과를 코드 변경 없이 검증한다.

- 채널 URL을 `rtsp://127.0.0.1:8554/voxl1`로 변경
- 녹화 시작/중지 반복
- 전체화면 진입/종료
- VOXL `voxl-streamer` client count가 1로 유지되는지 확인

이 단계에서 `viewer` 녹화는 여전히 별도 RTSP player를 만든다. 단, 그 연결 대상은 VOXL이 아니라 MediaMTX다.

### Step 2. Relay 설정을 앱 모델에 반영

채널 모델을 아래처럼 분리한다.

```text
sourceUrl: rtsp://169.254.4.1:8900/live   # VOXL 원본
relayPath: voxl1
playUrl:   rtsp://127.0.0.1:8554/voxl1    # viewer가 재생할 URL
relayEnabled: true
```

현재 코드 기준으로는 `VlcWidget`보다 `ConnectionDialog`와 `MainWindow`를 먼저 바꾸는 것이 맞다. `VlcWidget::play()`, `VlcWidget::startRecording()`, `MainWindow::openFullscreenTab()`은 이미 `viewer->url()`을 사용한다. 따라서 `VlcWidget`에 들어가는 URL이 relay URL이면 재생, 녹화, 전체화면은 자동으로 MediaMTX를 바라본다.

수정 지점:

- `ConnectionDialog.h`
  - `ConnectionInfo`에 `sourceUrl`, `relayEnabled`, `relayPath`를 추가한다.
  - 기존 `rtspUrl`은 `playUrl` 의미로 바꾸거나, 호환성을 위해 남기되 내부 계산값으로만 사용한다.
- `ConnectionDialog.cpp`
  - 입력 필드는 "원본 RTSP URL"을 받는다.
  - "MediaMTX relay 사용" 체크박스를 추가한다.
  - relay path 기본값은 `voxl1`, `voxl2`처럼 채널 번호 기반으로 제안한다.
- `MainWindow::addChannel()`
  - 사용자가 입력한 `sourceUrl`을 저장한다.
  - relay enabled면 `playUrl = rtsp://127.0.0.1:8554/<relayPath>`를 생성하고 `viewer->play(playUrl, name)`을 호출한다.
- `MainWindow::editChannel()`
  - source URL, relay 사용 여부, relay path 변경 시 `playUrl`을 다시 계산한다.
  - 이미 열린 fullscreen `VlcWidget`도 같은 `playUrl`로 재시작한다.
- `MainWindow::saveChannels()`
  - 기존 `url` 외에 `sourceUrl`, `playUrl`, `relayEnabled`, `relayPath`를 저장한다.
  - 과거 버전 호환을 위해 `url`은 계속 `playUrl`로 저장한다.
- `MainWindow::loadChannels()`
  - `sourceUrl`이 없으면 기존 `url`을 source로 간주한다.
  - `relayEnabled == true`면 저장된 `playUrl`을 신뢰하지 말고 현재 relay host/path 기준으로 다시 생성한다.
- `VlcWidget`
  - 1차 구현에서는 변경하지 않는다.
  - `m_url`은 viewer가 실제로 재생/녹화할 URL, 즉 relay URL로 유지한다.

저장 포맷 예:

```json
{
  "gridCols": 3,
  "channels": [
    {
      "name": "Camera 1",
      "sourceUrl": "rtsp://169.254.4.1:8900/live",
      "url": "rtsp://127.0.0.1:8554/voxl1",
      "relayEnabled": true,
      "relayPath": "voxl1",
      "autoReconnect": true,
      "gridIndex": 0
    }
  ]
}
```

### Step 3. MediaMTX process 관리

제품형 viewer가 같은 PC에서 MediaMTX를 함께 실행한다면 `QProcess`로 관리한다.

관리 항목:

- 앱 시작 시 MediaMTX 실행
- 포트 `8554` 충돌 검사
- config 파일 생성/갱신
- 프로세스 crash 감지 및 재시작
- 앱 종료 시 graceful stop
- 로그를 viewer 로그 탭에 연결

권장 위치:

```text
src/MediaRelayManager.h
src/MediaRelayManager.cpp
ops/mediamtx/mediamtx.yml
```

초기 구현은 단일 장비 `voxl1`만 지원하고, 이후 여러 VOXL 장비로 확장한다.

권장 책임:

- 앱 시작 시 `~/.ziilab/mediamtx.yml`을 생성한다.
- repo의 `ops/mediamtx/mediamtx.yml`은 개발 기본 템플릿으로 둔다.
- 채널 목록이 바뀌면 MediaMTX config의 `paths`를 갱신한다.
- MediaMTX가 config hot reload를 지원하더라도, 초기 구현은 명시 restart로 단순하게 간다.
- `http://127.0.0.1:9997/v3/paths/list`로 source 상태를 읽어 viewer 로그에 표시한다.
- MediaMTX process가 죽으면 1차는 로그/상태만 표시하고, 자동 재시작은 안정성 검증 후 켠다.

초기 class 형태:

```cpp
class MediaRelayManager : public QObject {
    Q_OBJECT
public:
    explicit MediaRelayManager(QObject *parent = nullptr);
    void setChannels(const QVector<RelayChannel> &channels);
    bool start();
    void stop();
    QString playUrlForPath(const QString &relayPath) const;

signals:
    void logMessage(const QString &message, LogLevel level);
    void pathStateChanged(const QString &path, const QString &state);
};
```

### Step 4. 녹화 방식 결정

초기에는 현재 viewer 녹화를 유지한다.

```text
MediaMTX -> viewer recording player -> MKV
```

장점:

- 현재 UI/토스트/파일 목록을 그대로 사용
- manual start/stop semantics 유지
- 구현량 작음

단점:

- MediaMTX에는 viewer 녹화용 RTSP client가 하나 더 붙음
- 단, VOXL에는 추가 client가 붙지 않으므로 제품 병목은 크게 줄어든다

제품 최종 후보는 MediaMTX recording으로 전환하는 것이다.

```text
VOXL -> MediaMTX -> recording segment
```

주의:

- MediaMTX 기본 recording format은 `fmp4` 또는 `mpegts`다.
- 현재 viewer는 MKV 파일 하나를 저장한다.
- UI의 "녹화 시작/중지" 의미를 MediaMTX segment recording과 맞추는 설계가 필요하다.

### Step 5. 전체화면 처리

초기에는 현재 구조를 유지한다.

```text
grid VlcWidget      -> rtsp://127.0.0.1:8554/voxl1
fullscreen VlcWidget -> rtsp://127.0.0.1:8554/voxl1
```

두 RTSP client가 생기지만 MediaMTX에만 붙는다. VOXL에는 영향이 없다.

추후 최적화 후보:

- 전체화면 진입 시 grid viewer를 일시 중지하고 같은 player를 이동
- fullscreen 전용 client 유지
- WebRTC/LL-HLS preview로 UI 렌더링 방식 변경

제품 1차에서는 fullscreen client를 유지해도 된다. 목표는 VOXL client 수 고정이다.

## 운영 배치 옵션

### Option A. viewer PC same-host relay

```text
VOXL -> viewer PC MediaMTX -> viewer
```

장점:

- 가장 빠르게 구현 가능
- 네트워크 토폴로지 단순
- viewer PC 한 대 현장 운영에 적합

단점:

- viewer PC가 꺼지면 relay도 중단
- 여러 PC에서 같은 stream을 보려면 viewer PC가 relay 서버 역할을 해야 함

초기 제품 prototype 권장안이다.

### Option B. 현장 gateway relay

```text
VOXL -> gateway MediaMTX -> 여러 viewer PC
```

장점:

- viewer PC와 stream ingest 분리
- 다중 viewer와 원격 접근에 유리
- 장애 격리 쉬움

단점:

- gateway 장비 추가
- 배포/업데이트/모니터링 복잡도 증가

제품 양산/다중 클라이언트 운영 후보안이다.

### Option C. 중앙 서버 relay

```text
VOXL -> WAN/VPN -> central MediaMTX -> clients
```

현재 단계에서는 권장하지 않는다. 현장 네트워크 품질, NAT, 보안, 지연 변수가 커진다. 원격 관제 요구가 확정된 뒤 별도 설계한다.

## 검증 체크리스트

### Relay 동작

- `rtsp://127.0.0.1:8554/voxl1`에서 `ffprobe` 성공
- codec이 VOXL 설정과 일치: 현재 `hevc`
- viewer grid 표시 정상
- viewer 전체화면 표시 정상
- viewer 녹화 파일 생성 정상

### VOXL client count

아래 명령으로 MediaMTX 전환 전/후를 비교한다.

```bash
adb shell journalctl -u voxl-streamer -n 80 --no-pager -l
```

검증 목표:

```text
viewer grid only:           VOXL clients 1
viewer grid + recording:    VOXL clients 1
viewer grid + fullscreen:   VOXL clients 1
grid + recording + fullscreen: VOXL clients 1
```

### 지연

relay 전/후 glass-to-glass 지연을 측정한다.

```text
직접 연결: VOXL -> viewer
relay 연결: VOXL -> MediaMTX -> viewer
```

제품 판단에는 평균, p95, 최대 지연을 기록한다.

### 부하

VOXL:

```bash
adb shell 'top -b -d 1 -n 30 | grep voxl-streamer'
```

viewer PC:

```bash
ps -axo pid,etime,pcpu,command | grep -E 'viewer|mediamtx'
```

### 장시간

- 1시간 이상 preview 유지
- 녹화 시작/중지 50회
- 전체화면 진입/종료 50회
- VOXL streamer 로그에 `New frame rejected`, 503, repeated disconnect가 줄어드는지 확인

## viewer 수정 우선순위

1. MediaMTX 수동 실행 + 채널 URL 수동 변경
2. `channels.json`에 source/relay/play URL 분리
3. `MediaRelayManager`로 MediaMTX process 관리
4. 녹화는 viewer recording 유지
5. MediaMTX recording 전환 여부 재평가
6. 여러 VOXL 장비 path 자동 생성

## 롤백

MediaMTX 문제가 생기면 채널 URL을 VOXL 원본으로 되돌린다.

```text
rtsp://169.254.4.1:8900/live
```

VOXL streamer H264 롤백:

```bash
adb shell cp /etc/modalai/voxl-streamer.conf.bak-h264-20260609 /etc/modalai/voxl-streamer.conf
adb shell systemctl restart voxl-streamer
```

## 참고

- MediaMTX configuration: https://mediamtx.org/docs/features/configuration
- MediaMTX RTSP-specific features: https://mediamtx.org/docs/features/rtsp-specific-features
- MediaMTX sample configuration: https://github.com/bluenviron/mediamtx/blob/main/mediamtx.yml
