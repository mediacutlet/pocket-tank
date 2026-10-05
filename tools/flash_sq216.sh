#!/bin/bash
# flash_sq216.sh - build the 2.16 image (the 480 x 480 square tank,
# CONFIG_POCKET_TANK_SQ216) and flash it to a Waveshare ESP32-S3-Touch-AMOLED-2.16.
#
#   tools/flash_sq216.sh                # build, flash the app (the save and the model stay)
#   tools/flash_sq216.sh --build-only
#   tools/flash_sq216.sh --full         # a blank board: clear nvs, the model + its trailer, bootloader + table + app
#   tools/flash_sq216.sh --log 15       # ... then print 15 s of the boot log (no second reset)
#   SQ216_SERIAL=AA:BB:CC:DD:EE:FF tools/flash_sq216.sh   # the board by its USB serial (or set it in tools/boards.local.sh)
#
# With no SQ216_SERIAL the board is found on its own when it is the ONLY
# Espressif USB device plugged in (USB vendor 303A); with several, name it.
#
# The reset at the end is the chip's watchdog, not the RTS line (2026-10-04):
# on macOS the USB-Serial-JTAG hard reset left this board in DOWNLOAD mode -
# a black glass, "waiting for download" - every time. esptool >= 4.9 has
# --after watchdog_reset (ESP-IDF 5.5 ships it); an older one falls back to
# the hard reset and says how to get out of download mode.
#
# Its own build directory and sdkconfig (~/.cache/pocket-tank/fw-build-216).
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f tools/boards.local.sh ] && . tools/boards.local.sh   # your boards' USB serials (not tracked)
SERIAL=${SQ216_SERIAL:-}
B=~/.cache/pocket-tank/fw-build-216
LOG_S=0
for ((i = 1; i <= $#; i++)); do [ "${!i}" = "--log" ] && { j=$((i + 1)); LOG_S=${!j:-15}; }; done
. ~/esp/esp-idf/export.sh > /dev/null 2>&1 || { echo "flash_sq216: ESP-IDF export failed"; exit 1; }
[ -f firmware/keys/ota_signing_key.pem ] || { echo "flash_sq216: no firmware/keys/ota_signing_key.pem - tools/ota_key.sh"; exit 1; }
LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT
( cd firmware && idf.py -B "$B" -DSDKCONFIG="$B/sdkconfig" -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.sq216" build ) > "$LOG" 2>&1 \
  || { echo "flash_sq216: build FAILED"; grep -Ei "error|fatal|failed" "$LOG" | head -30; exit 1; }
grep -E "pocket_tank.bin binary size" "$LOG" || true
grep -q "CONFIG_POCKET_TANK_SQ216=y" "$B/sdkconfig" || { echo "flash_sq216: $B is not a 2.16 build"; exit 1; }
[[ " $* " == *" --build-only "* ]] && exit 0
PORT=$(python - "$SERIAL" <<'PY'
import sys
from serial.tools import list_ports
want = sys.argv[1].upper()
ports = list(list_ports.comports())
if want:
    hits = [p for p in ports if (p.serial_number or "").upper() == want]
else:
    hits = [p for p in ports if p.vid == 0x303A]          # Espressif's USB-Serial-JTAG
    if len(hits) > 1:
        print("several Espressif boards: " + ", ".join(f"{p.device} ({p.serial_number})" for p in hits), file=sys.stderr)
        sys.exit(2)
if len(hits) != 1: sys.exit(1)
print(hits[0].device.replace("/dev/tty.", "/dev/cu."))
PY
) || { echo "flash_sq216: no board found${SERIAL:+ with USB serial $SERIAL} - set SQ216_SERIAL=<its USB serial> (tools/boards.local.sh)"; exit 1; }
echo "flash_sq216: the 2.16 is $PORT"
AFTER=watchdog_reset
python -m esptool --help 2>/dev/null | grep -q watchdog_reset || AFTER=hard_reset
ESPTOOL="python -m esptool --chip esp32s3 -p $PORT -b 921600"
if [[ " $* " == *" --full "* ]]; then
  [ -f model/out/model_q4.bin ] || { echo "flash_sq216: no model/out/model_q4.bin"; exit 1; }
  $ESPTOOL --after no_reset erase_region 0x9000 0x7000 | grep -E "Erase completed|rror"
  python tools/model_trailer.py --check --out "$B/model_trailer.bin" > /dev/null 2>&1 || rm -f "$B/model_trailer.bin"   # (none: the tank heals it at boot)
  $ESPTOOL --after no_reset write_flash 0x290000 model/out/model_q4.bin \
    $( [ -f "$B/model_trailer.bin" ] && echo "0xA8F000 $B/model_trailer.bin" ) | grep -E "Wrote|verified|rror"
  ( cd "$B" && $ESPTOOL --after "$AFTER" write_flash "@flash_args" | grep -E "Wrote|verified|rror" )
else
  # otadata too: after an over-the-air update the board boots ota_1, and an app written to ota_0 alone would never run
  $ESPTOOL --after "$AFTER" write_flash 0xA90000 "$B/ota_data_initial.bin" 0x10000 "$B/pocket_tank.bin" | grep -E "Wrote|verified|rror"
fi
if [ "$AFTER" = hard_reset ]; then
  echo "flash_sq216: this esptool has no watchdog_reset - if the glass stays black (download mode), unplug the board"
  echo "             (and its battery, if one is fitted) and plug it back without holding BOOT"
fi
if [ "$LOG_S" -gt 0 ]; then                       # the boot log: a plain open of the port, which resets nothing
  python - "$PORT" "$LOG_S" <<'PY'
import sys, time, serial
end = time.time() + float(sys.argv[2]); s = None
while time.time() < end:
    try:
        if s is None: s = serial.Serial(sys.argv[1], 115200, timeout=0.2)
        d = s.read(4096)
        if d: sys.stdout.write(d.decode(errors="replace")); sys.stdout.flush()
    except (serial.SerialException, OSError):
        s = None; time.sleep(0.1)                  # the port comes back after the reset
PY
fi
echo "flash_sq216: done"
