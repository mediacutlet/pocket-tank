#!/bin/bash
# Build and flash the 412px round LCD image for the Waveshare ESP32-S3-Touch-LCD-1.46C.
# Set ROUND146_SERIAL in tools/boards.local.sh or in the environment.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f tools/boards.local.sh ] && . tools/boards.local.sh
SERIAL=${ROUND146_SERIAL:-}
[ -n "$SERIAL" ] || { echo "flash_round146c: set ROUND146_SERIAL=<the board's USB serial>"; exit 1; }
B=${TANK_BUILD:-~/.cache/pocket-tank/fw-build-146c}
. ~/esp/esp-idf/export.sh > /dev/null 2>&1 || { echo "flash_round146c: ESP-IDF export failed"; exit 1; }
[ -f firmware/keys/ota_signing_key.pem ] || { echo "flash_round146c: no firmware/keys/ota_signing_key.pem - tools/ota_key.sh"; exit 1; }
LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT
( cd firmware && idf.py -B "$B" -DSDKCONFIG="$B/sdkconfig" -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.146c" build ) > "$LOG" 2>&1 \
  || { echo "flash_round146c: build FAILED"; grep -Ei "error|fatal|failed" "$LOG" | head -30; exit 1; }
grep -E "pocket_tank.bin binary size" "$LOG" || true
grep -q "CONFIG_POCKET_TANK_ROUND_146=y" "$B/sdkconfig" || { echo "flash_round146c: $B is not a 1.46C build"; exit 1; }
[[ " $* " == *" --build-only "* ]] && exit 0
PORT=$(python - "$SERIAL" <<'PY'
import sys
from serial.tools import list_ports
want = sys.argv[1].upper()
for p in list_ports.comports():
    if (p.serial_number or "").upper() == want:
        print(p.device)
        sys.exit(0)
sys.exit(1)
PY
) || { echo "flash_round146c: no board with USB serial $SERIAL on USB"; exit 1; }
echo "flash_round146c: the 1.46C ($SERIAL) is $PORT"
ESPTOOL="python -m esptool --chip esp32s3 -p $PORT -b 460800"
if [[ " $* " == *" --full "* ]]; then
  $ESPTOOL --after no_reset erase_region 0x9000 0x7000 | grep -E "Erase completed|rror"
  $ESPTOOL --after no_reset write_flash 0x290000 model/out/model_q4.bin | grep -E "Wrote|verified|rror"
  python tools/model_trailer.py --check --build-dir "$B"
  $ESPTOOL --after no_reset write_flash 0xA8F000 "$B/model_trailer.bin" | grep -E "Wrote|verified|rror"
  ( cd "$B" && $ESPTOOL write_flash "@flash_args" | grep -E "Wrote|verified|rror" )
else
  $ESPTOOL write_flash 0xA90000 "$B/ota_data_initial.bin" 0x10000 "$B/pocket_tank.bin" | grep -E "Wrote|verified|rror"
fi
echo "flash_round146c: done"
