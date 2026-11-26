# The 2.8" ESP32-S3 CYD (ES3C28P)

This is LM's fork of [pocket-tank](https://github.com/mediacutlet/pocket-tank).
It runs the same tank, the same model and the same game on a stock "cheap
yellow display" as well: the ES3C28P from QDtech, an ESP32-S3 behind a
2.8-inch IPS panel with capacitive touch. No modifications to the board -
everything here uses it as it comes. The vendor's specification:
[ES3C28P_ES2N28P_Specification_V1.0.pdf](https://www.lcdwiki.com/res/ES3C28P/ES3C28P_ES2N28P_Specification_V1.0.pdf).

Upstream's boards build from this tree as from upstream's own - its four
Waveshare AMOLED boards and, since v0.4.3, its ESP32-P4 home tanks - and
[README.md](README.md) is upstream's own, with one block at its top that
points here. This page is the CYD owner's: what the fork changes, how the
tank is used on the CYD's glass, how to build and flash it, the board and
what the port changed, and how the fork stays in step with upstream.

A new board shows the vendor's demo until it is flashed: back it up and
flash it first (*Building and flashing*), then *Using it*.

## What this fork changes

Against upstream's main (`git log upstream/main..main`):

- **The CYD.** `CONFIG_POCKET_TANK_CYD_320X240` builds a
  320 x 240 tank for the ES3C28P: an ILI9341 display port over SPI, the
  FT6336 touch, the ES8311 on the CYD's own pins. It is built and flashed by
  cable with `tools/build_cyd.sh` (*Building and flashing*).
- **Pages for a 240 px glass.** `common/ui.h` scales the pages to the
  page the build draws; the stats card goes to two columns, the milestones page
  takes 24 px badges, the shop and settings span the whole glass, and the
  icon feet draw half-size copies of upstream's sprites
  (*What the port changes*).
- **Two settings of the 320 x 240 boards' own.** SCREEN, a tile beside
  GRAPHICS, turns the picture on a board with no IMU and becomes ROTATION
  when one answers; the SLEEP row (NEVER / SCREEN / LIGHT) chooses what
  every way into sleep does (*Settings*).
- **A sleep that never deep-sleeps.** Two sleep modes upstream does not
  have, screen and lightsleep, darken the tank and wake it on a touch, a
  pick-up or BOOT; both 320 x 240 boards ship on lightsleep (*Sleep*).
- **A second IMU.** An MPU-6050 beside the QMI8658, each a Kconfig line,
  probed at boot. The CYD has neither on the board: a breakout goes on its
  I2C socket, and laying the board face down then sleeps the tank
  (*The IMU*).
- **What the CYD goes without.** No updates over Wi-Fi and no UPDATES
  button, no battery pill or page, no BATTERY row on the hidden ADVANCED
  page, no PWR key, no clock chip, and no crew's tanks in THE LIBRARY
  (*What the CYD does not have*).
- **Two rules scaled to the smaller tank, on the 320 x 240 boards only.**
  The model's distance bands scale with the tank's width, and the algae
  films the glass at the 1.8's pace per cell (*What the port changes*).
- **The simulator's 320 x 240 world.** `make -C sim 320X240=1` builds
  `fishsim-320x240`, and `make -C sim check-all` runs the selftests in
  all seven worlds, upstream's six and this one.
- **MY TANKS without BOOT.** Upstream's shelves of tanks (0.3.9.3) open
  with BOOT pressed twice or, since 0.4, the milestones page's MY TANKS.
  On a 320 x 240 board a single BOOT press waits 0.4 s for a second before
  it sleeps, a finger held still for 5 s on a touch-locked glass opens MY
  TANKS, whose padlock undoes the lock, and one held still 5 s in the
  bottom-left corner of an experimental tank opens it too (*MY TANKS*).
- **A second 320 x 240 board.** `CONFIG_POCKET_TANK_WST_320X240` builds the
  same tank for the Waveshare ESP32-S3-Touch-LCD-2, contributed by adampog
  (*The Waveshare ESP32-S3-Touch-LCD-2*). Both boards select
  `CONFIG_POCKET_TANK_320X240`, the part every 320 x 240 board shares; each
  keeps its own pins and chips under its own name.

What reaches upstream's boards: the IMU code split into one driver per chip
(`imu_port.c`, `imu_qmi8658.c`, `imu_mpu6050.c`), the sleep-mode choice in
Kconfig (deepsleep, their default, which is upstream's behaviour), the
touch hooks, and the tank link's ESP-NOW send callback in ESP-IDF 5.5's
form beside upstream's 5.4 one. Their simulator pages are byte for byte
upstream's but for the lines that print the tree's build id; their
firmware builds, and has been read against upstream's, not run on one of
those boards.

## Licence

Pocket Tank's code in this tree, upstream's and the port's, is under two
licences, in three layers (`NOTICE-CYD` says what they mean together - a
summary; the licence texts are what count). The third-party code the tree
also carries keeps its own: ESP Web Tools in `installer/vendor/`
(Apache-2.0), and llama2.c's `runq.c` inside `common/llm/q4_model.c` and
`model/runq4.c` (MIT); `NOTICE-CYD` names them.

| Layer | Licence | Copyright | Its text |
|---|---|---|---|
| upstream, up to and including v0.3.9.2 | MIT | MediaCutlet | `NOTICE` |
| upstream, from v0.3.9.3 on ("the release after v0.3.9.2") | PolyForm Noncommercial 1.0.0 | Strato Doumanis | `LICENSE`, under its three `Required Notice:` lines |
| the CYD port: the files `LICENSE-MIT-CYD` lists, and LM's changes inside upstream's files as far as they can be separated | MIT | Leonid Moiseichuk | `LICENSE-MIT-CYD` |

- The firmware as a whole - what anyone clones, builds or flashes from this
  tree - contains upstream's code from v0.3.9.3 on, so it may be used,
  changed and shared for noncommercial purposes only. Commercial use needs
  Strato's written permission (strato@pocketank.com).
- Whoever gets upstream's code from v0.3.9.3 on - the tree or the firmware
  as a whole, or any part of that code - must get PolyForm's terms, or
  their URL, and the three `Required Notice:` lines. A copy that keeps
  `LICENSE` as it is carries both, and `NOTICE-CYD` repeats the lines word
  for word. A file of the port's own, passed on by itself, holds none of
  that code: its MIT notice is enough (the next point).
- The CYD port's own files may be reused under MIT alone, apart from the
  rest of the tree, keeping `LICENSE-MIT-CYD`'s copyright and permission
  notice - and for `imu_mpu6050.c`, `orientation.c`, `sleep_setting.c` and
  `tools/gen_icons_small.py`, which hold a few lines of upstream's MIT
  releases, MediaCutlet's copyright notice from their headers as well.
- `LICENSE`, `NOTICE` and `CONTRIBUTING.md` are upstream's and are never
  edited here, so every sync merges them clean. Two of them also have to
  be passed on: `LICENSE` holds what PolyForm's Notices clause asks for -
  its terms and the three `Required Notice:` lines - and `NOTICE` holds the
  copyright and permission notice the MIT License asks for, for the code up
  to v0.3.9.2. Neither licence asks for `CONTRIBUTING.md`; it stays because
  it is upstream's. The fork's part is in `LICENSE-MIT-CYD` and
  `NOTICE-CYD`, files upstream does not have.
- adampog's Touch-LCD-2 (pull request #2, merged 2026-10-08) came to the
  fork while it was all MIT code. It is adampog's, so the files it touched
  are not in `LICENSE-MIT-CYD`'s list.
- `CONTRIBUTING.md` is about pull requests to upstream: opening one gives
  Strato a right to the contribution for any purpose, commercial ones
  included. The fork means to send none, but one is open: pull request #15,
  opened 2026-10-01 from the fork's `main`. A pull request follows its
  branch, so every push of `main` to `origin` offers upstream whatever
  `main` then holds, `LICENSE-MIT-CYD`'s files included. Until the
  v0.3.9.5 sync its head was `4d913b5` (the v0.3.9.2 sync), from before
  upstream had a `CONTRIBUTING.md`; the push of that sync's `main`
  (2026-11-01) moved it to `daf18df`, after upstream had one. Whether to
  close it or keep it is LM's to settle before the next push of `main`;
  step 9 of *Syncing with upstream* checks it, and `NOTICE-CYD`'s
  *CONTRIBUTING UPSTREAM* says which.

**Never label an upstream file MIT.** A path upstream ever had, in any
commit or tag, is upstream's, however much of it the fork has rewritten - and
so is a file the fork renamed or split out of one: `imu_port.c` and
`imu_qmi8658.c` hold upstream's `imu_port_qmi8658.c`, and changes upstream
makes to that file are carried into them by hand. Such a file gets no SPDX
header and stays out of `LICENSE-MIT-CYD`, whatever share of its lines is
LM's. PolyForm allows no sublicensing: the fork cannot offer upstream's code
from v0.3.9.3 on under MIT, and a header that said so would be wrong.

**Keeping `LICENSE-MIT-CYD`'s list current.** The list is computed, never
written from memory. At every sync, and after adding a file of the fork's
own:

```sh
git fetch upstream
comm -23 <(git ls-tree -r --name-only HEAD | sort) \
         <(for c in $(git rev-list upstream/main --tags); do git ls-tree -r --name-only "$c"; done | sort -u) |
while read -r f; do
  echo "== $f"
  git log --follow -M --diff-filter=AR --name-status --format='   %h %an' -- "$f" | grep -v '^$'
  git blame -w -C -C -C --line-porcelain HEAD -- "$f" | sed -n 's/^author /   lines: /p' | sort | uniq -c
done
```

It prints each path at HEAD that no upstream commit or tag ever had (the
fork makes no tags; every tag here is upstream's), how it came to be (`A`
added, `R` renamed from the path on its left), and whose lines it holds,
with lines copied from other files traced to them (`-C -C -C`). Reading it:

- **Added by LM, from no upstream path, every line LM's:** in the list. A
  source file - C, a header, a script, a Kconfig or sdkconfig fragment,
  CMake - takes `SPDX-License-Identifier: MIT` and
  `SPDX-FileCopyrightText: 2026 Leonid Moiseichuk` at its head, in its own
  comment syntax, code untouched. A file with no comments, or binary, or
  documentation (this page) is in the list only.
- **Renamed or split from an upstream path:** upstream's. Out.
- **A few lines of an upstream author** in a file otherwise LM's: check
  each blamed commit with `git merge-base --is-ancestor <commit> v0.3.9.2`.
  All of them in v0.3.9.2 or earlier means MIT throughout: in the list, the
  header naming `SPDX-FileCopyrightText: 2026 MediaCutlet` too, and
  `LICENSE-MIT-CYD` saying which lines (`imu_mpu6050.c`'s I2C helpers,
  `orientation.c`'s and `sleep_setting.c`'s NVS lines,
  `gen_icons_small.py`'s PIL import and paths). One from v0.3.9.3 on: out.
- **Lines of another contributor** (adampog's `display_port_spi.c` and
  `sdkconfig.defaults.wst`): not LM's to license. Out.
- **Generated** (`sim/testdata/saves/2026-10-08-1328-cyd.sav`, written by
  the CYD build from upstream's v0.2.0 fixture): out.
- `LICENSE-MIT-CYD` and `NOTICE-CYD` print too: they are the notices, not
  files of the port, and stay out of the list.

A file that leaves the list because upstream now has the path too loses
its header in the same commit; a file the fork deletes leaves the list with
it.

## Using it

Day to day on the glass the CYD is the tank README.md describes under
[*The living tank*](README.md#the-living-tank): the same fish, cards,
badges, shop and chores. What
follows is how to reach each thing on this board, and where it differs.

### Finding the pages

- **Tap a fish** for its stats card at the top left: the needs down the
  left, the traits and MORE down the right, and the toolbox under it. The
  arrow just right of the card steps it on to the next fish. The card goes
  by itself after 10 s; a tap on empty glass closes it.
- **Tap the card, or its MORE,** for the milestones page: a row per fish,
  the TANK row with the tank's badges, and five squares at the foot (0.4):
  the sand dollar (UPGRADES), the cog (SETTINGS), two tanks on a bench (MY
  TANKS), a passport (VISITORS) and an X (CLOSE).
- **UPGRADES** opens the shop, which shows your balance; **SETTINGS** the
  settings page; **MY TANKS** the shelves of tanks (*MY TANKS*), whose CLOSE
  goes back to the tank; **VISITORS** the fish you collected from tanks
  linked to yours (README.md's *The living tank*, under *Visitors*).
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
stage, what it is worth, and since 0.4.2 a row of picture buttons - RENAME
(a pencil), SELL (a sand dollar) and MORE (its own pages), with GIVE while
the tank is linked and MOVE (the travel bowl) once the board has a second
tank.

- **RENAME** opens the letter wheel: swipe a letter up or down, DONE keeps
  the name, CANCEL leaves it as it was.
- **SELL** turns into OK?, and the worth line into TAP AGAIN TO SELL FOR
  and the price; a second tap sells the fish for sand dollars. A tank keeps
  at least two fish, so with two the button is dim.

### The shop

UPGRADES, the sand dollar at the foot of the milestones page. The shop
shows your balance at its top left, and since 0.4.1 its own foot is five
squares too: ? (HOW TO EARN, what pays), DECORATIONS, CRITTERS,
BACKGROUNDS and CLOSE, a small bar under the area showing. Four rows to a
page, each item with its price and UNLOCK, lit once you can afford it and
dim until then, IN TANK once you own it, or ON THE SHELF with PUT BACK for
a piece put away; the arrows at the top right turn to the area's next
page.

Tap a row for the item's card: its picture, its words, its price, and
UNLOCK, which buys it. A tap anywhere else closes the card.

A plant or a decoration you buy comes up on its placement page, PLACE THE
<ITEM>: drag it left or right along the floor, pick its DEPTH (BEHIND,
AMONG or IN FRONT; some pieces offer two of the three), and DONE at the top
right keeps it there. The arrows at the two ends of the DEPTH row move it
behind or in front of the next piece at the same depth (the plant has
none). The coral adds a COLOR row and the reef cluster a LOOK row.

The page's top row is SELL, SHELVE, LOCK and DONE. SELL takes two taps: the
first shows the refund and OK?, the second sells. SHELVE puts the piece away,
kept as it is - a living one keeps growing on the shelf - and PUT BACK on
its shop row returns it to its spot. LOCK (LOCKED once on) pins it: a held
finger then passes it by.

To move it later, hold a still finger on the piece in the tank for a moment
(0.7 s) and its placement page opens; or tap its row in the shop, whose card
has MOVE and SHELVE for a piece in the tank, PLACE and SELL for one on the
shelf. A locked piece opens only from the shop.

### MY TANKS

New in 0.3.9.3: the board keeps more than one tank. Besides the tank on
the glass, up to 119 more wait on two wooden shelves, four to a shelf and
eight to a page. On the CYD: **MY TANKS**, the third square of the
milestones page's foot - a fish, its card, MY TANKS. BOOT pressed twice
opens it too, on the Touch-LCD-2 or a CYD out of its case. Since 0.4.2 its
own foot is four buttons, two each side of the page number: LIBRARY and
the padlock on the left, SETTINGS and CLOSE on the right. CLOSE goes back
to the tank.

- **A tank on the shelf** wears its rest: zZ, DORMANT, lives on as if
  asleep and catches up when it comes back, as a night does; two bars,
  PAUSED, waits exactly as it was left. The tank on the glass wears NOW.
- **Tap a tank** for its card: its picture, name, type, how long it has
  rested and its age; TIME (DORMANT or PAUSED); **SWAP IN**, RENAME and
  MORE. SWAP IN saves the tank on the glass onto the shelf and restarts the
  board into the other one; the wallet and the board's settings (LIGHTS OUT,
  the ROTATION lock) go with it. The tank on the glass has a card too, with
  RENAME alone.
- **RENAME** is a keyboard: two lines of seven for the name plate, fourteen
  big keys a page (MORE turns to the next page: A-N, O-Z ' ., digits and
  - ! ? /), DEL, SPACE, LINE (the other line), CLEAR, BACK and DONE. A key
  acts as the finger lifts on it.
- **MORE** on a card is REMOVE: the tank and its fish go for good, and its
  pieces sell back. HOLD TO SELL fills as the finger stays on it, 2 s.
- **An empty place**, or LIBRARY at the bottom left, opens THE LIBRARY:
  NEW TANK starts another fish tank, free for the first, 300 sand dollars
  after that. On these boards it lists the fish tank alone, with no jelly
  row and no second page (*What the CYD does not have*).
- **To move a tank**, hold it until it lifts, then tap its new place (a
  tank there trades places with it); CANCEL puts it back.

**The touch lock** is the padlock in MY TANKS's foot, second from the
left: shut, nothing a finger does reaches the tank or opens a page. The
note that comes up as it locks ends HOLD A FINGER 5 S / TO RETURN HERE, and
a touch then shows only TOUCH IS LOCKED and the same hold. Upstream's way
back is BOOT twice; on a 320 x 240 board **a finger held still on the glass
for 5 s** opens MY TANKS as well, and the padlock undoes the lock there.
Still means still: a finger that strays 24 px from where it landed has to
lift and start again, even if it comes back. The lock is the board's (NVS
`tank/tlock`): it stays through the tank's own restarts (a SWAP IN) and a
reset of the tank, and a locked board still wakes from the dark on a
touch. Since 0.3.9.6 **a hard boot unlocks it**: any start that is not the
firmware's own restart or a wake from deep sleep. On the CYD that is
pulling the power (the cable out, with no cell on the socket - or the cell
out as well), the RESET button (on the ESP32-S3 the chip's enable pin, a
power-on reset), a flash over the cable, a crash or a watchdog. The 5 s
hold is the everyday way; a hard boot is the way when nothing on the glass
answers.

Two pages can come up on a locked glass by themselves: a fry's welcome,
when one is born, and the first-run setup, after SWAP IN on a NEW TANK.
MY TANKS will not open over either, nor does BOOT twice. Since 0.3.9.6
the lock lets those pages take their taps (upstream's fix), so each
answers a finger as on an unlocked glass, and the lock is still on when it
closes. BOOT's chord for the reset prompt is refused on a locked glass,
and a prompt that is up anyway (the director's `reset` opens one) answers
NO by itself after 20 s.

**The experimental tanks** - upstream's jelly tank, piranhas and minnows -
are not in THE LIBRARY here (*What the CYD does not have*), but on the bench
the director makes one (`shelf jelly <0-2>`, `shelf minnows` or `shelf
piranha`), and `shelf swap <place>` puts it on the glass. Such a tank's
glass is all its own, and upstream's one way out of it is BOOT twice; on a
320 x 240 board **a finger held still for 5 s in the glass's bottom-left
corner** (a press that lands in its 56 px square) opens MY TANKS, as the
tank's thank-you says: HOLD BOTTOM LEFT 5 S / TO RETURN TO TANKS. Only
there: anywhere else a still finger is the tank's own gesture, held as long
as you like - the minnows mill round it, the jellies turn to it as a light -
and the corner is clear of each tank's controls (the jellies' nozzle at the
upper right, the minnows' feeding band along the top, the piranhas' taps).
It has to stay still as the touch lock's hold does; on a locked glass that
hold works anywhere. A hard boot (RESET, or the power pulled) brings the
main fish tank back, as it does on every board since 0.4.2.

Where the tanks live: the tank on the glass where it always has (NVS
`tank/save`), its name and place in NVS `frame`; each shelved tank in a
24 KB slot of the `storage` partition, past its first 64 KB (*Building and
flashing*). Since 0.3.9.6 every start by day plays upstream's show of the
tank building itself up - FILLING THE TANK after a SWAP IN, the light
coming up after any other start - and the loading screen stands in for it
only when the show finds no memory (*What the port changes*).

### Settings

| row | choices | what it does |
|---|---|---|
| BRIGHTNESS | a slider, 5% to 100% in steps of 5 | the backlight follows the knob while a finger drags it, and is kept in NVS when the finger lifts |
| VOLUME | a slider, OFF to 100% in steps of 10 | upstream's since 0.4, in place of OFF / QUIET / NORMAL: the sound follows the knob, a tick at each step, and is kept in NVS when the finger lifts. A level saved before 0.4 carries over as the percent that sounds the same |
| LIGHTS OUT | DOUBLE-TAP, or 5 SEC to 30 MIN | DOUBLE-TAP: the double-tap works the tank light. A time: the light goes out by itself after that long unhandled (*The light*) |
| AUTO FEED | ON / OFF | ON: the tank feeds a fish that is really hungry. OFF: only you feed; a fish left starving in a lit tank slowly loses trust |
| SCREEN | a tile, UPRIGHT or FLIPPED | the first of 0.4.2's two tiles, with no IMU answering: turns the picture and the touch 180 degrees, kept in NVS |
| ROTATION | a tile with a padlock, LOCKED or UNLOCKED | in SCREEN's place once an IMU answers. Tap it to lock the picture the way up it is now; tap it again to let it follow the board turned over |
| GRAPHICS | a tile, LOW, MEDIUM or HIGH | beside SCREEN or ROTATION (0.4.2). It opens the page's other face: QUALITY (LOW / MEDIUM / HIGH) and VIGNETTE (ON / OFF), the board's own, kept in NVS; ONE MOMENT while the grass loop is baked again for a change; BACK |
| SLEEP | NEVER / SCREEN / LIGHT | under the tiles: what every way into sleep does, on every 320 x 240 board. NEVER ignores it, SCREEN darkens the glass with the chip awake, LIGHT darkens it and light-sleeps. LIGHT is the factory default; a choice is kept in NVS and applies from the next sleep (*Sleeping and waking*) |

At the foot: the release over the build id, small and dim, on two lines
cut short of LINK ("V0.4.3 ALPHA" over "BUILD" and the build id, whose tail
gives way), then LINK and CLOSE on the last two of the milestones foot's
squares. LINK opens link mode, upstream's tank link (README.md's *The
living tank*, under *Tanks side by side*), which has never run on a
320 x 240 board (*Still open*). There is no UPDATES button, and MY TANKS
is on the milestones page's foot.

### The ADVANCED page

Hidden on purpose since 0.3.9.2, so that no child stumbles on it: no button
leads there. With the settings page up, hold BOOT for five seconds, then,
still holding it, tap the glass. In the board's case BOOT cannot be
reached, and the director's `advanced` is the only way in
(*The serial console (the director)*).

On the CYD and the Touch-LCD-2 it has one row, **TOUCH**, for a glass whose
taps miss their mark. CALIBRATE, then START, brings up nine crosses in turn:
tap the middle of each, holding the board as you usually do. Then one more
cross: tap it within 15 s and the new touch is kept, or the old one comes
back. Presses scattered too widely are refused (DID NOT WORK - TRY AGAIN),
and 30 s without a press on a cross stops the sitting with nothing changed.

Since 0.4.2 the flow goes on from the kept check. With an IMU answering,
TURN IT OVER asks for the board upside down and a second sitting there -
its crosses come by themselves once the IMU feels the turn - and the two
make one touch that holds every way up; SKIP, or a minute of nothing, keeps
the first sitting alone. With none answering (the CYD as it comes) the
board could never feel the turn, so the check ends at DONE and the touch is
kept for the way up it was taken; a sitting taken with SCREEN on FLIPPED
joins it as the other way up. DONE says TOUCH CALIBRATION SAVED., and its
OK goes back to the ADVANCED page. While a touch of your own is in use,
RESET TOUCH goes back to the board's own. BACK returns to the settings
page.

The touch is kept in NVS beside the tank's save, not in it (`adv/tfit`): a
reset tank keeps it, and erasing the save region over the cable drops it.

Upstream's page also has a **BATTERY** row, which sets the charge current
of the AMOLED boards' AXP2101 to the cell's size. No 320 x 240 board has a
charger the firmware can set, so here the row is not drawn and none of its
taps answer (`ADV_BATTERY_ROW` in `common/advanced.h`).

### Sleeping and waking

What puts the tank to sleep:

- a press of **BOOT**, 0.4 s after its release, unless a second press
  follows within those 0.4 s (two presses open MY TANKS) or the press
  opened the reset prompt or the ADVANCED page;
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
| a finger held still 5 s on a touch-locked glass | MY TANKS, whose padlock undoes the lock (*MY TANKS*) |
| a finger held still 5 s in the bottom-left corner of an experimental tank | MY TANKS (*MY TANKS*) |

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
| short press of BOOT | sleeps the tank 0.4 s after the release; another press wakes it in place (face down or not) |
| two short presses of BOOT, the second within 0.4 s | MY TANKS, or closes it |
| hold BOOT 5 s, then tap the glass with BOOT still down | *START OVER?* (*Starting over*); on the settings page, the ADVANCED page. A tap sooner is an ordinary tap, and letting go of BOOT then sleeps the tank |

The SLEEP row decides what every way into sleep does, face down included.
How each gesture is told apart: *Gestures* under *The IMU*.

### Starting over

Hold BOOT for five seconds, then, still holding it, tap the glass:
*START OVER?* comes up over the water (0.4.2) with two choices and BACK.

- **RESET THIS TANK** is the reset as it always was: its page, *RESET THIS
  TANK?*, asks NO or YES, and YES wipes the save, and two new fry take the
  tank with the first-run setup to name them.
- **ERASE EVERYTHING** leads to a page of its own, *ERASE EVERYTHING?*
  (BACK or NEXT), then to *ARE YOU SURE?*, where only HOLD TO ERASE, held
  for 3 s, erases - no tap ever does. Every tank goes, on the glass and on
  the shelf, with the sand dollars, everything bought and the visitor
  collection, and the board restarts as a new one: the boot show, two fry,
  the welcome. The touch map, BRIGHTNESS, VOLUME, GRAPHICS, SCREEN, SLEEP,
  the ROTATION lock and LIGHTS OUT stay. On these boards the page has no ALSO
  FORGET WI-FI box and says nothing of the Wi-Fi: they never save a
  network.

NO, BACK, a sleep, or twenty seconds with no answer keep everything.

The five seconds are new in 0.3.9.2, so that a child cannot wipe a tank by
accident. A tap sooner is an ordinary tap - on the surface it drops
pellets, on a fish it opens its card - and letting go of BOOT then puts the
tank to sleep, as a short press does. A finger already on the glass when
the five seconds are up does not count: lift it and tap again.

RESET THIS TANK's YES erases the tank on the glass alone (since 0.3.9.3:
its save and the director's parked copy, `tank/save` and `tank/bk`, and
since 0.4 the visitor collection and the other books kept beside them;
until then the whole `tank` namespace). The sand dollars and the tanks on
the shelf stay, as its page says, and so do BRIGHTNESS, VOLUME, GRAPHICS,
SCREEN, SLEEP, the touch lock and a touch kept on the ADVANCED page.
LIGHTS OUT and the ROTATION lock live in the save, but the reset carries
them to the new tank as a SWAP IN does; AUTO FEED is the tank's own and
goes back to ON. The chord does nothing while the touch is locked.

BOOT cannot be reached in the board's case: take it out, or erase the save
over the cable, which keeps the app and the model (*Recovering a board*).

### What the CYD does not have

- **No updates over Wi-Fi.** The settings page has no UPDATES button, and
  the tank never restarts into update mode or asks for a network; nor does
  it take the time from the internet, which upstream does only on a board
  with a PMIC. A new version goes on by cable (*Building and flashing*).
  With no network ever saved, ERASE EVERYTHING has nothing of the Wi-Fi to
  forget (*Starting over*).
- **No crew's tanks.** The jelly tank, the piranhas and the minnows are
  for upstream's Hatchery Crew, and a board joins the crew only through the
  crew check that update mode runs, which these boards never enter: their
  rows would stand locked for good. So THE LIBRARY shows the fish tank
  alone (`SHELF_XT_LIBRARY` in `common/shelf.h`, read by
  `firmware/main/main.c`, as `ADV_BATTERY_ROW` hides the BATTERY row);
  upstream's gate is untouched, and the director still puts one on the
  glass on the bench (*MY TANKS*).
- **No battery pill or battery page.** They need the AMOLED's fuel gauge
  (the AXP2101); with none answering the tank never shows them, and the
  low-battery notice never comes. The cell's voltage reaches GPIO9, but
  nothing reads it yet (*Still open*). Nor is there the ADVANCED page's
  BATTERY row, which sets that chip's charge current (*The ADVANCED page*).
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
alone, as are the tanks on the shelf in `storage` (*MY TANKS*). The model
partition is written only with `--model`.

The partition table is upstream's and has not changed since the CYD's
first flash: NVS `0x9000` (24 KB), `phy_init` `0xF000`, `ota_0`
`0x10000` (2.5 MB), the model `0x290000` (8 MB), `otadata` `0xA90000`,
`ota_1` `0xAA0000` (2.5 MB), and `storage` `0xD20000`, 2.875 MB, to the
end of the 16 MB. MY TANKS (0.3.9.3) keeps its shelf in `storage` past its
first 64 KB (the NVS rescue copy, `nvs_start` in `main.c`): 120 slots of
24 KB, one kept free for a swap, so 119 tanks. Nothing writes `storage`
but the firmware; whatever an earlier image left there (the vendor's demo)
fails the slots' checks and reads as empty places, and a slot is erased
before it is written.

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
- `advanced` and `advanced off` - the hidden ADVANCED page up or closed,
  without the BOOT chord: the only way to it with the board in its case
  (*The ADVANCED page*).

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

- **Back up the shelf** (MY TANKS) the same way: it is `storage` past its
  first 64 KB, and an update never writes it:

  ```sh
  esptool.py --chip esp32s3 -p <port> -b 921600 read_flash 0xD30000 0x2D0000 shelf.bin
  ```

- **Start a fresh tank without BOOT** by erasing the save alone; the app and
  the model stay, and the next boot opens the first-run setup. Brightness,
  VOLUME, GRAPHICS, SCREEN, SLEEP, the touch lock and a touch kept on the
  ADVANCED page go with it, and so does NVS `frame`. The shelved tanks stay
  in `storage` and come back on the shelf - **all but the board's first
  tank**: the fresh tank on the glass is tank 1 again, and the boot takes a
  shelved tank 1 for a leftover copy of it and erases its slot (`shelf_boot`
  in `common/shelf.c`, upstream's). So with tank 1 on the shelf this loses
  it as well as the tank on the glass; RESET THIS TANK's YES does not
  (it keeps `frame`). With the shelf backed up (above):

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

Since upstream's 0.4.1 the simulator also builds the firmware's own touch
port, `firmware/main/touch_port_ft3168.c`, on the host
(`--selftest-touch-port`), the 5 s holds of *MY TANKS* among what it
checks. This world builds neither board's own symbol, so the port takes the
Touch-LCD-2's way up there, not the CYD's 180-degree turn. The UPDATES
page's two faces are left out of this world's selftests, as its boards
never show them.

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
size, or take a smaller copy made from the same art (below); where they
would not fit, the page is laid out for the short tank instead (the CYD's
blocks in `common/render.h` and `common/render.c`):

- the **stats card** goes to two columns, the needs down the left and the
  traits and MORE down the right (its 24 px and 16 px icons cannot shrink);
- the **milestones page** uses 24 px copies of the 32 px badges, FIRST
  MUTATION's among them, and keeps every row on one screen; the detail
  modal keeps the full size. Its icon foot (0.4: UPGRADES, SETTINGS, MY
  TANKS, VISITORS, CLOSE) is five 22 px squares spread across the glass,
  x 8 to 312, each with its sprite's 18 px half-size copy (`foot_art`). The
  TANK row slid left in 0.4.1 to seven badges a page, its page pips right
  under the arrow's rings (`MSP_TPIP_Y`), as on the 1.8. THE WAITING ROW
  (0.4.2) answers a tap from the rows' own left edge, where its bowl starts.
  A fish's card (0.4.2) is the design scaled but for its buttons, which
  draw the half-size copies (`icon_card_*_half`, the foot's sand dollar for
  SELL, the travel bowl halved as it is drawn) in 26 px squares at the
  design's pitch: five take 202 px of the card's 219;
- the **shop** takes the 32 px coin for its 64 px one. Its area foot
  (0.4.1) is the keeper page's 22 px squares with the half-size sprites,
  and the area showing is marked by an 8 x 2 bar on the glass's last two
  rows, the half-size foot's scale of upstream's 16 x 2 - which, 4 px under
  the square, would fall below the glass. Every row - the items', the
  FRY's, and 0.4.1 to 0.4.3's BACKGROUNDS, SELECTIVE BREEDING and THE FISH
  LAB - stands in the fork's columns (`SHP_ROW_X`, `SHP_TEXT_X`), four to a
  page, its button centred on its art (`SHP_BTN_DY`). A pop-up draws its
  art at 1x, and its price line sits `UI(4)` higher than the design
  (`SHP_PRICE_Y` in `render.c`), so the 16 px coin clears the YOU HAVE
  line under it. THE FISH LAB's pop-up, not owned and short of its price,
  says YOU HAVE N without upstream's second coin, which would reach its
  button;
- **settings** gains a SCREEN tile, UPRIGHT or FLIPPED, for a board with
  no IMU to turn the picture (`firmware/main/orientation.c`, kept in NVS
  beside the brightness, and re-saved after a tank reset as the brightness
  is), beside GRAPHICS in 0.4.2's tile row. Once an IMU answers, the same
  tile is ROTATION instead (see *The IMU*). Under them, IMU or not, a
  **SLEEP** row - NEVER / SCREEN / LIGHT - picks what every way into sleep
  does (see *Sleep*); a deepsleep build has none. The four rows under
  VOLUME took the room of upstream's FISH ARE QUIET AT NIGHT note, dropped
  in 0.4.2, at a 30 px pitch; the tiles' icons (SCREEN's arrow, GRAPHICS'
  bars, ROTATION's padlock) are drawn a size down for the 24 px tile; and a
  row's tap band starts `SET_BAND_UP` above its segments, `UI(6)` = 4 px
  here, where upstream's boards take 12. The foot is the release over the
  build id on the left, then LINK and CLOSE on the keeper foot's last two
  squares: no UPDATES (*What the CYD does not have*), and MY TANKS went to
  the keeper page's foot in 0.4.

**The small sprites are upstream's art, filtered down.** A 240 px page has
room neither for the milestones page's 32 px badges nor for the icon feet's
44 px squares, and pixel art has no clean size in between: so
`tools/gen_icons_small.py`, the fork's, box-filters the `ms_*` badges to
24 px and the feet's, the shop tabs' and the fish card's button sprites (32
to 36 px) to half their size, the one clean step down for pixel art, and
appends the copies to upstream's generated `common/icons.c` and
`common/icons.h`. `badge_art()` and `foot_art()` in `common/render.c` swap
them in on a 320 x 240 board; a tall tank never references them, and the
linker drops them. The travel bowl is drawn from code, not from a PNG, so
the script has nothing to copy: `bowl_icon_half` halves it as it draws,
with the same filter and `bowl_icon`'s colours. How the copies are made
again at a sync: step 2 of *Syncing with upstream*.

Every page of the AMOLED build renders pixel-for-pixel as it did before the
port: the sim's `--snapshot` set (69 pages) was compared before and after.

**MY TANKS has a layout of its own here** (0.3.9.5's sync). Upstream draws
the shelves, a tank's card, the RENAME keyboard, REMOVE, THE LIBRARY, the
loading screen and the touch lock's note in the frame's own coordinates,
with a set of numbers per board (`SH_*` and `NK_*` in `common/render.c`).
The 320 x 240 boards have a set beside them: two rows of four little tanks,
64 px wide, their pictures 60 x 44 (the glass's own shape); the keyboard's
seven 40 px keys across the glass under the name plate; the foot from the
glass's edges. Every other measurement in those pages is `UI(n)` or
`UI_TEXT(s)`, identities on upstream's boards, whose pages stay byte for
byte upstream's.

Since 0.4.2 MY TANKS's buttons are an icon foot - LIBRARY, the padlock,
SETTINGS, CLOSE - two each side of the pager. Their drawings are pixel art
at their own size, so on 320 x 240 the buttons are 38 x 26, the 1.8's
44 x 30 at 0.86 (`SH_LOCK_W`, `SH_ICON_H` in `common/render.c`); they stand
from the glass's edges (`SH_FOOT_X0`, 8 px), centred on the pager's line
(`SH_ICON_Y`), 2 px off the glass's foot, and static asserts keep each
drawing a pixel inside its button and the buttons clear of the widest
pager, 15 OF 15. THE LIBRARY's rows are placed by one rule (`SH_LIB_Y0`,
`SH_LIB_PITCH`, `SH_LIB_Y`) for the page, its taps, the loading bubbles and
a late picture: on 320 x 240 a row is taller than its picture
(`SH_LIB_ROW`), and upstream's 0.4.2 numbers put the first row's bubbles
and picture 16 px under its glass. The library's note and the touch lock's
note are the design scaled, each held on the glass by a static assert, and
the lock's note names the 5 s hold, not BOOT.

**START OVER? and TURN IT OVER are the design scaled** (0.4.2's pages), but
for one width: the reset prompt's tall pages are 240 px wide, glass x 40 to
280, because at `UI(336)`, 219 px, ERASE EVERYTHING stood on its button's
heavy edge; RESET THIS TANK?, whose widest words have 14 px either side,
keeps 219. The ADVANCED page's TURN IT OVER and DONE go through `UI()` and
`UI_TEXT()`, their pictures 56 x 42, the glass's own 4:3.

**The seahorse's card starts its text past the arrow.** The critter cards'
arrows (0.3.9.3) keep their 34 px box, 8 px in from the corner, on a card
the fork scales; the seahorse's column, after its 1x picture, started at
X + 40 and the box covered the title's S and the days line. On 320 x 240
the column starts past the box (`seahorse_card_draw`), and static asserts
there hold the longest line inside the card and the two beside the right
arrow clear of it. The other critters' titles are centred and clear. The
critter cards' NAME | SHELVE foot (0.4.2) letters at scale 1, and SHELVE's
SURE? gives way under each card's scaled title - on the seahorse's card,
left of the right arrow. The crab's and the octopus's COLLECT answer only
4 px below themselves (`CRIT_COLLECT_SLOP_DN`), because NAME under them
keeps the slop `--selftest-crit-names` holds.

**The cards over the tank are the design scaled** (0.4.3's sync). A
visitor's passport, the gift card, the GIVE modal and the travel bowl's
cards, FISH SWAP's cards and the PAIR card are 219 px wide on a 320 x 240
board, the critter cards' width. Their numbers go through `UI()`, their
words through `UI_TEXT()` and their fish are times `UI_F`, and their hit
tests use the same numbers as their drawing. The hearts, the travel bowl's
bowl and the coin keep their pixels, and the PAIR card's heart is drawn at
1x. A line of text keeps its 7 rows at scale 1, so a step over one is the
line's rows plus the design's gap scaled (`7 + UI(3)` for upstream's 10
under a label). The passport stacks the most under its photo - VISITING,
its time, INTERESTED IN, the hearts and two machine lines - and is 177 px
tall, 13 more than `UI(252)` (`render.h`'s `PP_*`); its COLLECTED stamp
stands beside the hearts, and its VISITED rings are 28 and 25 px, so they
hold the word at scale 1, and stand 3 px under the design's place, clear
of a 15-letter FORM such as BULBOUS CLASSIC. The gift card is not shown at
its real size here, as upstream shows it on every glass: at 336 x 252 it
does not fit.

**The tank link's pages are the design scaled** (0.4.3's sync). Link
mode's chips (DONE, DISCONNECT), our badge, the door tabs and the coin
buttons under them are the tank's own, as a tool's chip is, and keep their
size; the hint, NEARBY and the DRIFTED notice are page measurements,
`UI()` and `UI_TEXT()`. Upstream's hint stood at page y 284, under a 240 px
glass, its lines up to 324 px wide at scale 2; here it stands at y 182 to
226, just over the sand and between the coins, at scale 1. Our badge
stands in the middle of the gap between DONE and DISCONNECT (x 137), not at
the glass's middle, where DISCONNECT's chip (which comes with an open door)
ran 6 px over its box. The arrow that says DONE is next bobs under DONE's
word: under the chip's middle it crossed the left door tab's top corner.
The DRIFTED notice is a 210 x 107 panel at y 48, under our badge's box and
between the door tabs, which it drew over when it came up over link mode.
Static asserts beside `link_line` in `common/render.c` hold all of them.

**VISITORS is upstream's grid through `UI()`**: four 60 px columns, three
52 px rows, twelve a page, the names at scale 1, the fish at 0.9 times
`UI_F`. The page arrows keep their 34 px box (`RENDER_CRIT_NAV_W`, as on
the critter cards), so the grid starts under the box and its slop, at y 48
(`COLL_Y0`), not at `UI(60)`. PAGE n OF m starts at glass x 8, as far in as
CLOSE stands from the right, its pips at their own size. A collected fish's
passport takes the visitor's card's size (`PASSPORT_CARD_W` x
`PASSPORT_BTN_Y`); its labels stay at scale 1, so a value under a label
steps over the label's 7 rows and the gap scaled, and its COLLECTED stamp
stands beside STAGE, since IN YOUR TANK at scale 1 reaches the stamp's
design place.

**The paragon's reveal and THE FISH LAB are the design scaled** (0.4.3's
sync), and so are the first run's BODY, FINS and COLORS, which are the
lab's stage pages over the live tank. `render.h`'s 320 x 240 block gives
`EVP_*`, `LAB_*`, `STG_*` and `LAB_TUBE_*` their `UI()` values, and
`render.c` draws the tiles, the stage's handles, the bench, the wall
aquarium and the tube with `UI()`, `UI_TEXT()` and `UI_F`. Some things keep
their size: the reveal's parent mark (two small fish, which read as two
bars at 0.65), the parents' heads on ITS NATURE, and a live fish's ring in
the tank (the first run's NOW). ITS NATURE's rows stay at least 28 px
apart, so one bar's lower head and the next bar's upper one keep the
design's 4 rows between them. The held pages' moving boxes hold all they
redraw: the stage box's foot is `SETUP_ST_BOT + 2`, where a held
belly-fin handle's ring passed the plate, and THE TUBE's grown fish's box
starts 4 rows higher, where a held page kept a dorsal tip. Two more pages
of the setup flow had fixed-size things to clear: a critter's name page
(0.4.2) stands its plate between the top buttons, because the critter's
art keeps its size, up to the octopus's 80 x 48, and under the buttons the
plate ran into the letters' band; and the sword plant's placement page,
with a piece beside the plant, moves its hint to the drag zone's top and
the chevron onto the sand under it.

**A hold-approach starts 120 px from the finger, not 140** (`common/tank.c`,
`HOLD_APPROACH_FROM`). Upstream's 0.3.9.3 moved the start out to 140 px,
past the ring its fish now swim round a held finger. On the 320 x 240 tank a
finger held mid-tank has no fish that far out but in the corners, and the
FIRST HOLD APPROACH badge and the fry's HOLD step would wait on one being
there; the ring itself keeps its pixels, as the fish do.

**The octopus starts at the other end of the critters' row** (0.4.3's
sync, `tank_octopus_place` in `common/tank.c`). Upstream sets the critters
down in a row right of the reef bed: the urchin 18 px past the bed's end,
the crab 70 px, and the octopus 170 px, 100 px past the crab's spot. On the
320 px floor that last spot is past the end of the octopus's span (x 274),
so it stood 18 to 42 px from the crab's spot, and a crab bought after it
was set down on top of it; with a crab already in the water, the search for
clear sand pushed it against the urchin's spot instead. Here it starts left
of the urchin's spot by its own half and 30 px (x 110 to 134 for a baby),
where neither critter is set down on it later.

**BOOT's single press waits 0.4 s** on a 320 x 240 board. Upstream's two
presses for MY TANKS (0.3.9.3) cost nothing on its boards, where BOOT alone
does nothing; here BOOT alone is the sleep key, so a single press sleeps only
once the window for a second has passed (`s_btn_sleep_due`, `main.c`), and
one held longer than the window sleeps at its release, as before.

**The grass loop runs on the CYD as on upstream's boards** (0.3.9.6, which
turned it back on after 0.3.9.5 parked it; nothing in it is the fork's). The
back grass is baked into a loop of moments of its sway, and each frame by day
takes the moment the clock has reached; the night, and a loop being baked
again, draw the grass live. On a 320 x 240 board:

- **Its size.** 88 moments (`GRASS_LOOP_MOMENTS`: `main.c` names the square
  and the watch, and every other board takes the 1.8's). A band is the rows
  from the tallest back frond's tip down to the sand - 240 at most, the
  whole tank, 150 KB.
- **Its memory.** `GRASS_LOOP_BUDGET`, 2816 KB, is not all of it. The budget
  is what the bands and their masks, one bake buffer and its mask share with
  the arena of differences, and the arena takes whatever the bands leave, so
  in each plan below that part is the whole 2816 KB. The odd fronds' masks
  (one a band), a second bake mask and the loop's tables (`GL_META_BYTES`,
  32 KB) come on top of it (`gl_geometry` in `common/render.c`): 66 KB more
  with a new tank's 50-row band, 144 KB with a 240-row one. Planned in the
  simulator with the firmware's own rule (`gl_alloc` takes a block only
  while PSRAM keeps that block plus 1224 KB free): 2882 KB on a new tank,
  2941 KB with the beds at 0.4, 2960 KB from 0.7 up to full grass, in 38
  blocks, nothing refused. Of the ~4900 KB the CYD had free at 0.3.9.5 that
  leaves ~1940 KB; the boot show's 187 KB (a frame and a landing map) comes
  out of it and goes back when the show ends. Below ~4200 KB free the plan
  shrinks (fewer bands, a smaller arena) and is still whole at 3400 KB.
  Upstream's "a very tall grass kept live" is that plan giving up for want
  of memory: the CYD's band is never more than 240 rows and does not reach
  it. Internal RAM: nothing at run time - the loop's blocks are all PSRAM,
  and the bake task's 16 KB stack was already started in 0.3.9.5 - but 1.3
  KB more static RAM in the image (`.dram0.bss` 57,456 to 58,760 bytes),
  most of it the director's new `glcheck` (`colh[TANK_W]`).
- **Its core.** The bake runs on core 1 (the `bake` task), a slice at a
  time and only inside the window of a frame's flush (`s_bake_deadline`,
  the flush's running average); the CYD's 30 ms SPI send is a wide window.
  The tank task on core 0 lays the band into the frame and sends it as
  before.
- **Its display path** is unchanged. The bands are drawn by the frame's own
  code, so with `PT_FB_SWAP` they are in the panel's byte order already, and
  `display_port_spi.c` copies the frame into its DMA stripes unswapped. The
  320 x 240 world's pages - the boot show's frames, loop on and off, among
  them - match byte for byte between the plain and the `PT_FB_SWAP=1` build.
- **Its cost to a frame**, on the host (`--bench`, the 320 x 240 world,
  three runs each): a full canopy drawn live took 186-198 us a frame at
  0.3.9.5 and 186-194 us at 0.3.9.6 with the loop off; laid from the loop,
  once whole, ~32 us (68-71 us with the stats card's 36). At nubs, 37-39
  us became 25. The grass was ~85% of a full canopy's frame. The CYD drew
  a frame in 5 to 14 ms and sent it in 30, so the estimate is a drawing of
  a few ms and the send deciding the rate, near the 25 fps ceiling
  (`FRAME_MIN_MS`). Unmeasured on the glass (*Still open*).
- **Its one-pixel A/B.** The loop's frames match the live ones but for one
  pixel at a frond's tip, one green of the grass against another a step of
  the 565 colour apart (day, moment 7 of 104), the same in 0.3.9.5 and
  0.3.9.6. It is the grass's own - not the castle's, the sword plant's or
  the seahorse's: each taken out of the staging, it stays - and what draws
  it apart is not found. `--selftest-grassloop` allows four such pixels a
  frame on this world (`GL_CYD_NEAR`) and nothing else.

**The boot show** comes back with the loop: the tank built up piece by piece
on every start by day - FILLING THE TANK, the water rising, after a SWAP IN;
the light coming up after any other start - with the grass drawn live when
the loop is off or has no memory (0.3.9.6). It is the tank's own pixels at
full frame, so it needs no layout of its own on 320 x 240
(`--boot-show`, `--boot-show-fill`; about 5 s at the boards' 11 fps). The
loading screen, laid out for 320 x 240 in 0.3.9.5's sync, stands in only
when the show finds no memory. A wake from the dark is not a start: the
tank lights where it was, with no show.

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
| QMI8658 / QMI8658C | 0x6B, or 0x6A | 0x05 | `POCKET_TANK_IMU_QMI8658` (y) | the AMOLED board's chip; on the CYD mounted as the MPU-6050 is, axes measured 2026-10-17, in `sdkconfig.defaults.cyd` |
| MPU-6050 (GY-521 and the like) | 0x68 (AD0 low), or 0x69 | 0x68; clone dies reporting 0x70, 0x71, 0x72, 0x73 or 0x98 are taken too | `POCKET_TANK_IMU_MPU6050` (y) | what is fitted now; axes measured, in `sdkconfig.defaults.cyd` |

Anything else on the bus is ignored; with neither answering, the boot log
says so and the settings page keeps its SCREEN tile.

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

**The settings page's SCREEN tile becomes ROTATION once an IMU answers.**
SCREEN (a row until 0.4.2's tiles) was the keeper's way to turn the picture
on a board with no IMU; with one, the IMU turns it, so the tile is
ROTATION, and a SCREEN choice saved before is set aside. With no IMU the
tile is SCREEN, as before. The gesture has no switch of its own any more
(FACE DOWN, SLEEP / IGNORE, until 2026-10-08): it does what the SLEEP row
says, and NEVER ignores it as it does every other way into sleep
(*Sleep*). The AMOLED boards' layouts have neither the fork's SCREEN nor
its SLEEP, and are unchanged.

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
| BOOT, short press | the button (GPIO0), at release, when the press opened no chord | sleep - on a 320 x 240 board the dark, which a press ends; in the deepsleep mode a press within the 20-minute grace wakes in place, and after it BOOT boots | `sleep_button_poll`, `enter_sleep_for` |
| BOOT held 5 s + a tap | a touch landing at least `ADV_BOOT_HOLD_S` (5 s) after BOOT went down, BOOT still down; a finger landing sooner is an ordinary tap, and one already resting there does not count (0.3.9.2) | *START OVER?*; on the settings page the ADVANCED page | `sleep_button_poll` |
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
- **The QMI8658C** is fitted on a second CYD (2026-10-17), flat against the
  back as the MPU-6050 is: it answers at 0x6B, is probed first and takes over
  by itself, and its axes, read in the stand with the director's `imu`, are
  the MPU-6050's (X up, negative; Z out of the glass, negative) -- now in
  `sdkconfig.defaults.cyd`. A different mounting is one upright and one flat
  reading again.
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
  its page stay hidden (the page is still laid out for 448 x 368, and the
  pill, at x 260, stands over the NEXT arrow's 236 to 270; the director's
  staged `battery` shows it there).
- **The ADVANCED page's touch sitting is unmeasured on the glass.** Its
  limits are pixels set on the 1.8's 322 ppi, and stand about 2.25 times
  larger on the CYD's 143: a map may move the glass's edges up to 80 px, a
  sitting whose presses lie more than 20 px (rms) off its fitted map is
  refused, the check cross takes a press within 36 px, and the corner
  crosses stand 48 and 40 px in from the edges (`common/advanced.c`). A
  sitting on the board is the test.
- **A power cut, or the RESET button, loses the time** (it resets the chip's
  RTC timer too): the clock starts again from the save's stamp (or the
  build time, if that is later), and that absence is not lived through.
  Deep sleep keeps it.
- **Sending while drawing.** A frame is drawn and then sent, one after the
  other; sending it while the next one is drawn would lift the ceiling at
  40 MHz to about 33 fps.
- **The decorations** keep their pixel sizes: the castle is 146 px tall in a
  240 px tank. Worth a look on the glass.
- **The volcano at its own spot**, the middle of the 320 px floor (x 160,
  `VOLCANO_X_DEFAULT`, `TANK_FLOOR_X(224)`; it can stand only at x 122 to
  198). Its box, 192 x 208, leaves 64 px of water each side, less than an
  adult fish's box, and its top is at y 16, so a fish behind it has no way
  out to a held finger (0.4's "they come out to a held finger"): over the
  top needs y 24 or more, a side the whole box and 8 px. Across the span
  only x 122 and 198 let all four test fish out, from about x 144 to 176
  none come out, and an adult's depth against it hardly ever changes (7
  changes in twenty minutes in `--selftest-depth-pop`'s old staging, none
  with it IN FRONT); `--selftest-hold-emerge` and `--selftest-depth-pop`
  stage it at x 122 in this world. Undecided: a 320 x 240 default spot at
  one end of the span (new volcanoes only), a side-exit fallback in
  `tank.c` (it frees fish up to size ~1.16 at x 160, not fed adults or
  elders), or leaving it.
- **The grass is taller than the glass**, and not since 0.4: the 320 x 240
  world has always taken the 1.8's frond pitch (`VEG_SEG_PX`, 3.2 px, in
  `common/tank.h`), where upstream's taller boards scale theirs so that
  growth 1 is the surface. Here growth 1 is 342 px over 224 px of water: a
  bed passes the top of the glass from about growth 0.65, and every frond's
  ceiling (0.72 to 0.95, made for "a ragged skyline in the top third of the
  glass") is above it. A grown tank is a wall of grass past the surface,
  the grass loop's band is the whole tank whenever the grass is grown and a
  trim seldom shrinks it, and a fish at the surface over a bed is inside its
  canopy, so a starving fish's dash there runs at 0.6 speed. A 2.0 px pitch
  (107 x 2.0 from y 224, the tip at 10) puts the skyline under the surface
  as on the 1.8, but tightens every frond's waves, shrinks a nursery bed to
  28 px, and in the simulator made `--selftest-shop`'s dark snatch and
  `--selftest-tend`'s hold-approach fail and `--selftest-seahorse-stack`
  pass; scaling only the ceilings (about x 0.62) is the third way.
  Undecided.
- **The seahorse on a full-grown sword leaf.** The plant keeps the 1.8's
  leaf pixels, so on the 240 px glass a leaf's grip is at y -4 to 12, and a
  seahorse coiled there is drawn above the glass. It chose no sword leaf by
  itself in 18 simulated hours across six tanks; the director can put it
  there.
- **EMBERS' 48 embers** (0.4.2's volcanic vent) are the same count on every
  glass, so on the CYD they stand about twice as thick as on the 1.8;
  whether they look busy wants a look on the glass.
- **MY TANKS has not been on the glass** (0.3.9.5's sync): the shelves, the
  cards, the keyboard and the touch lock's 5 s hold are laid out and checked
  in the simulator only - the hold in the firmware's own touch port, which
  the simulator builds since 0.4.1 (`--selftest-touch-port`, the
  experimental tank's corner with it), but not in `main.c`, which opens MY
  TANKS on it and is not in the simulator. So are 0.3.9.6's two ways out of
  the lock - a fry's welcome and a new tank's setup taking taps while
  locked, and a hard boot (RESET, or the power pulled) unlocking.
  A SWAP IN restarts the board in software, which keeps the chip's timer
  as deep sleep does, so the tank that comes back should have lived its
  time on the shelf; nobody has seen it do so here.
- **The grass loop has not run on the glass** (0.3.9.6's sync; off on every
  board in 0.3.9.5). Its memory and its cost to a frame are the
  simulator's (*What the port changes*). What to read in the log on the
  first boot: the "starting the tank task: internal heap" line (it was
  59 KB at 0.3.9.5; the image holds 1.3 KB more static RAM); "loading: the
  boot show is over ... fps" after the show; then, every few seconds, the
  display lines - the frame rate, the draw and the send, "grass loop: 88
  moments, band N rows, diffs a of b KB", "core 1 baked ... slices" - and
  no "grass loop: ... refused" or "no loop - the plan gave up". The
  director's `glcheck` compares every baked moment with its neighbours (a
  flicker shows as pixels that differ from both). Two things only the board
  can say: whether the bake on core 1 slows the send it shares the PSRAM bus
  with, and whether the sway looks smooth at the CYD's frame rate.
- **The frame rate with the loop** is unmeasured. Before it, 0.3.9.4's swim
  depth and shadows made the host's full-canopy frame ~28% dearer (155 to
  200 us), at worst ~4 ms a frame on the CYD, about 21 fps down to 19; the
  loop takes ~85% of that frame off the host's (*What the port changes*).
- **`HOLD_APPROACH_FROM` 120** comes from the simulator, where one fish in
  four came in to a finger held mid-tank. Whether the FIRST HOLD APPROACH
  badge and the fry's HOLD step come in fair time is a check on the glass.
- **The crab card's COLLECT** (0.3.9.4) touches the big numbers above it on
  a 240 px card, both scaled from upstream's design.
- **0.4.3's pages have not been on the glass**, a CYD's or a
  Touch-LCD-2's: everything the sync laid out is checked in the simulator
  only - the keeper page's 22 px icon foot, the fish card's 26 px buttons
  with their half-size sprites and the halved bowl, the TANK row's pips
  under its arrow, the shop's area bar (8 x 2 on rows 238 and 239) and the
  half-size crab's 2 px lift in its square, the cards over the tank,
  VISITORS, THE FISH LAB and the paragon's reveal. The tank link has never
  run on either board: link mode, the DRIFTED notice and VISITORS are
  checked only in the 320 x 240 world's pictures.
- **The golden-hash selftests compare only on Apple arm64**
  (`--selftest-bg-golden`, `--selftest-gfx-high`'s `GFX_GOLD`,
  `--selftest-vignette-off`'s `VO_GOLD_MED`, `--selftest-libload`'s and
  `--selftest-oldfish`'s). The 320 x 240 world takes the rectangle's tables
  (their `#else`), so on Linux it passes them unchecked, and on a Mac it
  would fail them. It needs tables of its own, made on a Mac, or a skip in
  this world.
- **Upstream's own, seen in its worlds in the simulator** (the fork sends
  nothing back): THE FISH LAB's held FINS page leaves a ring's trail under
  the stage's moving box for some long belly fins (rows 266 to 268 on the
  1.8), and THE TUBE's held page leaves 1 to 31 px of a long VEIL fin (rows
  118 to 121 on the 1.8). The 320 x 240 world's boxes are the fork's and
  leave neither (*What the port changes*); if upstream fixes them its own
  way, the fork's are cut back (step 2 of *Syncing with upstream*). And a
  divisor only a test sets can make the night picture retake itself every
  other frame, never on a board.

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
SCREEN tile (UPRIGHT / FLIPPED) where the CYD with an IMU has ROTATION,
beside GRAPHICS, and under them the SLEEP row, as every 320 x 240 board
has.

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

The fork takes upstream's releases by merging them and means to send
nothing back - but upstream's pull request #15 comes from the fork's
`main`, so a push of `main` to `origin` updates it (*Licence*, and step 9
below). The `upstream` remote is `https://github.com/mediacutlet/pocket-tank.git`
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
     For the pages these boards scale - the reset prompt's tall pages, the
     critter cards' foot, the paragon's reveal, THE FISH LAB
     (`RESET_PANEL_*`, `RENDER_CRIT_*`, `EVP_*`, `LAB_*`, `STG_*`,
     `LAB_TUBE_*`) - the block holds upstream's numbers through `UI()`, and
     `EVP_FISH_Y1`'s `EVP_TILE_Y - UI(6) - 1` mirrors upstream's `- 7`: an
     upstream change to any of them has to be repeated there, so compare
     them at every sync. `RESET_PANEL_W` is the fork's own 240. The fork's
     settings taps, `SET_TAP_FLIP` and `SET_TAP_SLEEP`, are 40 and 41,
     `#define`s beside upstream's enum (below says why).
   - `common/render.c` - the CYD's paths for the card, the badges, the shop's
     coin and rows and the settings rows; since 0.4.3's sync also the icon
     feet's half-size sprites (`foot_art`, `bowl_icon_half`), the cards over
     the tank, link mode and VISITORS (`LINK_*`, `COLL_*`), MY TANKS's foot
     and THE LIBRARY (`SH_*`), the reset prompt and the paragon's held
     boxes.
   - `firmware/main/main.c` - the IMU, the sleep modes and the dark, the
     SCREEN and SLEEP settings, the update paths the CYD leaves out, the sleep's
     grace (20 minutes on a board with no PMIC, where upstream tests the
     clock chip alone), upstream's sleep hooks, which the dark calls itself,
     and THE LIBRARY's fish tank alone (`SHELF_XT_LIBRARY`).
   - `sim/main.c`, and since 0.4.3's sync upstream's harnesses beside it
     (`selftest_touch.c`, `selftest_libload.c`, `selftest_move.c`,
     `selftest_octokeep.c`, `selftest_full_reset.c`,
     `selftest_critshelf.c`, `selftest_upgrade042.c`, `stretch_test.inc`,
     `evo_kept_test.inc`, `updui_shot.c`) - the 320 x 240 world's selftest
     branches and snapshots.
   - `firmware/main/touch_port_ft3168.c` - the FT6336's and the CST816D's
     init, the CYD's turn, and the 5 s holds that open MY TANKS on a locked
     glass and from an experimental tank's corner.
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
     The rules 0.4.3's sync laid the designer's and the lab's pages out by:
     a fish drawn on a page takes `UI_F`, while a live fish in the tank and
     its ring keep their size; the stage's drag distances scale; pixel-art
     compositions and small shape marks keep their size. `setup.h`'s
     320 x 240 static asserts, at its end, hold the rows that stand on
     `render.h`'s bench, ITS NATURE's head gap, the critter's plate and the
     sword plant's hint.
   - `common/icons.c` and `common/icons.h` - generated, and upstream's as
     they come, as `tools/gen_icons.py` is. Never run that script again
     here: since v0.4.3 upstream's `icons.c` holds art whose PNGs upstream
     does not ship (the castle's three stages, the octopus), and a run
     would drop them. Never merge the two by hand either: take upstream's,
     then run `python3 tools/gen_icons_small.py`, the fork's. It makes the
     copies with upstream's own generator in a scratch directory and
     appends them between two marker lines, at the end of `icons.c` and
     before `icons.h`'s `#endif` - the `ms_*` badges' 24 px copies
     (`icon_ms_<name>_24`) and the half-size copies (`icon_<name>_half`, a
     2:1 box filter) of the art its `HALVES` table names. A second run
     changes nothing.
   - `docs/DEVICE.md` - upstream's bench log, with a fork section, *The CYD
     right now*, and a CYD note in its first rule. It conflicts where
     upstream edits beside them.
   - A board world upstream adds (v0.3.4's square, `TANK_SQUARE`) meets the
     320 x 240 in every list that names the boards: `common/tank.h`'s
     size, `common/render.h`'s corner chains, `common/version.h`'s
     `PT_BOARD`, tankcore's `CMakeLists.txt`, `map_touch` and
     `touch_port_init` in `touch_port_ft3168.c`, and `sim/Makefile`'s
     worlds and `check-all`. Both stay, each in a branch of its own.
   - Smaller blocks that can meet an upstream edit:
     `firmware/main/CMakeLists.txt` and `Kconfig.projbuild` (the board, the
     IMUs, the sleep modes), `board_pins.h`, `audio_port_es8311.c`,
     `director.c`, `link_port_espnow.c` and `link_spike.c` (the ESP-NOW
     callback), `common/tank.c` (the algae pace, the hold-approach, the
     octopus's starting spot), `common/advanced.c` and `advanced.h` (the
     BATTERY row, a calibration with no IMU), `common/shelf.h` (the crew's
     tanks), `common/update.h` (`UPD_CLOSE_W`),
     `common/llm/advisor_core.c` (the distance bands) and
     `common/progression.c` (the old CYD saves).

   Some changes merge cleanly and are still wrong:
   - a new question in `display_port.h` needs its answer in
     `display_port_spi.c`, the CYD's and the Touch-LCD-2's SPI display
     port, or neither links;
   - a new badge (`assets/icons/ms_*.png`) gets its 24 px copy when
     `tools/gen_icons_small.py` runs (0.4's FIRST MUTATION did), but it also
     needs its pair in `badge_art()`'s table in `common/render.c`. Without
     one it draws at 32 px in the CYD's 28 px rows, over its neighbours,
     with no error. A new sprite on an icon foot, a shop tab or the fish
     card's buttons likewise needs an entry in the script's `HALVES` table
     and its pair in `foot_art()`. A sprite upstream draws from code, not
     from a PNG (0.4.2's travel bowl, `BOWL_PX` in `render.c`), has no
     generated copy at all: the fish card's MOVE halves it as it draws
     (`bowl_icon_half`) with a copy of `bowl_icon`'s palette, so a colour
     upstream changes in the bowl has to be changed there too;
   - a new page measurement upstream writes as a literal, not `UI(n)`,
     draws at full size on the CYD's 240 px page. On the lab's stage, its
     room, the tube or the paragon's reveal, look at `--lab-shot`,
     `--evo-page` and `--evo-page-hold` in this world;
   - a step over a line of scale-1 text, or lines a fixed 20 px apart,
     written as `UI(n)`: the line keeps its 7 rows at every size, so the
     step comes out shorter than the line and a label runs into its value.
     Write it as the rows plus the gap scaled - `7 + UI(3)` for upstream's
     10 under a label, `7 * UI_TEXT(2) + UI(6)` - which is upstream's
     literal on its boards. 0.4's visitor's passport needed rows of its own
     (`PP_*`) and a taller card, and the collected one steps its labels this
     way;
   - a fixed-size widget on a header `UI()` scales pushes everything under
     it down: VISITORS' 34 px page arrows sit on a header scaled to 39 px,
     so its grid starts under the arrows' box and their slop (`COLL_Y0`),
     not at `UI(60)`;
   - marks upstream places at its own glass's pixel offsets beside ones the
     fork scales: the TANK row's page arrow drew its fresh ring 3 and 4 px
     out, where the badges' rings are `UI(3)` and `UI(4)` here (it takes
     theirs now), and its pips at `MSP_TANK_Y + 40`, which `UI(40)` put on
     the chevron's last step. The pips are `MSP_TPIP_Y` here, right under
     the rings as on the 1.8, and `MSP_TANK_BOT` is their last line, as
     upstream's `MSP_TANK_Y + 42` is;
   - hit-test slops through `UI()` beside a gap `UI()` also shrinks can
     make two buttons share points: START OVER?'s two choices (0.4.2), 78 px
     high and 8 apart, round to 51 and 5, closer than the hit test's two
     `UI(4)` slops of 3 px. A block of the fork's takes a row off each
     button, and the assert by `RP_*` checks that two slops fit in every
     gap. Where a selftest holds one slop to the pixel
     (`--selftest-crit-names`: NAME answers 11 px out and 5 up, not 13 and
     7), the neighbour's slop gives way, not the test - the crab's and the
     octopus's COLLECT reach only 4 px below themselves here
     (`CRIT_COLLECT_SLOP_DN`);
   - a rule upstream adds that is keyed on the tank's size needs a look at
     320 x 240;
   - an `#if` upstream puts around a function the fork has split into two
     branches can lose its `#endif` to the fork's: v0.3.4's `#if !TANK_WORN`
     around `set_lock_icon` in `common/render.c` merged unterminated, and
     the simulator would not build;
   - a name upstream removes that a fork assert used: the assert can lose
     half its check without a word. 0.4.2 removed `RENDER_TOOLS_Y` and
     `RENDER_TOOLS_H`, and `render.h`'s "card and toolbox inside the tank"
     lost its toolbox half; that half is a static assert beside `TILE_*` in
     `render.c` now (on 320 x 240 the row of four ends at y 234, x 244);
   - a settings tap upstream adds takes the next free number, which was the
     fork's three times: v0.3.9's `SET_TAP_BRIGHT_LIVE` came in as 10,
     `SET_TAP_FLIP`'s, v0.3.9.3's `SET_TAP_TANKS` as 11, and 0.4's
     `SET_TAP_LINK` and `SET_TAP_VOLUME_LIVE` as 12 and 13, FLIP's and
     SLEEP's. Since 0.4.3's sync the two are 40 and 41, `#define`s beside
     upstream's enum, clear of every answer a settings tap can carry:
     `common/render.h` asserts them past upstream's `SET_TAP_*` and the big
     screens' `SET_TAP_TO_*` (a tap upstream adds joins that assert), and
     `common/render.c` below its LIGHTS OUT hits (`SET_HIT_LIGHT_*`, 100)
     and the GRAPHICS page's `GFXP_HIT` (200). Plain numbers, not a second
     enum: GCC's `-Wenum-compare` warns on two enums' constants compared, as
     the asserts compare them, and ESP-IDF builds with `-Werror=all`;
   - a page these boards never show still compiles there, and its asserts
     hold: the UPDATES page's BACK and the GRAPHICS face's BACK share one
     footprint (the guard after UPDATES reads it from the GRAPHICS face,
     and `update.c` asserts it), so on 320 x 240 `UPD_CLOSE_W` is
     `MSP_CLOSE_W` (`common/update.h`), and a change to either BACK's reach
     is made to both. The page itself is not laid out for these boards:
     `sim/Makefile` leaves `--selftest-wifi3` and `--selftest-updui` out of
     this world's list, and `--selftest-update` leaves out the page's two
     faces here;
   - the frame's byte order: since v0.3.9 tankcore builds every board with
     `PT_FB_SWAP=1` (`common/render.h`), so the frame is already in the
     panel's order and `display_port_spi.c` copies it out of PSRAM without
     swapping. A port that swapped it again would scramble every colour,
     and the simulator, built without it, would never show it;
   - a scratch window upstream sizes for its own frames, which are 368 px
     high or more: v0.3.9's shelf thumbnails draw into a window of the frame
     up to 256 px square, 16 rows past the end of the CYD's 240, so
     `SHELF_FULL` is `TANK_H` on a 320 x 240 board;
   - upstream's selftests place pieces at fixed x on a 448 px floor: one
     past where the CYD's 320 px floor clamps it fails as a spot that "did
     not take". Give the CYD's share with `FLOOR_X()` or a branch of its
     own in `sim/main.c`. A staging that places or moves the full castle or
     the volcano with `TANK_FLOOR_X()` lands in x 122 to 198 here, the only
     span either can stand in, where a move can vanish;
   - upstream selftests staged on the 448 px floor and the 368 px glass,
     where the 320 x 240 changes the staging but not what is tested (eight
     in 0.4.3's sync). Any two of the castle and the volcano overlap here,
     and a move outward stops at the end of the span (`--selftest-roost`);
     the octopus's den is under the castle wherever the two stand
     (`--selftest-octo-keep`); the hero tank's coral and reef cluster stand
     over the castle's door (`--selftest-octo-den`,
     `--selftest-octo-den-move`); the volcano's own spot, the middle of the
     floor, leaves an adult fish behind it no way out and almost no change
     of depth (`--selftest-hold-emerge`, `--selftest-depth-pop`; *Still
     open*); its peak is 24 px under the surface
     (`--selftest-seahorse-stack`); the crab's own spot is right of the
     volcano, not left of it (`--selftest-crit-cuts`); and a pellet reaches
     the sand before the fish eat it, which brings the crab out of its
     roost (`--selftest-octo-keep`). Each has a 320 x 240 branch with its
     reason, and each was shown to fail with its bug put back: three bugs
     put back in `common/` for a moment (the roost's move re-check, the
     den's move, the octopus's front lane), and the controls
     `PT_SH_STACK_OFF`, `PT_DEPTH_POP_OLD`, `PT_HOLD_EMERGE_OFF`,
     `PT_OCTO_NOGATHER` and `make 320X240=1 check-crit-cuts-control`;
   - a layout name upstream adds that pins a thing to its own glass:
     v0.3.9.1's `MSP_FOOT_Y` put the milestones foot at y 326, off a 240 px
     page. It needs a value in `render.h`'s 320 x 240 block, and a static
     assert there to hold it;
   - layout names upstream keeps in `common/render.c`, not `render.h`
     (link mode's `LINK_*`, VISITORS' `COLL_*`, as MY TANKS's `SH_*` and
     `NK_*`): `render.h`'s 320 x 240 block cannot redefine them, because
     `render.c` defines them after the header is read. Their 320 x 240
     values stand beside upstream's in `render.c` - a branch in a chain, an
     `#ifdef` round a single name, or `UI()` round the literal - with the
     static asserts that hold them. A sim tap at the rectangle's pixels
     misses such a page's places: `--snapshot`'s collection cards tapped
     page (70, 100), off this grid, so `render_collection_where` (320 x 240
     only) gives a place's middle;
   - a whole page upstream adds in literals (v0.3.9.2's ADVANCED page):
     every number becomes `UI(n)` or `UI_TEXT(s)`, and then every one of
     its pages wants a look in the 320 x 240 world's snapshots;
   - a shop row or pop-up upstream writes in the 1.8's columns: the art at
     32, the words at 76, at top + 2 and top + 20 in scale 2, the button at
     the row's top; in a pop-up, the art through `blit_icon_scaled(..., 2)`
     and the lines at Y + 88, 118, 138, 164 and 184. v0.4.1's BACKGROUNDS,
     v0.4.2's SELECTIVE BREEDING and v0.4.3's FISH LAB all came in this
     way: on 320 x 240 such a row stands about 40 px right of the others,
     and its pop-up's lines run over its button. It takes `SHP_ROW_X`,
     `SHP_TEXT_X`, `SHP_BTN_DY` and `UI()`/`UI_TEXT()` as `shop_fry_row`
     has them, the pop-up's art at `UI_TEXT(2)` and its price at
     `Y + SHP_PRICE_Y`; `render.h`'s shop asserts and `render.c`'s
     `SHP_PRICE_Y` assert then check the rows, the foot and the price line.
     A line upstream puts under a pop-up's price with a coin of its own
     (0.4.3's lab: YOU HAVE and its coin) does not fit at all: the 16 px
     coin does not shrink, and two such lines and the button overrun the
     320 x 240 pop-up's 159 px;
   - a tap band upstream writes in the 1.8's numbers on a page whose
     320 x 240 columns are the glass's: 0.4.2's waiting row tapped from
     x 24 to `PAGE_W - 24`, and `render_bowl_row` reported that box, but its
     bowl starts left of it here. The row answers from `MSP_WAIT_HIT_X0`
     (`MSP_HIT_X0` here, the divider's 24 upstream) to `MSP_RULE_X1`;
   - an upstream selftest that taps or measures a card at the 1.8's
     numbers (`--selftest-nest` tapped the PAIR card at
     `(TANK_W - 336) / 2 + 12 + 75`; `--selftest-move` measured the travel
     bowl's card at 336 px and scale 2) can pass by luck on 320 x 240 while
     the card is unscaled, and break once it is laid out. Its numbers take
     `UI()` and `UI_TEXT()`, which are the literals on upstream's boards,
     and the check is shown still to fail with the bug put back;
   - a page or a row for hardware the 320 x 240 boards lack is hidden there,
     not scaled: v0.3.9.2's BATTERY row sets the AXP2101's charge current,
     and drawn here it showed the 1.8's numbers and "set" a current nothing
     applied (`ADV_BATTERY_ROW`). So is one for a service they lack: the
     crew's tanks (0.4.2) need the crew check only update mode runs, so a
     row or a page upstream adds for the crew stays out of the 320 x 240
     library (`SHELF_XT_LIBRARY`), and `lib_thumbs.c` needs no 320 x 240
     size while page 2 stays hidden. A step of a flow that no board here
     can take goes the same way: `adv_quarter`'s -1 means no IMU on a
     320 x 240 board (`main.c`), and `advanced.c` then ends the calibration
     at DONE (`s_no_imu`) where upstream's would wait on TURN IT OVER;
   - a hook upstream adds to its sleep path: the fork's `enter_dark`
     returns before upstream's lines run, so it calls the hook itself
     (v0.3.9.2's `adv_close`; 0.4's `link_dev_sleep`, `keys_up_wait` and
     the watchdog's feed; 0.4.3's `tank_link_swap_park` and the black box's
     grace events);
   - an upstream selftest whose probe lands on something only the 320 x 240
     layout overlaps: v0.3.9.1's probe above the milestones foot fell on
     the sand dollar, and the battery pill stands over the NEXT arrow. A
     branch in `sim/main.c` says which, and why;
   - a family of pages upstream draws in the frame's coordinates, a set of
     numbers per board in an `#if PAGE_BOWL / TANK_WATCH / TANK_SQUARE /
     else` chain: v0.3.9.3's MY TANKS (`SH_*`) and its keyboard (`NK_*`).
     The 1.8's numbers run off a 320 px glass; the 320 x 240 needs a branch
     in each chain, and the drawing code's own numbers `UI()`/`UI_TEXT()`.
     A number upstream writes in several functions of one page goes through
     one fork name: THE LIBRARY's row y in `render_library`, its tap, the
     loading bubbles and a late picture all take `SH_LIB_Y`, so the 320 x
     240 value reaches every one - 0.4.2's new functions had copied
     upstream's literal and missed the fork's taller rows;
   - a second press upstream gives BOOT (v0.3.9.3: twice opens MY TANKS)
     where its boards give the single press nothing to do: on a board
     without a PMIC upstream's code sleeps at the first press. The 320 x
     240's single press waits out the window (`s_btn_sleep_due`);
   - a way back only BOOT gives (v0.3.9.3's touch lock): the CYD in its case
     needs one on the glass (the 5 s hold, `touch_port_take_lock_open`).
     Look for the pages that can appear with nobody touching: a fry's
     welcome and a new tank's setup come up by themselves on a locked glass,
     and MY TANKS will not open over them. 0.3.9.5's sync made the hold
     undo the lock over them; 0.3.9.6 lets them take their taps through the
     lock, so the fork's branch went and the hold is left for the plain
     locked tank. Look for the words that name BOOT too: on 320 x 240 the
     touch lock's note (`TLNOTE`), its hint, and the experimental tank's
     thank-you (`render_xt_notice`) name the hold instead. And for a glass
     of its own: 0.4.2's experimental tanks have one, so the hold leaves
     them too - only from the glass's bottom-left corner (`XT_BACK_PX`,
     56 px), since a still finger anywhere else is the tank's own gesture
     (the experimental-tank branch of `touch_port_ft3168.c`, under
     `CONFIG_POCKET_TANK_320X240`). Both holds count only a finger that
     stayed still: a press that once leaves its 24 px slop is a drag to its
     lift (`s_hold_moved`). `--selftest-touch-port`'s `xt_hold_port`, in
     this world, tries the corner, the middle, a finger that strays and
     comes back, and the locked glass;
   - a fix upstream makes for a case the fork already fixed its own way
     (that one, 0.3.9.6): take upstream's and cut the fork's back to what
     upstream's leaves uncovered on the CYD - one rule, not two, and fewer
     lines to meet at the next sync;
   - a rule keyed on how the board started (0.3.9.6: a hard boot unlocks the
     touch lock, by `esp_reset_reason`): on the ESP32-S3 the RESET button is
     the chip's enable pin, a power-on reset, so on the CYD it counts as a
     hard boot, as pulling the power and a flash over the cable do, and a
     SWAP IN (`esp_restart`) does not. Read what each reason is on this
     chip (`esp_system/port/soc/esp32s3/reset_reason.c`) before deciding a
     rule needs a CYD branch - this one did not;
   - a widget upstream adds at a fixed size onto a page the fork scales:
     v0.3.9.3's critter arrows (a 34 px box) covered the start of the
     seahorse card's scaled text column. `--critter-cards <prefix>` draws
     every critter's card - the snapshot set has only the snail's and the
     crab's - and `--shelf-shot` in the 320 x 240 world adds pages for this
     world alone: `_shelf_locked_now3`, the locked NOW tank at the glass's
     right edge (beside the padlock, until 0.4.2 moved it into the foot),
     and `_library_fish_alone`, THE LIBRARY as these boards show it;
   - a rule that reads a save by where its pieces stand: v0.3.9.3's
     relayout takes a save from before 0.3.9.3 with a spot past the floor
     for the 1.8's world. The 1.8's fixtures do that on the 320 px floor
     (their bubble column at 358), so `--selftest-saves` moves its expected
     images the same way in that world (`cyd_expect_relayout`); a CYD's own
     save never has a spot past 320;
   - a distance in pixels chosen on the 1.8's tank: v0.3.9.3's
     `HOLD_APPROACH_FROM` 140 (120 here), and v0.4.3's octopus set down
     100 px past the crab's spot, past the end of its span on the 320 px
     floor (`tank_octopus_place`: it starts at the row's other end here);
   - upstream selftests with a number or a shape of their own glass: the
     seahorse's snack sum (a fish, hungry after ten minutes of sleep, ate
     one of the pellets on the small tank), the sword plant's leaves on the
     pumpkin (54-63 px here, 100 asked), the padlock inside a 40 px rounded
     corner (the CYD's glass is square), the grass loop's A/B (a shade at
     a frond's tip). Each has a 320 x 240 branch in `sim/main.c` with its
     reason;
   - the same kind, from 0.4.3's sync, each a 320 x 240 branch with its
     reason, each moved staging shown to fail with the bug it guards put
     back for a run. The famine dash in `--selftest-sleep`: the school begs
     inside bed 0's canopy, which reaches past the glass here, so the keeper
     trims the beds to `VEG_START` before feeding. `--selftest-nod`'s bar
     is the big screens' 5: this water's control rocks 25.5 times a slow
     minute against the 1.8's 17.8, and the fix cuts both by 84-85%.
     `--selftest-backgrounds`: the drift budget is Twilight's count on the
     1.8's glass (68), because EMBERS' 48 embers are fixed where Twilight's
     32 here scale with the glass; its tube worms stand 2.1 px apart, so a
     tip is compared with the first pixel to its left that is no other
     worm's tip. `--selftest-grassloop-shrink` multiplies each scissors
     height by 0.65, the share of a full frond this water holds.
     `--selftest-gfx-night`'s short-of-room case divides by 200 so that a
     40-row slice fits, and its pool sweeps straddle this glass's 352 KB
     night picture: MEDIUM keeps it from a 950 KB pool, and the day loop
     alone fits at 770-900 KB. `--selftest-vignette-off` holds the volcano
     to 122-198, so it moves 16 px back from where it stands.
     `--selftest-crit-shelf` pays a snack the seahorse took in the water
     before the rest is measured. `--selftest-upgrade-042` takes this world
     for another world of the rectangle's fixtures (`UP_RECT` 0), whose
     saves load here in proportion. `--selftest-libload`'s "the bubbles
     stood still" compares frames two apart, because a bubble crosses the
     44 px glass in 2.4 s, under a pixel a 25 fps frame; it also checks that
     row 0's late picture lands on the page's own row, and its harness keeps
     the jelly row in this world, though `main.c` shows none, because it
     tests the common picture machinery. `--selftest-lab`'s drags take
     `UI(n)` and `ST_PAST(n)`, as `--selftest-pop`'s do;
     `--selftest-evo-kept`'s HOLD-track probes are `UI(n)`; and
     `--selftest-nature-marks` takes `setup.c`'s forms. The held boxes drawn
     0.13 s behind while a handle is held, for one, are caught by the moved
     mid-drag staging and missed in this world by the 1.8's (200, 200);
   - an upstream selftest staged on fronds the 320 px floor does not have:
     0.3.9.6's `--selftest-grasspatch` nibbles bed 1's fronds 0 and 4, and
     bed 1 has three here, so the case made two boxes, not three, and
     failed its own staging check. A staging moved for this world has to be
     shown to catch the bug it was written for: the first choice (bed 2's
     fronds 0 and 4) passed with 0.3.9.5's splice put back; bed 0's fronds
     0 and 4 with bed 2's last frond fails there, as upstream's does on the
     1.8;
   - a held page's moving boxes (`setup_lab_bands`, `render_evo_boxes`)
     must hold everything the page redraws. On 320 x 240 the lab's stage
     box's foot is `SETUP_ST_BOT + 2`, because a held belly-fin handle's
     ring passed the plate, and THE TUBE's grown fish's box starts 4 rows
     higher, because a held page kept a dorsal tip. `--selftest-lab`'s L7,
     in this world only, drags every handle of every body and fin set and
     compares the held page with the page drawn whole. Upstream's boards
     still leave both trails (*Still open*);
   - the IMU's interface grows: v0.3.9.3's quarter turns (`imu_port_sides`,
     `imu_port_quarter`, the way up seeded from NVS `inv` and `side`) went
     into `imu_port.c` by hand, the side's sign from each chip's own axes,
     and upstream moved `imu_port_init` ahead of update mode, the fork's
     `s_imu` with it;
   - `display_port.h`'s questions in v0.3.9.3: `display_port_set_quarter`,
     `display_port_quarters`, `display_port_side_tune`,
     `display_port_side_info`, `display_port_probe`, `board_port_hw`,
     `board_port_probed` and `board_sd_select`, answered in
     `display_port_spi.c` (no sides; `board_port_hw` NULL, since nothing it
     probes tells the CYD from the Touch-LCD-2, so no WRONG VERSION card);
   - an ESP-IDF call whose signature changed between 5.4, which upstream
     builds its S3 boards with, and 5.5, which the fork builds every board
     with: 0.4's tank link registers an ESP-NOW send callback, and
     `esp_now_send_cb_t` takes `(const esp_now_send_info_t *,
     esp_now_send_status_t)` in 5.5 where 5.4's took
     `(const uint8_t *mac, ...)`, so `firmware/main/link_port_espnow.c` and
     `link_spike.c` pick theirs by `ESP_IDF_VERSION`;
   - **the simulator never compiles `main.c`**: a conflict resolved there
     is checked only by a firmware build (0.3.9.5's sync left a stray brace
     there). Run `tools/build_cyd.sh` before the long sim runs. Since 0.4.1
     it does build the touch port, `firmware/main/touch_port_ft3168.c`, on
     the host (`sim/selftest_touch.c`, with `sim/touchhost` standing in for
     ESP-IDF), so `board_pins.h` includes `sdkconfig.h` only when
     `TOUCH_PORT_HOST` is not defined. The 320 x 240 world's stand-in
     (`HB_320X240`) runs the port's 320 x 240 map - the Touch-LCD-2's way
     up, not the CYD's 180-degree turn - so a new `map_touch` or
     `cal_point` path needs a look there.

3. **The licence.** Upstream's `LICENSE`, `NOTICE` and `CONTRIBUTING.md`
   come in as upstream has them and are never edited here. See whether this
   release changed them:

   ```sh
   git diff main upstream/main -- LICENSE NOTICE CONTRIBUTING.md
   ```

   Anything printed is read before the merge goes further: a `Required
   Notice:` line added or changed goes into `NOTICE-CYD` word for word, and
   a new licence, or a new release where one ends, changes `NOTICE-CYD`'s
   layers and the table in *Licence*. Then, whether or not they changed,
   re-run the file-list command in *Licence* and bring `LICENSE-MIT-CYD`'s
   list and the SPDX headers in line with what it prints: a path upstream
   now has as well is upstream's from this sync on.

4. **The simulator, in all seven worlds:**

   ```sh
   make -C sim check-all          # the 1.8, the round board, the watch, the square, the P4, the Tab5, then the 320 x 240
   ```

   A pocket world's `check` runs, after its selftests, the grass loop's
   tests again at GRAPHICS LOW and MEDIUM, the variants
   (`--selftest-crab-depth` at LOW, at MEDIUM and with `PT_VIG=off`,
   `PW_ONLY=ABCDEF --selftest-pw`, the octopus's tests with
   `PT_OCTO_ASYNC=1`) and the controls that must fail. The cuts tests'
   controls are targets of their own: `make -C sim 320X240=1
   check-cuts-control check-crit-cuts-control` proves those tests in this
   world. The golden-hash selftests compare only on Apple arm64, so on
   Linux this world passes them unchecked (*Still open*).

   (From v0.3.5 to v0.3.8 the round world failed `--selftest-tend` on
   Linux, in upstream's own tree too; v0.3.9 widened the two-fish hold's
   window and it passes.)

   Then the 320 x 240 world once more with the frame in the panel's byte
   order, as the firmware builds it, and its pages against the plain
   build's: every one must match byte for byte (`write_ppm` unswaps).

   ```sh
   make -C sim 320X240=1 OUT=fishsim-320x240-swap BUILD=build-320x240-swap EXTRA_CFLAGS=-DPT_FB_SWAP=1
   ```

   The pages: every page option the simulator has, not a chosen few. Each
   `--*-shot` (`grep -ho '"--[a-z0-9-]*-shot"' sim/*.c | sort -u` lists
   them) but the P4 worlds' `--big*`, which never end in a pocket world;
   `--snapshot`, `--critter-cards`, `--evo-page` and `--evo-page-hold`; the
   boot show's frames (`--boot-show`, `--boot-show-fill`, and `--boot-show`
   again with `PT_LOOP_OFF=1`); and `--selftest-front-keep <prefix>` with
   `PT_FRONT_KEEP_PLAIN=1`. 0.4.3's sync compared 853 pages this way, where
   0.3.9.6's chosen few were 449. Three of upstream's harnesses write theirs
   with writers of their own that do not undo the swap (`ll_ppm` in
   `sim/selftest_libload.c` for `--libload-shot`, `ppm` in
   `sim/selftest_xtnotice.c` for `--xt-notice-shot`, and
   `sim/jelly_sim.c`'s for `--jelly-shot`): their 17 pages differ in every
   pixel between the two builds and match once the swap build's are
   unswapped - the frame is right, and the writers are upstream's and stay.
   (Two developer sheets outside this set, `--genes` and `--eye-sheet`,
   fill their background with a raw RGB565 value, so in the swap build the
   background alone differs.) `--linknote-shot` and `--notice-shot` never end in
   a pocket world, in upstream's tree too: run them under `timeout` and
   compare the exit codes (124), not pages.

   Each run can have a `/tmp` of its own (bubblewrap: `bwrap --dev-bind /
   / --tmpfs /tmp ...`) and `POCKET_TANK_SAVE` pointing at a scratch file;
   then they run side by side (step 5 says why they must not share).

5. **Upstream's six worlds against upstream's own tree.** Build upstream's
   simulator in a worktree of `upstream/main` (it needs its own `sim/lvgl`),
   write every page option of step 4 from `fishsim`, `fishsim-round`,
   `fishsim-watch`, `fishsim-square`, `fishsim-p4` and `fishsim-tab5` in
   both trees - the `--big*` options in the P4 and Tab5 worlds alone - and
   compare them with `cmp`. The only differences expected are the lines
   that print the tree's own `git describe`: at 0.4.3 the settings pages'
   build id (a 71 x 8 px box at the foot) and the UPDATES pages' version
   line (142 x 14 px). Any other page that differs is the fork reaching an
   upstream board. The Tab5's update-mode pages (`--bigum-shot`) change
   with the machine's load: run them back to back in both trees, and they
   match.

   Give each run a private `/tmp`, as step 4 does, or run them **one at a
   time**; and each with `POCKET_TANK_SAVE` pointing at a scratch file.
   `--snapshot` keeps its Wi-Fi network in one fixed file,
   `/tmp/pocket-tank-snapshot-wifi.txt`, so two run side by side overwrite
   each other's and the update pages (`update_uptodate`,
   `update_wrong_board`, `update_one_action`) change from run to run, even
   in upstream's own tree (v0.3.4's sync); other options keep scratch saves
   and boards at fixed paths under `/tmp` too. And without
   `POCKET_TANK_SAVE` each run writes the sim's own save,
   `~/.cache/pocket-tank/tank.sav`.

   0.4.3's sync compared the code as well: every `common/*.c` and
   `sim/*.c` compiled to assembly (`-O2 -S`, labels normalised) in
   upstream's six worlds, the tree just after the merge and its build fixes
   - whose pages had matched upstream's - against the finished branch
   (`common/render_big.c` and `render_wide.c` are fragments `render.c`
   includes, not units of their own). One difference is expected:
   `sim/main.c`'s P4-only big-shop test has a `BTN` macro whose failure
   message prints `__LINE__`, so a line the fork adds above it in
   `sim/main.c` moves two constants in the P4 worlds' code; compiled with
   `-Wno-builtin-macro-redefined -D__LINE__=0` the two are the same.

6. **The eight firmware images build:** `tools/build_cyd.sh` with no port;
   the Touch-LCD-2 with `idf.py` into `build_wst`, as
   *The Waveshare ESP32-S3-Touch-LCD-2* shows;
   the 1.8, the round board, the watch and the square with `idf.py`, each
   into its own build directory as README.md's
   [*Run it on real hardware*](README.md#run-it-on-real-hardware) shows -
   delete those directories first, or at least their `sdkconfig`, since the
   defaults only fill in what an sdkconfig lacks; and since v0.4.3
   upstream's P4 home tank and the Tab5, on their own target:

   ```sh
   idf.py -B <dir> -D IDF_TARGET=esp32p4 -D SDKCONFIG=<dir>/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.p4" build
   ```

   (the Tab5's defaults are `"sdkconfig.defaults;sdkconfig.p4;sdkconfig.tab5"`).
   The two need an ESP-IDF 5.5 later than 5.5.0, which lacks the DSI
   panel's `on_frame_buf_complete` callback and declares no
   `gpio_deep_sleep_hold_en` for the P4 (upstream's own lines); 5.5.5
   builds both. Build them outside the repository, from an export of the
   tree (`git archive`) with build directories of their own: a P4 build in
   `firmware/` rewrites `firmware/dependencies.lock` and
   `firmware/managed_components` for the esp32p4 (both ignored by git, so
   nothing shows), the next S3 build resolves them again, and no
   `firmware/` build directory for them is in the fork's `.gitignore`. Copy
   the OTA signing key into the export, and delete it after. At 0.4.3 the
   CYD's image fills 95% of its 2.5 MB app slot (`0x261000` of
   `0x280000`), as upstream's four S3 boards' do; the Touch-LCD-2's 88%,
   the two P4 images' 90%.

7. **Back up the CYD's NVS (the tank's save) and its `storage` (MY
   TANKS's shelves, since 0.3.9.3) before flashing it:**

   ```sh
   esptool.py --chip esp32s3 -p <port> -b 921600 read_flash 0x9000 0x6000 nvs_before_<release>_<MAC>_<date>_<time>.bin
   esptool.py --chip esp32s3 -p <port> -b 921600 read_flash 0xD20000 0x2E0000 storage_before_<release>_<MAC>_<date>_<time>.bin
   ```

   `firmware/partitions.csv` puts NVS at `0x9000`, `0x6000` long, and
   `storage` at `0xD20000`, to the end of the 16 MB, and neither moves. The
   board's MAC (the one in its by-id path) and the time in the names keep
   two boards' backups, and two of one board's, apart. A flash that loses
   the tank goes back with `esptool.py --chip esp32s3 -p <port> write_flash
   0x9000 nvs_before_...bin`, and one that loses the shelves with
   `write_flash 0xD20000 storage_before_...bin` (*Recovering a board*).

8. **Flash the CYD** with `tools/build_cyd.sh <port>` and check it on the
   glass: the tank loads the save it had, the pages and the settings rows
   answer, and it sleeps and wakes.

9. **Only then fast-forward main:**

   ```sh
   git switch main
   git merge --ff-only sync-<release>
   ```

   Nothing is ever pushed to `upstream`; `origin` is the fork. But a push
   of `main` to `origin` also updates every open pull request to upstream
   whose head is the fork's `main`, and since v0.3.9.3 upstream's
   `CONTRIBUTING.md` puts its terms on what a pull request carries
   (*Licence*). Before that push, see which are open:

   ```sh
   curl -s 'https://api.github.com/repos/mediacutlet/pocket-tank/pulls?state=open&per_page=100' |
     python3 -c 'import json, sys
   for p in json.load(sys.stdin):
       if p["head"]["label"].startswith("lmoiseichuk:"):
           print(p["number"], p["head"]["label"], p["head"]["sha"][:12], p["title"])'
   ```

   On 2026-10-31 it printed #15, `lmoiseichuk:main` at `4d913b5`; on
   2026-11-01, after the v0.3.9.5 sync's push, the same at `daf18df`. One
   whose head is `lmoiseichuk:main` takes this sync - upstream's code,
   the fork's changes and `LICENSE-MIT-CYD`'s files - when `main` is
   pushed. Close it, or keep it, before the push, and make `NOTICE-CYD`'s
   *CONTRIBUTING UPSTREAM* and *Licence* above say which.
