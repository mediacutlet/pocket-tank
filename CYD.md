# The 2.8" ESP32-S3 CYD (ES3C28P)

This is LM's fork of [pocket-tank](https://github.com/mediacutlet/pocket-tank).
It runs the same tank, the same model and the same game on a stock "cheap
yellow display" as well: the ES3C28P from QDtech, an ESP32-S3 behind a
2.8-inch IPS panel with capacitive touch. No modifications to the board -
everything here uses it as it comes. The vendor's specification:
[ES3C28P_ES2N28P_Specification_V1.0.pdf](https://www.lcdwiki.com/res/ES3C28P/ES3C28P_ES2N28P_Specification_V1.0.pdf).

Upstream's three Waveshare boards build from this tree as before, and
[README.md](README.md) is upstream's own, with one block at its top that
points here. This page is the CYD owner's: what the fork changes, how the
tank is used on the CYD's glass, how to build and flash it, the board and
what the port changed, and how the fork stays in step with upstream.

A new board shows the vendor's demo until it is flashed: back it up and
flash it first (*Building and flashing*), then *Using it*.

## What this fork changes

Against upstream's main (`git log upstream/main..main`):

- **A fourth board.** `CONFIG_POCKET_TANK_CYD_320X240` builds a
  320 x 240 tank for the ES3C28P: an ILI9341 display port over SPI, the
  FT6336 touch, the ES8311 on the CYD's own pins. It is built and flashed by
  cable with `tools/build_cyd.sh` (*Building and flashing*).
- **Pages for a 240 px glass.** `common/ui.h` scales the pages to the
  page the build draws; the stats card goes to two columns, the milestones page
  takes 24 px badges, and the shop and settings span the whole glass
  (*What the port changes*).
- **Two settings rows of the 320 x 240 boards' own.** SCREEN turns the
  picture on a board with no IMU and becomes ROTATION when one answers;
  SLEEP (NEVER / SCREEN / LIGHT) chooses what every way into sleep does
  (*Settings*).
- **A sleep that never deep-sleeps.** Two sleep modes upstream does not
  have, screen and lightsleep, darken the tank and wake it on a touch, a
  pick-up or BOOT; both 320 x 240 boards ship on lightsleep (*Sleep*).
- **A second IMU.** An MPU-6050 beside the QMI8658, each a Kconfig line,
  probed at boot. The CYD has neither on the board: a breakout goes on its
  I2C socket, and laying the board face down then sleeps the tank
  (*The IMU*).
- **What the CYD goes without.** No updates over Wi-Fi and no UPDATES
  button, no battery pill or page, no PWR key, no clock chip (*What the CYD
  does not have*).
- **Two rules scaled to the smaller tank, on the 320 x 240 boards only.**
  The model's distance bands scale with the tank's width, and the algae
  films the glass at the 1.8's pace per cell (*What the port changes*).
- **The simulator's fourth world.** `make -C sim 320X240=1` builds
  `fishsim-320x240`, and `make -C sim check-all` runs the selftests in
  all four worlds.
- **A second 320 x 240 board.** `CONFIG_POCKET_TANK_WST_320X240` builds the
  same tank for the Waveshare ESP32-S3-Touch-LCD-2, contributed by adampog
  (*The Waveshare ESP32-S3-Touch-LCD-2*). Both boards select
  `CONFIG_POCKET_TANK_320X240`, the part every 320 x 240 board shares; each
  keeps its own pins and chips under its own name.

What reaches upstream's boards: the IMU code split into one driver per chip
(`imu_port.c`, `imu_qmi8658.c`, `imu_mpu6050.c`), the sleep-mode choice in
Kconfig (deepsleep, their default, which is upstream's behaviour), and the
touch hooks. Their simulator pages are byte for byte upstream's; their
firmware builds, and has been read against upstream's, not run on one of
those boards.

## Using it

Day to day on the glass the CYD is the tank README.md describes under
[*The living tank*](README.md#the-living-tank): the same fish, cards,
badges, shop and chores. What
follows is how to reach each thing on this board, and where it differs.

### Finding the pages

- **Tap a fish** for its stats card at the top left: the needs down the
  left, the traits and MORE down the right, and the toolbox under it. The
  card goes by itself after 10 s; a tap on empty glass closes it.
- **Tap the card, or its MORE,** for the milestones page: a row per fish,
  the TANK row with the sand dollar at its left and your balance under the
  coin, and three buttons at the foot:
  SETTINGS, UPGRADES and CLOSE.
- **SETTINGS**, at the bottom left of the milestones page, opens the
  settings page. **UPGRADES**, or the sand dollar, opens the shop.
- **CLOSE** on the settings page or the shop goes back to the milestones
  page; CLOSE on the milestones page goes back to the tank.

So settings are three taps away: a fish, its card, SETTINGS.

### Feeding

Tap the water's surface - the top 26 px of the tank, about 5 mm of the
CYD's glass - or drag down from the top edge, and pellets drop where the
finger is. Not with a tool in hand. With AUTO FEED on, the tank also drops a
pellet when somebody is really hungry; with it off every meal is yours.

### The light

Two quick taps on the glass, then a pause, turn the tank light off and on,
while LIGHTS OUT is on DOUBLE-TAP, the default. Step the row's arrows to a
time, from 5 SEC to 30 MIN, and the tank goes dark by itself after that long
without being handled - a touch, or with an IMU fitted, being held - and the
double-tap no longer works the light. The light is the tank's day and night,
not the board's sleep (*Sleeping and waking*).

Three quick taps spook the fish near the finger. A finger held still on the
glass for three seconds brings over the fish that trust you.

### Wiping, trimming and the toolbox

With bare hands one stroke can do both chores: a stroke across the glass
wipes the algae off it, and a sideways stroke that starts on the grass
trims the fronds it crosses.

Under a fish's stats card are two tools for doing only one. Tap the sponge
and strokes only wipe algae; tap the scissors and they only trim grass.
While a tool is in hand the glass takes its strokes and nothing else; DONE
at the top left puts it back, and it goes back by itself after two minutes
without a stroke.

### Renaming or selling a fish

On the milestones page, tap a fish's name for its card: its name, its
stage, what it is worth, and RENAME and SELL.

- **RENAME** opens the letter wheel: swipe a letter up or down, DONE keeps
  the name, CANCEL leaves it as it was.
- **SELL** turns into the price and OK?, and the worth line into TAP AGAIN
  TO SELL; a second tap sells the fish for sand dollars. A tank keeps at
  least two fish, so with two the button is dim.

### The shop

UPGRADES on the milestones page, or the sand dollar on its TANK row. A row
per item with its price and UNLOCK, lit once you can afford it and dim until
then, or IN TANK once you own it; the arrows at the top right turn to the
second page, and HOW TO EARN lists what pays.

Tap a row for the item's card: its picture, its words, its price, and
UNLOCK, which buys it. A tap anywhere else closes the card.

A plant or a decoration you buy comes up on its placement page, PLACE THE
<ITEM>: drag it left or right along the floor, pick its DEPTH (BEHIND,
AMONG or IN FRONT; some pieces offer two of the three), and DONE at the top
right keeps it there. The coral adds a COLOR row and the reef cluster a
LOOK row.

To move or sell it later, hold a still finger on the piece in the tank for
a moment (0.7 s), and its placement page opens with SELL at the top left;
or tap its row in the shop, whose card has MOVE and SELL. SELL takes two
taps: the first shows the refund and OK?, the second sells.

### Settings

| row | choices | what it does |
|---|---|---|
| BRIGHTNESS | 30% / 60% / 100% | the backlight, kept in NVS |
| VOLUME | OFF / QUIET / NORMAL | the sound, kept in NVS. "FISH ARE QUIET AT NIGHT" under it is a note, not a row |
| LIGHTS OUT | DOUBLE-TAP, or 5 SEC to 30 MIN | DOUBLE-TAP: the double-tap works the tank light. A time: the light goes out by itself after that long unhandled (*The light*) |
| AUTO FEED | ON / OFF | ON: the tank feeds a fish that is really hungry. OFF: only you feed; a fish left starving in a lit tank slowly loses trust |
| SCREEN | UPRIGHT / FLIPPED | with no IMU answering: turns the picture and the touch 180 degrees, kept in NVS |
| ROTATION | one padlock button, with LOCKED or UNLOCKED beside it | in SCREEN's place once an IMU answers. Tap the padlock to lock the picture the way up it is now; tap it again to let it follow the board turned over |
| SLEEP | NEVER / SCREEN / LIGHT | what every way into sleep does, on every 320 x 240 board: NEVER ignores it, SCREEN darkens the glass with the chip awake, LIGHT darkens it and light-sleeps. LIGHT is the factory default; a choice is kept in NVS and applies from the next sleep (*Sleeping and waking*) |

At the foot: the release and the build id, small and dim ("V0.3.3 ALPHA
BUILD ...", cut short of CLOSE), and CLOSE. There is no UPDATES button.

### Sleeping and waking

What puts the tank to sleep:

- a short press of **BOOT** (at its release);
- **laying it face down**, level and still, for 2 s - with an IMU fitted;
- the director's `deepsleep [N]` and `poweroff`, over the serial port
  (*The serial console (the director)*).

What happens is the SLEEP row's: with NEVER nothing does, with SCREEN or
LIGHT the tank saves, the backlight and the sound go off, and the glass is
dark for as long as it takes. It never deep-sleeps. In the board's case BOOT
cannot be reached, so with no IMU fitted nothing on the glass puts the tank
to sleep.

What wakes it, where it was, fish and all:

- **a touch** on the glass. That touch does nothing in the tank;
- **a pick-up or a tilt**, with an IMU, once it has lain still for about a
  second after going dark;
- **turning it face up**, in place of a touch or a pick-up, if it went
  dark lying face down;
- **BOOT**.

The time it was dark is lived through at the wake, as a night is: the fish
grow at a quarter of the pace, get hungry, the grass and the algae grow. On
LIGHT with a USB host attached it stays awake instead of light-sleeping, so
on the bench LIGHT behaves as SCREEN. Nothing takes it back into the dark by
itself: a wake nobody meant leaves it lit (*Still open*).

### Gestures, in short

On the glass, with no tool in hand:

| gesture | what the tank does |
|---|---|
| tap a fish | its stats card; tap the card for the milestones page |
| tap the surface (the top 26 px), or drag down from the top edge | pellets drop where the finger is |
| a stroke across the glass | wipes the algae it crosses |
| a sideways stroke that starts on the grass | trims the fronds it crosses |
| two quick taps, then a pause | the tank light on or off (LIGHTS OUT on DOUBLE-TAP, the default) |
| three quick taps | the fish near the finger bolt and stay spooked |
| a finger held still for about three seconds | the fish that trust you come over |
| a finger held still on a decoration you own, 0.7 s | its placement page, with SELL |
| a swipe along the milestones page's TANK row | the next page of the tank's badges, when it has more than one |

The movement gestures need an IMU. The CYD has none on the board: a
QMI8658 or an MPU-6050 breakout on its I2C socket (VCC 3V3, GND, SDA IO16,
SCL IO15), detected at boot - which chips, and the wiring pin by pin, in
*Supported IMUs, and wiring one*. Turning, holding still and handling
are the AMOLED board's too; face down is the CYD's. Moving it, and BOOT:

| gesture | what the tank does |
|---|---|
| turn it upside down, about 0.75 s | the picture and touch turn 180 degrees to stay readable; turn it back and they follow |
| lay it flat, or stand it on its side | nothing - it keeps the way it was, so it never flaps on a table |
| pick it up, carry it | the sound stays warm, and holding it counts as attention for the tank light |
| lay it screen down, level and still, 2 s | it saves, darkens and sleeps, as a short press of BOOT |
| turn it screen up, or pick it up | it wakes where it was, fish and all |
| touch the glass while it sleeps | it wakes where it was; that touch does nothing in the tank (not while it lies face down) |
| short press of BOOT | sleeps the tank; another press wakes it in place (face down or not) |
| hold BOOT and tap the glass | the *Reset tank?* prompt |

The SLEEP row decides what every way into sleep does, face down included.
How each gesture is told apart: *Gestures* under *The IMU*.

### Starting over

Hold BOOT and tap the glass: a *Reset tank?* prompt comes up over the water
with NO and YES. YES wipes the save, and two new fry take the tank with the
first-run setup to name them; NO, a sleep, or twenty seconds with no answer
keep everything.

YES erases the tank's whole NVS namespace. BRIGHTNESS, SCREEN and SLEEP are
saved again at once, so they stay as they were. LIGHTS OUT, AUTO FEED and
the ROTATION lock live in the save and go back to their defaults. VOLUME
keeps sounding as it was until the next restart, and is NORMAL after it.

BOOT cannot be reached in the board's case: take it out, or erase the save
over the cable, which keeps the app and the model (*Recovering a board*).

### What the CYD does not have

- **No updates over Wi-Fi.** The settings page has no UPDATES button, and
  the tank never restarts into update mode or asks for a network; nor does
  it take the time from the internet, which upstream does only on a board
  with a PMIC. A new version goes on by cable (*Building and flashing*).
- **No battery pill or battery page.** They need the AMOLED's fuel gauge
  (the AXP2101); with none answering the tank never shows them, and the
  low-battery notice never comes. The cell's voltage reaches GPIO9, but
  nothing reads it yet (*Still open*).
- **No PWR key.** BOOT is the sleep key.
- **No clock chip.** The time runs on through sleep; a power cut or the
  RESET button loses it (*Still open*).

## Building and flashing

The browser installer is for upstream's boards only; the CYD is built and
flashed from this tree with ESP-IDF 5.5 (`IDF_PATH`, or
`~/.espressif/esp-idf/v5.5`). `tools/build_cyd.sh` runs on Linux: it
flashes only through a `/dev/serial/by-id/` path, which other systems do
not have. Elsewhere, build with the script and no port, then run its last
`idf.py` line by hand with your own port.

**Find the port.** With the board plugged in, `ls /dev/serial/by-id/`
lists it as
`usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00`; `<port>` below is
that whole path. Never a `/dev/ttyACM<n>`, whose numbers shuffle between
plug-ins; the script refuses anything that is not a by-id path.

**Back up the factory image first**, before the first flash; with it the
board goes back to how it arrived (*Recovering a board*):

```sh
. "${IDF_PATH:-$HOME/.espressif/esp-idf/v5.5}/export.sh"   # esptool.py comes with ESP-IDF
esptool.py --chip esp32s3 -p <port> -b 921600 read_flash 0 0x1000000 factory_16MB.bin
```

**Then build and flash:**

```sh
tools/build_cyd.sh                                     # build only
tools/build_cyd.sh <port> --model                      # the first time: the app and the 8 MB model partition
tools/build_cyd.sh <port>                              # after that: the app
```

The first boot after the first flash opens the first-run setup, the
welcome and the names for the two fry, as README.md's
[*The living tank*](README.md#the-living-tank) describes under *First run*.

The script uses its own build directory (`firmware/build_cyd`) and its own
sdkconfig - `sdkconfig.defaults` with `sdkconfig.defaults.cyd` on top - so
the AMOLED build is untouched, and it flashes nothing when the build fails.
The defaults only fill in what an sdkconfig lacks, so a changed default
would never reach the existing one; the script starts it afresh whenever a
defaults file is newer.

Every build is signed, as upstream's are, with
`firmware/keys/ota_signing_key.pem` (never committed; `firmware/keys/` is
not in a clone). `tools/ota_key.sh` makes one if it is missing; it loads
ESP-IDF from `~/esp/esp-idf`, so with ESP-IDF elsewhere source its
`export.sh` and run the script's own three lines:

```sh
mkdir -p firmware/keys
espsecure.py generate_signing_key --version 2 --scheme rsa3072 firmware/keys/ota_signing_key.pem
chmod 600 firmware/keys/ota_signing_key.pem
```

The CYD takes no updates over the air, so which key signs its image does
not matter to it.

**Updating keeps the tank**, as on the AMOLED board: the script writes the
bootloader, the partition table, the app and a fresh otadata (`0xA90000`,
which app slot boots), and the save in NVS (`0x9000`, 24 KB) is left
alone. The model partition is written only with `--model`.

### The serial console (the director)

The firmware reads commands on the same USB port its log goes out on.
`tools/director.py -p <port> <command>` sends one and prints the reply;
`tools/director.py -p <port> help` lists them all, and
`tools/director.py -p <port> --watch` streams the log. Opening the port
this way does not restart the board. The ones a CYD owner uses:

- `imu` - eight raw IMU readings, a quarter second apart: the way to set a
  new IMU's axes (*The IMU*).
- `deepsleep [N]` and `poweroff` - put the tank to sleep as BOOT does; with
  `N`, it wakes by itself after `N` seconds.
- `clock` - the tank's time and where it came from.

The boot log, with its I2C bus scan, shows only from a restart. From
`firmware/`, the script's own `idf.py` line with `monitor` in place of
`flash` restarts the board and shows it from the first line; Ctrl+] leaves:

```sh
idf.py -B build_cyd -D SDKCONFIG="build_cyd/sdkconfig" -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.cyd" -p <port> monitor
```

### Recovering a board

Each of these needs ESP-IDF's environment (the `export.sh` line above).

- **Back up the save** before any flash you are unsure of, and put it back
  if the tank is lost. `firmware/partitions.csv` puts NVS at `0x9000`,
  `0x6000` long, and it never moves:

  ```sh
  esptool.py --chip esp32s3 -p <port> -b 921600 read_flash 0x9000 0x6000 nvs.bin
  esptool.py --chip esp32s3 -p <port> write_flash 0x9000 nvs.bin
  ```

- **Start a fresh tank without BOOT** by erasing the save alone; the app and
  the model stay, and the next boot opens the first-run setup. Brightness,
  the SCREEN row and the SLEEP row go with it:

  ```sh
  esptool.py --chip esp32s3 -p <port> erase_region 0x9000 0x6000
  ```

- **Start over from a blank board**: erase all of it and flash the model
  again:

  ```sh
  esptool.py --chip esp32s3 -p <port> erase_flash
  tools/build_cyd.sh <port> --model
  ```

- **Put the factory image back:**

  ```sh
  esptool.py --chip esp32s3 -p <port> -b 921600 write_flash 0 factory_16MB.bin
  ```

- **No port, or a board that restarts over and over:** take the board out
  of its case, hold BOOT, press and release RESET, then release BOOT. The
  chip waits in its ROM's download mode, where `esptool.py` reaches it
  whatever the flash holds; the by-id path is the same. Press RESET again
  after flashing to run the new image.

### The simulator

The simulator runs the CYD's world on the desktop, the same `common/` code
at 320 x 240, for previews and the selftests:

```sh
make -C sim 320X240=1                       # builds sim/fishsim-320x240
make -C sim 320X240=1 check                 # its selftests
```

README.md's [*Try it: PC simulator*](README.md#try-it-pc-simulator) has the
rest: what it needs, the keys and the flags.

## The board, against the Waveshare AMOLED

| | Waveshare 1.8" AMOLED | ES3C28P CYD |
|---|---|---|
| Chip | ESP32-S3, 16 MB flash, 8 MB octal PSRAM | the same (40 MHz crystal) |
| Screen | SH8601 / CO5300 AMOLED over QSPI, 368 x 448, 322 ppi | ILI9341V IPS over 4-wire SPI, 240 x 320, 143 ppi |
| Touch | FT3168 / CST816 | FT6336G (the FT5x06 family, I2C 0x38) |
| Audio | ES8311 + NS4150B | ES8311 (I2C 0x18) + an amp enabled low on GPIO1 |
| Power | AXP2101 PMIC, fuel gauge, PWR key | a charger for a LiPo on its socket; the cell's voltage on GPIO9; no PMIC |
| IMU | QMI8658 (I2C 0x6B), on the board | none on the board: a QMI8658C or an MPU-6050 (0x68) on the I2C socket, SDA IO16 / SCL IO15, whichever answers at boot - an MPU-6050 now (see *The IMU*) |
| Gestures | upside-down flip, handling (codec warm, the light's idle rule); the PWR key sleeps | the same, plus face down sleeps and face up wakes; BOOT is the sleep key; asleep, a touch or a pick-up wakes it (see *Gestures*, *Sleep*) |
| Clock | PCF85063 RTC | none |

Pins: `firmware/main/board_pins.h`, from section 4.2 of the specification.
The LCD resets with the chip (CHIP_PU), so there is no reset pin to drive.

## What the port changes

**The tank's size is the board's.** `common/tank.h` takes `TANK_W` x `TANK_H`
from Kconfig: `CONFIG_POCKET_TANK_320X240` = 320 x 240, the part every
320 x 240 board shares, which the board's own choice selects
(`CONFIG_POCKET_TANK_CYD_320X240` here; named by the resolution because CYDs
come in several, and other boards have it too). `make 320X240=1` in `sim/`
builds the simulator at that size (`fishsim-320x240`) for previews. The
tank scene lays itself out from those two already.

**The pages scale, the pixel art does not.** `common/ui.h`: `UI(n)` is a length
designed at 368 px of page height and `UI_TEXT(s)` a text scale, both scaled
to the page the build draws - identities on the AMOLED, about 0.65 on the
CYD. At 143 ppi against 322 a page at 0.65 of the pixels still stands a
little larger in the hand than the original. Icons are drawn a pixel at a time and keep their
size; where they would not fit, the page is laid out for the short tank
instead (the CYD's blocks in `common/render.h` and `common/render.c`):

- the **stats card** goes to two columns, the needs down the left and the
  traits and MORE down the right (its 24 px and 16 px icons cannot shrink);
- the **milestones page** uses 24 px copies of the 32 px badges, which
  `tools/gen_icons.py` box-filters from the same art, and keeps every row on
  one screen; the detail modal keeps the full size;
- the **shop** takes the 32 px coin for its 64 px one;
- **settings** gains a SCREEN row, UPRIGHT or FLIPPED, for a board with no
  IMU to turn the picture (`firmware/main/orientation.c`, kept in NVS beside
  the brightness, and re-saved after a tank reset as the brightness is).
  Once an IMU answers, the same row is ROTATION instead (see *The IMU*).
  Under it, IMU or not, a **SLEEP** row - NEVER / SCREEN / LIGHT - picks
  what every way into sleep does (see *Sleep*); a deepsleep build has none.

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

**The algae grows at the 1.8's pace per cell.** Its steps were tuned on the
1.8's 28 x 23 = 644 cells of glass; the CYD's is 20 x 15 = 300. A step
claims or thickens one cell, so at the same pace per step the CYD's film
covered its glass twice as fast, and a night filmed it to the cap. On the
CYD a step comes once per 644 / 300 of the time instead, so a night lands
near DIRTY there too (`common/tank.c`, `ALGAE_STEP_*`).

**A save file is the same on every board.** Since v0.3.3 every board keeps
the 1.8's 644 algae cells, the CYD's 300 among them. A save the CYD wrote
before that - 1312, 1320 or 1328 bytes, lengths no other build ever wrote -
is moved into place on load (`common/progression.c`).

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
system time is kept and a power-on is seeded from the save's stamp, or
from the build time if that is later. A night in deep sleep is lived
through at the wake, as on the AMOLED; without this the re-seed put the
clock behind the save's stamp and the sleep counted as nothing.

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
- Sleep on BOOT, in the deepsleep build (then the only mode): a short
  press darkens the tank and light-sleeps it; a press within 20 min
  resumes in place (a 6 s nap, the fish where they were). Past that it
  deep-sleeps, and BOOT - or the director's timer - wakes it with a
  boot that puts the fish back and lives the time through (240 s asleep came
  back as 0.1 h).
- On the I2C bus: 0x18 (the ES8311), 0x38 (the FT6336) and, with the
  breakout fitted, 0x68 (the MPU-6050). The AMOLED's other parts are absent
  and say so at boot: no AXP2101, no QMI8658, no PCF85063.

## Sleep

**The 320 x 240 boards as shipped never deep-sleep** (2026-10-08). The dark
was made for the CYD: in its case BOOT cannot be reached, and deep sleep
hears nothing else, so after the 20-minute grace the tank stayed dark for
good. Since the same day it is every 320 x 240 board's, part of
`POCKET_TANK_320X240` - the Touch-LCD-2's too, which until then slept as
upstream's boards do, BOOT its only way back (*The Waveshare
ESP32-S3-Touch-LCD-2*). What sleep does is the sleep mode, and every
way into sleep goes through it: BOOT's short press, the face-down gesture,
the director's `deepsleep [N]` and `poweroff`, and the PWR key on a board
with a PMIC.

On a 320 x 240 board the mode is the keeper's: the settings page's
**SLEEP** row (NEVER / SCREEN / LIGHT, 2026-10-08) changes it at run time,
kept in NVS as `tank/sleep` and re-saved after a tank reset if the keeper chose one (the
build's default is never written). The Kconfig choice,
`POCKET_TANK_SLEEP_MODE`, is only the factory default - what the tank does
until a segment is tapped; the boot log says which of the two it is.
deepsleep is a build choice only: a 320 x 240 board built for it has no
SLEEP row and sleeps as deepsleep always has, the face-down gesture
included.

| mode | Kconfig (on a 320 x 240 board, the row's factory default) | SLEEP row | sleep is |
|---|---|---|---|
| none | `POCKET_TANK_SLEEP_NONE` | NEVER | nothing: one log line says what asked and that it was ignored |
| screen | `POCKET_TANK_SLEEP_SCREEN` | SCREEN | the dark, with the CPU running |
| lightsleep | `POCKET_TANK_SLEEP_LIGHT` (the CYD's and the Touch-LCD-2's, `sdkconfig.defaults.cyd` and `.wst`) | LIGHT | the dark, light-sleeping between looks; never deep sleep |
| deepsleep | `POCKET_TANK_SLEEP_DEEP` (the default, so the AMOLED's) | no row | the grace, then deep sleep or the PMIC power-off, as it always was |

screen and lightsleep are offered on the 320 x 240 boards alone. The
AMOLED's panel sleep holds its touch controller in reset and cuts the
panel's rails, so a dark there could not wake on a touch, and cycling those
rails beside an awake IMU is what railed its X and Z on 2026-08-31.

**The dark** (`enter_dark`, `firmware/main/main.c`): the tank saves, the
backlight and the sound go off, the tank stops drawing, and ten times a
second it looks for a wake, for as long as it takes. It lights again where
it was on:

- **a touch** - a finger on the glass, once the glass has been seen without
  one. The finger on the glass at the wake does nothing in the tank.
- **a pick-up or a tilt** (below).
- **face up**, in place of a touch or motion, in a dark begun face down: lying on its glass
  the table could be the finger, and a knock read as motion would light it
  face down, where the gesture, already spent, could not darken it again.
  For the same reason face up counts on two reads in a row, 0.2 s apart:
  one read out of the face-down band can be a knock.
- **BOOT**, the PWR key, or the director's timer (`deepsleep N`).

The dark is lived through at the wake as a deep sleep is - growth at a
quarter, the full-night badge - and counted once. One log line going dark
(what asked, the mode, what will wake it) and one waking (what woke it,
after how long). In lightsleep the chip light-sleeps between looks, woken
early by BOOT or by the touch controller's INT - GPIO17 on the CYD, GPIO46
on the Touch-LCD-2 - armed only while it reads high; while a USB host is
attached it stays awake instead, because light sleep takes the USB port
down - so on the bench lightsleep behaves as screen, and the log keeps
flowing.

Light sleep isolates every pin not armed as a wake, so each board names
the outputs the dark keeps driven through it, and why, in
`firmware/main/board_pins.h` (`BOARD_DARK_HELD_PINS`): on the CYD the
touch controller's reset, the amplifier's enable and the backlight; on the
Touch-LCD-2 the backlight and the TF card's select. A pin a board does not
have is not listed - the Touch-LCD-2's LCD and touch reset is a net with
its own pull-up and no GPIO, so it keeps its level with nothing held.

**A pick-up or a tilt:** the IMU stays awake through the dark and is read
five times a second. The board must first lie still - four reads in a row,
each within 220 counts of the one before, about a second; a hand holding it
reads hundreds, so a tank darkened in the hand settles only once it is put
down - and that pose is the rest pose. Off it by more than 2500 counts
(~0.15 g, about 9 degrees of tilt), summed over the axes, on two reads in a
row, wakes it; a knock on the table is one read. **Provisional**: not yet
tuned on the glass (`REST_*` in `firmware/main/imu_port.c`).

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
CYD). Screen down, level and still for 2 s is a short press of the sleep key,
and what follows is the SLEEP row's: on LIGHT, the default, the tank saves,
darkens and light-sleeps. "Still" here is a motion count
under 1000 a poll, not the handling detector's 220: a hand steadying the
board reads 200-800, which kept the gesture from firing on the bench. **Any
sleep that starts face down wakes when it is turned face up** - the
gesture's, or BOOT pressed while it lies there; a BOOT sleep face up keeps
BOOT as its only wake (in the deepsleep mode; the dark also wakes on a
touch or a pick-up). For those the IMU stays awake - there are no rails to
cycle on the CYD - and is read as the tank sleeps: no longer face down
(turned up, or picked up) resumes in place, as BOOT does. No answer from the
IMU keeps it asleep. In the deepsleep mode that lasts the 20-minute grace,
read once a second; then the IMU sleeps and the board deep-sleeps, and only
BOOT wakes it (motion could only with the INT wire). The gesture fires once
per lie-down, so waking it with BOOT while it still lies face down does not
put it straight back to sleep. Face down means the axis out of the glass
reads more than 0.5 g toward the table with both in-screen axes under
0.35 g; the sign that
axis reads screen-up is `POCKET_TANK_IMU_MPU6050_OUT_NEGATIVE` (y for the
mounting above: flat, screen up, Z reads -0.79 g; screen down, +1.23 g).

Bench, 2026-09-30, the breakout held flat against the back: face down slept
the tank and face up woke it within a second; BOOT woke it and slept it again
while it lay face down, and that BOOT sleep woke on face up too.

**The settings page's SCREEN row becomes ROTATION once an IMU answers.** The
row was the keeper's way to turn the picture on a board with no IMU; with
one, the IMU turns it, so the row is ROTATION, and a SCREEN choice saved
before is set aside. With no IMU the row is SCREEN, as before. The gesture
has no switch of its own any more (FACE DOWN, SLEEP / IGNORE, until
2026-10-08): it does what the SLEEP row says, and NEVER ignores it as it
does every other way into sleep (*Sleep*). The AMOLED's layout has neither
row and is unchanged.

### Gestures

How each one is told apart. Every movement reading is the IMU polled at 4 Hz
by `imu_port_poll` (`firmware/main/imu_port.c`), in counts at +-2 g (16384 a
g); *Gestures, in short* is the keeper's version of this.

| gesture | detected as | effect | where |
|---|---|---|---|
| upside down | the up axis past 0.21 g the other way, and dominant over the other in-screen axis, for 3 polls (~0.75 s) | picture and touch turn 180 degrees | `imu_port.c`, applied per frame in `main.c` |
| flat, or on its side | the up axis not dominant | nothing: the last orientation holds | the same vote |
| picked up, carried | one poll's summed change over 220 (~0.013 g): `moving`, held 1 s | the codec stays warm | `main.c`, `audio_port_prewarm` |
| held | `moving` on two polls in a row: `handled` | counts as attention for the light's idle rule (LIGHTS OUT set to a time) | `tank_handled` |
| screen down, level, still, 2 s (CYD) | out-of-glass axis over 0.5 g toward the table, in-screen axes under 0.35 g, motion under 1000, 8 polls; once per lie-down | sleeps as a BOOT press | `imu_port_take_face_down`, `main.c` |
| screen up / picked up, asleep (CYD) | no longer face down on two reads in a row, 0.2 s apart (one read, each 1 s of the deepsleep mode's grace) | wakes in place - after a sleep that began face down | `imu_port_face_down_now`, `enter_dark` |
| picked up or tilted, dark (320 x 240) | still for ~1 s sets the rest pose; then off it by 2500 (~0.15 g) on two reads in a row, 0.2 s apart; not in a dark begun face down | wakes in place | `imu_port_rest_moved`, `enter_dark` |
| a touch, dark (320 x 240) | a finger on the glass, after the glass was seen clear; not in a dark begun face down | wakes in place; that touch does nothing in the tank | `touch_port_finger_now`, `touch_port_swallow` |
| BOOT, short press | the button (GPIO0), at release | sleep - on a 320 x 240 board the dark, which a press ends; in the deepsleep mode a press within the 20-minute grace wakes in place, and after it BOOT boots | `sleep_button_poll`, `enter_sleep_for` |
| BOOT held + a tap | a touch landing while BOOT is down | the *Reset tank?* prompt | `sleep_button_poll` |
| double-tap the glass | two quick taps, then a pause (LIGHTS OUT on DOUBLE-TAP, the default) | the tank light on / off, saved | `tank.c` (`light_manual_off`) |

The face-down rows need `POCKET_TANK_IMU_FACE_DOWN_SLEEP` (on for the CYD),
and the SLEEP row on SCREEN or LIGHT where there is one (NEVER ignores the
gesture; a deepsleep build has no row and always sleeps). In the deepsleep
mode, deep sleep after the grace hears only BOOT: waking it on movement
there needs the IMU's INT line wired (below).

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
  chip's motion interrupt armed at sleep. Not done: the CYD no longer
  deep-sleeps, and its dark wakes on movement by reading the IMU.
- **The dark is unmeasured on the bench** (2026-10-08). The pick-up and tilt
  numbers are provisional; GPIO17 has never been seen to move under a
  finger (without it a tap shorter than the 0.1 s between looks can be
  missed - a held one cannot); and nobody has measured what the dark draws.
  Where it could be cut: the panel is in DISPOFF, not SLPIN (120 ms more to
  wake); the MPU-6050 is awake (~0.5 mA) where its cycle mode would do; and
  the touch controller is read ten times a second, which may keep it out of
  its own monitor mode. In light sleep ESP-IDF isolates every pin not armed
  as a wake (`ESP_SLEEP_GPIO_RESET_WORKAROUND`); the dark keeps three
  outputs driven through it (`dark_hold_pins`) - the touch controller's
  reset (GPIO18, which floating could reset the FT6336 in every slice), the
  amplifier's enable (GPIO1, off when high) and the backlight (GPIO45) - and
  leaves the rest isolated, as every 1 s slice of the old grace did.
- **Nothing takes the tank back into the dark.** There is no idle timeout:
  a wake that nobody meant - a bump that read as a pick-up - leaves it lit
  until it is laid face down. If that happens in the case, a timeout back
  into the dark after a spell with no touch and no motion is the next step.
- **Two face-down sleeps stayed dark, BOOT included,** in the first hour of
  bring-up (2026-09-30) and never since, across every later round - BOOT and
  face up, in either order. Not explained. If one recurs, note the steps; the
  task watchdog armed across the sleep, for a backtrace, is the next
  instrument.

- **Battery.** The cell's voltage reaches GPIO9 through the board's divider,
  whose ratio is still to be measured. Without a meter the battery pill and
  its page stay hidden (the page is still laid out for 448 x 368).
- **A power cut, or the RESET button, loses the time** (it resets the chip's
  RTC timer too): the clock starts again from the save's stamp (or the
  build time, if that is later), and that absence is not lived through.
  Deep sleep keeps it.
- **Sending while drawing.** A frame is drawn and then sent, one after the
  other; sending it while the next one is drawn would lift the ceiling at
  40 MHz to about 33 fps.
- **The decorations** keep their pixel sizes: the castle is 146 px tall in a
  240 px tank. Worth a look on the glass.

## The Waveshare ESP32-S3-Touch-LCD-2

A second 320 x 240 board, the
[ESP32-S3-Touch-LCD-2](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2):
an ESP32-S3R8 (16 MB flash, 8 MB octal PSRAM, as the AMOLED board) behind a
2-inch ST7789T3 IPS panel, scanned landscape, with a CST816D touch panel and
a QMI8658 IMU on one I2C bus. It came to the fork as pull request #2 from
adampog, who ran it on the bench on the fork as it was before upstream's
v0.3.3 - 30-34 fps, a 16 ms flush at 80 MHz, a decision every ~3.5 s, the
touch and the colours right - and merged here onto today's main
(2026-10-08). Its image is built here but has never run here: nobody here
has one.

It shares everything in `CONFIG_POCKET_TANK_320X240` with the CYD - the
tank, the pages, the settings rows, the dark, no updates over Wi-Fi - and
its own `CONFIG_POCKET_TANK_WST_320X240` carries the rest: its pins
(`firmware/main/board_pins.h`), the ST7789 through the shared SPI display
port (`display_port_spi.c`, the TF card's select held high off the panel's
bus) and the CST816D (`touch_port_ft3168.c`, landscape from the driver with
no 180-degree turn). No codec, so no sound; no PMIC, so no battery pill and
no PWR key; no clock chip.

**Settings.** Its IMU senses handling - the light's idle rule - but does not
turn the picture (`CONFIG_POCKET_TANK_IMU_AUTO_FLIP=n`: with the AMOLED's
axes it flipped back and forth in the hand), so its settings page has the
SCREEN row (UPRIGHT / FLIPPED) where the CYD with an IMU has ROTATION, and
under it the SLEEP row, as every 320 x 240 board has.

**Sleep** (2026-10-08). Its sleep is the dark, as the CYD's (*Sleep*), on
lightsleep as shipped (`sdkconfig.defaults.wst`): BOOT, the director's
sleeps and the SLEEP row work as on the CYD, and a touch, a pick-up or a
tilt, BOOT or the director's timer light it again where it was. Until that
day it slept as upstream's boards do, deep after the 20-minute grace, and
only BOOT brought it back; a Touch-LCD-2 built for deepsleep still does,
and so does one built from an sdkconfig made before then, which keeps the
deepsleep it holds - the defaults only fill in what an sdkconfig lacks, so
delete it first (*Building and flashing*, below). On such a build nothing
holds the backlight pin (GPIO1) through the grace's light sleep or the deep
sleep after it, and the panel gets DISPOFF, not SLPIN: if the glass glows
or the night costs more than it should, look there first.
Face down does not sleep it: the gesture needs the axis out of the glass,
and this QMI8658's axes are not measured (`POCKET_TANK_IMU_FACE_DOWN_SLEEP`
stays off). In the dark's light sleep it keeps its backlight (GPIO1) low
and the TF card's select (GPIO41) high (`BOARD_DARK_HELD_PINS`); the LCD's
and touch panel's reset is one net with its own pull-up and no GPIO, so
nothing needs holding there. The CST816D's INT, GPIO46, is armed as a
light-sleep wake as the CYD's GPIO17 is. GPIO46 is a strapping pin, but a
strap is sampled only at a chip reset, when the pads are back in their
reset state, and the dark only reads it, with a pull-up: nothing drives
it. The CST816 family can stop answering I2C while it idles; in the dark a
read it does not answer is no finger, and is passed over without a log
line (`touch_port_finger_now` probes its address first).

What only its own hardware can confirm, since nobody here has one:

- **that GPIO46 falls under a finger and wakes the light sleep.** Without
  it a tap shorter than the 0.1 s between looks can be missed - a held one
  cannot, so the dark still wakes on a touch;
- **that the CST816D answers through the dark,** or, if it dozes, that a
  touch brings it back in time for the next look;
- **what the dark draws.** The panel gets DISPOFF, not SLPIN, and the IMU
  stays awake for the pick-up; if the glass glows or the cell drains faster
  than it should, the backlight's hold and those two are where to look. The
  touch controller is looked at ten times a second, which may keep the
  CST816D out of its own doze;
- **whether GPIO46 holds low under a resting finger or pulses.** The dark
  arms it only while it reads high, so a held line costs nothing; a line
  that pulses while a finger or a palm rests on the glass - as the CST816
  family's default is said to - would end each slice of a dark begun that
  way at the next pulse, and the dark would look many times a second, not
  ten, until the glass is clear. Its IMU reads are counted in looks, so the
  same would shorten the settle before the rest pose and the gap between
  the two reads a pick-up needs, and a knock could read as a pick-up;
- **what floats in each light-sleep slice.** Only the backlight and the TF
  card's select are held. The LCD's own select (GPIO45, a strapping pin
  with no pull-up), its DC and the SPI lines float, as the CYD's do, and so
  does the I2C bus (GPIO47, GPIO48) if the board has no pull-ups of its own,
  which its 400 kHz bus suggests it has. If the panel comes back blank, turned
  or with its colours wrong after a dark, or the touch or the IMU stops
  answering after one, add those pins to the board's `BOARD_DARK_HELD_PINS`.

**Building and flashing.** Its defaults are `firmware/sdkconfig.defaults.wst`,
on top of `sdkconfig.defaults` as the CYD's are, into its own build
directory. There is no script; from `firmware/`, with `<port>` its by-id
path (*Building and flashing*) and its factory image backed up the same
way:

```sh
. "${IDF_PATH:-$HOME/.espressif/esp-idf/v5.5}/export.sh"
idf.py -B build_wst -D SDKCONFIG=build_wst/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.wst" build
esptool.py --chip esp32s3 -p <port> -b 921600 write_flash 0x290000 ../model/out/model_q4.bin   # the first time: the model
idf.py -B build_wst -D SDKCONFIG=build_wst/sdkconfig -p <port> -b 921600 flash
```

The defaults only fill in what an sdkconfig lacks: after a defaults file
changes, delete `build_wst/sdkconfig` before building. `PT_BOARD` names the
image `wst_320x240`. An sdkconfig from the pull request's own build, which
named the board `CONFIG_POCKET_TANK_BOARD_TLCD2`, keeps its board:
`firmware/main/sdkconfig.rename` carries the old name across (and the
CYD's old `CONFIG_POCKET_TANK_BOARD_CYD_320X240` the same way).

## Syncing with upstream

The fork takes upstream's releases by merging them; it never sends anything
back. The `upstream` remote is `https://github.com/mediacutlet/pocket-tank.git`
with its push URL set to `no_push`, so a push there fails. Every line the
fork changes in an upstream file is a conflict waiting at the next sync,
which is why the CYD's code sits in blocks of its own and README.md is
upstream's text but for one block.

1. **Fetch, and branch off main:**

   ```sh
   git fetch upstream
   git log --oneline main..upstream/main          # what is coming
   git switch -c sync-<release> main
   git merge upstream/main
   ```

2. **Resolve.** The 320 x 240 layout lives in
   `#ifdef CONFIG_POCKET_TANK_320X240` blocks, and each board's own
   hardware under its own symbol (`CONFIG_POCKET_TANK_CYD_320X240`,
   `CONFIG_POCKET_TANK_WST_320X240`), so a conflict is usually upstream's
   change and the fork's block side by side, and both stay. Where
   conflicts land:
   - `common/render.h` - the CYD's page layouts are one block at the end of
     the file that redefines upstream's names, so upstream's own lines stay
     as written; a new layout name upstream adds may need a CYD value there.
     The fork's settings taps (`SET_TAP_FLIP`, `SET_TAP_SLEEP`) are numbered
     past upstream's, and a static assert keeps them there.
   - `common/render.c` - the CYD's paths for the card, the badges, the shop
     coin and the settings rows.
   - `firmware/main/main.c` - the IMU, the sleep modes and the dark, the
     SCREEN and SLEEP rows, the update paths the CYD leaves out, and the
     sleep's grace (20 minutes on a board with no PMIC, where upstream
     tests the clock chip alone).
   - `sim/main.c` - the CYD's selftest expectations and snapshots.
   - `firmware/main/touch_port_ft3168.c` - the FT6336's and the CST816D's
     init, and the CYD's turn.
   - `README.md` - take upstream's text and put the fork's block back under
     the title; `git diff upstream/main -- README.md` shows that block and
     nothing else.
   - `firmware/main/imu_port_qmi8658.c` - deleted in the fork, split into
     `imu_port.c` and `imu_qmi8658.c`. A change upstream makes to it is
     carried into the split by hand.
   - `common/setup.c` and `common/setup.h` - the first-run setup and the
     placement page, where the fork wrote every measurement as `UI(n)`
     in place of upstream's literal. Almost any upstream edit to them
     conflicts; keep upstream's change and the `UI()` around its numbers.
   - `common/icons.c` and `common/icons.h` - generated by
     `tools/gen_icons.py`, which in the fork also writes the 24 px badge
     copies. Never merge them by hand: take upstream's, resolve
     `tools/gen_icons.py` if it conflicts, and run the script again.
   - `docs/DEVICE.md` - upstream's bench log, with a fork section, *The CYD
     right now*, and a CYD note in its first rule. It conflicts where
     upstream edits beside them.
   - Smaller blocks that can meet an upstream edit:
     `firmware/main/CMakeLists.txt` and `Kconfig.projbuild` (the board, the
     IMUs, the sleep modes), `board_pins.h`, `audio_port_es8311.c`,
     `director.c`, `common/tank.c` (the algae pace),
     `common/llm/advisor_core.c` (the distance bands) and
     `common/progression.c` (the old CYD saves).

   Some changes merge cleanly and are still wrong:
   - a new question in `display_port.h` needs its answer in
     `display_port_spi.c`, the CYD's and the Touch-LCD-2's SPI display
     port, or neither links;
   - a new badge (`assets/icons/ms_*.png`) gets its 24 px copy when
     `tools/gen_icons.py` runs, but it also needs its pair in `badge_art()`'s
     table in `common/render.c`. Without one it draws at 32 px in the CYD's
     28 px rows, over its neighbours, with no error;
   - a new page measurement upstream writes as a literal, not `UI(n)`,
     draws at full size on the CYD's 240 px page;
   - a rule upstream adds that is keyed on the tank's size needs a look at
     320 x 240.

3. **The simulator, in all four worlds:**

   ```sh
   make -C sim check-all          # the 1.8, the round board, the watch, then the CYD
   ```

4. **Upstream's three worlds against upstream's own tree.** Build upstream's
   simulator in a worktree of `upstream/main` (it needs its own `sim/lvgl`),
   write each world's pages with `--snapshot <prefix>` from `fishsim`,
   `fishsim-round` and `fishsim-watch` in both trees, and compare them with
   `cmp`. The only difference expected is the build line, which prints the
   tree's own `git describe`; any other page that differs is the fork
   reaching an upstream board.

5. **The four firmware images build:** `tools/build_cyd.sh` with no port,
   and the 1.8, the round board and the watch with `idf.py`, each into its
   own build directory as README.md's
   [*Run it on real hardware*](README.md#run-it-on-real-hardware) shows.

6. **Back up the CYD's NVS - the tank's save - before flashing it:**

   ```sh
   esptool.py --chip esp32s3 -p <port> -b 921600 read_flash 0x9000 0x6000 nvs_before_<release>.bin
   ```

   `firmware/partitions.csv` puts NVS at `0x9000`, `0x6000` long, and it
   never moves. A flash that loses the tank goes back with
   `esptool.py --chip esp32s3 -p <port> write_flash 0x9000
   nvs_before_<release>.bin` (*Recovering a board*).

7. **Flash the CYD** with `tools/build_cyd.sh <port>` and check it on the
   glass: the tank loads the save it had, the pages and the settings rows
   answer, and it sleeps and wakes.

8. **Only then fast-forward main:**

   ```sh
   git switch main
   git merge --ff-only sync-<release>
   ```

   Nothing is ever pushed to `upstream`; `origin` is the fork.
