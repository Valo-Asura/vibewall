#!/usr/bin/env bash
set -euo pipefail

rss_kb() {
  local name="$1"
  {
    pgrep -x "$name" || true
    pgrep -x ".${name:0:14}" || true
  } | sort -u | while read -r pid; do
    awk '/VmRSS:/ { print $2 }' "/proc/$pid/status" 2>/dev/null || true
  done | awk '{ s += $1 } END { print s + 0 }'
}

start_ns="$(date +%s%N)"
vibewall status >/dev/null || true
sleep 0.2
daemon_rss="$(rss_kb vibewall-daemon)"

vibewall-picker --mode grid --benchmark-ready &
picker_pid="$!"
for _ in $(seq 1 100); do
  [ -e "/tmp/vibewall-picker-ready-$picker_pid" ] && break
  sleep 0.05
done
ready_ns="$(date +%s%N)"
picker_rss="$(rss_kb vibewall-picker)"

cpu_a="$(awk '{ print $14 + $15 }' "/proc/$picker_pid/stat" 2>/dev/null || echo 0)"
sleep 10
cpu_b="$(awk '{ print $14 + $15 }' "/proc/$picker_pid/stat" 2>/dev/null || echo 0)"
kill "$picker_pid" >/dev/null 2>&1 || true

startup_ms="$(( (ready_ns - start_ns) / 1000000 ))"
echo "daemon_rss_kb=$daemon_rss"
echo "picker_startup_ms=$startup_ms"
echo "picker_ready_rss_kb=$picker_rss"
echo "picker_idle_cpu_ticks_10s=$(( cpu_b - cpu_a ))"
echo "idle_redraw_policy=event-driven"
