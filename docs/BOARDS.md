# Four boards, one tank: how a change reaches all of them

Since 2026-10-01 the tank runs on three boards, since 2026-10-12 on four. This file is the rule for
changing anything they share - read it before a feature, the way DEVICE.md is
read before a flash.

## The boards

| board | glass | build | simulator | firmware | finds the board by |
|---|---|---|---|---|---|
| Waveshare 1.8 AMOLED (the tank, the reference) | 448 x 368, round corners | default | `make` -> `fishsim` | `tools/flash.sh` (preflight; build dir `fw-build`) | port, `TANK_PORT=` with several on USB |
| Waveshare 1.75C (the bowl) | 466 circle | `TANK_ROUND` | `make ROUND=1` -> `fishsim-round` | `tools/flash_round.sh` (`fw-build-175c`) | its USB serial (`ROUND_SERIAL`, tools/boards.local.sh) |
| Waveshare 2.06 (the watch) | 410 x 502 portrait, 100 px corners | `TANK_WATCH` | `make WATCH=1` -> `fishsim-watch` | `tools/flash_watch.sh` (`fw-build-206`) | its USB serial (`WATCH_SERIAL`, tools/boards.local.sh) |
| Waveshare 2.16 (the square, 2026-10-12) | 480 x 480, ~40 px corners | `TANK_SQUARE` | `make SQUARE=1` -> `fishsim-square` | `tools/flash_square.sh` (`fw-build-216`) | its USB serial (`SQUARE_SERIAL`, tools/boards.local.sh) |

Each board's own traps are in its doc (board-amoled-1.75c.md,
board-amoled-2.06-watch.md, board-amoled-2.16.md).

## What is shared, and where a board may differ

One source tree, one save layout, one model. A board is a compile-time world:

- **The world** (`common/tank.h`): `TANK_W` / `TANK_H`, the floor `TANK_BOT`,
  the walls `TANK_FX0` / `TANK_FX1`, the film grid, the beds. The game is
  written against these names.
- **The page** (`common/render.h`): every page is laid out in a 448 x 368
  PAGE space that sits centered on the glass at `PAGE_X` / `PAGE_Y`. The fish
  live in TANK coordinates, the pages draw in PAGE coordinates: anything that
  crosses over subtracts `PAGE_X` / `PAGE_Y` (a tap coming in, a ring round a
  fish going out).
- **The ports** (`firmware/main/*_port_*`, `board_pins.h`): the chips.

A board `#ifdef` belongs in those three places only: the geometry in tank.h,
a layout-constant block in render.h / render.c / update.c, a port. If a
feature's LOGIC needs `#ifdef TANK_ROUND`, the feature is written against a
pixel instead of a name - fix that instead.

## The rules for a feature

1. **Write it once, in `common/`.** The platforms only route: the page
   returns what was tapped (`MS_TAP_*`, `SHOP_TAP_*`, ...), the sim's loop and
   the firmware's touch port do the same few lines with it. The route is
   written twice (sim/main.c and touch_port_ft3168.c + main.c - the four
   boards share the firmware's), so keep it to a call or two and put the
   behavior behind it. Sounds go through `tank_emit`, not through the
   platform.
2. **Lay new page elements out in the box every glass shows whole:** PAGE
   x 56..392, y 60..300 (the modal's box). The watch shows PAGE x 19..428, the
   bowl's circle is at least that wide between those rows; the corners of the
   page belong to the bezel on three boards out of four. A page that must use
   the corners gets a layout block per board (the milestones page's
   `MSP_*`), not scattered conditions.
3. **Give the layout a voice.** A test, the director or a tool that needs to
   tap something asks the page where it is (`render_milestones_card`,
   `render_milestones_row`, `render_milestones_arrow`) or uses the layout's
   named constants with `PAGE_X` / `PAGE_Y`. A literal pixel in a test is a
   test for one board.
4. **A save field only ever appends** (progression.c's SAVE LAYOUT LOCK), and
   the save means the same on every board.
5. **Every board's SIZE, not just its layout (Strato, 2026-10-18: "always
   consider the different board screen dimensions when adding or adjusting
   features").** The glasses differ in depth and width - the 1.8 is 448 x 368,
   the bowl's floor is at 396 under a curved top, the square is 480 x 480, the
   watch 410 x 502, the P4 (its branch) 1280 x 800. Anything that pairs a
   distance with a time, a speed or a count - a lifetime, a fall, a swim, a
   reach, a spacing, a density, a timeout - tuned on the 1.8 is a 1.8 number.
   Derive it from the world's names (`TANK_BOT`, `FOOD_FLOOR_Y`, `TANK_FX0` /
   `TANK_FX1`, `TANK_W` / `TANK_H`) or prove it with a selftest that MEASURES
   it on every world (check-all runs it four times). Before adding or
   changing one, ask: does it still hold on the tallest glass (the watch), the
   widest (the P4), the curved one (the bowl)? The miss that wrote this rule:
   a pellet left in the water vanished at 45 s - the 1.8's fall - so on the
   watch, the square and the bowl it dissolved mid-water before the shrimp
   could reach it, for weeks (fixed in 0.3.8: `FOOD_LIFE_S` from the depth).
6. **Nothing is done until all four say so:**

   | step | command (from the project root) | what it proves |
   |---|---|---|
   | the checks | `make -C pocket-tank/sim check-all` | every selftest on all four worlds |
   | the pictures | `pocket-tank/tools/board_sheet.py --selftest-card` (or `--snapshot 20`) | the same page on four glasses, side by side, cut to each glass |
   | the firmware | `pocket-tank/tools/flash.sh` builds the 1.8; `flash_round.sh --build-only`, `flash_watch.sh --build-only` | it compiles for each board; compare `.bss` with the last flashed build (internal RAM is ~23 KB) |
   | the glass | flash the 1.8 first (it is the reference and has the preflight), then the other two | Strato's finger |

   The handoff entry for a feature says where each board stands: checked /
   pictured / built / flashed / tried on the glass. "Not flashed" is a fine
   state; "unknown" is not.

6. **The render stack (2026-10-24).** Anything new that stands on the sand
   or is placed in the tank - a critter, a decoration, a lane, a hiding
   place - gets its row in docs/render-stack.md FIRST, and `fishsim
   --selftest-stack` (the matrix that derives its checks from that table)
   must pass in four worlds before the thing is called done. Strato, after a
   day of catching the crab's layering by eye: "this will not be the last
   time this comes up as we add new decorations features and critters".

7. **Linux before a tag (2026-10-24).** The gate above runs on this Mac;
   CI runs it on Linux (gcc, glibc), and a selftest whose margin sits
   exactly where the two libraries round differently passes here and fails
   there. v0.3.9's public commit failed `checks` on two such margins (the
   tend hold's slower fish at 30 px = HOLD_APPROACH_AT; the seahorse's
   look from the sand over inside the second the test sampled at). So
   before a release tag: `pocket-tank/tools/check_linux.sh` (the same
   check-all in an ubuntu:26.04 container; `--all` lists every failure in
   one run). A test that fails on Linux alone is a test with a margin:
   test the claim, not the margin.

## Releases

One image per board, one release number. Built 2026-10-02 for 0.3.0 (Strato:
the bowl and the watch ship in 0.3.0; nothing public until the release):

| piece | how a board is kept to its own image |
|---|---|
| the board's id | `common/version.h` `PT_BOARD`: `amoled18`, `round175c`, `watch206` (from `TANK_ROUND` / `TANK_WATCH`) |
| the image | a board marker right after the app descriptor (`net_port_esp.c` `pt_board_marker`, at `PT_BOARD_MARKER_OFFSET` 0x120; kept by `-u pt_board_marker` in firmware/main/CMakeLists.txt). `tools/pt_boards.py` reads it back from a `.bin` |
| over the air | each board fetches `latest-<PT_BOARD>.json`; `common/update.c` refuses a manifest whose `board` is another (WRONG BOARD, never offered); the download reads the marker back from the slot at its first 4 KB and aborts on another board's (`NET_ERR_BOARD`) - all four share the signing key, so the signature alone would let a bowl install the 1.8's image |
| the release | `.github/workflows/release.yml` builds `firmware/build`, `build-round`, `build-watch`; `make_ota_manifest.py` names both files from the image's marker (`latest-<board>.json`, `pocket_tank-v<rel>-<board>.bin`) |
| the installer | `installer.yml` builds the four; `make_installer.py --build-dir <1.8> --board-build <round> --board-build <watch> --board-build <square>`: the 1.8 keeps `manifest.json` / `manifest-erase.json`, the others `manifest-<board>[-erase].json`; a board's file that differs from the 1.8's is `<name>-<board>.bin`. The page shows a "Pick your board" step when it carries more than one |
| pocketank.com/install | `tools/build_site.py` offers a board only once its manifest is LIVE on GitHub Pages - the public resync is the release, so a site publish before it shows the 1.8 alone |

Nothing reaches the public before the release: the dev repo is private, the
workflows publish only from `mediacutlet/pocket-tank`, the site gates on the
live manifests, and the 0.3.0 changelog entry is a `"draft"`. The resync must
carry `tools/pt_boards.py`. A cable install of the wrong board's image is not
caught on the board (the page's picker is the guard); it boots blank and the
right install puts it back, the save untouched.

### Another board's version (0.3.9.3)

A Discord keeper (MISSI, 2026-10-30) installed the 1.8's version on a watch:
it runs there, small and on its side, and the installer called the tank "up
to date" because it compares versions only. The 1.8's image is the one that
runs on every board; the other three draw nothing on a board not their own.
Every image already knows its board at boot (the chip probe in
`display_port_sh8601.c`); 0.3.9.3 puts that knowledge to work in four places:

| piece | what it does |
|---|---|
| the board it is | `board_port_hw()` (`progression.h`): the probe's answer as a `PT_BOARD` id, NULL when it cannot tell (a dead bus, the 1.8's path taken on the PMIC alone). The 1.75 / 1.75-B reads as `round175c`. The sim: `PT_BOARD`, or `--hw <id>` |
| the card | a cold start (not a wake) of a version on another board raises `NOTICE_WRONG_BOARD`: WRONG VERSION / THIS IS THE 2.06 WATCH / CHECK FOR UPDATES FIXES IT, up until a tap (the damaged model's card, the same triangle) |
| over the air | `update_target_board()` (`update.c`): the board it is, else `PT_BOARD`. The manifest fetched is `latest-<that>.json`, the WRONG BOARD check and the download's marker check compare against it, and a target that is not `PT_BOARD` is offered at ANY release (the same one included): "2.06 WATCH VERSION / MADE FOR THIS BOARD / YOUR FISH STAY" |
| the installer | the tank's Improv device name is `Pocket Tank|<the board it is>|<the board its version is for>` (`director.c`; the connect resets the board and the first question can beat the chip probe, so the answer waits for it, 2 s at most inside the pop-up's 4). The page's `overrides` (site/src/install, installer/index.html): `checkSameFirmware` is false when the version is another board's (the button installs, never erasing); `wrongBoard` stops a pick that is not the board the tank is - no install item, the pop-up names the board and the picker moves to it (`make_installer.py`'s two dialog edits). No board is preselected any more (a remembered pick or the 1.8 by default was how the watch got the 1.8's version) |
| the layout | the save records the world it was laid out in (`lay_w/h/fx0/fx1`, the film grid, at 1940..1951). A tank loaded in another world is laid out again in proportion to the floor (a castle 80% across stays 80% across), the film resampled, the snail placed fresh. An older save past this floor, on a board other than the 1.8, is read as the 1.8's world |

`fishsim --selftest-board [pfx]` (in `make check`) proves the layout, the card
and the update path in every world; pfx writes the card and the offer page.
A tank from before 0.3.9.3 tells the installer neither board: the version
decides for it, as before. A brand-new board on Waveshare's demo answers
nothing: the keeper's pick is all there is, which is why nothing is picked
for them.

### On a side (0.3.9.3)

Keepers asked to stand the square and the pendant on any edge. Their
pictures are square, so the way up is any of four: the picture's quarter
turns clockwise, 0..3 (`tank_orient` in tank.h; 2 is the old flip). The
watch and the 1.8 keep two ways (`TANK_QUARTER` reads a side as upright
where the picture is not square).

| piece | what it does |
|---|---|
| the IMU | `imu_port_sides()` on the square's and the pendant's own builds: the in-screen axis that is not "up" decides a side. The side's sign comes from the up axis (Z runs out of the glass on both). A new way up must carry gravity 1.3x the other in-screen axis for ~750 ms, so a tank near 45 degrees never flaps; flat holds. NVS `inv` stays the turned-over half (an older build reads it), `side` is the rest |
| the panel | a quarter is the CO5300's own MADCTL (rows and columns exchanged), sent by the flush task between frames: no cost per frame. The square 0x00 (its upright 0xA0). Turned over is still the flush's 180; the fourth way up is both. THE PENDANT TURNS IN ITS FLUSH instead (round_flush, a stripe column read as one frame row in order: 24.6 fps on a side, as before): on its side the CO5300 takes no row address past 466, so the round glass's 6 px offset could never be drawn - a strip along the top with the USB on the left at every gap (Strato's photos 10-30; `turn probe on` paints the memory white and bands the four edges). Director `turn [0-3 \| madctl <hex> \| gap <x> <y> \| sign \| probe on\|off]` forces a side and tries others live |
| touch | the calibration and the finger's low landing live upright; a tap is turned with the picture after them (`tank_quarter_from_upright`). The keeper's ADVANCED touch fit is fitted and applied upright too, on any side |
| the lock | settings ROTATION freezes whichever of the four it has; the save's `orient` byte carries a side in bit 4 (an older build reads the turned-over half) |

Seen on the glass 2026-10-30: both boards turn as intended all four ways,
and taps on a side land where the finger is.
The sim proves the lock on every side and the touch fit on every side
(`--selftest-sleep`, `--selftest-advanced`).

## What the first feature under these rules found (2026-10-01)

The fish card's RENAME and SELL were built this way. Two things turned up
that only the other boards could show:

- The ring round the fish being named (the first run, the color page, the
  birth announcement) was drawn at the fish's TANK position in PAGE space:
  right on the 1.8, 9 px right and 49 px low on the bowl, 19 px left and
  67 px low on the watch. Seen on the first three-board sheet; fixed in
  setup.c (`fish_ring`).
- Only the 1.8's simulator was ever checked: `make check` built and ran
  `fishsim` alone, and the selftests tapped the rectangle's pixels - 5 of 10
  failed on the bowl, 6 on the watch. Every one was the test, none the game;
  they ask the layout now (the pages' `MSP_*` / `SHP_*` / `SET_*` / `UPD_*`
  numbers live in render.h / update.h for that), and `make check-all` and CI
  run all of them in all three worlds.
- Still literal: the `--snapshot` mode's own taps (on the bowl its fry_modal
  picture opens another modal). A picture, not a check - repoint it when it
  is next touched.
