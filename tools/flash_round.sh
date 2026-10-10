#!/bin/bash
# flash_round.sh - build the ROUND image (the 466 px bowl, CONFIG_AQUA_PETS_ROUND)
# and flash it to the round board (Waveshare ESP32-S3-Touch-AMOLED-1.75C).
# The board is found by its USB SERIAL, never by port name: the tank and the
# round board are often on USB together and the names swap (2026-10-01).
#
#   tools/flash_round.sh               # build, flash the app
#   tools/flash_round.sh --build-only
#   tools/flash_round.sh --full        # a blank board: clear nvs, the model, bootloader + table + app
#   ROUND_SERIAL=AA:BB:CC:DD:EE:FF tools/flash_round.sh    # the board, by its USB serial (or set it once in tools/boards.local.sh)
#
# Its own build directory and sdkconfig (~/.cache/aqua-pets/fw-build-175c):
# the tank's fw-build is never touched. No preflight: this board holds no tank
# anyone would miss yet. docs/board-amoled-1.75c.md has the rest.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f tools/boards.local.sh ] && . tools/boards.local.sh   # your boards' USB serials (not tracked)
SERIAL=${ROUND_SERIAL:-}
[ -n "$SERIAL" ] || { echo "flash_round: set ROUND_SERIAL=<the board's USB serial> (in tools/boards.local.sh, or the environment)"; exit 1; }
B=~/.cache/aqua-pets/fw-build-175c
. ~/esp/esp-idf/export.sh > /dev/null 2>&1 || { echo "flash_round: ESP-IDF export failed"; exit 1; }
[ -f firmware/keys/ota_signing_key.pem ] || { echo "flash_round: no firmware/keys/ota_signing_key.pem - tools/ota_key.sh"; exit 1; }
LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT
( cd firmware && idf.py -B "$B" -DSDKCONFIG="$B/sdkconfig" -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.round" build ) > "$LOG" 2>&1 \
  || { echo "flash_round: build FAILED"; grep -Ei "error|fatal|failed" "$LOG" | head -30; exit 1; }
grep -E "aqua_pets.bin binary size" "$LOG" || true
grep -q "CONFIG_AQUA_PETS_ROUND=y" "$B/sdkconfig" || { echo "flash_round: $B is not a ROUND build"; exit 1; }
[[ " $* " == *" --build-only "* ]] && exit 0
PORT=$(python - "$SERIAL" <<'PY'
import sys
from serial.tools import list_ports
want = sys.argv[1].upper()
for p in list_ports.comports():
    if (p.serial_number or "").upper() == want:
        print(p.device.replace("/dev/tty.", "/dev/cu.")); sys.exit(0)
sys.exit(1)
PY
) || { echo "flash_round: no board with USB serial $SERIAL on USB"; exit 1; }
echo "flash_round: the round board ($SERIAL) is $PORT"
ESPTOOL="python -m esptool --chip esp32s3 -p $PORT -b 460800"
if [[ " $* " == *" --full "* ]]; then
  $ESPTOOL --after no_reset erase_region 0x9000 0x7000 | grep -E "Erase completed|rror"
  $ESPTOOL --after no_reset write_flash 0x290000 model/out/model_q4.bin | grep -E "Wrote|verified|rror"
  ( cd "$B" && $ESPTOOL write_flash "@flash_args" | grep -E "Wrote|verified|rror" )
else
  # otadata too: after an over-the-air update the board boots ota_1, and an app written to ota_0 alone would never run
  $ESPTOOL write_flash 0xA90000 "$B/ota_data_initial.bin" 0x10000 "$B/aqua_pets.bin" | grep -E "Wrote|verified|rror"
fi
echo "flash_round: done"
