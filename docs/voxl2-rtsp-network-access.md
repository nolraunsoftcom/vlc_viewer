# VOXL2 RTSP 네트워크 접근 가이드

VOXL2에서 송출하는 RTSP 영상을 `A` 컴퓨터에서 설정하고, 같은 공유기 망에 있는 `B` 컴퓨터에서 조회하는 절차를 정리한 문서다.

## 목표

- `A` 컴퓨터: VOXL2에 USB/ADB 또는 직결 이더넷으로 연결된 메인 컴퓨터
- `B` 컴퓨터: 같은 공유기 망에 있는 다른 컴퓨터
- 목적: `B`에서 VOXL2 RTSP 영상 조회

## 결론 먼저

VOXL2에 **Wi-Fi 동글이 없으면** `B` 컴퓨터에서 직접 RTSP에 접근할 수 없다.

이유:

- 현재 확인된 주소는 `169.254.4.1 (eth0)` 이다.
- `169.254.x.x` 대역은 링크 로컬 주소라서 보통 `A <-> VOXL2` 직결 구간에서만 유효하다.
- `B`가 집 Wi-Fi에 붙어 있어도 `rtsp://169.254.4.1:8900/live` 로는 접근할 수 없다.

즉, `B`에서 보려면 VOXL2가 공유기 Wi-Fi에 직접 붙어서 `wlan0`에 `192.168.x.x` 같은 주소를 받아야 한다.

## 현재 상태에서 가능한 것

Wi-Fi 동글이 없는 현재 상태에서는 `A` 컴퓨터에서만 아래 주소로 접근한다.

```text
rtsp://169.254.4.1:8900/live
```

이 주소는 `eth0` 직결 링크 기준이다. 같은 망의 다른 PC에서 재사용할 수 없다.

## B 컴퓨터에서 직접 보기 위한 준비물

- VOXL2용 USB 확장 보드
- 지원되는 USB Wi-Fi 동글
- 공유기 Wi-Fi SSID/비밀번호

ModalAI 문서에 나온 테스트된 동글 예시는 아래와 같다.

- `Alfa AWUS036EACS`
- `Alfa AWUS036ACS`
- `Alfa AWUS036EAC`

## 전체 절차

### 1. A 컴퓨터에서 보드 연결 확인

```bash
adb devices
adb shell
```

기본 네트워크 상태 확인:

```bash
adb shell ifconfig
adb shell voxl-my-ip
```

현재처럼 아래 상태면 Wi-Fi가 아직 없는 것이다.

```text
eth0: 169.254.4.1
```

또는

```text
No WiFi interface detected
```

### 2. Wi-Fi 동글 연결 후 무선 인터페이스 인식 확인

동글 장착 후 아래 명령으로 무선 인터페이스가 생겼는지 본다.

```bash
adb shell iw dev
adb shell ifconfig
adb shell lsusb
```

정상이라면 `wlan0`가 보여야 한다.

### 3. VOXL2를 집 Wi-Fi에 Station 모드로 연결

```bash
adb shell voxl-wifi
```

메뉴 예시:

```text
1) Configure Station
2) Configure SoftAP (2.4GHz)
3) Configure SoftAP (5GHz)
4) Disable WiFi
5) Factory Reset
6) Exit
```

여기서 `1) Configure Station`을 선택하고 다음 값을 입력한다.

- SSID: 집 공유기 이름
- Password: 공유기 비밀번호

설정 후 재부팅:

```bash
adb reboot && adb wait-for-device
```

### 4. Station 연결 성공 여부 확인

```bash
adb shell ifconfig
adb shell voxl-my-ip
adb shell iw dev
```

정상 조건:

- `wlan0`가 존재함
- `wlan0`에 `192.168.0.x`, `192.168.1.x` 같은 공유기 대역 IP가 있음
- `voxl-my-ip` 출력에 `wlan0: <IP>` 형태가 보임

예:

```text
wlan0: 192.168.0.37
```

이 IP가 `B` 컴퓨터에서 접근할 주소다.

### 5. RTSP 서비스 확인

```bash
adb shell voxl-inspect-services
```

아래 서비스가 살아 있어야 한다.

- `voxl-camera-server`
- `voxl-streamer`

필요하면 직접 시작:

```bash
adb shell systemctl start voxl-camera-server
adb shell systemctl start voxl-streamer
```

기본 RTSP URL은 아래 형식이다.

```text
rtsp://<VOXL_WLAN_IP>:8900/live
```

예:

```text
rtsp://192.168.0.37:8900/live
```

### 6. B 컴퓨터에서 영상 보기

#### VLC

- `Media > Open Network Stream`
- 아래 URL 입력

```text
rtsp://192.168.0.37:8900/live
```

#### ffplay

```bash
ffplay -fflags nobuffer -flags low_delay rtsp://192.168.0.37:8900/live
```

## 빠른 판단 기준

### B에서 직접 접근 가능

- `wlan0`가 있음
- `wlan0`에 공유기 IP가 있음
- `voxl-streamer`가 실행 중임
- `B`도 같은 공유기 망에 있음

### B에서 직접 접근 불가

- Wi-Fi 동글 없음
- `wlan0` 없음
- `voxl-my-ip`가 `eth0: 169.254.x.x`만 출력
- `No WiFi interface detected` 메시지 발생

## 장애 대응

### `No WiFi interface detected`

의미:

- Wi-Fi 동글이 없거나
- 동글이 인식되지 않았거나
- 지원되지 않는 동글일 가능성이 높다.

확인:

```bash
adb shell lsusb
adb shell iw dev
```

### `169.254.4.1 (eth0)` 만 보임

의미:

- 현재는 직결 이더넷 링크만 활성화된 상태다.
- `A`에서는 접속 가능할 수 있지만 `B`에서는 직접 접근할 수 없다.

### 같은 집 Wi-Fi인데도 B에서 접속 실패

확인 항목:

- `B`가 같은 SSID 또는 같은 내부망에 붙어 있는지
- 공유기에 `Guest Wi-Fi` 또는 `AP isolation` 이 켜져 있지 않은지
- `voxl-camera-server`, `voxl-streamer` 가 실행 중인지
- 접속 URL이 `wlan0` IP 기준인지

## 운영 메모

- 현재 장비 상태에서는 Wi-Fi 동글이 없어서 `B` 컴퓨터 직접 접속은 불가하다.
- 추후 Wi-Fi 동글을 장착한 뒤, 이 문서의 `3 ~ 6` 단계만 다시 수행하면 된다.
- 동글 장착 전 테스트는 `A`에서만 `rtsp://169.254.4.1:8900/live` 로 진행한다.

## 참고 문서

- ModalAI VOXL 2 WiFi Setup  
  https://docs.modalai.com/voxl-2-wifi-setup/
- ModalAI RTSP Video Stream (`voxl-streamer`)  
  https://docs.modalai.com/voxl-streamer/
- ModalAI Low Latency Video Streaming  
  https://docs.modalai.com/camera-video/low-latency-video-streaming/
