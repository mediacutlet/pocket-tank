# Audio — requirements (draft, 2026-09-15)

Beeps and boops for Aqua Pets: keeper feedback (a tap landed, food
dropped, the light flipped) and a few fish moments (a pellet eaten, a spook,
a birth). Sounds are feedback, never behavior: the advisor still owns every
decision and no sound ever changes what a fish does.

Status (2026-09-15 afternoon): BUILT, NOT FLASHED. The bank, the mixer,
the notices, the sim's SDL player and the device's I2S/ES8311 port are in
the tree and both firmware configs build (board 1.1 MB with the 580 KB
bank embedded, QEMU with the stub). The codec bring-up sequence is written
from the ES8311 user guide and the Espressif driver, unverified on the
bench: the first flash is the test. `docs/HANDOFF.md` has the bench list.
Volume + mute: the settings page (2026-09-15 night; from the milestones
page's SETTINGS button) - BRIGHTNESS 10 .. 100 by tens between two arrows (since 2026-10-10; four segments before) and VOLUME OFF/QUIET/NORMAL
as segment buttons; director `snd off|quiet|normal` still works.

## 1. What to hand over (sound files)

Any of these is fine as a source; the build tool converts them:

- **WAV** (preferred), **AIFF**, or **FLAC**. MP3/OGG work too but add
  encoder mush to sounds that are already 100 ms long, so avoid them.
- Any sample rate and bit depth. Mono preferred; stereo is folded to mono.
- **Short.** Most cues 60 to 400 ms; the longest (welcome, birth fanfare)
  under 2 s. The speaker is 12 x 10 mm, so there is no low end below
  ~600 Hz to spend time on. Design in the 800 Hz to 6 kHz band.
- **Tight ends.** No leading silence, a real fade to zero at the tail (a
  hard cut on a 12 mm speaker clicks). The tool trims silence below -60
  dBFS at both ends and adds a 5 ms fade, so long exported tails cost
  nothing.
- **Level:** export hot (peak near -1 to -3 dBFS). The tool peak-normalizes
  every cue and stores a per-cue playback gain in the manifest
  (`assets/sounds/gains.csv`, seeded from the levels of the first export
  batch), so relative loudness is tuned in one text file without re-export.
  The device applies volume and night attenuation on top.
- **Nothing below ~600 Hz counts.** The speaker cannot reproduce it, but it
  still eats headroom and amp power. The tool high-passes at 600 Hz; a cue
  whose energy is mostly down there comes out faint and thin on the device.
  `working-assets/sounds/speaker-preview/` holds a 16 kHz high-passed
  preview of each export: that is roughly what the speaker will say.
- **Mono.** Wide stereo cues lose level when folded (see the inventory):
  export mono when the sound is built from panned layers.
- **Variants welcome.** Two to four takes of the frequent sounds (eat, tap)
  stop them from being annoying; the player picks one at random.

Naming: `snd_<event>[_<n>].wav`, one file per cue, in `assets/sounds/`.
Events are the ids in section 4 (`snd_eat_1.wav`, `snd_eat_2.wav`,
`snd_light_on.wav`, ...). A short `NOTES.md` beside them saying what each
cue is for and any preference (pitch per fish, never at night, ...) is worth
more than perfect files.

The canonical on-device format is **16 kHz, mono, 16-bit signed PCM**,
produced by the tool, never by hand. Rationale: 32 KB per second of audio,
a full set of ~25 cues at ~0.4 s average is ~300 KB, and the factory app
partition (2.5 MB) has ~2 MB free. No compression needed. 16 kHz gives a
7 kHz ceiling, more than the speaker delivers.

## 2. Hardware (from `resources/ESP32-S3-Touch-AMOLED-1.8.pdf`)

| Part | Detail |
|---|---|
| Codec | ES8311, I2C 0x18, mono DAC + ADC, "14 mW playback and record" |
| Amp | NS4150B class-D, enable = **GPIO46** (`PA_CTRL`, 10k pulldown: off unless driven high) |
| Speaker | H2, 12 x 10 mm, 8 ohm, 1 W, soldered to the board |
| Mic | analog, into the ES8311 ADC (unused by this feature) |
| I2S | MCLK **GPIO16**, BCLK/SCLK **GPIO9**, LRCK/WS **GPIO45**, DAC data (ESP -> codec, `DSDIN`) **GPIO8**, ADC data (codec -> ESP, `ASDOUT`) **GPIO10** |
| Rails | codec digital + amp on VCC3V3 (DCDC1, always on); codec AVDD + mic on A3V3 = **ALDO1**, which `battery_port_trim_rails()` turns OFF at boot |

GPIO46 is a strapping pin (ROM log control), sampled only at reset; the
pulldown keeps the default, and driving it high at runtime is harmless.

## 3. Battery rules (the non-negotiables)

Measured context: awake ~96 mA, drowse floor ~4.7 mA, cell ~120 mAh usable
(`docs/HANDOFF.md`, battery pass). Codec figures: ~4 mA playing, tens of
uA powered down; amp ~3 mA enabled idle, <1 uA disabled.

1. **The amp is enabled only while a sound plays** (plus a short idle
   window, section 5). Never left high between sounds.
2. **Codec fully powered down and ALDO1 off before any sleep.** Hook in
   `enter_sleep_for()` before `esp_light_sleep_start` / deep sleep, and in
   the drowse entry. A stuck amp adds 3 mA to a 4.7 mA floor: that is the
   one way this feature can wreck the battery work.
3. **Awake budget: under 2% of awake draw** at a normal cue rate (a few
   hundred cues per hour). A 200 ms cue at moderate volume costs ~0.003
   mAh; the idle codec+amp between rapid cues is the only real cost, so the
   idle window is short.
4. **Verify, don't assume:** one batlog night after the feature lands must
   show the same drowse slope as nights 1 and 2 (`tools/preflight.py`
   first, as always). One awake battery hour with cues on vs off.
5. **No frame cost.** The tank task only pushes an event id into a ring;
   mixing and DMA feeding run on core 1. Never drop frames for audio.

## 4. Sound vocabulary

Tiers are build order. Tier 1 alone is a shippable feature.

**Tier 1 — keeper feedback (what you did registered)**

| id | when | notes |
|---|---|---|
| `tap` | any tap on the glass that is not a feed | tiny, dry, ~60 ms |
| `feed` | surface tap / pellets drop (`tank_feed`) | a plink or splash; once per drop, not per pellet |
| `light_on`, `light_off` | the light flipping: MANUAL (default) = the double-tap; AUTO = a pick-up / touch after the idle time, and going idle | on rises, off falls |
| `wipe` | algae squeegee stroke engaged | soft, loopable while the stroke moves? v1: one cue per stroke |
| `snip` | a frond cut (`veg_cut` returns > 0) | one per cut, rate-limited 150 ms |
| `card_open`, `card_close` | stats card up / dismissed | quiet |
| `wheel_tick`, `confirm` | setup and birth-flow letter wheel detent, YES/confirm | tick must be < 40 ms; the wheel spins fast |
| `bubbles_loop` | the setup's bubble-placement page (`SETUP_PG_BUBBLES`) is up | a LOOP, not a one-shot: starts when the page opens, loops seamlessly while the keeper drags the column, stops with a 50 ms fade when the page is left. The only looping voice in the design; the mixer needs a loop flag and a crossfaded loop point (section 5) |

**Tier 2 — fish moments**

| id | when | notes |
|---|---|---|
| `eat` | a fish takes a pellet | 2-4 variants, max one per 250 ms, pitch by stage (fry high, elder low) |
| `spook` | 3-tap aggression fires, fish flee | once per spook episode, not per tap |
| `investigate` | a long hold draws a fish over (trust earned) | gentle, once per hold |
| `bubbles` | a fish reaches the column for play (`GOAL_VISIT_BUBBLES` arrival) | DEFERRED by Strato 2026-09-15 after the first listen: fish visit the column too often for a cue to mean anything. The event still fires; the id stays |
| `beg` | a hungry fish begging at the glass | DEFERRED by Strato 2026-09-15; if it comes back: very quiet, cooldown 30 s per fish, never at night |

**Tier 3 — progression and system**

| id | when | notes |
|---|---|---|
| `welcome` | a fresh install: the first-run setup's welcome page comes up (`setup_begin` when `progression_setup_pending()`) | the tank's very first sound; a reset (BOOT+tap, YES) lands here too, so it is also the "new tank" sound. Up to ~2 s. Plays once per setup flow, never on an ordinary boot |
| `arrival` | a fry arrives / birth flow announce | the one "big" sound, ~1.5 s |
| `milestone` | a milestone or checklist line earned | plays with the milestone modal (section 4a) |
| `stage_up` | fry -> juv -> adult -> elder | |
| `sleep`, `wake` | PWR key press to sleep / wake | DEFERRED by Strato 2026-09-15, no cue yet; wake would play after the codec is up, so ~100 ms into the boot |
| `coin` | sand dollars earned (the "+N" toast, render_sd_toast) | not yet: no cue in the bank; the toast is silent. A purchase plays `confirm` |
| `low_battery` | gauge falls to 10% | once per discharge, with the low-battery modal and the persistent pill (section 4a) |
| `error` | save failed, reset prompt | |

Not in scope: music, ambient loops, a per-decision sound (the LLM decides
every ~4 s per fish; that would be constant noise), any sound in drowse.
Deferred by Strato (2026-09-15): `tap`, `wipe`, `card_close`, `sleep`, `wake`, `beg`, `bubbles`.

### 4a. UI that goes with the sounds (2026-09-15)

**Milestone modal.** Today a newly earned milestone only shows as a ring on
the milestones page the next time the keeper opens it (`ms_bits & ~ms_seen`).
New: the moment a fish or tank milestone bit is set (`set_tms` and the fish
equivalent in `progression.c`), a modal announces it over the live tank in
the style of the milestones page's detail modal (badge art at integer
scale, the milestone name, the fish's name where it is a fish milestone),
and `milestone` plays. Tap anywhere dismisses it; it also times out after
~6 s. It does NOT mark the milestone seen (the ring on the page stays until
the keeper visits, as now). Milestones earned during drowse or a save
restore queue and show one at a time on the next awake frame, spaced ~1 s.
Never over the setup or birth flow: queue behind them. Save semantics
unchanged.

**Low battery.** Today the battery pill (`render_battery`, top right) only
appears with a fish's stats card. New:
- When the gauge falls to **10%** and is not charging: `low_battery` plays
  once and a modal says the tank needs charging (short text, the pill
  drawn large). Tap dismisses; it times out after ~6 s. Once per discharge:
  the flag clears when charging is seen or the gauge is back above the
  hide threshold.
- While low, the pill stays on screen at the top right on every frame,
  over the tank, under any card or page. It leaves once the gauge reads
  **above 10%** for 30 consecutive seconds, or immediately when charging
  starts (the pill then shows the charging state until it clears).
- The gauge is read once a second already (main.c); reuse that sample.
  Caveat from the battery pass: the AXP2101 %-scale is not linear and sat
  at 100% for the first half hour of a discharge, so 10% by the gauge is
  the trigger, whatever that is in real mAh. If the batlog shows the pill
  appearing with hours of life left, the threshold moves, not the design.

## 5. Firmware design

New port pair, same pattern as display/touch (`audio_port.h`, an ESP
implementation and a stub for QEMU/bring-up):

```
bool audio_port_init(i2c_master_bus_handle_t bus);   // finds the ES8311, leaves it DOWN
void audio_port_play(int id, float gain, float pitch); // enqueue; safe from the tank task
void audio_port_set_volume(int level);                // 0 off, 1 quiet, 2 normal
void audio_port_stop(int id);                         // fade a looping voice out (50 ms)
void audio_port_sleep(void);                          // amp low, codec down, ALDO1 off; blocks until silent
```

- **Bank:** `sounds.bin` (concatenated 16 kHz s16 PCM, normalized,
  600 Hz high-passed, trimmed) + `sounds.h` manifest (id enum, offset,
  length, variant count, playback gain in 1/256), generated by
  `tools/make_sounds.py` from `assets/sounds/` and `gains.csv`. Embedded with `EMBED_FILES`
  next to `tokenizer.bin`; it lives in flash-mapped `.rodata`, so no RAM.
  The manifest is shared with the sim.
- **Player task:** core 1 (the LLM core; the advisor runs at priority 5 and
  is bandwidth-bound, not latency-bound), priority 6, small stack. Wakes on
  an event or a DMA half-empty callback. Software mixer: up to 3 voices,
  int32 accumulate, clip, 5 ms fade-in and fade-out on every voice.
  Pitch = fixed-point resampling step (1.0 = native). DMA: 2 x 10 ms.
  One voice may LOOP (`bubbles_loop`): the tool bakes a 20 ms equal-power
  crossfade across the loop point so the seam is silent, the mixer wraps
  the read index, and `audio_port_stop(id)` fades it out over 50 ms. A
  looping voice keeps the codec and amp up for as long as it runs, which
  is fine: it only exists inside the setup flow, on the charger or not,
  for the seconds it takes to place the column.
- **Power sequence** on the first cue after silence:
  ALDO1 on -> ES8311 up (the `espressif/es8311` registry component has the
  init sequence; MCLK 256 fs = 4.096 MHz) -> I2S TX with zeros ~20 ms ->
  GPIO46 high -> mix. Target under 60 ms from event to sound; the
  `wheel_tick` cue is the one that will show latency, so the codec stays up
  for the whole of a setup/birth flow.
  On a fresh install the welcome cue is the first thing the player does
  after `audio_port_init`, so init must not wait for a touch: bring the
  codec up as soon as the setup flow is pending and keep it up for the
  whole flow (wheel ticks, confirm).
  Idle: 2 s after the last voice ends -> GPIO46 low -> stop I2S -> ES8311
  back to the powered-down register set (`0x00=1F 0x01=00 0x0D=F8
  0x12=02`, already in `codec_port.c`) -> ALDO1 off.
- **Pops:** amp enabled only after the DAC is streaming zeros, disabled
  before clocks stop; every voice fades; the ES8311 DAC ramp (its
  user guide covers the soft-ramp bits) stays on.
- **Rate limits:** per-id cooldown from the tables above, plus a global cap
  of 6 cues per second (excess dropped, never queued late).
- **Night:** when the tank light is off, Tier 2 cues are muted and Tier 1
  cues play at -12 dB. Drowse/sleep: nothing, and `audio_port_sleep()` runs
  before the sleep call.
- **Volume:** three levels, saved in NVS (`tank/snd`). Default: normal.
  Set on the settings page (render_settings; the milestones page's SETTINGS
  button opens it), by the director (`snd off|quiet|normal`), or the sim's
  V key. Picking QUIET or NORMAL plays the confirm cue at that level. Director: `snd <id>`, `snd off`,
  `snd list` for b-roll; the existing `codec` dump stays.
- **The about page's jingle (2026-10-10):** the one long sound, and it is
  not in the bank - a 30 s clip would be 960 KB of flash. `audio_jingle(on)`
  (device: `audio_port_jingle`) runs a small synthesiser inside the mixer:
  twelve bars of 4/4 at 96 BPM (a 16th is exactly 2500 samples, so the loop
  is 480,000 samples = 30.000 s and wraps on a sample) - a kalimba-like
  arpeggio through the bar's chord, a hummed sine melody with vibrato, a
  soft root-and-fifth pad, bubble chirps - under a slow amplitude wobble
  (seven a loop, so it is periodic too) and a one-pole low-pass. Everything
  sits above the speaker's ~600 Hz floor (octaves 5 and 6). Notes still
  ringing at the wrap carry over, so the seam is silent; the sequencer just
  starts its next bar. Fades in over 0.1 s and out over 0.4 s, follows the
  VOLUME setting and the master level, counts as a live voice (the codec
  stays up while it plays; `audio_stop_all` / a sleep ends it). Since that
  night each theme has its own tune on the same four voices (`J_SONGS`, a
  `jsong_t` each: the 16th's sample count - an integer, so every loop still
  wraps on a sample -, chords, arpeggio figure with rests, melody, chirps,
  pluck timbre and decay, pad level, vibrato, wobble, low-pass): Original the
  kalimba at 96 BPM (30 s), Quiet Lagoon a slow Lydian lullaby on a muffled
  marimba at 72 (40 s), Tidepool Club a perky steel-pan tune at 120 (24 s),
  Blackwater sparse D-minor bells over a fuller pad at 60 (48 s). The tune is
  chosen from the active theme when the jingle starts from silence. Starts when
  the settings page's ABOUT page opens (`SET_TAP_ABOUT`, value 1) and stops
  when it closes (value 0).
- **Sim parity:** `sim/` gets the same `audio_port.h` on SDL2 audio
  (`SDL_QueueAudio`, 16 kHz s16). Same bank, same mixer code in `common/`
  so pitch and cooldown behave identically on the desk and on the device.
- **Host tests:** the mixer and rate limiter are pure C in `common/`;
  `host_test/` gets cases for cooldown, clipping and the fade.

## 5a. Asset inventory (first export batch, 2026-09-15)

Source: `working-assets/sounds/exports/` (19 files, all 32-bit float
stereo WAV at 44.1 or 48 kHz; the tool converts). Measured on the mono
fold; "after HP" is what survives the speaker's 600 Hz floor.

| file | cue | dur | peak | after HP peak | verdict |
|---|---|---|---|---|---|
| sound-wheel_tick | wheel_tick | 0.06 s | -1.0 | -0.9 | good |
| sound-light_on | light_on | 0.06 s | -1.7 | -1.6 | good (re-export 13:15: mono, hot) |
| sounds-light_off | light_off | 0.36 s | -1.5 | -2.1 | good (re-export 13:16: mono, hot); file still named `sounds-` not `sound-` |
| sound-eat | eat | 0.18 s | -4.9 | -2.5 | good; wants 2-3 more variants |
| sound-plant_snip | snip | 0.53 s | -9.5 | -8.7 | good |
| sound-feed | feed | 0.27 s | -19.3 | -20.0 | good but quiet; gain fixes it |
| sound-card_open | card_open | 0.25 s | -15.3 | -21.6 | ok, a third of it is sub-600 Hz |
| sound-investigate | investigate | 1.5 s | -19.0 | -21.6 | good; 1 s silent tail (trimmed) |
| sound-bubble_cluster | bubbles | 2.0 s | -3.0 | -3.9 | good, but DEFERRED after the first listen on the device (too frequent) |
| sound-bubbles_loop | bubbles_loop | 2.0 s | -11.1 | -23.6 | loops cleanly (seam step within the file's own sample-to-sample range, both ends near -40 dBFS; the last 250 ms sit ~5 dB under the first, the crossfade hides it). 92% sub-600 Hz so it will be faint on the device: fine for a background loop, or re-do brighter |
| sound-stage_up | stage_up | 2.75 s | -1.8 | -2.6 | good |
| sound-milestone | milestone | 2.5 s | -2.9 | -6.7 | ok; loses 7 dB of body on the speaker |
| sound-fry_arrival_v2 | arrival | 1.24 s | 0.0 | -4.5 | good (v2 13:38); the top end now carries it |
| sound-low_battery_v2 | low_battery | 1.5 s | -1.2 | -8.8 | good (v2 13:40) |
| sound-welcome_v2 | welcome | 2.0 s | -3.4 | -5.9 | good (v2 13:30): 22 dB more speaker-band level than v1, and now the 2 s of the spec. Stereo again (fold -2.6 dB), harmless since the tool normalizes after the fold |
| sound-spook_dash_v2 | spook | 0.6 s | -0.3 | -1.0 | good (v2 13:46); the biggest turnaround of the batch, from inaudible to full level |
| sound-beg | beg | 0.5 s | -28.2 | -31.6 | DEFERRED by Strato 2026-09-15 (on the fence about the cue itself); the tool skips it |
| sound-error_v2 | error | 2.0 s | -0.8 | -7.6 | good (v2 13:31); 0.77 s silent tail, trimmed |
| sound-confirm | confirm | 1.0 s | 0.0 | -2.8 | good (new 13:12); 74% of its energy is sub-600 Hz but the top survives at -2.8; 1 s is long for a confirm, the top of it is what will be heard |
| sound-confusion | (ignore) | 1.0 s | -9.4 | -18.6 | Strato: ignore this file; the tool skips it |

Every non-deferred cue now has a file. Filenames: `sound-<cue>.wav`
(hyphen after `sound`, underscores inside the cue name). A `_v2`, `_v3`
suffix is a revision: the tool takes the highest revision of a cue and
ignores the older files, so nothing needs deleting. The tool maps
`sound-plant_snip` -> `snip`, `sound-fry_arrival` -> `arrival`,
`sound-bubble_cluster` -> `bubbles`, `sound-spook_dash` -> `spook`, and
ignores `sound-confusion`. Bank size for this batch after trimming: about 600 KB at 16 kHz
s16, well inside the app partition.

## 6. Acceptance

- Every cue plays on the bench with no click at start or end, at all three
  volume levels; `director codec` shows SYS0D F8 and GPIO46 low within 3 s
  of the last cue.
- Frame log unchanged: fps and render/flush ms the same with cues firing.
- One drowse night on battery: slope within noise of nights 1 and 2.
- One awake hour on battery with cues at a natural rate: within 2% of the
  09-14 baseline.
- QEMU build runs with the stub port; `run_qemu.sh` unchanged.
- The sim plays the same cues.

## 7. Open questions for Strato

1. (answered 2026-09-15: `beg` deferred; eat, spook, investigate,
   bubbles stay.)
2. Per-fish voice: pitch by stage only, or a small per-fish offset rolled at
   birth (so each fish has "a voice")? Cheap either way.
3. Default volume: normal, or quiet until the keeper turns it up?
4. (answered 2026-09-15: sleep and wake deferred.)
