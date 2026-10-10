#!/bin/bash
# run_qemu.sh — boot the firmware in Espressif QEMU (esp32s3) with the model
# partition populated, UART on the terminal. Ctrl-A X to quit.
#   ./run_qemu.sh                 # hardware config (octal 8 MB PSRAM emulated)
#   B=build_qemu ./run_qemu.sh    # quad-PSRAM overlay build
# Needs the esp-develop-9.2.2+ QEMU (PSRAM 8 MB + octal emulation); the
# 9.0.0 build IDF 5.4.1 ships lacks both.
set -e
cd "$(dirname "$0")"
. ~/esp/esp-idf/export.sh >/dev/null 2>&1
QEMU_VER="${QEMU_VER:-esp_develop_9.2.2_20250817}"
export PATH="$HOME/.espressif/tools/qemu-xtensa/$QEMU_VER/qemu/bin:$PATH"
B="${B:-build}"
MODEL="../model/out/model_q4.bin"
# image on the local SSD: QEMU needs file locking, which AFP shares refuse
mkdir -p "$HOME/.cache/aqua-pets"
IMG="$HOME/.cache/aqua-pets/flash_qemu_$B.bin"
esptool.py --chip esp32s3 merge_bin --fill-flash-size 16MB -o "$IMG" \
  0x0      $B/bootloader/bootloader.bin \
  0x8000   $B/partition_table/partition-table.bin \
  0x10000  $B/aqua_pets.bin \
  0x290000 "$MODEL" >/dev/null
ls -la "$IMG" | awk '{print "flash image:", $5, "bytes"}'
if [ "$B" = "build_qemu" ]; then PSRAM="-m 4M"; else PSRAM="-m 8M -global driver=ssi_psram,property=is_octal,value=true"; fi
exec qemu-system-xtensa -nographic -machine esp32s3 $PSRAM \
  -drive file="$IMG",if=mtd,format=raw,file.locking=off \
  -serial mon:stdio "$@"
