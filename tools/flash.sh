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
#   TANK_PORT=/dev/cu.usbmodem101 tools/flash.sh   # with another board plugged in
#
# Every build is SIGNED with firmware/keys/ota_signing_key.pem (tools/ota_key.sh):
# the tank verifies over-the-air images against the key its running image
# carries, so a cable build signed with another key (or none) could never
# update itself. `idf.py flash` also writes the otadata initial image (the
# tank boots ota_0 again) and, with --model, the model trailer (docs/OTA.md).
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD=${TANK_BUILD:-~/.cache/aqua-pets/fw-build}   # another checkout (a worktree) builds in its own dir
PORTS=$(compgen -G '/dev/cu.usbmodem*' | sort || true)
[ -n "$PORTS" ] || { echo "flash: no /dev/cu.usbmodem* - wake the tank (BOOT) first"; exit 1; }
# several boards on USB: never guess (preflight also refuses a port whose
# device does not answer as the tank's director)
if [ -z "${TANK_PORT:-}" ] && [ "$(echo "$PORTS" | wc -l)" -gt 1 ]; then
  echo "flash: more than one board on USB:"; echo "$PORTS"; echo "flash: say which is the tank: TANK_PORT=/dev/cu.usbmodemNNN tools/flash.sh"; exit 1
fi
export TANK_PORT=${TANK_PORT:-$PORTS}
PORT=$TANK_PORT
. ~/esp/esp-idf/export.sh > /dev/null 2>&1 || { echo "flash: ESP-IDF export failed"; exit 1; }
[ -f firmware/keys/ota_signing_key.pem ] || { echo "flash: no firmware/keys/ota_signing_key.pem - tools/ota_key.sh (and back it up)"; exit 1; }
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
  python ../tools/model_trailer.py --check --build-dir "$BUILD" || exit 1
  run_logged "model trailer" "Wrote|verified" python -m esptool --chip esp32s3 -p "$PORT" -b 460800 write_flash 0xA8F000 "$BUILD/model_trailer.bin"
  sleep 3
fi
run_logged "firmware flash" "verified|Hard resetting" idf.py -B "$BUILD" -p "$PORT" flash
echo "flash: done - the tank boots now; docs/batlog has the pre-flash archive (commit it)"
