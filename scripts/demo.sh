#!/usr/bin/env bash
# Controlled experiment: calibrate a baseline on an idle system, inject a
# known CPU load and a known memory load, and record what vigiad detects.
#
# Leave the computer idle while it runs (about 5 minutes).
set -euo pipefail
cd "$(dirname "$0")/.."

CALIBRATE=${CALIBRATE:-60}   # seconds of calibration (1 sample per second)
LOAD=${LOAD:-45}             # seconds of each load
REST=${REST:-45}             # seconds of rest between phases
THREADS=$(nproc)
# Memory load: 25% of total RAM, never more than half of what is available.
MEM_MB=$(awk '/MemTotal/ {t=$2} /MemAvailable/ {a=$2}
              END {m=t/4; if (m > a/2) m = a/2; printf "%d", m/1024}' /proc/meminfo)

mkdir -p data docs
CSV=data/demo.csv
EVENTS=data/demo-events.csv
TOTAL=$((CALIBRATE + REST + LOAD + REST + LOAD + REST))

echo "start,end,label" > "$EVENTS"

./build/vigiad -i 1000 -c "$CALIBRATE" -n "$TOTAL" -o "$CSV" &
VIGIAD=$!
trap 'kill $VIGIAD 2>/dev/null || true' EXIT

echo "[demo] calibrating baseline for ${CALIBRATE}s, then ${REST}s idle"
sleep $((CALIBRATE + REST))

echo "[demo] CPU load: ${THREADS} threads for ${LOAD}s"
START=$(date +%s.%N)
./build/loadgen cpu "$THREADS" "$LOAD"
echo "$START,$(date +%s.%N),CPU load" >> "$EVENTS"

echo "[demo] resting ${REST}s"
sleep "$REST"

echo "[demo] memory load: ${MEM_MB} MB for ${LOAD}s"
START=$(date +%s.%N)
./build/loadgen mem "$MEM_MB" "$LOAD"
echo "$START,$(date +%s.%N),Memory load" >> "$EVENTS"

echo "[demo] resting until vigiad finishes"
wait $VIGIAD
trap - EXIT

PYTHON=python3
[ -x .venv/bin/python ] && PYTHON=.venv/bin/python
"$PYTHON" scripts/plot.py "$CSV" "$EVENTS" docs/demo.png
