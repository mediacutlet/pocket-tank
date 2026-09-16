# Track 4 — hardware bring-up checklist (Waveshare ESP32-S3-Touch-AMOLED-1.8)

Everything below was prepared in QEMU / compile-only before the board arrived.
Work top to bottom; each step has a pass signal.

1. **Identify the revision** from the back label. V1 = SH8601 panel + FT3168
   touch; V2 = CO5300 + CST816. The firmware auto-detects (probes CST816 @0x15)
   and applies the V2 x-gap; both use the same init sequence.
2. **Flash Waveshare's stock demo** first (wiki) → display + touch proven.
3. **Toolchain:** `. ~/esp/esp-idf/export.sh` (IDF v5.4.1). Build:
   `cd firmware && idf.py build`. Flash app + model:
   ```
   idf.py -p /dev/cu.usbmodem* flash
   esptool.py --chip esp32s3 -p /dev/cu.usbmodem* write_flash 0x290000 ../model/out/model_q4.bin
   idf.py -p /dev/cu.usbmodem* monitor
   ```
   Pass: log shows `PSRAM plan OK`, `model partition 8192 KB mmap'd, advisor LLM`,
   `sh8601: panel up`, `touch: FT3168 ready`, fish on the glass.
4. **Pins to verify on the bench** (`firmware/main/s3/board_pins.h`): I2C SDA/SCL —
   Waveshare's code says 15/14, the Arduino variant says 14/15. If the expander,
   touch, or RTC fail to probe, swap them. QSPI CS12 CLK11 D0–3 = 4/5/6/7 agree
   across sources.
5. **Power rails:** the IO expander (TCA9554 @0x20) drives LCD_RST (bit0),
   display power (bit1), touch reset (bit2); the firmware sequences them. If the
   panel stays dark, check whether the AXP2101 PMIC needs a rail enabled (see
   Waveshare example `90_axp2101_pmu`).
6. **Orientation:** the tank is rendered landscape 448×368 and rotated 90° in
   software in `display_port_flush`. If the image is mirrored/upside down, flip
   the mapping there (two lines), not the renderer.
7. **Touch mapping:** same rotation in `firmware/main/s3/touch_port_ft3168.c`; tap a corner and
   watch the log, adjust if mirrored.
8. **RTC:** first boot seeds the PCF85063 from build time; later boots read it.
   Power off ≥1 h, power on → fish ravenous until fed (the progression rule).
9. **Measure** (the numbers the video wants): decision latency and tok/s from
   the `advisor:` log lines; render fps from `display:`; heap from the 10 s
   status line. Compare with QEMU (~2.3 s, ~19 tok/s) and docs/memory_budget.md.
10. **Optimize on real silicon:** ESP-DSP SIMD dot products, dual-core matmul
    split — only worth tuning against real bandwidth numbers.

Known-unknowns to expect: flash-cache bandwidth dominates inference; PMIC rail
defaults; whether the AMOLED needs the 0x51 brightness command adjusted (the init
list sets full brightness).
