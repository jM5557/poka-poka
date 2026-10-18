#!/usr/bin/env bash
# One bounded diagnostic run of the Felyne Village G port, packed for sending.
#
#   diagnose.sh [seconds] [build_dir]
#
# Runs out/pokapoka/bin/Yakumo for `seconds` (default 90) with display, file,
# movie and save-data traces, a performance line every second, a screenshot
# every 60 presented frames, and a virtual gamepad that presses Start, ○ and
# ✕ every few seconds (to get past a "press start" screen). Writes
# diagnose-<time>.tar.gz in the current directory: the log, a summary and the
# screenshots. Nothing from the disc image is included, but the screenshots
# show the game's screen.
set -euo pipefail

seconds="${1:-90}"
profile_dir="$(cd "$(dirname "$0")/.." && pwd)"
repo_dir="$(cd "$profile_dir/../.." && pwd)"
build_dir="${2:-$repo_dir/out/pokapoka}"
binary="$build_dir/bin/Yakumo"
[[ -x "$binary" ]] || { echo "error: $binary not built" >&2; exit 1; }

stamp="$(date +%Y%m%d-%H%M%S)"
out="$PWD/diagnose-$stamp"
mkdir -p "$out/shots"

# Frames here are window-event pumps, about one per game frame. Start at
# frame 300 (about 10 s at 30 fps) and repeat Start, then circle, then cross.
script=""
for frame in $(seq 300 150 6000); do
    script+="$frame:pad start 6;$((frame + 40)):pad b 6;$((frame + 80)):pad a 6;"
done

echo "running for $seconds s; leave the window alone"
ulimit -s 65536 2> /dev/null || true
set +e
MHP3RD_TRACE_DISPLAY=1 MHP3RD_TRACE_IO=1 MHP3RD_TRACE_MPEG=1 MHP3RD_TRACE_SAVEDATA=1 \
    MHP3RD_PERF=log MHP3RD_HANG_REPORT=8 MHP3RD_SCREENSHOT_DIR="$out/shots" MHP3RD_SCREENSHOT_EVERY=60 \
    MHP3RD_INPUT_SCRIPT="$script" \
    timeout --signal=INT --kill-after=10 "$seconds" stdbuf -oL -eL "$binary" "$profile_dir/game" > "$out/run.log" 2>&1
status=$?
set -e
echo "exit status $status" >> "$out/run.log"

# A short summary, so the long log need not be read first. A grep that finds
# nothing is not an error here.
set +e
{
    echo "== exit status $status"
    echo "== last lines"
    grep -v '^\[io\] read' "$out/run.log" | uniq -c | tail -25
    echo "== hang reports"
    grep '\[hang\]' "$out/run.log" | head -12
    echo "== stubs reached"
    grep '\[hle-stub\]' "$out/run.log" || echo none
    echo "== display"
    grep '\[display\]' "$out/run.log" | head -40
    echo "== files opened"
    grep -E '\[io\] (open|getstat|dopen)' "$out/run.log" | sed -E 's/ flags=.*//' | sort | uniq -c | sort -rn | head -40
    echo "== reads per file"
    grep '\[io\] read' "$out/run.log" | awk '{print $4}' | sort | uniq -c | sort -rn | head -20
    echo "== performance (every 10 s)"
    grep -E '^\[perf\]' "$out/run.log" | awk 'NR % 10 == 1' | head -20
    echo "== movies / save data"
    grep -E '\[(mpeg|savedata|psmf)\]' "$out/run.log" | head -20
    echo "== screenshots"
    ls "$out/shots" | head -50
} > "$out/summary.txt" 2>&1
set -e

# Keep the archive small: the busiest read lines go, the rest stays.
gzip -9 "$out/run.log"
tar -czf "$out.tar.gz" -C "$(dirname "$out")" "$(basename "$out")"
rm -rf "$out"
echo "wrote $out.tar.gz ($(du -h "$out.tar.gz" | cut -f1)); please send it"
