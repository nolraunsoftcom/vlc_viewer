#!/bin/bash
# voxl-streamer-watchdog 제거 스크립트 (VOXL2 에서 서비스/파일 제거)
#   사용: ./uninstall.sh
set -e
ADB="${ADB:-adb}"

"$ADB" wait-for-device
echo "[1/2] 서비스 중지/비활성화"
"$ADB" shell "systemctl disable --now voxl-streamer-watchdog 2>/dev/null || true"
echo "[2/2] 파일 제거"
"$ADB" shell "rm -f /usr/bin/voxl-streamer-watchdog /etc/systemd/system/voxl-streamer-watchdog.service && systemctl daemon-reload"
echo "완료. (설정파일 /etc/modalai/voxl-streamer-watchdog.conf 는 보존 — 필요시 수동 삭제)"
echo "참고: 워치독을 제거하면 keepalive 가 사라져 voxl-streamer 가 on-demand 로 돌아갑니다."
