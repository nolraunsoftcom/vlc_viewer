#!/bin/bash
# voxl-streamer-watchdog 설치 스크립트
# 호스트(Mac/PC)에서 adb 로 VOXL2(ModalAI) 에 3개 파일을 배포하고 서비스로 등록한다.
#   사용: cd viewer/watchdog && ./install.sh
#   (다른 adb 경로면: ADB=/path/to/adb ./install.sh)
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ADB="${ADB:-adb}"

echo "[1/6] VOXL 연결 확인"
"$ADB" wait-for-device
echo "  device: $("$ADB" get-state)"

echo "[2/6] 의존성 확인"
"$ADB" shell 'for c in gst-launch-1.0 jq timeout systemctl pgrep; do command -v "$c" >/dev/null || echo "  MISSING: $c"; done; \
  gst-inspect-1.0 progressreport >/dev/null 2>&1 && echo "  progressreport: OK (stall 감지 동작)" || echo "  progressreport: 없음 -> stall감지 폴백off, keepalive+process-check 만 동작"'

echo "[3/6] rootfs 쓰기 가능 확인 (필요시 remount)"
"$ADB" shell 'touch /usr/bin/.wtest 2>/dev/null && rm -f /usr/bin/.wtest || { echo "  /usr/bin 읽기전용 -> remount"; mount -o remount,rw /; }'

echo "[4/6] 파일 push"
"$ADB" push "$DIR/voxl-streamer-watchdog"         /usr/bin/voxl-streamer-watchdog
"$ADB" push "$DIR/voxl-streamer-watchdog.conf"    /etc/modalai/voxl-streamer-watchdog.conf
"$ADB" push "$DIR/voxl-streamer-watchdog.service" /etc/systemd/system/voxl-streamer-watchdog.service

echo "[5/6] 권한 + systemd 반영 + enable(부팅 자동시작) + start"
"$ADB" shell "chmod +x /usr/bin/voxl-streamer-watchdog && systemctl daemon-reload && systemctl enable --now voxl-streamer-watchdog"

echo "[6/6] 상태 확인"
sleep 5
"$ADB" shell "echo -n '  active='; systemctl is-active voxl-streamer-watchdog; echo -n '  enabled='; systemctl is-enabled voxl-streamer-watchdog; \
  journalctl -u voxl-streamer-watchdog --no-pager -n 3 2>/dev/null | sed 's/^.*streamer-watchdog\[[0-9]*\]: /  log: /'"
echo
echo "완료. 실시간 로그:  $ADB shell \"journalctl -u voxl-streamer-watchdog -f\""
