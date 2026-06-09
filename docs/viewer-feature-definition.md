# viewer 기능 정의

작성일: 2026-06-09

## 목적

이 문서는 `viewer`의 제품 기능을 코드 리팩토링과 테스트 작성의 기준으로 고정하기 위한 기능 정의서다.

현재 앱은 RTSP 기반 다채널 영상 관제 프로그램이며, VOXL2 Mini에서 송출되는 RTSP 스트림을 직접 또는 MediaMTX relay를 통해 재생한다. 앞으로의 리팩토링은 아래 기능 정의를 깨지 않는 선에서 책임을 분리하고, 각 기능의 수용 기준을 테스트로 옮기는 방향으로 진행한다.

## 제품 범위

### 포함

* RTSP/rtsps 스트림 채널 관리
* MediaMTX relay 기반 재생 URL 분리
* 다채널 그리드 재생
* 채널별 연결 상태, 통계, 로그 표시
* 자동 재접속 및 실패 수렴 정책
* 스냅샷 저장
* 영상 녹화 저장
* 전체화면 탭 재생
* 채널 설정 자동 저장/복원
* 로컬 파일 목록 탐색
* 시스템 리소스 상태 표시

### 제외 또는 보류

* 사용자 계정/권한
* 원격 서버 동기화
* 웹 클라이언트 제공
* 장비 설정 자동 변경
* 영상 분석/AI 감지
* 장기 보관 정책과 파일 자동 삭제
* MediaMTX Control API 기반 상세 source/session 상태 표시
* 앱 내 실시간 네트워크 진단 UI

## 핵심 용어

* 채널: 사용자가 등록한 하나의 카메라/스트림 단위.
* 원본 URL: VOXL2 Mini 또는 카메라가 제공하는 실제 RTSP URL. 예: `rtsp://169.254.4.1:8900/live`.
* Relay path: MediaMTX 내부 path 이름. 예: `voxl1`, `radio20`.
* 재생 URL: viewer가 실제로 libVLC에 전달하는 URL. relay 사용 시 `rtsp://127.0.0.1:8554/<relayPath>`.
* 직접 재생: 원본 URL을 그대로 libVLC에 전달하는 방식.
* Relay 재생: MediaMTX가 원본 URL을 pull하고 viewer는 localhost relay URL을 재생하는 방식.
* 실제 연결: RTSP 세션이 열린 것뿐 아니라, 디코딩 또는 표시 프레임이 실제로 들어온 상태.
* 가짜 연결: MediaMTX가 트랙 정보만 응답해 libVLC `Playing` 이벤트는 발생했지만 실제 영상 프레임이 들어오지 않는 상태.

## 사용자 역할

### 운영자

* 현장에서 채널을 추가, 삭제, 수정한다.
* 영상 상태를 보고 장애 채널을 식별한다.
* 필요 시 전체화면, 스냅샷, 녹화를 수행한다.
* 재접속 실패 상태에서 수동으로 재연결하거나 채널 설정을 수정한다.

### 개발자/운영 지원자

* 로그와 통계를 통해 스트림, relay, 녹화 문제를 분석한다.
* 설정 파일과 MediaMTX 실행 상태를 확인한다.
* 기능별 테스트 케이스를 유지한다.

## 화면 구성

### 좌측 채널 패널

목적:

* 등록된 채널 목록을 빠르게 확인하고 조작한다.

현재 제공 기능:

* 채널명 표시
* 재생 URL 또는 relay/source 요약 표시
* 연결 상태 표시
* 요약 통계 표시
* 다중 선택
* 드래그로 채널 목록 순서 변경
* 우클릭 메뉴: 전체화면, 채널 정보, 채널 수정, 채널 삭제
* 하단 버튼: 추가, 삭제, 목록 순서대로 그리드 정렬

수용 기준:

* 채널 추가 시 목록 마지막에 표시된다.
* 채널 삭제 시 목록, 그리드, 전체화면 탭, 저장 파일 상태가 일관되게 갱신된다.
* 채널 선택 시 해당 그리드 셀이 하이라이트된다.
* 목록 순서 변경은 저장 대상에 반영된다.
* `정렬` 실행 시 그리드 배치가 좌측 목록 순서와 일치한다.

### 중앙 영상 영역

목적:

* 등록된 채널을 그리드 형태로 동시에 관제한다.

현재 제공 기능:

* 기본 탭 `전체`
* 채널별 영상 타일 표시
* 빈 셀 `No Stream` 표시
* Auto/1/2/3/4/5 컬럼 선택
* Auto 컬럼 규칙:
  * 0개: 3컬럼
  * 1개: 1컬럼
  * 2-4개: 2컬럼
  * 5-9개: 3컬럼
  * 10-16개: 4컬럼
  * 17개 이상: 5컬럼
* 그리드 셀 드래그 앤 드롭 배치
* 더블클릭으로 전체화면 탭 열기

수용 기준:

* 그리드 셀은 4:3 영상 비율과 정보 바 높이를 유지한다.
* 창 크기, 패널 토글, 컬럼 변경 시 셀 크기가 깨지지 않는다.
* 드래그로 occupied 셀에 drop하면 두 채널의 grid index가 교환된다.
* 빈 셀에도 drop이 가능하다.
* grid index는 앱 재시작 후 복원된다.

### 우측 패널

목적:

* 설정, 파일 목록, 로그를 보조 패널에서 제공한다.

현재 제공 탭:

* 설정: 그리드 컬럼 선택
* 스냅샷/녹화: 로컬 파일 목록
* 로그: 앱/VLC/MediaMTX 이벤트 로그

수용 기준:

* 좌우 패널은 독립적으로 숨김/표시할 수 있다.
* 패널 토글 후 그리드 레이아웃이 재계산된다.
* 로그는 최대 라인 수를 넘으면 오래된 로그부터 제거된다.

### 상태 바

목적:

* 전체 앱과 시스템 리소스 상태를 빠르게 확인한다.

현재 제공 기능:

* 채널 집계 상태
* 시스템 CPU
* 시스템 메모리

수용 기준:

* 1초 주기로 갱신된다.
* 리소스 값을 얻을 수 없는 플랫폼에서도 앱 동작을 막지 않는다.

## 채널 관리

### 채널 데이터

채널은 최소 아래 값을 가진다.

```text
name: string
sourceUrl: string
relayEnabled: bool
relayPath: string
playUrl: string
autoReconnect: bool
gridIndex: int
listOrder: int
```

저장 파일에는 하위 호환을 위해 `url` 필드를 유지한다.

```text
~/.ziilab/channels.json
```

저장 구조:

```json
{
  "gridCols": 3,
  "channels": [
    {
      "name": "Camera 1",
      "url": "rtsp://127.0.0.1:8554/voxl1",
      "sourceUrl": "rtsp://169.254.4.1:8900/live",
      "relayEnabled": true,
      "relayPath": "voxl1",
      "autoReconnect": true,
      "gridIndex": 0
    }
  ]
}
```

수용 기준:

* `sourceUrl`이 없던 과거 설정은 `url`을 원본 URL로 간주해 복원한다.
* relay 사용 채널은 저장된 `url`을 신뢰하지 않고 `relayPath` 기준으로 재생 URL을 재계산한다.
* 잘못된 URL 또는 relay path는 로드 시 건너뛰고 경고 로그를 남긴다.
* 저장은 원자적 쓰기 방식으로 수행한다.

### 채널 추가

입력 필드:

* 채널 이름
* 원본 RTSP URL
* MediaMTX relay 사용 여부
* Relay path
* 자동 재연결 여부

기본값:

* 채널 이름: `Camera N`
* 원본 RTSP URL: 빈 값
* relay 사용: true
* relay path: `voxlN`
* 자동 재연결: true

검증:

* 채널 이름은 비어 있으면 안 된다.
* 원본 URL은 `rtsp://` 또는 `rtsps://`이고 host가 있어야 한다.
* relay path는 ASCII 영문/숫자/점/하이픈/밑줄만 허용한다.

수용 기준:

* relay 미사용 채널은 원본 URL을 바로 재생한다.
* relay 사용 채널은 MediaMTX 설정 동기화 성공 후 재생한다.
* relay 기동/동기화 실패 시 채널은 추가되지만 재생은 보류하고 경고를 표시한다.

### 채널 수정

수정 가능 항목:

* 채널 이름
* 원본 RTSP URL
* relay 사용 여부
* relay path
* 자동 재연결 여부

수용 기준:

* 이름만 바뀌면 재생 세션을 재시작하지 않는다.
* 원본 URL, relay 여부, relay path, 재생 URL이 바뀌면 재생 세션을 재시작한다.
* relay 변경 적용 실패 시 기존 설정으로 롤백한다.
* 열려 있는 전체화면 탭도 수정 결과와 동기화된다.

### 채널 삭제

수용 기준:

* 선택된 채널 여러 개를 삭제할 수 있다.
* 삭제된 채널의 grid index, 목록 순서, 정보창이 제거된다.
* 삭제된 채널을 source로 쓰는 전체화면 탭은 닫힌다.
* 녹화 중 삭제 시 파일 flush가 끝나도록 정리한다.
* 삭제 후 MediaMTX 설정을 다시 동기화한다.
* 삭제 후 channels.json을 저장한다.

## MediaMTX relay

목적:

* VOXL2 Mini 원본 RTSP에 붙는 client 수를 장비당 1개로 제한하고, viewer 내부 preview/fullscreen/recording fan-out은 localhost relay 뒤에서 처리한다.

현재 동작:

* relay 채널이 있으면 `~/.ziilab/mediamtx.yml`을 생성한다.
* MediaMTX 실행 파일 탐색 순서:
  * `ZIILAB_MEDIAMTX`
  * PATH
  * 앱 실행 파일 주변
  * macOS app bundle Resources/Frameworks
* RTSP listen: `127.0.0.1:8554`
* API listen: `127.0.0.1:9997`
* path별 source는 원본 URL
* upstream transport는 TCP
* sourceOnDemand는 false
* 기존 프로세스가 실행 중이고 config signature가 같으면 유지한다.
* config 변경 시 프로세스 재시작 없이 hot reload를 기대한다.
* 이전 실행에서 남은 viewer-owned MediaMTX PID만 회수한다.

수용 기준:

* relay 채널이 하나도 없으면 MediaMTX를 중지한다.
* relay path 중복은 하나만 반영한다.
* path가 invalid인 채널은 relay config에 포함하지 않는다.
* MediaMTX 시작 실패는 로그와 토스트로 표시한다.
* viewer가 띄운 MediaMTX만 종료/회수한다.
* 임의의 외부 MediaMTX 프로세스는 이름만 보고 종료하지 않는다.

보류 기능:

* MediaMTX API를 조회해 path/source/reader 상태를 UI에 표시
* MediaMTX 비정상 종료 후 자동 재시작 정책
* path별 source health를 채널 상태와 분리 표시

## 스트림 재생

목적:

* 채널별 RTSP 스트림을 libVLC로 안정적으로 재생한다.

재생 URL 결정:

* relay 사용: `rtsp://127.0.0.1:8554/<relayPath>`
* relay 미사용: `sourceUrl`

libVLC 옵션 기준:

* audio disabled
* network/live caching: 1000ms
* 늦은 프레임 drop
* localhost RTSP는 TCP 사용
* 비-local RTSP는 VLC 기본 transport 사용
* macOS에서는 videotoolbox 비활성화

상태:

* Idle
* Connecting
* Connected
* Disconnected
* Reconnecting
* Failed

중요 정책:

* libVLC `Playing` 이벤트만으로 `Connected`로 확정하지 않는다.
* 실제 프레임이 디코딩 또는 표시되어야 `Connected`로 확정한다.
* `Playing` 이후 일정 시간 동안 데이터가 없으면 가짜 연결로 보고 재접속한다.

수용 기준:

* RTSP 세션만 열리고 프레임이 없으면 UI는 `연결됨`으로 표시하지 않는다.
* 실제 프레임 수신 후 재접속 카운터가 리셋된다.
* 연결 실패/끊김은 로그에 남는다.
* 수동 재연결은 Failed/give-up 상태를 해제한다.
* 재생 중 stop/delete/shutdown 경로에서 libVLC player가 정리된다.

## 자동 재접속

목적:

* 일시적인 네트워크/카메라 장애를 복구하되, 무한 연결 루프와 TIME\_WAIT 폭증을 막는다.

정책:

* 자동 재연결이 꺼진 채널은 자동 재시도하지 않는다.
* 최초 직접 RTSP 실패는 짧게 제한한다.
* 최초 relay 실패는 MediaMTX source 준비 시간을 고려한다.
* 연결된 적 있는 채널은 더 긴 복구 시도를 허용한다.
* 연결된 적 있는 채널은 exponential backoff를 적용한다.
* give-up 이후에는 수동 재연결 또는 채널 수정으로만 재개한다.

현재 코드 기준 값:

```text
INITIAL_DIRECT_MAX_RECONNECT = 3
INITIAL_RELAY_MAX_RECONNECT = 24
ESTABLISHED_MAX_RECONNECT = 10
RECONNECT_BASE_MS = 5000
RECONNECT_MAX_INTERVAL_MS = 60000
DATA_CONFIRM_TIMEOUT_MS = 5000
```

문서 불일치:

* `docs/2026-06-09-가짜연결-재접속루프-수정.md`에는 relay 최초 재접속을 5회로 낮췄다고 적혀 있으나, 현재 `src/VlcWidget.h`는 24회다.
* 제품 정책으로 5회 또는 24회 중 하나를 확정해야 한다.

수용 기준:

* 가짜 연결 반복 시 재접속 횟수가 계속 증가하고 결국 Failed로 수렴한다.
* `Connected` 확정 전에는 재접속 카운터가 리셋되지 않는다.
* Failed 상태 이후 자동 재접속 타이머가 재무장되지 않는다.
* 수동 재연결은 카운터와 give-up을 초기화한다.

## 통계와 채널 정보

목적:

* 운영자와 개발자가 스트림 품질을 확인할 수 있게 한다.

요약 표시:

* bitrate
* output fps
* recent lost frame
* recent discontinuity

상세 정보창:

* 재생 URL
* 원본 URL
* relay enabled/path
* audio decoded/played/lost
* video decoded/displayed/lost/output fps/nominal fps/recent lost
* input/demux bytes/bitrate/corrupted/discontinuity
* stream output sent packets/bytes/bitrate

수용 기준:

* 상세 정보창은 채널 삭제 시 닫힌다.
* 정보창 값은 1초 주기로 갱신된다.
* 통계 값이 없으면 대기 또는 0값을 표시하되 앱 동작을 막지 않는다.

## 전체화면 탭

목적:

* 선택 채널을 중앙 영역의 별도 탭에서 크게 표시한다.

현재 동작:

* 그리드 또는 채널 목록 더블클릭으로 연다.
* 우클릭 메뉴로 연다.
* 이미 열린 채널이면 기존 탭으로 전환한다.
* 그리드 원본은 유지하고 전체화면용 독립 VlcWidget을 만든다.
* 전체화면 타일은 원본 채널을 `sourceViewer`로 가진다.
* 녹화 상태 UI는 원본 채널 상태를 따라간다.

수용 기준:

* relay 채널이 아직 `Connected`가 아니면 전체화면 탭을 열지 않는다.
* 전체화면 탭 닫기 시 해당 player만 stop/delete된다.
* 원본 채널 삭제 시 연결된 전체화면 탭도 닫힌다.
* 전체화면에서 채널 수정/정보 요청은 원본 채널에 대해 수행된다.

설계 메모:

* 전체화면은 현재 별도 RTSP client를 만든다.
* 제품 기본 구조에서는 이 client가 VOXL이 아니라 MediaMTX에 붙어야 한다.

## 스냅샷

목적:

* 현재 재생 중인 채널의 화면을 이미지로 저장한다.

저장 경로:

```text
~/.ziilab/snapshots
```

파일명:

```text
<safe-channel-name>_<yyyyMMdd_HHmmss>.png
```

현재 동작:

* libVLC snapshot을 우선 시도한다.
* 실패 시 OS-level screen grab을 fallback으로 사용한다.
* 성공 시 로그와 토스트를 표시한다.
* 파일 목록이 스냅샷 탭이면 갱신한다.

수용 기준:

* 재생 중이 아니면 스냅샷 버튼/메뉴가 비활성화된다.
* 저장 성공 시 파일이 목록에 나타난다.
* 저장 실패 시 오류 로그가 남는다.
* 파일명에 OS에서 문제가 될 문자는 `_`로 치환한다.

## 녹화

목적:

* 현재 재생 중인 채널을 로컬 영상 파일로 저장한다.

저장 경로:

```text
~/.ziilab/recordings
```

컨테이너:

* MKV

상태:

* Idle
* Starting
* Active
* Stopping

현재 동작:

* 재생 중인 채널에서만 녹화 시작 가능
* 녹화용 별도 libVLC player 생성
* output file이 생성되고 크기가 증가해야 Active로 확정
* 시작 후 5초 동안 데이터가 없으면 실패 처리
* 중지 시 player flush 완료 후 파일 크기/duration을 기록
* disconnect 중 녹화가 끊기면 auto-stopped로 기록
* 전체화면 타일에서 녹화 조작 시 원본 채널의 녹화를 제어

수용 기준:

* Starting 중에는 녹화 버튼이 비활성화된다.
* Active 중에는 REC badge와 경과 시간이 표시된다.
* Stopping 중에는 저장 중 상태가 표시된다.
* 0바이트 또는 미생성 파일은 실패로 기록한다.
* 수동 중지, disconnect 중지, 삭제/종료 중지를 구분한다.
* 채널 삭제 또는 앱 종료 중에는 재생 복구 없이 파일 flush만 수행한다.
* 파일 저장 완료 후 녹화 목록이 갱신된다.

리스크:

* 녹화용 별도 player는 relay 없는 직접 재생 환경에서 원본 RTSP client 수를 증가시킨다.
* 제품 기본값은 relay 사용으로 이 리스크를 완화한다.

## 파일 목록

목적:

* 저장된 스냅샷과 녹화 파일을 앱 안에서 확인하고 열 수 있게 한다.

현재 기능:

* 스냅샷/녹화 토글
* 새로고침
* 저장 폴더 열기
* 파일 더블클릭 열기
* 우클릭 메뉴:
  * 열기
  * Finder/탐색기/파일 관리자에서 보기
  * 삭제
* 이미지 썸네일
* 비디오 placeholder 썸네일

수용 기준:

* 파일 목록은 최신 수정 시간순으로 표시된다.
* 삭제 전 확인창을 표시한다.
* 삭제 성공/실패를 로그에 남긴다.
* 파일 크기와 수정 시간이 표시된다.

## 로그와 알림

목적:

* 운영자에게 중요한 상태 변화를 즉시 알리고, 개발자에게 문제 분석 근거를 남긴다.

로그 레벨:

* DEBUG
* INFO
* WARN
* ERROR

로그 소스:

* 앱 시작/종료
* 채널 추가/수정/삭제/이동
* 재생/재접속/실패
* 녹화/스냅샷
* MediaMTX stdout 및 lifecycle
* VLC 오류 필터 통과분

토스트:

* 스냅샷 저장됨
* 녹화 저장됨
* 녹화 자동 저장됨
* 녹화 실패
* MediaMTX 시작 실패
* 전체화면 열 수 없음

수용 기준:

* 반복적이고 무해한 VLC warning/error는 사용자 로그에서 필터링한다.
* 사용자가 조치해야 하는 오류는 WARN/ERROR로 남긴다.
* 토스트 액션은 파일 열기, 폴더 열기, 로그 보기 등 즉시 조치 가능한 동작만 제공한다.

## 설정과 파일 경로

앱 데이터:

```text
~/.ziilab/channels.json
~/.ziilab/mediamtx.yml
~/.ziilab/mediamtx.pid
~/.ziilab/snapshots/
~/.ziilab/recordings/
```

수용 기준:

* 필요한 디렉터리는 자동 생성한다.
* 설정 저장 실패는 앱을 크래시시키지 않는다.
* 다음 실행에서 channels.json을 자동 로드한다.

## 비기능 요구사항

### 안정성

* 하나의 채널 실패가 다른 채널 재생을 중단시키지 않아야 한다.
* relay config 변경은 가능한 한 기존 정상 스트림을 끊지 않아야 한다.
* 앱 종료 시 libVLC player, recording player, MediaMTX child process를 정리해야 한다.
* 가짜 연결/무한 재접속/TIME\_WAIT 폭증을 방지해야 한다.

### 성능

* 20채널까지 등록/표시 가능한 구조를 목표로 한다.
* 통계 갱신은 1초 주기로 제한한다.
* UI thread에서 긴 player cleanup을 직접 블로킹하지 않는다.
* grid resize/update는 불필요한 재계산을 줄인다.

### 플랫폼

* macOS, Windows, Linux를 지원 대상으로 둔다.
* VLC runtime과 MediaMTX bundling은 플랫폼별로 다르게 처리한다.
* Windows에서는 MediaMTX console window가 별도로 뜨지 않아야 한다.

### 운영성

* 제품 기본은 MediaMTX relay 사용이다.
* VOXL 원본 RTSP client 수가 preview/fullscreen/recording 증가에 따라 늘어나지 않아야 한다.
* 현장 장애 분석을 위해 로그와 채널 통계가 충분해야 한다.

## 리팩토링 기준 책임 분리

리팩토링 후에도 위 기능은 유지하되, 책임은 아래처럼 나눈다.

```text
ChannelConfig / ChannelState
  채널 데이터, 저장 대상 상태

ChannelRepository
  channels.json save/load/migration

ChannelListModel
  좌측 채널 목록 표시 데이터

GridLayoutModel
  gridIndex, 컬럼, drop/swap 계산

MediaRelayService
  relay channel set, config, process lifecycle

VlcPlaybackSession
  libVLC 재생 player, 이벤트, 실제 프레임 확정

ReconnectPolicy
  재접속 횟수, 간격, give-up 판단

RecordingService / RecordingSession
  녹화 player, 파일명, 상태 전이, flush

SnapshotService
  snapshot 경로/파일명/저장

FilesPanel / FileCatalogService
  저장 파일 목록/삭제/열기

MainWindow
  화면 조립과 상위 signal 연결
```

## 우선 테스트 케이스

### 단위 테스트

* `ConnectionDialog::isRtspUrlAllowed`
* `MediaRelayManager::normalizeRelayPath`
* `MediaRelayManager::isValidRelayPath`
* MediaMTX config 생성 결과
* channels.json legacy/current schema load
* grid index first-free/swap/align
* reconnect attempt/backoff/give-up 계산
* recording/snapshot filename sanitize
* 파일 크기 formatting

### 통합 테스트 후보

* relay 채널 추가 후 MediaMTX config 생성
* relay 기동 실패 시 채널 재생 보류
* 채널 수정 relay 실패 시 롤백
* 채널 삭제 시 전체화면 탭 정리
* 가짜 연결 후 Failed 수렴
* 수동 재연결 후 give-up 해제
* 녹화 시작 실패: 파일 미생성
* 녹화 시작 실패: 0바이트 유지
* 녹화 중 disconnect auto-stop
* 녹화 중 채널 삭제 flush
* 스냅샷 저장 후 파일 목록 갱신

### 현장 검증

* VOXL `voxl-streamer` client count가 relay 사용 시 1로 유지되는지
* fullscreen/recording을 켜도 VOXL client count가 증가하지 않는지
* 카메라 OFF 시 TIME\_WAIT가 무한 증가하지 않는지
* 카메라 복귀 후 수동 재연결이 정상 동작하는지
* 장시간 재생 중 지연 누적 watchdog이 과도하게 재접속하지 않는지
* 녹화 50회 반복 중 0바이트 파일이 생기지 않는지

## 확정 필요 항목

* relay 최초 실패 허용 횟수: 5회 vs 24회
* relay source가 늦게 올라오는 현장 평균 시간
* 제품 기본 transport 정책: upstream TCP 고정 여부
* direct RTSP 모드를 제품에서 공식 지원할지, 개발/긴급용으로만 둘지
* 전체화면을 독립 RTSP client로 유지할지, 같은 decoded surface를 확대하는 구조를 검토할지
* 녹화 저장 포맷을 MKV로 고정할지, MP4 옵션을 제공할지
* 파일 자동 보존/삭제 정책을 둘지
* 채널 최대 개수를 README의 20채널로 고정할지, 성능 기준으로 재정의할지
