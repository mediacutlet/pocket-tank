# Tab5: what was left, and where each item stands

Items 1, 3, 4 and 5 are done and verified on the unit; 2 (sound) is written but
blocked by the chip's I2S, with the whole trail below; 6 (the radio) is
deliberately not brought up, with the argument for that in its section. The two
things that are *unverified* rather than undone are at the end. Every number
here is measured on the bench unit - ST7123 glass, silicon rev v1.3 - not
estimated.

## 1. The advisor LLM on the P4: done, 2.2 -> 10.6 tok/s

**Measured:** a decision went from ~19.5 s at 2.2 tok/s to **4.06 s at 10.6
tok/s** (the 1.8 board: ~12 tok/s, ~3.7 s) - the project's own line is that
"decisions past 4 s are too slow", so this board is now on it rather than far
past it.

**What landed:** `common/llm/q4_model.c` has a third kernel arm for this chip.
The S3's asm translated one mnemonic at a time - `esp.zero.xacc`,
`esp.vld.128.ip`, `esp.vmulas.s8.xacc` (the same 40-bit scalar accumulator the
MAC sums its 16 int8 products into) and `esp.srs.s.xacc rd, rshift` reading it
out where the S3 says `rur.accx_0 rd`. Boot bench, before -> after:

```
dot100k        164706 us ->   8462 us   (19.5x)
batch44 flash  415717 us ->  35340 us   (11.8x)
batch44 ram    412987 us ->  32608 us
decode flash    21593 us ->  12008 us   ( 1.8x)
decode ram      18999 us ->   9322 us
```

**The trap, for whoever touches this next:** every general-purpose operand of a
PIE instruction must be one of **twelve registers** - a0-a5, t3-t6, s0, s1. The
assembler's `illegal operands` is the only warning, and `zero` (x0) is not one of
them, so the read-out's shift needs a register holding 0. The pointers are pinned
with local register variables; esp-dsp's `*_arp4.S` moves its argument registers
into t3/t4 for the same reason.

**Where the time goes now, if anyone wants more** (not needed for the bar): the
*single-token* path still unpacks each 4-bit group in C (`unpack_group`), and the
PIE dot is now fast enough that the unpack dominates the decode - one w1 matrix
costs 0.52 ms of dotting against 12.0 ms total. The S3 answers this with its
`Q4_PK` kernel (`ee.andq` + `ee.vsr.32`, unpacking inside the vector unit); the
P4 arm sets `Q4_PK 0`. A port is *possible* but was not done: the P4 exposes
shifts only through its `src`/`srci` family, and a hardware probe (feeding
`0x10..0x2f` through `esp.srci.2q q, q, 4` and `esp.src.q` with `sar.bytes`)
matched no simple shift interpretation - `srci.2q` added 8 to the first half's
bytes and the funnel shift returned zeros - so those semantics want a datasheet
and not a guess. The low-risk version of the same idea is a 256-entry
byte -> two centred int8 values lookup table in `unpack_group`.

## 2. Sound: done - the preload, a stuck peripheral, and direct writes

**It plays.** The tank's own log at the end of a cue, on the bench unit:

```
audio: ES8388 ready at 0x20: 17 of 24 cues, bank 518 KB in flash, volume 1
audio: up in 209 ms (preloaded 320 bytes)
audio: played 577 blocks (184640 bytes, 5770 ms of audio) before going quiet
audio: down (idle)
```

577 consecutive blocks with no failure, where a day earlier the third write of
every attempt timed out - and **confirmed by ear** on 2026-10-17 ("我的确听到了"),
at volume 2, with the audio chain's far end (the ES8388's output mixer and the
speaker amp on the IO expander) untouched from the BSP's own defaults. Three
things had to be true, and a 60-line standalone probe
(`E:\development\tab5-i2s-probe`, kept outside `.tmp`: `new_channel` -> `init_std_mode` ->
`preload` -> `enable` -> `write`, printing every return code, plus the
peripheral's own `bck_cnt`) is what separated them.

**1. `i2s_channel_preload_data()` is required, while the channel is disabled.**
Without it the DMA descriptor queue is empty from the start and every
`i2s_channel_write` waits for a free descriptor that never comes
(`ESP_ERR_TIMEOUT`/263, 0 bytes) - exactly the silence the tank had. It accepts
only what fits the DMA buffer (3072 bytes at the default 6 x 512 geometry: a
3200-byte preload loads 3072, and a write larger than the buffer also times out),
and it is refused with `ESP_ERR_INVALID_STATE` ("data can only be preloaded when
the channel is READY") while the channel is enabled. So the order is
disable -> preload -> enable.

**2. The codec dev's I2S data interface is not the data path here.**
`esp_codec_dev_write` times out with 0 bytes where a direct `i2s_channel_write`
on the same channel moves everything (measured back to back in the port). The
ES8388 keeps `esp_codec_dev` for its registers, volume and mute; the samples go
straight to the channel. This is also why the port's samples work on
**esp_codec_dev 1.5.11** (what the registry BSP asks for) exactly as they do on
1.3.4: measured both ways, 577 blocks each. No vendoring and no version pin is
needed for that.

**3. The peripheral had to be cleared.** With the preload in place, writes moved
data but only the first two, after which every write timed out and
`bck_cnt.tx_bck_cnt` froze - identical in every shape tried (TX-only and duplex,
RX in TDM, 16/48 kHz, MONO/STEREO, MCLK on or off, `bclk_div` 0/8, `auto_clear`
on/off, `dma_frame_num` and `dma_desc_num` at either end, all three clock
sources, IDF 5.5.6 and 5.4.2). What cleared it was **writing the whole flash
again** (partition table included; it happened while flashing M5's own demo and
coming back). A control experiment crossing "disable+enable before every write"
against "a 20 ms delay between writes" then had all four variants pass 8/8, so
neither of those was ever the variable - a reset that reaches the peripheral is.
**Next silent board: re-flash the whole chip or power-cycle it before believing
an I2S is broken.**

**Two claims in the earlier version of this file were wrong and are corrected:**
the "dead BCLK pad" reading was an artefact (the pad's input path was never
enabled - the peripheral's own counter is the honest instrument, and it runs), and
`I2S_CLK_SRC_DEFAULT` is not a pre-v3 trap in IDF 5.5 (`i2s_ll.h` maps it to XTAL
under `CHIP_SUPPORT_MIN_REV < 300`), so the port's explicit `APLL` guess is gone.
The reference that made the hunt worth continuing was M5Stack's own Tab5 demo
([M5Tab5-UserDemo](https://github.com/m5stack/M5Tab5-UserDemo)), which plays audio
on this hardware; it is built and flashed from `E:\development\.tmp\M5Tab5-UserDemo`
whenever a baseline is needed.

**The port also keeps what the hunt taught about honesty:** `bring_up()` bounds
itself (the first failed write ends the attempt, says why once, later cues are
answered without touching the I2S, `snd` prints the state), and every cue ends
with the byte count above - the proof that the stream ran, with the ear as the
last link rather than the first.

## 3. The clock: done - the RX8130CE

**Shipped:** `rtc_port_rx8130.c` in `firmware/components/tankcore/`, chosen by
target in that component's CMakeLists (the four Waveshare boards keep the
PCF85063). This board has a Seiko **RX8130CE at 0x32**, a different register map
(0x10..0x16 BCD, 0x17 bit 7 = the "supply dipped" VBL flag, 0x1F = the
supplied-in reset/test state).

**Verified on the unit:** the first boot seeded it from the build time
(2026-10-16 22:43:57), the next reset read it back and set the system clock
(22:44:16, ~19 s later - so the chip is ticking, not just holding).

**Two things the first version got wrong**, both now guarded: a factory-fresh
chip answers with bytes that are not BCD at all (0xA7 reads through bcd2bin as
year 107 = "2107", which passes a "since 2024" test), so every register is
checked nibble-wise with the year windowed to 2024..2100; and a chip left in its
reset/test state (0x1F bit 6/7) does not tick, so that state is cleared at init,
loudly.

## 4. The flip: done - the BMI270

**Shipped:** `imu_port_bmi270.c`, chosen by target in `main/CMakeLists.txt`. The
driver is Espressif's `espressif/bmi270` (declared for the P4 in
`idf_component.yml`), because this chip does not run its data path until an 8 KB
configuration blob has been downloaded into it - not something to do by hand.

**The physics is the QMI8658 port's, unchanged**, in g instead of counts: flip
0.214 g, motion 0.0134 g, a 250 ms poll, three polls the other way up before
flipping, "handled" requiring two polls in a row. The component works in g and
the contract wants counts at +-2 g (the director's `imu` prints them), so the
scaling happens at that boundary.

**Calibrated on the unit** (standing in its stand, tilted back ~30 degrees):
g = [+0.866 +0.070 -0.514] - the screen's vertical in landscape is the panel's
short axis X, so up = +X. Verified: the driver initialises, the samples are
stable, no spurious flip fires.

## 5. The gauge: done - a 2S Li-ion pack, watched by the INA226

**This board does have a battery** (2S Li-ion; the bench unit reads 8.17-8.21 V,
4.09-4.11 V/cell, ~90-93%), and its **INA226** (I2C 0x41, verified by its TI ids
0x5449/0x2260) is the monitor for it - so `battery_port_tab5.c` is a real gauge,
not a placeholder:

- the bus voltage is the pack (1.25 mV/LSB), the shunt the current (2.5 uV/LSB,
  measured -1392.5 uV = -278 mA at 5 mOhm = 2.3 W with the tank running);
- the percentage is a 2S Li-ion open-circuit table (8.40 V = 100% ... 6.00 V =
  0%) interpolated linearly, with the pack's internal resistance compensated
  while discharging (V_open = V + I_discharge x 150 mOhm) so a load does not read
  as a flat battery;
- the state follows the current's sign: charging (>= 30 mA), full (charging and
  >= 8.30 V), on battery (<= -30 mA), plugged (the cable in, the charger resting);
- `set_rail`/`trim_rails`/`pin_rail` are no-ops (no PMIC), `poweroff()` is false
  (nothing to cut), the key hooks are empty (the side button is PWR/RST/BOOT in
  hardware), and `charge <mA>` reports what the monitor sees rather than setting
  anything - the charger is the board's own IC.

**Two numbers here are assumptions, and both are said so in the log**: the shunt
resistance is *assumed* 5 mOhm (the schematic is not in this repo; the chip's own
current register agrees with the shunt arithmetic at 5 mOhm), and the sign
convention - which way round IN+/IN- sit - cannot be read off the chip. The
hypothesis "negative current = charge flowing in" is what the bench showed with
the cable in (and the pack's voltage rose 8173 -> 8210 mV across it, and the
board's own `batlog` history shows 8160 -> 8196 mV rising after a replug), which
is why the gauge reports "cable in ... charging" at 93%. If a future unit comes
out the other way, `CHARGING_SIGN` is the one constant to flip.

**Verified:** the tank logs `battery 93% 8210 mV`, shows its own "cable in at 93%
(charging) - the pill shows 6 s", and `pmic` dumps bus/shunt/current/power/state.

## 6. The radio: deliberately not brought up, and here is the argument

**The tank does not need the internet.** The advisor is on-device (7.5 MB Q4
model in flash, 10.6 tok/s on this chip), the save is local NVS, and every cue
and every rule is local. What the radio adds is exactly three conveniences, in
order of value:

1. **over-the-air updates** (from 0.3 a tank updates itself) - without it the
   cable is the only path, which works today (`tools/flash_tab5.ps1`);
2. **first-time provisioning over the installer page** (Improv: tell it your
   Wi-Fi from a browser) - without it the cable is again the path;
3. **network time** - and the Tab5 now has its own RX8130CE (item 3), so this
   one is already covered.

**Nothing hangs or lies without it.** `net_port_stub.c` answers *every* network
call at once with `NET_FAILED`/`NET_ERR_RADIO` (its own comment: "so no page
waits on it"), and `update.c` turns that reason into its own page: "NO RADIO -
THE WI-FI RADIO DID NOT START. TRY AGAIN LATER". The scan/connect/check/install
calls all return immediately, and `net_time_sync` returns false.

**What it would take**, if it is ever wanted: the P4 has no radio of its own, so
Wi-Fi 6 comes from the board's **ESP32-C6 over SDIO** - `espressif/esp_hosted`
with the C6's slave firmware in a new partition, `esp_wifi_remote`, the
expander's WIFI_EN (0x44 bit 0), and a replacement for `net_port_stub.c` with
`net_port_esp.c` on this target. That is a multi-hour, partition-touching job
whose only payoff is items 1 and 2 above, and - given that the I2S peripheral
turned out not to start on this pre-v3 silicon (item 2) - it may well meet its
own platform surprises. Left undone on purpose, not by oversight.

---

## Unverified, not undone

1. **The five-world gate.** BOARDS.md: nothing in `common/` is done until
   `make -C sim check-all` passes in every world. `make TAB5=1` and `check-all`'s
   fifth line exist, but the bench machine has **no host C toolchain and no
   SDL2** (no make, gcc, clang, msys), so the Tab5 world's selftests have never
   run. The geometry this port adds is small (`TANK_W/H` 720x405,
   `ALGAE_CELLS` 576, `VEG_SEG_PX` 3.5), but the pages' layout in a 720 x 405
   world has only been *seen*, not asserted. Run it on a machine with a host
   toolchain before this is called finished.
2. **The touch's landing calibration.** The 1.8, the bowl, the watch and the
   square each carry a fitted 3x3 ("reported = a x + b"); this one uses the
   shared 10 px bias only. A fingertip's own scatter on a 5-inch glass is ~10 px,
   so it is usable as it stands - `director touch bias <px>` tunes it live, and
   `tools/touch_calib.py` does the fitting when someone wants it exact.

## The port's own numbers, for the next reader

```
Discovered board version 2 (LCD ST7123, Touch ST7123, FW 3)
panel up (720 x 1280, 2 frame buffers)
touch ready (ST712x at 400 kHz)
25.7 fps | render 17.7 ms flush 1.6 ms
frame 38.9 ms = polls 0.3 + tick 0.7 + prefetch wait 0.0 + render 17.7 + flush 1.6 + tail 0.0 + sleep 18.6
flush parts (Tab5): prefetch issue 0.0 + port 1.1 + touch 0.5 = 1.6 of 1.6
```

25.7 fps is this project's own ceiling (`FRAME_MIN_MS`, 25 fps: frames and LLM
decisions share one budget), so the tank is not frame-limited here - the render
uses 17.7 ms of the ~24 ms it is allowed, and the 18.6 ms "sleep" is the cap
working. The frame *before* item 1 lands is what limits this board.
