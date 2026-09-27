#!/bin/bash
# flash.sh - the only way to flash the tank. Archives the battery log and the
# tank state first (tools/preflight.py -> docs/batlog/), then builds and
# flashes the app; `--model` also writes the model partition. Refuses to run
# without a preflight archive: battery measurements run in parallel with
# feature work, and a reflash used to wipe the night's log (2026-09-15).
#
#   tools/flash.sh              # preflight, build, flash app
#   tools/flash.sh --model      # ... and model/out/model_q4.bin at 0x290000
#   tools/flash.sh --no-build   # preflight, flash the existing build
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD=~/.cache/pocket-tank/fw-build
PORT=$(compgen -G '/dev/cu.usbmodem*' | sort | head -1 || true)
[ -n "$PORT" ] || { echo "flash: no /dev/cu.usbmodem* - wake the tank (BOOT) first"; exit 1; }
. ~/esp/esp-idf/export.sh > /dev/null 2>&1 || { echo "flash: ESP-IDF export failed"; exit 1; }
python tools/preflight.py

LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT
run_logged() {
  local label=$1 pattern=$2
  shift 2
  : > "$LOG"
  if ! "$@" > "$LOG" 2>&1; then
    echo "flash: $label FAILED"
    grep -Ei "error|fatal|failed" "$LOG" || tail -40 "$LOG"
    return 1
  fi
  grep -E "$pattern" "$LOG" || tail -5 "$LOG"
}

cd firmware
if [[ " $* " != *" --no-build "* ]]; then
  run_logged "build" "binary size|build complete" idf.py -B "$BUILD" build
fi
if [[ " $* " == *" --model "* ]]; then
  run_logged "model flash" "Wrote|verified" python -m esptool --chip esp32s3 -p "$PORT" -b 460800 write_flash 0x290000 ../model/out/model_q4.bin
  sleep 3
fi
run_logged "firmware flash" "verified|Hard resetting" idf.py -B "$BUILD" -p "$PORT" flash
echo "flash: done - the tank boots now; docs/batlog has the pre-flash archive (commit it)"
