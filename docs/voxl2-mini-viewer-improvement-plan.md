# VOXL2 Mini 기반 viewer 개선 계획

작성일: 2026-06-09

## 목적

현재 `viewer`를 ModalAI VOXL2 Mini 기반 제품으로 발전시키기 위해, 실제 장비 설정과 viewer 코드 구조를 기준으로 충돌 가능성, 비효율 지점, 개선 우선순위를 정리한다.

## 현재 장비 스냅샷

아래 값은 ADB로 실제 장비에서 확인한 상태다.

```text
hardware:       VOXL2 Mini M0104
system-image:   1.8.06-M0104-14.1a-perf
voxl-suite:     1.6.3
voxl-camera-server: 2.2.19, Enabled, Not Running
voxl-uvc-server:    Running
voxl-streamer:      0.8.0, Running
network:        169.254.4.1 (eth0)
rtsp:           rtsp://169.254.4.1:8900/live
stream:         H265/HEVC Main, 640x480, 30fps
```

현재 RTSP는 ModalAI MIPI camera server의 `hires_small_encoded` 경로가 아니라 UVC 경로다.

```text
UVC camera
  -> voxl-uvc-server
  -> /run/mpa/uvc (YUV422, 640x480, 30fps)
  -> voxl-streamer (H265 encode, 1 Mbps configured)
  -> RTSP :8900/live
  -> MediaMTX relay
  -> viewer
```

관련 설정:

```json
// /etc/modalai/voxl-uvc-server.conf
{
  "pipe_name": "uvc",
  "width": 640,
  "height": 480,
  "fps": 30
}
```

```json
// /etc/modalai/voxl-streamer.conf
{
  "input-pipe": "uvc",
  "bitrate": 1000000,
  "rotation": 0,
  "decimator": 1,
  "encoder": "h265",
  "port": 8900
}
```

H264 설정은 `/etc/modalai/voxl-streamer.conf.bak-h264-20260609`에 백업되어 있다.

## 확인된 위험 지점

### 1. VOXL RTSP에 여러 클라이언트가 직접 붙음

현재 viewer는 상황에 따라 같은 VOXL RTSP URL에 여러 번 연결한다.

```text
일반 재생:      RTSP client 1개
녹화 시작:      녹화용 RTSP client 추가
전체화면 탭:    전체화면용 RTSP client 추가
외부 ffprobe:   테스트용 RTSP client 추가
```

`voxl-streamer` 로그에서 `total clients: 5`, `total clients: 6`까지 올라간 기록이 있었다. 이 구조는 RTSP 세션 정리 지연, 503 응답, 프레임 reject, 녹화 시작 실패를 만들 수 있다.

### 2. UVC raw pipe를 VOXL에서 다시 인코딩 중

현재 `/run/mpa/uvc` pipe는 `YUV422` raw frame이다. `voxl-streamer`가 이 raw frame을 H265로 다시 인코딩한다.

장비 `top` 샘플에서 `voxl-streamer` CPU가 순간 `76.5%`까지 올라갔다. 640x480 30fps 기준으로도 높은 편이다. ModalAI 문서 기준으로 pre-encoded H264/H265 MPA pipe를 구독하는 경로가 가장 효율적이고, raw/nv12/raw8 pipe를 streamer에서 인코딩하면 gstreamer/memory copy overhead가 생긴다.

### 3. streamer 로그에 프레임 reject가 있음

최근 로그에서 아래 패턴이 있었다.

```text
ERROR: New frame rejected, status = -2
rtsp client disconnected
no more rtsp clients, closing source pipe intentionally
```

이것만으로 단정할 수는 없지만, 다중 클라이언트, 인코딩 부하, source pipe 재연결이 겹칠 때 stream 안정성이 흔들릴 가능성이 있다.

### 4. 제품 네트워크로 링크 로컬 IP는 부적합

현재 주소는 `169.254.4.1 (eth0)`이다. 이는 직결 링크에서만 유효한 링크 로컬 주소다. 제품 운영에서는 SoftAP, Station mode, USB Ethernet static, 전용 무선 링크 중 하나를 제품 네트워크 프로파일로 확정해야 한다.

## 현재 viewer에서 이미 맞는 방향

- 재생/녹화 모두 TCP 강제 없이 UDP 기본 RTSP transport를 사용한다.
- `network-caching=1000`, `live-caching=1000`으로 안정성을 우선한다.
- 녹화 중지 시 `Stopping` 상태로 cleanup race를 줄였다.
- 이전 녹화 player에서 늦게 온 event가 새 녹화를 종료하지 않도록 current event source를 검증한다.
- MKV 저장을 사용해서 중간 중단/비정상 종료에 MP4보다 안전하다.

## 개선 우선순위

### Phase 1. viewer 내부 안정화

목표: 현재 구조를 유지하되, 현장 테스트에서 예측 가능한 동작을 만든다.

- 녹화 중 채널 삭제 시 저장 완료 로그/목록 갱신 처리 보강
- 녹화 실패 로그에 VOXL 응답, 파일 크기, duration, stop reason을 더 명확히 기록
- 전체화면/녹화/삭제/재접속 edge case 테스트 케이스 문서화
- RTSP 연결 실패 시 `503`, timeout, no data written, file not created를 구분 표시

### Phase 2. MediaMTX relay 도입

제품형 권장 구조:

```text
VOXL2 Mini RTSP :8900/live
  -> MediaMTX relay (VOXL RTSP client 1개)
      -> viewer preview
      -> recording
      -> fullscreen
      -> future remote clients
```

결정:

- 제품 1차는 MediaMTX를 사용한다.
- viewer는 `rtsp://127.0.0.1:8554/voxl1` 같은 relay URL을 재생/녹화 대상으로 사용한다.
- 전체화면과 녹화가 별도 RTSP client를 만들더라도 MediaMTX에만 붙고, VOXL에는 추가 client가 붙지 않는다.
- GStreamer는 초저지연 pipeline이나 특수 녹화 분기가 필요할 때 2차 후보로 남긴다.

상세 전환 절차는 [MediaMTX relay 전환 가이드](mediamtx-relay-migration-guide.md)에 둔다.

### Phase 3. viewer relay 설정 내장

목표: MediaMTX를 수동 실행/수동 URL 변경이 아니라 viewer 제품 기능으로 관리한다.

- `channels.json`에 원본 URL과 재생 URL을 분리한다.
- `MediaRelayManager`를 추가해 MediaMTX process, config, port, log, crash restart를 관리한다.
- 앱에서 source 상태와 relay 상태를 분리 표시한다.
- MediaMTX control API를 이용해 path/read session 상태를 viewer 로그에 반영한다.

보류 후보:

- libVLC `duplicate` sout으로 녹화 연결을 제거하는 방식은 relay가 없는 환경의 전술적 대안이다.
- 제품 기본 구조에서는 MediaMTX가 fan-out을 맡기 때문에 우선순위를 낮춘다.

### Phase 4. VOXL source pipeline 최적화

현재 제품이 UVC camera 기반이면 아래를 검토한다.

- UVC source가 H264/MJPEG 직접 출력 가능한지 확인
- `voxl-uvc-server`가 raw YUV422만 publish하는 현재 구조의 CPU 비용 측정
- `voxl-streamer` bitrate, encoder, decimator를 제품 프로파일로 고정
- 640x480 30fps 1Mbps가 화질/지연/CPU 요구에 맞는지 장시간 테스트

ModalAI MIPI camera를 사용할 수 있다면 아래 경로가 더 적합하다.

```text
voxl-camera-server
  -> hires_small_encoded (H264/H265 pre-encoded)
  -> voxl-streamer
  -> RTSP
```

이 경우 해상도, FPS, codec, GOP, bitrate는 `/etc/modalai/voxl-camera-server.conf`에서 관리한다.

## 제품 기본값 후보

현재 안정성 우선 기준:

```text
viewer network-caching: 1000ms
viewer live-caching:    1000ms
transport:              UDP default
recording container:    MKV
VOXL stream:            H265/HEVC 640x480 30fps, 1Mbps
relay:                  MediaMTX same-host prototype
play URL:               rtsp://127.0.0.1:8554/voxl1
```

저지연 우선 프로파일은 별도 실험으로 분리한다.

```text
viewer cache:       300-500ms
VOXL fps:           30 or 60
GOP/P-frame count:  낮을수록 손실 후 회복 빠름
bitrate:            링크 품질에 따라 조정
```

## 검증 체크리스트

- 앱 실행 후 VOXL `voxl-streamer` client count가 1로 유지되는가
- 녹화 시작/중지 50회 반복 시 0바이트 파일이 생기지 않는가
- 녹화 중 채널 삭제 시 파일이 저장되고 리소스가 정리되는가
- 전체화면 진입/종료 시 VOXL RTSP client count가 증가하지 않는가
- 네트워크 일시 단절 후 재연결 시 지연이 누적되지 않는가
- 장시간 녹화 중 파일 크기가 계속 증가하는가
- VOXL CPU, 온도, streamer 로그에 이상 패턴이 없는가

## 운영 확인 명령

```bash
adb shell voxl-version
adb shell voxl-my-ip
adb shell voxl-inspect-services
adb shell cat /etc/modalai/voxl-uvc-server.conf
adb shell cat /etc/modalai/voxl-streamer.conf
adb shell cat /etc/modalai/voxl-camera-server.conf
adb shell cat /run/mpa/uvc/info
adb shell journalctl -u voxl-streamer -n 80 --no-pager -l
adb shell top -b -n 1 | head -40
```

RTSP metadata 확인:

```bash
ffprobe -v error \
  -timeout 5000000 \
  -show_entries stream=codec_name,profile,width,height,r_frame_rate,avg_frame_rate,bit_rate \
  -of default=noprint_wrappers=1 \
  rtsp://169.254.4.1:8900/live
```

## 참고 링크

- ModalAI RTSP Video Stream (`voxl-streamer`): https://docs.modalai.com/voxl-streamer/
- ModalAI Camera Server: https://docs.modalai.com/voxl-camera-server/
- ModalAI Low Latency Video Streaming: https://docs.modalai.com/camera-video/low-latency-video-streaming/
- ModalAI Record Video: https://docs.modalai.com/voxl-record-video/
- ModalAI VOXL2 WiFi Setup: https://docs.modalai.com/voxl-2-wifi-setup/
- MediaMTX relay 전환 가이드: mediamtx-relay-migration-guide.md
