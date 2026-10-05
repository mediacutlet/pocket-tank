# Fourth board: Waveshare ESP32-S3-Touch-AMOLED-2.16 (square, 480 x 480) - port notes, 2026-10-04

A community port, brought up on one board. Sources: Waveshare's BSP component
`waveshare/esp32_s3_touch_amoled_2_16` (2.0.1) and
github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16 (the schematic, examples,
and `firmware/...-FactoryOnly-260318.bin`, written at 0x0, to restore it).

## Flashing

    tools/flash_sq216.sh              # build + the app (the save and the model stay)
    tools/flash_sq216.sh --full       # a blank board: nvs cleared, model + trailer, bootloader + table + app
    tools/flash_sq216.sh --log 15     # ... then 15 s of the boot log

The board is found by `SQ216_SERIAL` (its USB serial, e.g. in
tools/boards.local.sh), or on its own when it is the only Espressif USB device.
Every build is signed: `tools/ota_key.sh` once.

**The trap: download mode on macOS.** The usual end of an esptool write - a
hard reset on the RTS line - left this board in DOWNLOAD mode on a Mac every
time (`boot:0x23 DOWNLOAD(USB/UART0)`, "waiting for download", a black glass),
and opening the port from a script with DTR/RTS set did the same. The script
resets with `--after watchdog_reset` (esptool >= 4.9, ESP-IDF 5.5) instead,
and reads the log by a plain open of the port, which resets nothing. Stuck in
download mode by hand: `python -m esptool --chip esp32s3 -p <port> --before
no_reset --after watchdog_reset read_mac`. With a battery fitted, unplugging
USB is not a power cycle.

## The hardware

The 1.75C's family on a square panel: CO5300 over QSPI, CST9220 touch (the
CST9217's protocol, I2C 0x5A), ES8311 + NS4150B speaker amp, ES7210 mic ADC
(0x40, unused), AXP2101, QMI8658 (0x6B), PCF85063 RTC (0x51), a microSD
slot (the tank's backups, below). No IO expander: the resets are GPIOs. A3V3 (the codec and the
mic ADC) is ALDO1, as on the 1.75C; DSI_PWR_EN is pulled up to VCC3V3.

| what | GPIO | | what | GPIO |
|---|---|---|---|---|
| LCD CS / PCLK | 12 / 38 | | I2C SDA / SCL | 15 / 14 |
| LCD D0..D3 | 4 5 6 7 | | TP INT / RST | 11 / 40 |
| LCD RST | 39 | | I2S MCLK / BCLK / WS / DOUT | 42 / 9 / 45 / 8 |
| PWR sense (SYS_OUT) | 16 | | amp enable | 46 |
| keys | BOOT 0, IO18 18 | | | |

It has the watch's tell (an ES7210 AND an RTC chip, no expander), so it is
not told apart at boot: it is its own build, `CONFIG_POCKET_TANK_SQ216`
(sdkconfig.sq216), board marker `sq216`. Its MCLK is not the 1.8's GPIO 16 -
16 is SYS_OUT here.

## The square build (`TANK_SQUARE`)

The rectangle's world a size up: 480 x 480, the panel px for px. The film
grid is 24 x 24 cells of 20 px (576, inside the 644 the save keeps), fronds
4.2 px a segment so a full bed still reaches the surface. The pages keep their
448 x 368 layout, centred (16, 56).

- **The way up**: init from the BSP with MADCTL turned so the keys sit on top
  (60). The square turns ALL FOUR WAYS with the IMU (up axis -Y, side axis X:
  keys to the left = +1 g on X = the picture a quarter turn clockwise), the
  turn done by the panel - MADCTL 60 / 00 / A0 / C0, set between frames with
  both DMA stripes home. Touch is turned back the same way. Director
  `rot <0-3>|auto` holds a turn.
- **Touch**: calibrated on the nine crosses - reported = 1.046 x - 14.5,
  1.030 y - 2.9 (touch_port_ft3168.c). The panel calibration is undone
  upright, then the turn, then the finger's low-landing bias in the
  picture's own down.
- **IO18** (the third key, no job on the other boards): a TAP feeds - three pellets
  at the keeper's usual spot, at the release - and a HOLD (0.6 s) is the light, as
  the double tap on the glass is (MANUAL toggles it; AUTO puts it out or back on).
- **The microSD keeps copies of the tank** (firmware/main/sd_backup.c):
  the NVS save blob, byte for byte, to `PTANK/SAVE.BIN` a minute after
  boot, every 3 h, and at every sleep and power-off, plus one dated copy a
  day (`PTANK/Syymmdd.BIN`, the last 14 kept). A board that boots with NO
  tank in NVS (a `--full` flash, an erased chip) brings the latest copy back
  before the tank loads - never over a saved tank, so a reset stays a reset.
  The card (FAT, SDMMC 1-bit: CLK 2, CMD 1, D0 3) is mounted only while a
  copy is written. On the glass: settings -> UPDATES -> SD BACKUPS lists
  them newest first (the latest, BEFORE RESTORING, the days); a tap and a
  YES restarts the tank into that copy, the tank as it was kept first as
  PTANK/UNDO.BIN ("before restoring") - one step back is always there.
  Director: `sd` (the copies), `sd backup`, `sd restore [Syymmdd.BIN]`. Bench, 2026-10-04: NVS erased
  under a running tank, the next boot came back with the same fish.
  Brightness and volume live in NVS on their own and are not on the card.
- **Memory**: the frames are 460 KB each (PSRAM); the internal heap runs at
  ~37 KB free with the tank up (49 on the 1.8's frame).
