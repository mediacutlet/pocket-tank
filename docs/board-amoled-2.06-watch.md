# Third board: Waveshare ESP32-S3-Touch-AMOLED-2.06, the WATCH (410 x 502) - port notes, 2026-10-01

Strato's watch (the "unidentified third board" of the round board's
day). Sources:
`resources/ESP32-S3-Touch-AMOLED-2.06-Watch/` (schematic, wiki page, the
maker's dimension drawing, and `...-factory-readback-2026-10-01.bin`: the
whole 32 MB flash as it shipped, read back before the first flash - written
at 0x0 it restores the board), github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06
and Waveshare's BSP component `waveshare/esp32_s3_touch_amoled_2_06` (the
panel's init sequence and gap came from it).

## The watch build (`TANK_WATCH`): a PORTRAIT tank

The first picture on the glass was the rectangle's world a size up, on its
side (502 x 410 - the panel turned, as the 1.8 turns its own). Strato, with
it on his wrist: "as a wearable we actually need the tank to be in portrait
orientation ... I have to turn my head in an awkward way". So the watch has
its own world, 410 wide and 502 tall (common/tank.h):

- **The glass has round corners**, ~8 mm of radius on the maker's drawing:
  `TANK_CORNER_R` 100 px. Swimmers, their targets, the film grid, the snail
  and the shrimp meet the arcs (tank.c `corner_in` / `corner_clamp`, and
  `tank_glass_x0 / x1 / top` are the arcs, so the model's wall sense and the
  feed gesture follow them). The radius is read off a drawing, not measured
  on the glass: if a card's corner or a badge is clipped, this is the number.
- **The floor** is flat (TANK_BOT = TANK_H) and 322 px long: the sand line
  (y 486) meets the lower corners 46 px in, so the floor things keep to
  `TANK_FX0..TANK_FX1` (44..366), as on the bowl. Bed 1 stands against the
  right glass, bed 2 is five fronds beside it, the reef bed gives up four
  fronds: open sand between them (the landscape beds covered this floor wall
  to wall). The sword plant's first spot is x 196.
- **Fronds are taller**: a segment is 4.3 px here (3.2 on the 1.8), so growth
  1 still reaches the surface of a tank 118 px taller (`VEG_SEG_PX`). What a
  trim pays is counted at the 1.8's pitch on every build (`VEG_PAY_PX`): the
  same cut, the same sand dollars.
- **Film grid**: 23 x 28 cells of 18 px = 644, exactly what the save keeps;
  the cells the corners hide are not on the glass (`tank_algae_cells`).
- **Pages**. The glass is 38 px NARROWER than the 448 px page. The page still
  sits centred: `PAGE_X` is -19 (the glass shows page x 19..428) and `PAGE_Y`
  67, the middle of the glass's height, where the corners no longer reach.
  The layouts' own margins are 24..32 px, so nearly everything was already
  inside that window; `PAGE_NARROW` (render.h) marks the few columns that came
  in: the milestones page's badges (38 px pitch) and its tank-row arrow, the
  settings page's segments, a row's portrait. Widgets on the tank (card,
  toolbox, pill, chip) have their own places, in from the corners.
- **The picture's way up is a setting, not the live flip** (2026-10-02): on
  a wrist the arm swings through every angle, so the IMU's live flip is off
  in this build (main.c). But a watch can be worn either way around -
  buttons toward the hand or the elbow - and the second way shows the tank
  upside down. Settings has a row on this build only, **SCREEN:
  NORMAL / TURNED** (where the other boards have ROTATION, 0.3.2) (tank.h `tank_screen_*`, saved in the save's tail at
  1672). The settings page uses the glass above and below the PAGE box for
  it (its own `SET_*` block in render.h). Motion still keeps the codec warm
  and, with LIGHTS OUT on AUTO, the light on - on a wrist that is all day;
  see Open.
- **No AUTO** (built and dropped the same day): an AUTO that learned the
  way up from the IMU at each tap - "a watch tilted to be read has gravity
  toward the screen's foot" - failed on Strato's wrist. Raised to read it
  the usual way, gravity pointed toward the screen's TOP (g = [-7780 1570
  -15030]); worn turned and tapped, the same sign again (-12500). The tilt
  of a raised wrist depends on the arm, not on which way the watch is
  strapped on.
- **The IMU's axes** (propped on the desk, the tank right side up, 2026-10-02):
  g = [13100 900 -9400] - the panel's long axis is the chip's X, its foot
  +X (`s_up_axis` 0, `s_up_sign` +1). Nothing reads it on this build.
- **Touch through the turn**: the calibration is the panel's own stretch,
  fitted upright, so since 2026-10-02 the touch port undoes it in the
  upright frame and turns the point after (`cal_view`); the finger's own
  low landing (`s_bias_y`) follows the viewer. The worn fit (below) was made
  on one wrist the usual way: a turned sitting on the other wrist is still
  to do.

Build and flash: `tools/flash_watch.sh` (`--full` for a blank board:
bootloader, table, model, a cleared NVS). It builds with
`SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.watch"` into
`~/.cache/pocket-tank/fw-build-206` and finds the watch by USB serial. The
sim: `make WATCH=1` -> `./fishsim-watch` (its window shows the round corners;
`--snapshot` works; since 2026-10-01 every selftest passes here too -
`make WATCH=1 check`, docs/BOARDS.md).

Three images now. Each recognizes the board at boot over I2C: the 1.8 has a
TCA9554 at 0x20; with none, an ES7210 at 0x40 is the 1.75C or the watch, and
the watch alone has the RTC chip at 0x51.

- the rectangle image on a watch still runs: its 448 x 368 frame turned 90
  degrees (the 1.8's way), centred in the glass with a black border;
- the watch image on another board shows nothing (and says so in the log).

## The hardware, against the 1.8

| | 1.8 | watch (2.06) |
|---|---|---|
| Panel | SH8601 / CO5300, 368x448 | CO5300, 410x502, x gap 22 (0x16) |
| QSPI | CS 12, D0-3 4/5/6/7, PCLK 11 | the same |
| Resets | TCA9554 expander | GPIOs: LCD_RST **8**, TP_RST **9** |
| Panel power | expander (DSI_PWR_EN) | DSI_PWR_EN pulled up to **ALDO2** |
| Touch | FT3168 (V1) / CST816 (V2) | FT3168, I2C 0x38, INT 38 (unused) |
| I2S | MCLK 16, BCLK 9, WS 45, DOUT 8 | MCLK 16, BCLK **41**, WS 45, DOUT **40** |
| Amp | NS4150B on 46 | the same |
| Mic ADC | - | ES7210, I2C 0x40, whole on A3V3 (ALDO1) |
| RTC | PCF85063 | PCF85063 |
| PWR key | PMIC only | PMIC, and a sense line on GPIO **10** (high = down) |
| Extras | SD (expander CS) | SD on 1/2/3/17, vibration motor on 18 (ALDO3) - unused |
| Flash | 16 MB | 32 MB (used as 16: the same table) |

## Found on the bench

- **The 1.8's I2S bit clock and data pins are this board's RESET lines**
  (GPIO 9 = touch reset, 8 = panel reset). The audio port takes its pins per
  board now (audio_port_es8311.c); on the watch the 1.8's would have clocked
  the panel's reset.
- **ALDO2 is the panel's power switch** (the schematic: DSI_PWR_EN through
  10k to ALDO2), and the PMIC keeps its rail switches across a reset while a
  cell is connected. The display port switches ALDO1 + ALDO2 on itself
  before it touches the panel (`watch_rails_on`), and both are pinned against
  the boot trim (`battery_port_pin_rail`, which holds two rails now).
- **The FT3168 NACKs a read every ~3.7 s when idle** (its monitor mode) - five
  error lines each through the esp_lcd driver. The watch reads it directly
  (touch_port_ft3168.c `ft3168_read`): ack check off, a read it did not
  answer is no report, and silence inside a press is bridged for 60 ms like
  the CST9217's.
- Sleep, as far as it was run: `deepsleep 12` (the director's timed deep
  sleep, on the landscape build) went down and came back - panel, touch, the
  fish put back. The PWR key (this board has the RTC chip, so it sleeps the
  1.8's way: the 20 min grace, then the PMIC power-off): Strato on the
  portrait build, 2026-10-01 - "the board sleeps and wakes with press". That
  is the tap and the wake inside the grace; the power-off after the grace,
  the held press and the cold boot from them have not been reported.

## Measured

Portrait build, two fry: 30-32 fps in the tank view (render 6-7 ms, flush
12.6-13 ms - the frame goes out row for row, nothing turned), ~23 with a
card up (the card cache is in PSRAM). The landscape build before it: 24-27
fps (flush 14-16 ms through the turn). LLM 3.65-3.8 s / decision, 11.4-11.8
tok/s. Internal heap 46 KB free while running. .dram0.bss 48,592 B (the
1.8's image 47,536).

The model in the portrait world (sim, `--selftest-llm`): goal shares within
a few points of the rectangle's (inspect_reef 52%, follow_friend 27%,
explore 5%, visit_bubbles 10%), fish-time at a landmark 81% (70% in the
rectangle - the landmarks are closer in a narrow tank).

## Open

- Touch calibration: DONE 2026-10-01 (`tools/touch_calib.py <usb serial>`,
  the presses in docs/touch/). This panel reads SMALL: reported = 0.963 x +
  14.8, 0.957 y + 13.5 (bias inside), fitted from two sittings WORN on the
  wrist (45 presses; each sitting's rows and columns within 7 px of the
  map). A first sitting held in the hand and tapped with the thumb sat 10 px
  off these and was left out: the grip moves the landing, and a watch is
  used worn. A fresh worn sitting on the final map (18 presses): columns
  within 2 px, the middle and foot rows within 3..7, the top row 9 px high
  that time (the three worn sittings together: within 4 px every row).
- The corner radius (100 px) and the widgets' places want his eye on the
  glass.
- Battery: the cell's capacity is not known (the charge current is the
  1.8's 100 mA), the battery page's default drain is the 1.8's, and nothing
  was measured - awake, in the grace (the panel is only in sleep-in there;
  cutting ALDO2 is the 1.8's "expander off"), or powered off.
- Worn all day: motion keeps the codec warm (~5-7 mA on the 1.8) and holds
  an AUTO light on. A wrist wants its own rule (raise-to-wake, a dim idle).
- The PWR key: the tap sleeps and a press wakes (Strato, 2026-10-01); still
  to see - the power-off once the grace has passed, the held press, and the
  boot back from a power-off.
- Audio by ear (the pins are per board now; the codec and amp come up in
  the log).
- The vibration motor (GPIO 18) could tick on a tap or a milestone.
- Releases: a third image for the installer / OTA / CI, as for the round one.
