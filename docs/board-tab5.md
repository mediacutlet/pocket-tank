# The Tab5 (M5Stack, ESP32-P4): the fifth board

The tank on a 5-inch 720 x 1280 MIPI-DSI glass, held landscape. Build
`TANK_TAB5` + `CONFIG_POCKET_TANK_DISPLAY_TAB5` + `CONFIG_POCKET_TANK_TOUCH_GT911`
with ESP-IDF **v5.5.x** (not the v5.4.1 the four Waveshare boards use - see
"the toolchain" below), target `esp32p4`.

This file is the board's traps, the way the other boards' docs are. Read
BOARDS.md first: the rules there (write it once in `common/`, a board `#ifdef`
lives in the geometry, a page layout, or a port) are exactly what this port
follows - and the two places it deliberately differs are called out below.

## What it is

| | |
|---|---|
| chip | ESP32-P4, **silicon rev v1.3** (pre-v3: see the toolchain), 360 MHz, 32 MB hex PSRAM @ 200 MHz, 16 MB flash |
| glass | 720 x 1280 MIPI-DSI, **ILI9881C or ST7123 or ST7121** - the unit's own (see below) |
| touch | **GT911 (0x14) or ST712x (0x55)**, paired with the glass |
| codec | ES8388 at 0x10 (wired up: the port streams - see docs/tab5-next.md item 2 for the preload trap) |
| RTC | RX8130CE at 0x32 (not wired up yet: `rtc_port_pcf85063.c` knows the PCF85063) |
| PMIC / gauge | none: the board has a **2S Li-ion pack** watched by an INA226 at 0x41 (`battery_port_tab5.c`: pack voltage, current, a 2S curve for the percentage, charging from the current's sign) |
| IMU | BMI270 at 0x68 (not wired up: no auto-flip, the picture is always upright) |
| radio | **none on the chip.** Wi-Fi 6 comes from an ESP32-C6 on SDIO (esp_hosted), which this port does not bring up: `net_port_stub.c` answers every network call "no radio" |
| buttons | the side button is PWR/RST/BOOT (a single press is a reset, a double-click powers off, a hold is download mode); there is no PWR-sense line, so `board_pwr_sense_pin()` is -1 |

The chips the tank does not use yet are named in one line each at boot
(`no AXP2101`, `no RTC chip`, `no ES8311`, `no QMI8658`): those ports probe and
find nothing, which is exactly the graceful path they were written with.

## The three glasses (this is the trap that costs a day)

A Tab5 is not one board. Espressif's BSP carries **three** display
configurations, and which one a unit has is decided at runtime by *which touch
chip answers on the I2C bus* (`bsp/m5stack_tab5`, `bsp_get_board_version`):

| version | glass | touch | how it is told |
|---|---|---|---|
| 1 | ILI9881C | GT911 | the GT911 answers at its backup address **0x14** |
| 2 | ST7123 | ST7123 | 0x55 answers, and its register 0x0000 reads **3** |
| 3 | ST7121 | ST712x-compatible | 0x55 answers, and register 0x0000 reads **1** |

**Our unit is version 2 (ST7123, FW 3)** - the boot log says so:

```
I (877) M5Stack Tab5: Discovered board version 2 (LCD ST7123, Touch ST7123, FW 3)
```

What that costs if you assume ILI9881C (the first attempt here did):

- the ILI9881C driver (espressif/esp_lcd_ili9881c) begins by *reading* the
  panel's ID registers over the DSI bus. On a non-ILI9881C glass nothing
  answers, and the DSI HAL spins forever on an empty RX FIFO -
  `mipi_dsi_host_ll_gen_is_read_fifo_empty` has no timeout - so `app_main`
  stops with **no log line after "MIPI D-PHY rail up"**. What finds it is the
  task watchdog's backtrace 30 s later (`MEPC` -> `addr2line` ->
  `panel_io_dbi_rx_param`).
- a hand-written sequence instead (the vendor table, writes only) gets no
  further: the DSI host's *command* FIFO never drains and the watchdog shows
  `mipi_dsi_host_ll_gen_is_cmd_fifo_full` inside `panel_io_dbi_tx_param`. The
  host is waiting for the panel's acknowledgement, and this is not that panel.

So: **let the BSP own the glass.** This port depends on
`espressif/m5stack_tab5_noglib` (the same BSP without LVGL - the tank draws its
own pixels) and calls `bsp_display_new_with_handles`, which does the detection,
the two PI4IOE5V6408 expanders (LCD_EN bit 4, TOUCH_EN bit 5), the D-PHY's LDO
(chan 3, 2500 mV), the DSI bus, the variant's DPI timing and sequence, and the
panel driver. `bsp_touch_new` would do the touch the same way, but see below.

## The toolchain: IDF v5.5.x, and pre-v3 silicon

Two hard requirements, both from Espressif's own code:

1. **ESP-IDF v5.5.x.** With v5.4.2 the BSP's `espressif/usb` dependency does
   not compile (`usb_dwc_hal_fifo_config_is_valid` does not exist in that
   HAL). The Tab5 builds with v5.5.6 here; the four Waveshare boards keep
   v5.4.1. `tools/flash_tab5.ps1` takes the path from `IDF_TAB5_PATH`.
2. **`CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` + `CONFIG_ESP32P4_REV_MIN_100=y`.**
   ESP-IDF treats P4 "rev < 3.0" and "rev >= 3.0" as mutually exclusive builds
   ("huge hardware difference", `Kconfig.hw_support`). IDF 5.5 builds for
   **rev v3.1** by default, and the image is then refused - by esptool at flash
   time *and* by the bootloader - with "requires chip revision in range
   [v3.1 - v3.99], this chip is revision v1.3". This unit is v1.3.

## Build and flash

```powershell
# from the project root, PowerShell (no bash on this machine)
.\tools\flash_tab5.ps1                     # build + flash the app to COM9
.\tools\flash_tab5.ps1 -Full               # a board still running M5's factory
                                           # image: app + model + a clean nvs
.\tools\flash_tab5.ps1 -SkipBuild -Monitor # flash what is in build-tab5, watch it
python tools\tab5_boot_log.py COM9 25 log.txt   # reset + capture the boot log
```

Its own build dir (`firmware/build-tab5`) and its own sdkconfig
(`firmware/sdkconfig.tab5`), which is **standalone**: sdkconfig.defaults is the
S3 family's (octal PSRAM, Wi-Fi, signed images with a key that is not in the
repo) and half of it does not exist on the P4. `-Full` wipes the nvs region
only because the factory image left its own keys there.

## The two places this port deliberately differs

1. **The frame is 720 x 405, not the panel's 720 x 1280** (`common/tank.h`,
   `TANK_TAB5`). A software renderer cannot fill 922k pixels at 30 fps on this
   chip; 291k (1.8x the 1.8-inch board's) it can. The 16:9 world is turned a
   quarter and scaled x16/9 onto the glass by the P4's **pixel-processing
   accelerator**, in one pass, by `display_port_tab5.c`. The renderer, its
   double buffer, the scene cache and the game are untouched: the turn lives in
   exactly one constant (`TAB5_ROT_DEG`) and its inverse
   (`display_port_panel_to_tank`, for the touch).
   `TAB5_ROT_DEG` is **90** for this unit: at 270 the tank came up upside down
   (the stand had to be turned over), and 270 + 180 = 90 is the turn that
   landed. If a future unit shows it the other way up, that one constant is the
   fix (and the taps follow it).
2. **The PPA runs one frame behind.** A blocking turn-and-scale measured
   19.9 ms of a 39.5 ms frame (2026-10-16). The port now starts the PPA
   **non-blocking** and collects it at the *start of the next flush*, by which
   time the tank has drawn its next frame: the wait costs ~1 ms. Safe because
   the tank double-buffers and the PPA writes the DSI buffer that is not being
   scanned. This is the port's own trick, not shared logic.

## The traps found on the glass (all with numbers)

| symptom | cause | fix |
|---|---|---|
| `app_main` stops after "MIPI D-PHY rail up", watchdog fires, no panic | the ILI9881C driver's DSI ID read, on an ST7123 glass that does not answer; the DSI HAL's RX wait has no timeout | let the BSP choose the panel (it detects the unit) |
| the DSI command FIFO never drains (`gen_is_cmd_fifo_full`) | a hand-written sequence on the wrong glass: the host waits for an acknowledgement that never comes | the same |
| 9.2 fps, 108 ms frames | the scene prefetch through `esp_async_memcpy`: **66 ms** for 583 KB (~9 MB/s) where the CPU's own `memcpy` inside `render_tank` does it in 14 | this target does not install async memcpy (`main.c`: "THE TAB5 DOES NOT INSTALL IT") |
| the frame's flush half measured 20 ms with the port's own timer showing 0.1 | the PPA in `PPA_TRANS_MODE_BLOCKING` | run it one frame behind (above): 19.9 ms -> 1.6 ms |
| the touch read was 20 ms of the frame | `esp_lcd_touch_st7123.h`'s own IO config hardcodes **`.scl_speed_hz = 100000`**, and the driver reads 92 bytes a poll (adv-info + max-touches + 9 bytes x 10 points) | the port builds its own IO at 400 kHz (and reads 0.30 ms a poll), and polls once a frame instead of twice (`touch_port_wants_second_poll`) |
| the tank came up upside down | `TAB5_ROT_DEG` 270 | 90 |
| esptool refuses the image | IDF default rev v3.1 vs this v1.3 silicon | the two pre-v3 Kconfigs above |

## Where it stands (2026-10-16, first working build)

```
I (877)  M5Stack Tab5: Discovered board version 2 (LCD ST7123, Touch ST7123, FW 3)
I (1329) display: panel up (720 x 1280, 2 frame buffers): the turn and the scale are the PPA's from here
I (1831) touch: touch ready (ST712x at 400 kHz)
I (11282) pocket-tank: model partition 8192 KB mmap'd, advisor LLM
I (21253) display: 25.7 fps | render 17.5 ms flush 1.6 ms | ...
I (21254) display: frame 38.9 ms = polls 0.3 + tick 0.7 + prefetch wait 0.0 + render 17.5 + flush 1.6 + tail 0.0 + sleep 18.8
```

- **25.7 fps**, which is this project's own ceiling (`FRAME_MIN_MS`, 25 fps -
  frames and LLM decisions share one budget), so the port is no longer
  frame-limited: the render is 17.5 ms of the ~24 ms it is allowed.
- the tank, the save, the shop, the pages and the touch all run.
- **the advisor LLM: 10.6 tok/s, 4.06 s a decision** (the 1.8 board does ~12
  tok/s, ~3.7 s), against 2.2 tok/s and 19.5 s before the P4's PIE kernels
  existed. `q4_model.c` now carries a third kernel arm for this chip - the S3's
  four kernels translated one mnemonic at a time (`esp.vmulas.s8.xacc` into the
  40-bit `xacc`, `esp.srs.s.xacc` to read it out) - and the advisor checks them
  against the scalar dot at every boot. The measured jumps: the group-dot kernel
  19.5x, a 44-token prefill 11.8x, the single-token decode 1.8x. That last one
  is the remaining work, and it is not the dot any more: it is the scalar nibble
  *unpack* (the S3 does it with `ee.andq`/`ee.vsr.32` inside its `Q4_PK` kernel;
  the P4 arm still unpacks in C), which is why decode moved least.
  The trap that costs an afternoon: **every GPR operand of a PIE instruction must
  be one of twelve registers** - a0-a5, t3-t6, s0, s1 - and the assembler's
  "illegal operands" is the only warning you get (`zero` is not allowed as the
  read-out's shift, which needs a register holding 0).

Also worth doing, in the order they would be worth the time: the RX8130CE (a real
clock across a power-off), the BMI270 (auto-flip), the INA226 (the battery meter),
and the ESP32-C6 over SDIO for Wi-Fi (updates over
the air, the setup page's Wi-Fi step, the clock from the internet).
**[docs/tab5-next.md](tab5-next.md) is that queue**: each item with the
measurement that says it is missing, where the code goes, and how to verify it -
including the one that matters most, the advisor LLM's 2.2 tok/s (the P4's PIE
kernel has never been written; `q4_model.c` runs the scalar path here).

## Not done, and one thing that could not be run here

- **The five-world gate has not been run.** BOARDS.md's rule is that nothing in
  `common/` is done until `make -C sim check-all` passes in every world. The sim
  target exists (`make TAB5=1` -> `fishsim-tab5`, and `check-all` includes it),
  but this bench machine has no host C toolchain and no SDL2 (no make, gcc,
  clang or msys), so the selftests for the Tab5 world are **unverified**. The
  geometry this port adds is small (TANK_W/TANK_H, ALGAE_CELLS 576, VEG_SEG_PX
  3.5) but the pages' layout in a 720 x 405 world has only been seen on the
  glass, not asserted by a test.
- the touch's landing calibration is the shared 10 px bias only; the 1.8, the
  bowl, the watch and the square each carry a fitted 3x3 number, this one not
  yet (a fingertip's own scatter on a 5-inch glass is ~10 px, so it is usable
  as it stands - `director touch bias <px>` tunes it live).

