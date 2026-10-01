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
| IMU | QMI8658 (I2C 0x6B), on the board | none on the board: a QMI8658C or an MPU-6050 (0x68) on the I2C socket, SDA IO16 / SCL IO15, whichever answers at boot - an MPU-6050 now (see *The IMU*) |
| Gestures | upside-down flip, handling (codec warm, the light's idle rule); the PWR key sleeps | the same, plus face down sleeps and face up wakes; BOOT is the sleep key (see *Gestures*) |
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
  Once an IMU answers, the same row is FACE DOWN, SLEEP / IGNORE instead
  (see *The IMU*).

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
- On the I2C bus: 0x18 (the ES8311), 0x38 (the FT6336) and, with the
  breakout fitted, 0x68 (the MPU-6050). The AMOLED's other parts are absent
  and say so at boot: no AXP2101, no QMI8658, no PCF85063.

## The IMU

The firmware's IMU work is all accelerometer, polled four times a second: the
180-degree flip (held while the board lies flat or stands on its side, so it
never flaps), and the handling detector (`moving` keeps the codec warm,
`handled` counts as touching the tank for the light's idle rule). No gyro, no
tap engine, no interrupt, so no INT wire is needed.

**Each chip is a Kconfig line, and both are on for the CYD**
(`sdkconfig.defaults.cyd`): `POCKET_TANK_IMU_QMI8658` and
`POCKET_TANK_IMU_MPU6050`. At boot `imu_port_init` probes the enabled ones in
that order and runs on the first that answers, so the QMI8658C takes over by
itself when it is fitted. The logic is one copy, in `imu_port.c`; each chip is
a driver behind `imu_chip.h` (`imu_qmi8658.c`, `imu_mpu6050.c`). The AMOLED
build has only the QMI8658 line, at the axes it always had.

### Supported IMUs, and wiring one

Either of these, on the CYD's I2C socket. The firmware probes the enabled
ones at boot, in this order, and uses the first that answers:

| chip | I2C address | WHO_AM_I | Kconfig line (CYD default) | notes |
|---|---|---|---|---|
| QMI8658 / QMI8658C | 0x6B, or 0x6A | 0x05 | `POCKET_TANK_IMU_QMI8658` (y) | the AMOLED board's chip; on the CYD its axes need one bench reading (*Still open*) |
| MPU-6050 (GY-521 and the like) | 0x68 (AD0 low), or 0x69 | 0x68; clone dies reporting 0x70, 0x71, 0x72, 0x73 or 0x98 are taken too | `POCKET_TANK_IMU_MPU6050` (y) | what is fitted now; axes measured, in `sdkconfig.defaults.cyd` |

Anything else on the bus is ignored; with neither answering, the boot log
says so and the settings page keeps its SCREEN row.

| IMU breakout | CYD | why |
|---|---|---|
| VCC | **3V3** | never 5 V: GPIO15 and GPIO16 are not 5 V tolerant, and some breakouts pull SDA and SCL up to their VCC |
| GND | GND | |
| SDA | **IO16** | the board's I2C bus, shared with the touch (0x38) and the codec (0x18), at 400 kHz |
| SCL | **IO15** | |
| INT | not connected | nothing polls it; a deep-sleep wake on movement would need it (*Still open*) |
| AD0 / SA0 | as the breakout has it | picks between the two addresses above; both are probed |
| XDA, XCL (MPU-6050) | not connected | its auxiliary bus, unused |

The socket's pin order is not in the board's specification - read it off the
silkscreen, or meter it against the touch controller's lines. No pull-up
resistors were needed for the MPU-6050: the bus enables the chip's internal
ones, and the GY-521 carries its own 4.7 k. A bare QMI8658C with none of its
own may want a pair - check its breakout. **Mount it flat against the CYD's back**, so its Z axis
is the one out of the glass; the up axis and both signs then come from one
upright and one flat reading with the director's `imu`. Proof it is wired:
the boot log's bus scan lists its address (`i2c: device at 0x68`), and
`imu: MPU-6050 up at 0x68` (or `QMI8658 up at ...`) follows.

**The MPU-6050**, a GY-521-style breakout:

- wired as in the table above; it answers at 0x68 (AD0 low).
- accel only at +-2 g, the QMI8658's scale (16384 counts per g), so every
  threshold carries over; a 10 Hz low-pass keeps a still table at a motion
  count of 6-92 against the 220 threshold; gyros in standby; its sleep bit for
  the drowse bracket.
- **mounted flat against the CYD's back, its pins toward the top edge.** The
  axes in `sdkconfig.defaults.cyd` are for that mounting, measured 2026-09-30:
  upright reads X at -0.95 g, flat on its back reads Z (out of the glass). A
  different mounting is one bench reading: stand the board upright, run the
  director's `imu`, and the axis carrying ~16000 counts, with its sign, is
  `POCKET_TANK_IMU_MPU6050_UP_AXIS` / `_UP_NEGATIVE`.
- this breakout's Z reads about 0.79 g at rest. The flip never uses Z.

Measured on the bench with the director's `imu` (2026-09-30): upside down
flipped the picture 0.6 s later, back upright flipped it back, on its side in
either direction changed nothing, and a pick-up read MOVING.

**Face down sleeps the tank** (`POCKET_TANK_IMU_FACE_DOWN_SLEEP`, on for the
CYD). Screen down, level and still for 2 s is a short press of the sleep key:
the tank saves, darkens and light-sleeps. "Still" here is a motion count
under 1000 a poll, not the handling detector's 220: a hand steadying the
board reads 200-800, which kept the gesture from firing on the bench. **Any
sleep that starts face down wakes when it is turned face up** - the
gesture's, or BOOT pressed while it lies there; a BOOT sleep face up keeps
BOOT as its only wake. For those the IMU stays awake through the 20-minute
grace - there are no rails to cycle on the CYD - and each 1 s wake of the
grace reads it once: no longer face down (turned up, or picked up) resumes
in place, as BOOT does. No answer from the IMU keeps it asleep. After
the grace the IMU sleeps and the board deep-sleeps; only BOOT wakes it then
(motion could only with the INT wire). The gesture fires once per lie-down,
so waking it with BOOT while it still lies face down does not put it straight
back to sleep. Face down means the axis out of the glass reads more than
0.5 g toward the table with both in-screen axes under 0.35 g; the sign that
axis reads screen-up is `POCKET_TANK_IMU_MPU6050_OUT_NEGATIVE` (y for the
mounting above: flat, screen up, Z reads -0.79 g; screen down, +1.23 g).

Bench, 2026-09-30, the breakout held flat against the back: face down slept
the tank and face up woke it within a second; BOOT woke it and slept it again
while it lay face down, and that BOOT sleep woke on face up too.

**The settings page's SCREEN row becomes FACE DOWN, SLEEP / IGNORE, once an
IMU answers.** The row was the keeper's way to turn the picture on a board
with no IMU; with one, the IMU turns it, so the row's place goes to the
gesture's switch (SLEEP by default, kept in NVS as `tank/facedn`), and a
SCREEN choice saved before is set aside. With no IMU the row is SCREEN, as
before. The AMOLED's layout has no such row and is unchanged.

### Gestures

How each one is told apart. Every movement reading is the IMU polled at 4 Hz
by `imu_port_poll` (`firmware/main/imu_port.c`), in counts at +-2 g (16384 a
g); the README's *Gestures* table is the keeper's version of this.

| gesture | detected as | effect | where |
|---|---|---|---|
| upside down | the up axis past 0.21 g the other way, and dominant over the other in-screen axis, for 3 polls (~0.75 s) | picture and touch turn 180 degrees | `imu_port.c`, applied per frame in `main.c` |
| flat, or on its side | the up axis not dominant | nothing: the last orientation holds | the same vote |
| picked up, carried | one poll's summed change over 220 (~0.013 g): `moving`, held 1 s | the codec stays warm | `main.c`, `audio_port_prewarm` |
| held | `moving` on two polls in a row: `handled` | counts as attention for the light's idle rule (AUTO) | `tank_handled` |
| screen down, level, still, 2 s (CYD) | out-of-glass axis over 0.5 g toward the table, in-screen axes under 0.35 g, motion under 1000, 8 polls; once per lie-down | sleeps as a BOOT press | `imu_port_take_face_down`, `main.c` |
| screen up / picked up, asleep (CYD) | one read each 1 s slice of the grace: no longer face down | wakes in place - after a sleep that began face down | `imu_port_face_down_now`, `enter_sleep_for` |
| BOOT, short press | the button (GPIO0), at release | sleep; within the 20-minute grace a press wakes in place; after it, deep sleep, and BOOT boots | `sleep_button_poll`, `enter_sleep_for` |
| BOOT held + a tap | a touch landing while BOOT is down | the *Reset tank?* prompt | `sleep_button_poll` |
| double-tap the glass | two quick taps, then a pause (LIGHTS OUT = MANUAL, the default) | the tank light on / off, saved | `tank.c` (`light_manual_off`) |

The face-down rows need `POCKET_TANK_IMU_FACE_DOWN_SLEEP` (on for the CYD)
and FACE DOWN = SLEEP in settings. Deep sleep after the grace hears only
BOOT: waking it on movement needs the IMU's INT line wired (below).

## Still open

- **The MPU-6050 is held, not mounted.** The axes in `sdkconfig.defaults.cyd`
  are for it flat against the back with its pins toward the top edge; solder
  or glue it that way, or re-read the axes (*The IMU*) for the mounting it
  gets. Its power LED draws 1-3 mA, which dominates deep sleep on a cell -
  lift it, or its resistor, if the cell's life matters.
- **The QMI8658C, when it arrives,** is probed first and takes over by
  itself, but its axes are the AMOLED's defaults
  (`POCKET_TANK_IMU_QMI8658_UP_AXIS` / `_UP_NEGATIVE` / `_OUT_AXIS`, and
  `_OUT_NEGATIVE` for face down): one upright and one flat reading with the
  director's `imu` set them for the CYD.
- **Waking from deep sleep on movement** needs the IMU's INT line on an RTC
  GPIO - GPIO21 or GPIO14 on the 4-pin expansion socket (not GPIO3, a
  strapping pin; never GPIO0) - an ext1 wake beside BOOT's ext0, and the
  chip's motion interrupt armed at sleep. Not done: within the 20-minute
  grace, face up already wakes it.
- **Two face-down sleeps stayed dark, BOOT included,** in the first hour of
  bring-up (2026-09-30) and never since, across every later round - BOOT and
  face up, in either order. Not explained. If one recurs, note the steps; the
  task watchdog armed across the sleep, for a backtrace, is the next
  instrument.

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
