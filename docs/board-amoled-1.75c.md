# Second board: Waveshare ESP32-S3-Touch-AMOLED-1.75C (round, 466 px) - port notes, 2026-10-01

Strato's board is on the bench. Sources:
`resources/ESP32-S3-Touch-AMOLED-1.75C/` (schematic, wiki page, the factory
image to restore it: `...-FactoryOnly-260114.bin`, written at 0x0),
github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C (examples) and
Waveshare's BSP component `waveshare/esp32_s3_touch_amoled_1_75c`.

Strato, with the first picture on the glass: "we need it to fill the whole
round display, that's a main part of the appeal of this form factor". So the
round board has its own world: a BOWL.

## The round build (`TANK_ROUND`)

The world's shape is compiled in (common/tank.h). `TANK_ROUND` makes the frame
466 x 466, the square around a circle of glass:

- Water fills the circle. Sand lies flat across the bottom: the floor is the
  chord at y 380 (`TANK_BOT - 16`), x 54..412 (`TANK_FX0` / `TANK_FX1`), with
  a pebbled bed under it down to the glass (render.c `floor_rgb`).
- Floor things - the beds, pellets, the snail's walk, the shrimp, the decor,
  the bubbles - measure from `TANK_BOT` / `TANK_FX0` / `TANK_FX1`. In the
  rectangle those ARE `TANK_H` / 0 / `TANK_W`, so that build is unchanged.
- Swimmers meet the glass itself: `tank_glass_x0/x1/top/clamp` (tank.c), a
  radial wall avoidance and bounce. The model's wall sense
  (advisor_core.c) and the feed gestures use the same glass.
- The floor is 358 px, not 448: the two right-hand beds keep their place
  against the right glass, the reef bed has two fronds fewer, the outer
  fronds' ceilings stop under the glass (`tank_veg_cap`).
- Film grid: 26 x 22 cells of 18 px, only the cells on the circle
  (`tank_algae_cells`). The SAVE is the same layout on both builds: it keeps
  644 film cells, the bowl uses the first 572 (progression.c `ALGAE_SAVE`).
- UI. Widgets on the tank (card, toolbox, tool chip, battery pill, toast)
  have per-build places in the frame (render.h). The full-screen pages, the
  prompts, the setup flow and the update pages draw in PAGE space
  (render.h `PAGE_X` / `PAGE_Y`): their 448 x 368 layout centred in the
  frame, taps converted from the frame's coordinates on the way in. On top of
  that the milestones page has a bowl layout (Strato's: SETTINGS top centre,
  the rows lower and 34 px apart, UPGRADES and CLOSE together at the foot)
  and the shop / settings / updates pages have their corners drawn in.

Build and flash: `tools/flash_round.sh` (`--full` for a blank board). It
builds with `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.round"` into
`~/.cache/aqua-pets/fw-build-175c` with its own sdkconfig, and finds the
board by USB serial. The sim: `make ROUND=1` -> `./fishsim-round`
(`--snapshot` works; since 2026-10-01 every selftest passes here too -
`make ROUND=1 check`, docs/BOARDS.md).

Two images now, one per board. Each recognizes the board at boot over I2C
(the 1.8 has a TCA9554 at 0x20; the 1.75C has none and an ES7210 at 0x40):

- the rectangle image on a 1.75C still runs - the 448x368 frame inside the
  circle (director `view fit|full`);
- the round image on a 1.8 shows nothing (and says so in the log).

## The hardware, against the 1.8

| | 1.8 | 1.75C |
|---|---|---|
| Panel | SH8601 / CO5300, 368x448 portrait | CO5300, 466x466 round, x gap 6 |
| QSPI | CS 12, D0-3 4/5/6/7, PCLK 11 | the same, PCLK **38** |
| Resets | TCA9554 expander | GPIOs: LCD_RST 1, TP_RST 2 |
| Touch | FT3168 / CST816 | CST9217, I2C 0x5A, INT 11 |
| PMIC, IMU, codec, amp | AXP2101, QMI8658, ES8311, NS4150B on 46 | the same, same pins |
| Mic ADC | - | ES7210, I2C 0x40 |
| RTC | PCF85063 | **none** |
| PWR key | PMIC only | PMIC, and a sense line on GPIO 3 (unused) |
| Flash | 16 MB | 32 MB (used as 16: the same table) |

## Found on the bench

- **ALDO1 must stay on.** The ES7210 has every supply on A3V3 (ALDO1), its
  digital and I/O pins too. The 1.8's boot trim switched ALDO1 off and the
  unpowered chip clamped the I2C bus: PMIC, touch, IMU and codec all gone,
  and the PMIC keeps the rail off across resets - only a power cycle brings
  the bus back. Now: `battery_port_pin_rail("aldo1")` on any board that is
  not positively the 1.8, the ES7210 suspended over I2C instead
  (`codec_port_mic_adc_down`), and a dead bus at boot drives no panel.
- **The CST9217 answers some reads mid-press with nothing** (1-5 per press in
  the log). Taken at their word one press read as two taps. The port holds
  the finger down until no report has shown it for 60 ms
  (touch_port_ft3168.c `CST_LIFT_US`). Its axes are mirrored against the
  picture, as Waveshare's BSP has them. Strato: taps land (CLOSE, SETTINGS,
  the toolbox all hit).
- **The double tap that did not always take (2026-10-03).** That 60 ms of
  silence, read at the bowl's 17 fps, showed a lift 65-95 ms late: a quick
  pair (finger off the glass 26-49 ms in Strato's log) merged into one long
  press, and a slow tap's bridge counted against the 350 ms tap limit. Now a
  real report with no finger in it (byte 6 = AB) is a lift at once, only
  silence is bridged, and a press is timed to the last read that showed the
  finger. 30 taps, a 5 s hold and a wipe on the board: each one press, 27 of
  30 lifts by the chip's word. Director `touch lift <ms>` / `touch said
  on|off` tune it live; `touch log on` says how each press ended.
- **The feed strip is deeper on the bowl (2026-10-03).** Strato: taps that do
  not register, the light going off unasked, no food from a tap at the top.
  The strip was 26 px under the rim - 2.5 mm of glass, against a fingertip's
  9 px scatter - and a feed tap that fell short was a tank tap: two of them,
  the light's double tap. `FEED_ZONE_Y` is 45 px on TANK_ROUND. His log (36
  presses, `touch log on`): feed taps land 0-19 px inside the rim, no phantom
  second press at a lift; one rim tap rolled 26 px (over the 24 px tap limit,
  so nothing happened) and one lasted 17 ms. The tap limit stays at 24 px
  (Strato: "not sure we need to loosen").
- **Pages must never mark the dirty mask.** A page ctx's coordinates are not
  the frame's (its y runs negative on the bowl); the settings page's fill
  wrote in front of the mask and the board died with "stack overflow in
  IDLE1". render.c `g_dirty_hold`.
- The scene prefetch copies whole 64-byte lines: frame buffers are allocated
  as `PLAN_FB_ALLOC` (the bowl's 434,312 bytes is not a multiple of 64 and
  the DMA copy refused it - 15 ms of CPU copy per frame until then).
- No RTC: `rtc_port_init` probes for the chip and leaves the clock unset, so
  `clock_port_now_unix()` is 0 and time away is not lived through.

## Measured

28-31 fps in the tank view (render ~6 ms, flush ~14-15 ms: only the circle's
chord of each stripe is sent), ~19-23 with a card up. LLM 3.8 s / decision,
11.4 tok/s. Internal heap 44-56 KB free while running. Battery gauge and
cell present (100%, 4.14 V on the cable).

## Flip and sleep (2026-10-01, evening)

- **Flip**: upright in the hand with the USB port down reads +X ~16.8k
  (Strato held it; director `imu`). imu_port_init sets the up axis per board
  (the 1.8: -Y). It turns over and back on the glass, taps land inverted.
- **The keys**: the SMALL key by the USB port is BOOT (GPIO 0), the larger
  one is PWR. The PWR key's sense line (GPIO 3) is HIGH while the key is
  down (director `pwrpin`: both keys and the PMIC's press flags, live).
- **Sleep keeps time** (Strato: "worried about the power draw but let's
  explore"): with no RTC chip a PMIC power-off forgets how long it lasted,
  so on this board the night is a DEEP sleep and the ESP32's own clock runs
  through it.
  - PWR tap: the 20 min grace as on the 1.8 (light sleep; the sense line
    wakes it at once, the PMIC is still polled). A tap resumes in place.
  - After the grace: deep sleep, ext1 on the sense line - the PWR key wakes
    it, the boot lives the absence through and puts the fish back
    ("wake from deep sleep (the PWR key): 0.1 h simulated", verified).
  - PWR held 1.5 s: the PMIC power-off, as before. Time stops: at the next
    boot the clock is unset and is seeded from the save's own stamp
    (`rtc_port_seed`), so a power cut reads as no time at all, never as an
    absence nobody can vouch for. (That seed path has not been seen run: a
    USB reset keeps the RTC domain alive.)
  - Pads for the deep sleep: the panel's reset held high, the touch chip's
    low, the panel's CS high (`display_port_deep_sleep_pins`), the audio
    lines as on the 1.8, everything else isolated by the hold.
- **MEASURED 2026-10-02: POOR.** First night in deep sleep
  (docs/batlog/2026-10-02_round-175c-night.txt): `deep` 80% / 3949 mV ->
  `wake` 59% / 3781 mV over 11 h 47 min = **21% in ~12 h, ~1.8 %/h**. The
  1.8's power-off night lost ~1% in 8.5 h (~0.12 %/h): this is ~15x that. An
  8 h night costs ~14%; a full cell lasts ~2 days asleep. (The mAh column
  assumes the 1.8's 200 mAh cell; this board's cell is unknown, so read
  percent and hours.) Strato: "definitely not as efficient as the original
  board". Frozen time (the PMIC power-off) is NOT a way out: "never going to
  be a viable option as the user experience would drift too much between
  devices".
- **Where it can go (the schematic, 2026-10-02).** Nothing left to cut at
  the PMIC: the panel module (and the touch chip on its flex) takes all its
  power from VCC3V3 = DCDC1, the ESP32's own rail; ALDO1 must stay; every
  other output is already off; DCDC1 is in auto PWM/PFM (REG 81 = 00), CCM
  off. A fixed cost: the PWR sense line is a BSS138 whose gate is PWRON
  (high at rest) pulling SYS_OUT/GPIO 3 down through R9 10K to VCC3V3 -
  ~0.33 mA, awake or asleep, for as long as the key is NOT pressed.
- **The chips' own sleeps, before the deep sleep** (main.c `deep_sleep_now`,
  director `sleepcfg <mask>`, NVS, default 15 = all): 1 the CST9217's sleep
  command (D1 1E x2, D1 01, D1 05 - SensorLib's sequence) with its reset held
  HIGH, instead of held in reset; 2 the CO5300's deep standby (4Fh 01: ~3 uA
  vs sleep-in's ~135, datasheet p. 102 - only a > 3 ms reset ends it, the
  boot's); 4 the QMI8658's Power-Down (CTRL1 sensorDisable, ~20 vs ~50 uA;
  its boot now turns the clock back on before the soft reset, and retries -
  the first try NACKed). The batlog's deep row says what took: "deep 7". Two
  30 s timer windows: everything back at the wake.
- **Night 2 with sleepcfg 7 (2026-10-02/03,
  docs/batlog/2026-10-03_round-175c-night-sleepcfg7.txt): `deep 7` 87% /
  4016 mV -> `wake` 61% / 3798 mV in 15 h 57 min = 26%, 1.63 %/h** (night 1:
  1.78). Inside the gauge's 1% steps: the chips' own sleeps are not where
  the drain is. What is left is not reachable by a command we know of.
- **The cell is 500 mAh** (Strato read the label, 2026-10-03; the charge rows
  agree: ~19 %/h at the fixed 100 mA). So the batlog's mA column reads 2.5x
  low here, and the deep sleep draws **~8 mA**, not 3 - something is on.
- **sleepcfg 8: the QSPI clock + data lines held low** (display_port_deep_sleep_bus;
  the panel stays powered here, and the hold left them floating into it).
  Window 2026-10-03 (docs/batlog/2026-10-03_round-175c-window-sleepcfg15.txt):
  `deep 15` 62% / 3834 mV -> `wake` 58% / 3783 mV in 2 h 20 min = ~1.7 %/h.
  No change: the floating lines were not it either. Kept (harmless, the
  boot releases the holds; the picture checked on the glass after a wake).
- **THE CLOCKLESS NIGHT: power-off + the time from the internet (2026-10-03,
  Strato's go after the three tries above).** main.c "THE CLOCKLESS NIGHT",
  net_time.h / net_port_esp.c `net_time_sync`. With a saved Wi-Fi network,
  a clock on real time ("netclk" in NVS) and this morning's sync good
  ("netok"), the night is the PMIC power-off; the boot after it joins the
  network before the tank exists (the radio has the internal heap, as in
  update mode), asks NTP by hand (pool.ntp.org, time.google.com,
  time.cloudflare.com; 2 s each - ESP-IDF's client waits a random 0-5 s and
  sets the clock itself), sets the clock, and progression_boot lives the
  time away from the save's stamp. The FIRST sync re-bases the stand-in
  clock (build time / save stamp) with nothing lived for the jump. A failed
  sync: the clock resumes at the save, the night after is a deep sleep, and
  every wake asks again - the stretch is owed, not lost (the clock lags real
  time by exactly it; the next good sync lives it). No network saved: the
  deep sleep as before. Director `clock` says what the boot's sync did, the
  hours lived, and what tonight is.
  Verified 2026-10-03: Wi-Fi saved on the glass -> "the FIRST sync (clock
  moved, nothing lived)", the clock within 3 s of the Mac's; a long-press
  power-off at 10:56, PWR at ~11:07:50 -> "the clock had stopped (the time
  away lived from the save)", 0.19 h lived. The night on it: owed.
  The wake shows WAKING UP / CHECKING THE TIME with the update pages'
  spinner while the sync runs (render_clock_sync, common/update.c; drawn by
  its own task) - the 2-3 s were dark, and a dark glass after a press gets
  pressed again (Strato). The PWR grace (in-place wake) is 60 min here
  (SLEEP_GRACE_CLOCKLESS_US), 20 on the boards with a clock chip: Strato,
  20 min "might feel a little cumbersome" when every wake past it checks
  Wi-Fi. The grace is light sleep: on this board ~1% per 20 min by the
  batlog's sleep rows, so the hour costs ~2% more a night.
- **Awake on the 500 mAh cell: ~6 1/2 h** (batlog stretches on battery:
  14-17.5 %/h, e.g. 99% -> 83% in 55 min; the battery page learned the same,
  6 h 30 min): ~80 mA, about the 1.8's ~88. The public hardware page said
  so (2026-10-03); asleep stays "about 2 1/2 days" (1.7 %/h).
- Found on the way, in SHARED code: the audio port came straight back up
  after going down for a sleep when it had been warm at the press (a prewarm
  asked for while it was still coming up was left standing) and stayed up
  for the whole sleep. On the 1.8 that is the 20 min grace with the codec and
  amp on. Fixed (audio_port_es8311.c); the 1.8 gets it at its next flash.

## Open

- CLOSED for 0.3.0 (Strato, 2026-10-03: "we've tested the no-wifi mode and
  have a decent idea of the power draw in deep sleep ... i dont think we
  need the additional overnight"): the deep sleep's draw is measured, ~8 mA
  on the 500 mAh cell (1.6-1.8 %/h, two nights and a window above), and a
  bowl with a saved network powers off at night and takes the time from the
  internet at the wake. Another night would not change what ships; the site
  makes no standby promise. If it is ever reopened, the suspects left: the
  PMIC's own ADCs (REG 30 = 03: VBAT + the TS current source), the PMIC's
  sleep mode (REG 26), the flash/PSRAM domain.
- The long-press power-off and its cold boot have not been run on this board.
- Audio: the codec and amp come up in the log; nobody has listened yet.
- Touch calibration: DONE 2026-10-01 (`tools/touch_calib.py <usb serial>`,
  the presses in docs/touch/). 63 presses: the report is 6% long in y
  (reported y = 1.059 y - 3.0 with the 10 px bias inside; the foot of the
  bowl read 18 px low), x is inside the taps' own scatter and left alone.
  Verified upright and turned over (`--flip`): rows within 3..6 px.
- Charge current is the 1.8's 100 mA; this board's cell is not known.
- The model has not been checked on the bowl's state distribution (the wall
  sense follows the circle now): run the `--selftest-llm` census on a round
  sim before trusting it; nothing in the schema changed.
- Releases: the installer, the OTA manifest and CI know one image. A second
  board needs its own image, manifest entry and a guard so a tank never takes
  the other board's update.
- (closed 2026-10-01: the selftests ask the layout for their targets and run
  on the bowl - `make check-all`.) Three layout nits seen while doing it,
  and Strato's own from the glass ("the cancel button when scanning for wifi
  networks is not centered" - the first of the three), fixed the same
  evening in the layout constants, the rectangle and the watch untouched:
  update mode's lone button is the page's middle (`UPD_BTN_MID_X`; it was
  measured from the left button, which the bowl draws in - 30 px right of
  center); the badges stand at the watch's 38 px pitch and the TANK row's
  page arrow at 396, its "new" ring and pips inside the circle; the settings
  segments start at 172 with an 80 px pitch, the "100%" one whole. Checked on
  the three-board sheets; the glass is Strato's to confirm.
- The 1.8 itself has NOT been flashed from this tree. Its build compiles
  (.bss +168 B) and the rectangle sim passes `make check`.
