#!/usr/bin/env bash
set -euo pipefail

rss_kb() {
  local pattern="$1"
  ps -eo rss=,comm=,args= | awk -v pat="$pattern" '
    $2 ~ /^(awk|bash|sh|ps|rg|timeout)$/ { next }
    $2 == pat || index($2, pat) || index($0, "/" pat) || index($0, pat " ") { s += $1 }
    END { print s + 0 }
  '
}

cpu_percent() {
  local pattern="$1"
  ps -eo pcpu=,comm=,args= | awk -v pat="$pattern" '
    $2 ~ /^(awk|bash|sh|ps|rg|timeout)$/ { next }
    index($2, pat) || index($0, pat) { s += $1 }
    END { printf "%.1f\n", s + 0 }
  '
}

start_ns="$(date +%s%N)"
vibewall status >/dev/null || true
sleep 0.2
daemon_rss="$(rss_kb vibewall-daemon)"
daemon_cpu="$(cpu_percent vibewall-daemon)"

vibewall-picker --mode grid --benchmark-ready &
picker_pid="$!"
for _ in $(seq 1 100); do
  [ -e "/tmp/vibewall-picker-ready-$picker_pid" ] && break
  sleep 0.05
done
ready_ns="$(date +%s%N)"
picker_rss="$(rss_kb vibewall-picker)"
picker_cpu_ready="$(cpu_percent vibewall-picker)"

cpu_a="$(awk '{ print $14 + $15 }' "/proc/$picker_pid/stat" 2>/dev/null || echo 0)"
sleep 10
cpu_b="$(awk '{ print $14 + $15 }' "/proc/$picker_pid/stat" 2>/dev/null || echo 0)"
kill "$picker_pid" >/dev/null 2>&1 || true

startup_ms="$(( (ready_ns - start_ns) / 1000000 ))"
echo "daemon_rss_kb=$daemon_rss"
echo "daemon_cpu_percent=$daemon_cpu"
echo "picker_startup_ms=$startup_ms"
echo "picker_ready_rss_kb=$picker_rss"
echo "picker_ready_cpu_percent=$picker_cpu_ready"
echo "opened_app_rss_kb=$picker_rss"
echo "mpvpaper_rss_kb=$(rss_kb mpvpaper)"
echo "mpvpaper_cpu_percent=$(cpu_percent mpvpaper)"
echo "hyprpaper_rss_kb=$(rss_kb hyprpaper)"
echo "hyprpaper_cpu_percent=$(cpu_percent hyprpaper)"
echo "noctalia_rss_kb=$(rss_kb noctalia)"
echo "noctalia_cpu_percent=$(cpu_percent noctalia)"
echo "picker_idle_cpu_ticks_10s=$(( cpu_b - cpu_a ))"
echo "idle_redraw_policy=event-driven"
