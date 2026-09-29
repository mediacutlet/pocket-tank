# The 2.8" ESP32-S3 CYD (ES3C28P)

The tank on a stock "cheap yellow display": the ES3C28P from QDtech, an
ESP32-S3 behind a 2.8-inch IPS panel with capacitive touch. No modifications
to the board - everything here uses it as it comes. The vendor's
specification: [ES3C28P_ES2N28P_Specification_V1.0.pdf](https://www.lcdwiki.com/res/ES3C28P/ES3C28P_ES2N28P_Specification_V1.0.pdf).

## The board, against the Waveshare AMOLED

| | Waveshare 1.8" AMOLED | ES3C28P CYD |
|---|---|---|
| Chip | ESP32-S3, 16 MB flash, 8 MB octal PSRAM | the same (40 MHz crystal) |
| Screen | SH8601 / CO5300 AMOLED over QSPI, 368 x 448, 322 ppi | ILI9341V IPS over 4-wire SPI, 240 x 320, 143 ppi |
| Touch | FT3168 / CST816 | FT6336G (the FT5x06 family, I2C 0x38) |
| Audio | ES8311 + NS4150B | ES8311 (I2C 0x18) + an amp enabled low on GPIO1 |
| Power | AXP2101 PMIC, fuel gauge, PWR key | a charger for a LiPo on its socket; the cell's voltage on GPIO9; no PMIC |
| Orientation | QMI8658 IMU | none - the SCREEN setting instead |
| Clock | PCF85063 RTC | none |

Pins: `firmware/main/board_pins.h`, from section 4.2 of the specification.
The LCD resets with the chip (CHIP_PU), so there is no reset pin to drive.

## Building and flashing

```sh
tools/build_cyd.sh                                     # build only
tools/build_cyd.sh <port> --model                      # the first time: app + the model partition
tools/build_cyd.sh <port>                              # after that: the app
```

`<port>` is `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00`,
never a `/dev/ttyACM<n>`, whose numbers shuffle between plug-ins. The script
uses ESP-IDF 5.5 (`IDF_PATH`, or `~/.espressif/esp-idf/v5.5`), its own build
directory (`firmware/build_cyd`) and its own sdkconfig - `sdkconfig.defaults`
with `sdkconfig.defaults.cyd` on top - so the AMOLED build is untouched.
The defaults only fill in what an sdkconfig lacks, so a changed default would
never reach the existing one; the script starts it afresh whenever a defaults
file is newer.

Back up the factory image before the first flash; the board then goes back
to how it arrived with one `write_flash`:

```sh
esptool --chip esp32s3 -p <port> -b 921600 read_flash 0 0x1000000 factory_16MB.bin
```

## What the port changes

**The tank's size is the board's.** `common/tank.h` takes `TANK_W` x `TANK_H`
from Kconfig (`CONFIG_POCKET_TANK_BOARD_CYD28` = 320 x 240); `make CYD=1` in
`sim/` builds the simulator at that size (`fishsim-cyd`) for previews. The
tank scene lays itself out from those two already.

**The pages scale, the pixel art does not.** `common/ui.h`: `UI(n)` is a length
designed at 368 px of height and `UI_TEXT(s)` a text scale, both scaled to
the build's height - identities on the AMOLED, about 0.65 on the CYD. At 143
ppi against 322 a page at 0.65 of the pixels still stands a little larger in
the hand than the original. Icons are drawn a pixel at a time and keep their
size; where they would not fit, the page is laid out for the short tank
instead (`UI_COMPACT`):

- the **stats card** goes to two columns, the needs down the left and the
  traits and MORE down the right (its 24 px and 16 px icons cannot shrink);
- the **milestones page** uses 24 px copies of the 32 px badges, which
  `tools/gen_icons.py` box-filters from the same art, and keeps every row on
  one screen; the detail modal keeps the full size;
- the **shop** takes the 32 px coin for its 64 px one;
- **settings** gains a SCREEN row, UPRIGHT or FLIPPED, for a board with no
  IMU to turn the picture (`firmware/main/orientation.c`, kept in NVS beside
  the brightness, and re-saved after a tank reset as the brightness is).

Every page of the AMOLED build renders pixel-for-pixel as it did before the
port: the sim's `--snapshot` set (69 pages) was compared before and after.

**The stats card cannot write past the frame.** It is copied into the frame
row by row with no clipping; at 258 px tall on a 240 px frame it wrote past
the end of the framebuffer, and a static assert in `render.h` now stops any
card that does not fit.

**The model's distance bands scale with the width.** The encoder sorts
distances into near, mid and far at 70, 180 and 380 px of the 448-wide tank
it was trained on; they now scale by `TANK_W / 448`, so a 320-wide tank asks
about distances the model knows.

**A save file is shaped by its tank.** It carries the algae grid, whose size
follows the tank, so the migration for the first installer's 1432-byte saves
applies to the 448 x 368 build alone.

**Display:** the ILI9341 scans landscape through MADCTL (`swap_xy` and the
mirrors), so a frame goes out row by row, only byte-swapped, from two
ping-pong DMA stripes of 20 rows; 40 MHz SPI. The backlight is LEDC PWM on
GPIO45; the flip is the same register with both mirrors toggled.

**Audio:** the same ES8311 on the CYD's own I2S pins. The pins and the level
that turns the amplifier on are the board's (`board_pins.h`): the CYD's is
enabled low on GPIO1, so the port sets it off before the pin becomes an
output and holds it high through deep sleep. No PMIC switches the codec's
analog supply here; the port's call to do so finds none and does nothing.

**Clock:** there is no RTC chip. ESP-IDF's system time runs on through deep
sleep on the chip's own RTC timer, so with no chip answering, a plausible
system time is kept and only a power-on is seeded from the build time. A
night in deep sleep is lived through at the wake, as on the AMOLED; without
this the re-seed put the clock behind the save's stamp and the sleep counted
as nothing.

**Touch:** the FT5x06 driver with the reset on GPIO18. On the bench the panel
reads turned 180 degrees from the picture, and the AMOLED's 10 px
finger-landing correction made every miss land above its button, so it is
0 here.

## Measured (2026-09-26, the first board)

- 17.5 to 21 fps at 40 MHz SPI: 5 to 14 ms to render a frame (the scene
  decides), 30 ms to send it.
- **80 MHz does not work on this panel.** The firmware sent a frame in 16 ms
  (29 to 32 fps), but the glass showed stripes and no picture; back at 40
  it is right again. The bus has no step between the two: the clock divides
  an 80 MHz source.
- The model: 12.7 tokens a second, 3.5 s a decision - as on the AMOLED board.
- Sound, on a speaker on the board's socket: the codec and amplifier up
  255 ms after the first touch, down 5 s after the last sound; 17 of the 24
  cues are in the bank.
- Sleep on BOOT: a short press darkens the tank and light-sleeps it; a press
  within 20 min resumes in place (a 6 s nap, the fish where they were). Past
  that it deep-sleeps, and BOOT - or the director's timer - wakes it with a
  boot that puts the fish back and lives the time through (240 s asleep came
  back as 0.1 h).
- On the I2C bus: 0x18 (the ES8311) and 0x38 (the FT6336). The AMOLED's
  other parts are absent and say so at boot: no AXP2101, no QMI8658, no
  PCF85063.

## Still open

- **Battery.** The cell's voltage reaches GPIO9 through the board's divider,
  whose ratio is still to be measured. Without a meter the battery pill and
  its page stay hidden (the page is still laid out for 448 x 368).
- **A power cut, or the RESET button, loses the time** (it resets the chip's
  RTC timer too): the clock starts again from the build time, and that
  absence is not lived through. Deep sleep keeps it.
- **Sending while drawing.** A frame is drawn and then sent, one after the
  other; sending it while the next one is drawn would lift the ceiling at
  40 MHz to about 33 fps.
- **The decorations** keep their pixel sizes: the castle is 146 px tall in a
  240 px tank. Worth a look on the glass.
