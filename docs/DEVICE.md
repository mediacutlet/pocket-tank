# The device: what is in flight, and the rules for touching it

Read this before ANY action that resets the tank (flash, esptool, power-off).
Battery measurements run in parallel with feature work; the tank's own
battery log is the instrument, and a reset used to wipe it. On 2026-09-15
the first full night of deep sleep on battery - the measurement the whole
09-11 battery pass was waiting for - was lost when the morning's model flash
reset the chip before anyone read the log.

## Rules

1. **Flash only through `tools/flash.sh`** (`--model` for the model partition
   too). It runs `tools/preflight.py` first, which archives `batlog` + `state`
   to `docs/batlog/<date_time>.txt`, and refuses to flash without the archive.
   Commit the archive with the flash. **The 2.8" CYD is the exception:** it is
   flashed with `tools/build_cyd.sh` (docs/CYD.md). It has no fuel gauge, so
   there is no battery log to archive, and this table is about the AMOLED tank.
2. **Any morning after a night on battery: `tools/preflight.py` first**, before
   the director, before a flash, before anything. Then update the table below.
3. **The log itself now survives resets** (batlog.c: RTC_NOINIT + magic + crc;
   only a power-on / PMIC power-off clears it), so a forgotten preflight is no
   longer fatal - but the archive is still the record between sessions.
4. **Every session that starts a measurement writes it in the table below,
   with what would spoil it.** Every session that ends one closes the row.
   The table is the handoff between parallel workstreams; HANDOFF.md carries
   the narrative, this file carries the state.

## In flight on the device

| started | what | spoiled by | status |
|---|---|---|---|
| 2026-09-15 | Deep-sleep current, first full night on battery (BOOT sleep, unplugged, read `batlog` at wake) | any reset before the read (now survives), a mid-night charge, waking it | **CLOSED 2026-09-16: the cell died overnight.** Morning archive docs/batlog/2026-09-16_0710.txt: the RTC log was wiped (a true power-off = the PMIC cut at the empty cell), first row 0% / 3232 mV. The 09-15 daytime windows had already put deep sleep at ~15 gauge-mA (2:22-2:37 15.2, 3:48-4:10 15.9) vs 4.7 for the old light-sleep drowse - deep sleep is WORSE than drowse. Cause (ESP-IDF sleep_gpio.c): digital pads are only isolated in deep sleep when `gpio_deep_sleep_hold_en()` was called, and the firmware never calls it; Espressif's own comment there: un-isolated, "the bottom current of deep sleep will be higher than light sleep". Fix + a fresh night: see HANDOFF 2026-09-16. Bedtime SoC unknown (the log died with the cell) - the sleep row should also go to NVS. |
| 2026-09-16 | **First night on POWER-OFF** (PWR key short press, unplugged - the power-off comes 20 min after the press now -, a HELD PWR press in the morning, then `tools/preflight.py`: the `bed` row from NVS + the boot row give the night's mA; expect the gauge not to move) | a USB plug-in (it powers the PMIC on), a press | **CLOSED 2026-09-17: the power-off works.** Archives docs/batlog/2026-09-17_0739.txt + _0746.txt: `bed` 63% / 3826 mV at the power-off (~23:18, 20 min after the sleep press), the tank read 62% a minute after the morning boot (07:38, on USB by then) and the 5-min boot row 64% (charging). 8 h 28 min for ~1% = ~2 mAh: at or under ~0.25 mA, the gauge's 1% step (2 mAh) being the floor of what it can resolve - consistent with the datasheet's 40 uA, vs ~15 gauge-mA for the un-isolated deep sleep that killed the cell and 4.7 for the old drowse. The tank lived the night (grass to 1.00, 68 algae cells, two fish begging). Caveat: the boot row lands 5 min after boot, so a tank plugged in at wake is already charging when it is written - an immediate boot row would make the number clean. The evening is what drains the cell: 79% on the charger at 20:29 -> 63% at bedtime. |
| 2026-09-15 | Awake draw with 4 fish + the boredom re-asks (09-14 batlog: 72-120 mA awake at 60% brightness, 96% -> 52% in an hour) | brightness changes, charging mid-window | PARKED 2026-09-17 by Strato: "battery is at a manageable level, we may open this back up at a later point. but what we are trying to solve for has been achieved, long term sleep mode." Nothing is in flight; to reopen: measure a clean hour on battery, then look at ask cadence with 4-5 fish |

## How to resume the battery work (everything needed is committed)

1. **Deep-sleep night.** Wake the tank, unplug it, short-press BOOT, leave it
   overnight. Morning: plug in, `tools/preflight.py` FIRST. The archive shows
   the `sleep` and `wake` rows and the "asleep: N h, X mAh -> Y mA" line; the
   gauge's %-scale runs ~0.6x (docs/HANDOFF.md, the battery pass), so compare
   nights, don't trust the absolute. Target: the ESP32-S3 deep-sleep floor
   with touch/codec rails still up on VCC3V3 - expect single-digit mA or
   better; if it is not, `pmic` on the director dumps the rails.
2. **Awake draw, 4 fish.** Unplug at a known SoC, leave the tank awake and
   untouched for an hour at 60% brightness, preflight. Compare with 09-14's
   72-120 mA. The lever is LLM duty: the boot log's "asks / decisions" line
   (`t=...s 4 fish goals: ... | asks N decisions M`); with 4 fish the core is
   ~84% busy (13.6 decisions/min x 3.7 s). Knobs: ADVISOR_MIN_INTERVAL /
   ADVISOR_IDLE_CEILING and the state_signature bands in common/tank.h /
   tank.c (the 09-11 pass cut asks 55% by calming the signature; the boredom
   band of 2026-09-14 added ~2 asks/min/fish while idle).
3. Record results in this table and in docs/stats.md; close the row.

## Firmware on the tank right now

- App: everything through the 09-15 night (sand dollars, the shop, the
  snail, audio, settings, the light) PLUS 2026-09-16: the PWR key is the one
  sleep/wake key, sleep = 20 min grace (was 90 s; 2026-09-16 evening) then PMIC power-off, cold boot lives
  the absence, deep sleep (bench / no-PMIC) holds + isolates its pads,
  bedtime batlog row in NVS, director `poweroff` (flashed 2026-09-16
  morning; archive docs/batlog/2026-09-16_*.txt), the GLASS gate + the
  snail's card (midday), and the CASTLE - shop item 3, BEHIND / IN FRONT
  of the grass (flashed 2026-09-16 13:17; archive docs/batlog/2026-09-16_1317.txt),
  and the SNAIL drawn procedurally, crawling (flashed 2026-09-16 14:37;
  archive docs/batlog/2026-09-16_1437.txt); and everything through
  2026-09-24 - the frond ceilings, the coral, the fry SPAWNING (a staged
  fry is born on its own, no light-on; flashed 2026-09-24 11:04, archive
  docs/batlog/2026-09-24_1104.txt), the reef cluster + SELL, and the
  battery page with the color-blind-safe pill (bolt + sweep; flashed
  2026-09-24 13:15, archive docs/batlog/2026-09-24_1315.txt). The battery
  page's history lives in NVS namespace "bat" (fresh at that flash).
- Model partition: v4m (model_q4_v4m.bin, flashed 2026-09-15 ~06:00).
- Save: mem + lira (juveniles, 0.7 h old, 20 sand dollars) at the 2026-09-24 13:15 flash - the morning's lori / mira / bolt / sol / kelp were already gone in that preflight archive (wiped between 11:16 and 13:15).
