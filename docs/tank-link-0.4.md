# Tank link (0.4): tanks side by side, fish that visit

Status: DESIGN, 2026-10-04. No code yet. Branch `link-0.4` in the worktree
`~/.cache/pocket-tank/wt-link`, off dev main 29c6f49 (after v0.3.1).
The other 0.4 item, genetics, is on branch `genetics-0.4` in its own session.

## What we are building

Put two or three tanks next to each other on a desk. Each keeper opens LINK
on their tank. The tanks find each other over the radio, the keeper holds an
arrow on the edges that touch, and the glass between them opens. Fish swim
from one tank into the next, look around, and swim home. If the tanks come
apart while a fish is next door, it poofs into a cloud of bubbles there and
comes back out of a cloud of bubbles at home. Later in 0.4: a visitor can
court a fish in its host tank (cross-tank fry), and a keeper can give a fish
to a friend.

This is also the "fish passport": one fish record that can leave a tank and
come back. Everything here is built on that record.

## The five facts that shape the design

1. **Internal RAM.** The running tank has about 54 KB of internal RAM free
   (OTA.md, on the 0.3 builds that link Wi-Fi). Starting the Wi-Fi station
   for an update took about 73 KB, which is why update mode runs BEFORE the
   tank exists. A link needs the radio WHILE the tank runs. Whether it fits
   is the first thing to measure, on all three boards, before any other work.
   This is the go / no-go for the whole feature.
2. **4 s a decision.** Frames and the model share one memory bus. A visitor
   is one more fish to draw and one more fish for the advisor to ask about.
   Radio traffic adds a little bus and CPU work. Visitors are capped, and fps
   plus decision time get read on every board after each phase.
3. **A fish has no identity of its own yet.** Names, colors, parents, badges
   are stored by slot. A fish that leaves needs a permanent ID, and parents
   need to be IDs (a parent may live in another tank).
4. **Never two copies, never zero.** A fish is either simulated at home or
   next door, never both, and a lost radio frame or a dead battery can never
   delete a fish. This is the hard part of the feature and it gets its own
   section (leases) and its own selftest.
5. **Three boards.** The rectangle, the bowl and the watch must all link with
   each other, in any mix. Everything lives in `common/` and works in PAGE /
   TANK names, per BOARDS.md.

## The radio: ESP-NOW if the spike says it fits (recommendation)

The idea said "activate the Bluetooth radio". On these chips Bluetooth and
Wi-Fi are the same 2.4 GHz radio. I recommend **ESP-NOW** (Espressif's
peer-to-peer Wi-Fi frames, no router, no network) for the link:

| | ESP-NOW | Bluetooth LE |
|---|---|---|
| already in the image | yes - Wi-Fi is linked for OTA, ESP-NOW adds a few KB of code | no - NimBLE + controller, ~300-400 KB of flash (we have ~640 KB) |
| internal RAM | Wi-Fi driver without lwIP / TLS: to be measured, est. 30-50 KB | controller + host: est. 30-50 KB (host can go to PSRAM) |
| three or four tanks at once | trivial: every tank talks to every tank, broadcast or unicast, hardware acks | each pair is a connection; central / peripheral roles; much more code |
| latency | 1-5 ms | 7.5-50 ms connection interval, worse with several links |
| a fish half in each tank (smooth crossing) | easy: 20 position updates a second | hard |
| frame size | 250 B (1470 B with ESP-NOW v2, IDF 5.4+) | 20-244 B per write after MTU exchange |
| power while linked | higher: the receiver stays on (~80-100 mA; ESP-NOW has a wake-window power save to try) | lower |
| phones | no | yes (a phone could someday see a tank) |

Keeper-facing words never say "Bluetooth" or "ESP-NOW": the row is LINK.
The radio sits behind a port (`link_port.h`, like every other chip), so a
board can use BLE later without touching the game.

## The network

- **Tank ID**: a random 32-bit number made once and saved (the MAC is the
  radio address, not the identity: the ID survives a board swap via a save
  restore later).
- **Badge**: every tank in link mode shows a badge = a SHAPE and a COLOR
  together (circle / triangle / square / diamond, each with its own fixed
  color). Strato is color blind: the shape is the identity, the color is a
  second cue, never the only one. Two tanks nearby that rolled the same badge:
  the one with the lower tank ID picks another.
- **Edges**: v1 links the LEFT and RIGHT sides only. Fish swim sideways; the
  floor is sand and the top is the surface, so a tank above or below makes no
  sense in water. A network is therefore a ROW of tanks, at most 4, and every
  tank knows its left and right neighbor. That is the whole map.
- **Depth across different glasses**: a fish leaving at 40% of the way from
  the surface to the floor arrives at 40% on the other side. The bowl and the
  watch have other heights; the fraction is what crosses.
- **The door**: the middle band of the side glass (where it is upright on the
  bowl and below the watch's corners). Outside the band the side is still
  glass.

## Linking: what the keeper does

```
   TANK A (triangle)              TANK B (square)
 +--------------------+         +--------------------+
 |        [▲]          |         |        [■]          |
 |<                  >|  <-->   |<                  >|
 |    fish  fish      |  hold    |      fish           |
 +--------------------+  both   +--------------------+
```

1. **Settings -> LINK TANKS.** The tank goes into LINK MODE: the fish keep
   swimming, the tank's badge shows at the top, a gray arrow sits on each
   side wall, a DONE chip at the bottom. Link mode owns the glass, like a tool
   in hand: no feeding, no light, no cards, only the arrows and DONE.
   Link mode times out after 2 minutes with nothing happening.
2. **Another tank in link mode is heard nearby** (signal strength over a
   threshold, roughly an arm's length, so a room full of tanks does not list
   everyone): its badge appears in a small "NEARBY" row.
3. **Put them side by side. Hold the arrow on the edge that touches.** A ring
   fills under the finger (the placement page's hold). Holding A's right arrow
   tells every nearby tank "A is asking". On B (and any other tank in link
   mode) the free arrows turn into A's badge: "hold here to join the
   triangle". If two tanks are asking at once, the arrow shows both badges
   and nothing links until one lets go (signal strength cannot tell which
   one is touching - phase 0, below).
4. **Hold the matching arrow on B** (within 5 s of A's, or at the same time
   with two hands). Both glasses: the arrows meet, a cue sounds, and that
   side's glass opens into a door with the neighbor's badge above it.
5. **A third tank**: hold B's other arrow and C's matching one. A-B-C.
6. **DONE** closes link mode. The doors stay open; the tank works as usual.

**The link closes when**: the keeper reopens LINK and holds an open door; a
tank goes to sleep (PWR key); a tank is not heard for 10 s (battery died,
carried off) - see the leases below. On close the door turns back into
glass, and any fish on the wrong side goes home.

**A tank that moves does NOT close the link** (Strato, 2026-10-04: "i just
dont want a sharing session to be abruptly ended because one tank moved a
little. kids will be using these."). Moving is forgiven in steps:

- **A nudge** (a tap on the glass, a bump, a short slide - the IMU sees
  motion for under ~1 s): nothing happens.
- **Drifting** (motion that goes on, or a tank lifted and set down): the
  doors on that tank pause - no fish starts a crossing; fish already next
  door stay there. Both glasses show a plain notice:
  "THE TANKS DRIFTED. PUT THEM BACK SIDE BY SIDE AND KEEP THEM STILL, OR THE
  FISH WILL SWIM HOME." with a countdown ring (about 30 s).
  - Both tanks still again before it runs out: the notice goes away and the
    doors open again. No holds needed.
  - Still moving when it runs out: the visitors swim home through the door
    (a calm swim, not a poof - the radio is still fine), then the door asks
    for the arrows again: "HOLD BOTH ARROWS TO JOIN AGAIN". Holding both
    re-links with no trip through settings.
- **A worn watch** moves all the time: it never pauses for drifting; only
  the other rules close its link.
- The words are for kids: short, no "link", "radio" or "error"; the fish
  are never "lost" in any message.

## Visiting

### Crossing (two-phase: never cloned, never dropped)

A fish swims into a door heading out. Home tank A, host tank B:

1. A sends **OFFER**: the fish's record (the passport, below) + depth +
   heading + speed. The fish keeps swimming toward the edge on A's glass.
2. B checks it has room (at most 2 visitors) and answers **ACCEPT**, holding
   a hidden slot. No answer in 0.5 s, or a refusal: on A the door acts as
   glass for this fish (it turns, as at any wall) and the door counts as busy
   for a few seconds.
3. When the fish's middle passes A's edge, A FREEZES it (the slot is kept,
   marked AWAY at B - this frozen copy is the backup) and sends **GO**.
4. B gets GO and swims the fish in from its own edge, after a short pause
   for the two bezels so the motion reads as one swim. B answers with the
   first **VISIT** report.

B never simulates the fish before GO; A stops simulating before it sends GO.
At no instant do both run it. If GO is lost, B drops the hidden slot after
1 s and A restores its fish by the lease rule.

Later polish (phase 6): the fish drawn half on each glass while it crosses,
with 20 position updates a second. v1 lets the bezel gap hide the handoff.

### On the host

- Visitors sit in their own slots AFTER the host's fish (`fish[n_fish ..]`).
  Every existing loop over `n_fish` - the save, progression, the fry gates,
  selling, the cards, the milestones - skips them **by default**. They are
  added on purpose only where they belong: movement, drawing, food,
  following a friend, the advisor. About 300 places use `n_fish`; this way
  none of them change behavior by accident.
- The host's model decides for the visitor, from the visitor's own drives
  and personality, the same way it decides for its own fish. The model is
  the same everywhere (and if 0.4 ships a new one, both tanks still run the
  same prompt shape; the record carries drives, not tokens).
- A visitor eats the host's food (counts as a meal; not as a pellet from its
  own keeper's hand, so its trust does not move). It can follow a host fish.
- At most 2 visitors per tank, and at most 6 fish swimming in a tank in all
  (the device has never run more than its 5): fps and the 4 s decision
  budget. Phase 4 measures it; the cap moves with the numbers.
  **10-05:** 5 swimming a tank, visitors counted. **0.4.1 (Strato 11-05):** two
  visitor places whatever the tank holds - 5 of its own + 2 = 7 swimming at
  most (release-0.4.md 2k).
- The host keeper can tap a visitor: its card says the fish's name and
  "VISITING FROM" + the home tank's badge. No SELL, no RENAME.
- Every second B sends A a **VISIT** report: the visitor's drives, meals,
  badges. A writes them into the frozen copy, so the backup is never more
  than a second old.

### Going home

- A visitor goes home through the door it came in (v1: one hop only - a
  fish of A can visit B, not C beyond it).
- B: the visitor passes the edge -> B stops simulating it and sends
  **RETURN** with the final record, repeated every 200 ms until A answers.
- A: on RETURN it updates the frozen fish, unfreezes it at its door, answers
  **HOME**. B forgets the fish on HOME. A repeated RETURN after HOME is
  ignored (sequence number).

### The poof (a tank leaves while a fish is visiting) - leases

The rule that makes it safe: **a visitor lives on the host only on a
lease**, and only the home renews it.

- Home renews each away fish's lease once a second, but ONLY while it has
  heard the host in the last 2 s.
- The host keeps a visitor only until 6 s after its last renewal. Then the
  visitor POOFS on the host: a burst of bubbles where it was, the fish fades.
- Home restores a fish 10 s after it last heard the host: a cloud of bubbles
  at the door it left by (the middle of the tank if that door is closed), the
  fish fades in with the last VISIT report's state, and a small "MIRA SWAM
  HOME" note.

Why it never makes two: the host stops at most 2 + 6 = 8 s after home last
heard it; home restores at 10 s. Why it never makes zero: home never deletes
a fish because of the radio - only a GIVE (below) does, and that asks both
keepers. Each tank times with its own clock (seconds, no clock sync).

| what happens | on the host | at home |
|---|---|---|
| the keeper closes the door | visitors swim home through it if they are near, else poof at once (RETURN sent) | they come out of the door / a bubble cloud at once |
| a tank is moved or picked up | the DRIFTED notice; set back and still within ~30 s = nothing happened; else visitors swim home through the door | the same notice; its fish swim home |
| a tank goes to sleep (PWR) | before sleeping: sends its visitors home (poof + RETURN), recalls its own fish (RECALL) | the same, mirrored |
| the battery dies / out of range | the lease runs out: poof after <= 8 s | restored after 10 s |
| home crashes or is reflashed | the lease runs out: poof | at boot every AWAY fish is restored (visitors are never saved anywhere) |
| a frame is lost | resent; leases cover the rest | - |

## Encouraging visits, with the model still deciding

The model owns the decisions; visits come from the
goals it already has, with no retrain:

- **The neighbor is the stalest place there is.** EXPLORE already picks a
  destination in a zone the fish has not seen for a while. A linked
  neighbor counts as a zone never seen, so an exploring fish heads for the
  door. After a visit, the neighbor ages like any other zone.
- **A pulse of curiosity when a door opens.** Curiosity is a drive the model
  reads; every fish gets a bump when a link is made ("something new over
  there"), so EXPLORE comes up more often for the first minutes.
- **The prompt's `wall` stops seeing a door as a wall.** A fish next to an
  open door reads "wall clear" there.
- **Following a friend** goes through doors: a fish that follows a friend
  who crossed, crosses (if the host has room).
- **Home pull.** The longer a visitor is away, the more its EXPLORE
  destination leans to the door home; after ~5 minutes it is the only
  destination EXPLORE picks. Eating and resting are never overridden - a
  visit has no hard timeout that takes the decision away from the model.

A retrain could add a `door` token (schema v5) later; not needed for 0.4.

## Breeding across tanks (phase 5)

Strato's rule (2026-10-04): a visitor can court a host fish when the HOST's
fry gates are met (the host's glass, grass, fed fish). **Each tank that has
an open slot gets a fry** - both tanks if both have room (the population cap,
POP_CAP 5 on the sim and every board since 2026-11-03, keeps this in hand). A tank with no room gets none; the
courtship still counts for the other. Each fry belongs to the keeper of the
tank it is born in; its parents are stored by fish ID + name, so its family
page says "FROM MIRA (triangle)". The home tank's fry is born when the
visitor gets home (its RETURN carries "a fry is waiting"), out of the same
bubble cloud if it poofed home.

**A cross-tank fry is worth 0 sand dollars and cannot be sold** (SELL dims,
its reason on tap, like the keep-2 rule), so linking two tanks to farm fry
has no payoff. Assumed: unsellable for life, not only while a fry.

With genetics, this is the point of it all: a VEIL fin you never had can come
into your line from a friend's tank.

**The home holds the place (2026-11-03, the five-fish cap).** The host asks
the visitor's home for room (FRY_ASK) when the pair forms and again every
30 s, yes or no (TANK_LINK_FRY_ASK_S). A yes is a place the home HOLDS
(tank_t.fry_hold, counted by fish_held like a gift's ARRIVING note), so
nothing else takes it - the home's own birth, a gift, another fry, a shop fry.
The hold ends when the fry comes (LEV_FRY_IN), when the host says the pair is
off (FRY_ASK with its flag byte 1: a birth of ours, the partner gone, a
hand-off that could not start), when that fish of ours is no longer next
door, or 120 s after the last ask (TANK_LINK_FRY_HOLD_S). The host counts a
yes only 75 s after it heard it (TANK_LINK_FRY_FRESH_S), so a fry born for its
home always finds its place still held. Before this a yes held nothing:
courting takes 5-10 min, the home filled up, refused the FRY, and the host
kept the fry as a sixth fish (at six it was dropped without a word). The
last resort, if the hand-off still fails (the radio down before the offer, a
home rebooted mid-court) and the host is full: the birth is undone - the fry
waited in a hidden visitor place and was never on the glass - and the pair
courts again. An older 0.4 build next door: as a host it asks once and never
says the pair is off, so its yes lapses at our end 120 s on (it may still
keep a refused fry as its own sixth); as a home it holds nothing, but our
asks every 30 s narrow the gap, and a refused fry never takes us past five.

## Giving a fish (phase 6)

While linked, a fish card shows GIVE. Both keepers must agree on their own
glass (the giver picks the neighbor; the receiver gets an ACCEPT / NO
modal). The same rules as selling: a tank keeps at least 2 fish, the
receiver needs a free slot. Done in steps that survive a crash at any point:
giver marks the fish GIVING (saved) -> sends it -> receiver saves it as
ARRIVING (not alive yet) and answers -> giver deletes it and saves a
"given" note (its ID) -> receiver brings it to life. If the radio dies in the
middle, the two tanks settle it the next time they link, from the given
notes. The fish keeps its name, genes, badges, and a FROM line.

A SWAP (both give at once) can come after.

### Strato's decisions on the glass side (2026-10-20, from the mockup docs/media/give-sheet-1.8.png)

- GIVE sits on the fish card between RENAME and SELL (three buttons), on the
  small KEEPER card - ONLY while a door is open;
  unlinked, the card is RENAME | SELL as before.
- A first tap ARMS it: the button reads SURE? and the line TAP AGAIN TO GIVE
  (as SELL's "+N OK?" / TAP AGAIN TO SELL). The second tap asks the other tank.
- While it says ASKING (THE line: ASKING THE <BADGE> TANK) a tap on ASKING
  takes the fish back (until the other keeper accepts).
- The receiving tank: A GIFT FOR YOUR TANK, the fish's photo, NAME / FROM
  (badge shape + words) / FORM / STAGE, IT WOULD LIVE HERE FOR GOOD, ACCEPT /
  NO THANKS - over the live tank (like the Passport).

### As built: the engine (2026-10-20; common/link.c, link.h)

The engine half is built and proven in the sim; the tank half (the notes in
NVS, the card's GIVE, the receiver's ACCEPT / NO, the fish leaving and
arriving on the glass) is next. Two changes from the paragraph above:
GIVING is NOT saved - before the giver's GIVEN note the fish is simply home,
so a crash, a timeout or the keeper cancels the gift for free - and the GIVEN
note carries the whole passport, so the fish can come back from it.

| step | giver (G) | receiver (R) |
|---|---|---|
| offer | `link_give` -> GIVE (uid, nonce, passport), resent every 0.5 s | LEV_GIVE_ASK -> the keeper's ACCEPT / NO (60 s) |
| yes | | save ARRIVING (uid, nonce, passport), then `link_give_answer(yes)` -> TAKE, resent until answered |
| taken | LEV_GIVE_TAKEN -> save GIVEN (uid, nonce, passport), take the fish out, save, then `link_give_done` -> GIVEN, resent | |
| alive | | LEV_GIVE_ALIVE -> add the fish, note RECEIVED (uid, nonce), drop ARRIVING, save, then `link_give_alive` -> GIVEN_OK |
| closed | LEV_GIVE_CLOSED -> drop the GIVEN note | |

The other answers: NO (refused / full / no answer) -> LEV_GIVE_NO, the fish
stays. A TAKE the giver no longer has a gift for -> CANCEL -> R drops its
ARRIVING note (LEV_GIVE_CANCEL). A GIVEN that R holds nothing for (its note
lost) -> GIVEN_LOST -> LEV_GIVE_RESTORE: the fish lives at G again from its
note. After a boot the engine knows nothing: the tank answers for its notes
through `link_host_t.give_state`, and on LEV_LINKED hands back what it was
in the middle of (`link_give_resume_out` per GIVEN note, `_in` per ARRIVING
note). At boot a fish with a GIVEN note is removed (the note wins), and an
ARRIVING note whose fish is already alive becomes RECEIVED. Every confirming
call names the gift by uid AND nonce: an answer to a question that had closed
landed on a newer offer of the same fish (found by the soak below).

`--selftest-give` (in make check): two tanks, the hostile radio of
selftest-link, crashes that lose the work between an event and its call
(each split in two, as two saves would be), now and then the receiver's
ARRIVING notes lost with a crash. After every 10 ms step: never two living
copies; a fish alive nowhere has a GIVEN note. At the end every note settles
and every fish lives exactly once. Two planted bugs (GIVEN for a stale TAKE;
the fish out before its note) fail it at once. LINK_SOAK=2000: 56,733 gifts,
108 brought back by a GIVEN note, OK.

## The passport (the fish record)

One fixed, versioned binary record, ~100 bytes (fits one ESP-NOW frame):

- version, fish ID (32-bit, random, new), home tank ID
- name, colors (body, fin, accent), the genes (shape, fins, pattern +
  carried - from genetics-0.4)
- personality (bold, sociable, lazy, and their starting values), drift
- drives (hunger, energy, stress, curiosity, bored), trust
- stage, meals, meals from the keeper's hand, badges and seen badges
- parents as fish IDs (+ names, for a parent that lives in another tank)

`passport_write` / `passport_read` in `common/passport.c`. **Everything read
is checked and clamped**: names to the font's characters, numbers to their
ranges, genes to known values. A bad record is refused, never drawn. A
stranger's tank must not be able to crash yours.

The save gets one new tail: tank ID, every fish's ID and parent IDs, the
AWAY flag + host tank ID per fish, the given notes. It goes after the genes
tail (genetics takes 1688..1760), so **genetics should land on main first**;
this branch rebases onto it before its save tail is written. An old save gets
fresh IDs at its first 0.4 boot.

## The protocol

All frames: magic, protocol version, sender tank ID, sequence number.
Unicast between known tanks (hardware ack + our own resend), broadcast for
beacons. A tank with another protocol version shows as "needs an update" in
the NEARBY row and cannot link.

| frame | from -> to | when |
|---|---|---|
| BEACON | broadcast | 2/s in link mode, 1/s while linked: badge, free edges, room for visitors |
| ASK / JOIN | broadcast / unicast | holding an arrow / holding the matching one |
| LINKED / UNLINK | unicast | a door opens / closes (with the reason) |
| OFFER / ACCEPT / GO | home -> host -> home -> host | a fish crossing out |
| VISIT | host -> home | 1/s per visitor: its state; also the "I still have it" |
| LEASE | home -> host | 1/s per away fish, only while the host is heard |
| RETURN / HOME | host -> home -> host | a fish crossing back (or poofing on purpose) |
| RECALL | home -> host | home is going to sleep / closing |
| GIVE / TAKE / GIVEN | (phase 6) | |
| POS | crossing tank -> neighbor | (phase 6) 20/s while a fish straddles the seam |

## Code layout (BOARDS.md rules)

- `common/passport.c/.h` - the record, read / write / check.
- `common/link.c/.h` - the state machine: badges, edges, leases, crossing,
  return, poof. Pure C, no radio: it gets frames through `link_rx()` and sends
  through a port function. Driven from the tank's frame step.
- `common/tank.c` - doors in the side glass, visitor slots, the explore
  target, the home pull, the curiosity pulse; `advisor_core.c` - `wall clear`
  at a door.
- `common/render.c` - link mode (badge, arrows, NEARBY, DONE), doors, the
  poof; laid out in the PAGE safe box, edge arrows from TANK names.
- `firmware/main/link_port_espnow.c` - Wi-Fi up in station mode with no
  network, fixed channel, ESP-NOW, a receive queue to the tank task. The
  radio is ON only in link mode or while linked; off otherwise.
- `sim/` - two links: an in-process fake (two tanks in one program, with
  dropped / late frames and cut cables on a schedule) for the selftests, and
  UDP on localhost so two `fishsim` windows side by side on the Mac link for
  real (any mix of rectangle, bowl and watch).
- director: `link status`, `link close`, `link fake <pct loss>` for testing
  and b-roll.

### The tests

`--selftest-link`, in all three worlds, hours of simulated time with random
drops, delays and cuts:

- at every step, no fish ID is simulated by two tanks;
- after things settle, every fish ID lives in exactly one save;
- a fish whose host vanished is home within 10 s, with state no older than
  the last VISIT;
- passports round-trip byte for byte; garbage frames (fuzzed) never crash
  and are never drawn;
- a rectangle-to-bowl and a bowl-to-watch crossing land at the same depth
  fraction.

## Budget to hold

| | gate |
|---|---|
| internal RAM with the radio up, tank running | measured first; keep >= ~20 KB spare |
| decision time, linked, 2 visitors | <= 4 s on all three boards |
| fps | 25 ceiling unchanged; report it with the decision time |
| flash | ESP-NOW is small (Wi-Fi is in already) |
| battery | gauge mA linked vs not; link mode times out, a quiet link may too |
| `.bss` | compare against the last flashed build; visitor slots are 2 more fish_t in `tank_t` (static, internal) |

## Phases

0. **The radio spike (go / no-go, ~a day).** A director switch that starts
   Wi-Fi + ESP-NOW beside the running tank and sends beacons. On all three
   boards: internal RAM left, fps, decision time, gauge mA, signal strength
   at 1 cm / 30 cm / 2 m (sets the NEARBY threshold). If RAM does not fit:
   fewer Wi-Fi static buffers, the card cache to PSRAM, or BLE with the host
   in PSRAM - measured, not guessed.
1. **Identity + passport.** Fish IDs, tank ID, parents as IDs, the record,
   its checks, the save tail (after genetics lands). Selftests.
2. **The link core + the sim.** link.c, the fake, the UDP link, the
   selftest with cuts. Two sim windows linking.
3. **Link mode on the glass.** Badges, arrows, holds, NEARBY, DONE, closing,
   the DRIFTED notice and re-joining. Strato's finger on two real tanks.
4. **Doors and visits.** Crossing, visitor slots, the host's card, VISIT,
   leases, the poof, explore + the curiosity pulse + the home pull. fps and
   decision time with 2 visitors on all three boards.
5. **Cross-tank fry** (needs genetics).
6. **GIVE, the seam crossing, knock to link** - polish, in whatever order
   feels best by then.

## PINNED 2026-10-04 - where to pick up

Strato paused the link here to close out genetics first. State:

- Branch `link-0.4` (worktree `~/.cache/pocket-tank/wt-link`, off dev main
  29c6f49), not pushed. Commits: e6768a4 (design + radio spike) .. 5d1230a
  (phase 1). The worktree has an untracked symlink `sim/lvgl` (never
  commit it) and copied `model/out/*.bin`.
- Boards: all four (both 1.8s, the bowl, the watch) run the SPIKE build -
  0.3.1-era main + `link spike` on the director, radio off unless asked. They
  are normal tanks otherwise. Phase 1 is not on any board. Build dirs:
  fw-build-link (1.8), fw-build-175c-link, fw-build-206-link.
- Spike helpers: `tools/link_multi.py` (director lines to several boards at
  once, the link / decision / frame lines kept, a chime as the cue to move
  boards), `tools/link_phase_report.py` (decision, fps, heap, loss per step),
  `tools/link_distance_report.py` (signal per spot). Usage in each header.
- Pushed to the private dev repo as branch `link-0.4` (a backup; main and
  the public repo untouched).

IDs moved to genetics (2026-10-04, Strato: build them there first): the
genetics session took this branch's identity hunks AS-IS, same names, as
genetics-0.4 d326460 - and SAVED them at the layout below: tank uid @1760,
fish uid[6] @1764, parent_uid[6][2] @1788; save_t = 1840 (1836 of fields +
alignment; the next field still goes at 1836); fixture
sim/testdata/saves/2026-10-04-1840-ids.sav. Two changes there: tank_init
zeroes uid / uid_n, and a reset ("start over") gets a new tank ID
(progression_reset -> tank_ensure_ids). Its genes match the passport exactly
(GENE_* order SHAPE, FINS, PATTERN, BODY, ACCENT; 3/3/3/8/8; mutated 0..5;
5-bit from_*). The family page shows "?" for a parent in another tank until
phase 5 stores parent names.

RESUMED 2026-10-04 evening (Strato: "genetics is in a good place. Let's
continue here"): link-0.4 rebased onto main 26d0544 (genetics merged; its
identity code kept, this branch's copies dropped), the passport carries the
real genes (SHAPE_N / FINS_N / PATTERN_N / LOOK_N, static asserts on the
GENE_* order), check-all 13 x 3 worlds pass. Phase 2 done (below); next: phase 3.

Two timing rules found while planning phase 2 (they change "The poof"):
- **Restoring is per fish, on its own reports**: home restores a fish 10 s
  after the last VISIT about THAT fish - not after the host goes quiet. A lost
  GO would otherwise leave a fish frozen forever beside a host whose beacons
  keep arriving. Leases are per fish the same way (renewed only while that
  fish's VISITs arrive).
- **A rebooted home waits 10 s** before restoring the fish its save says are
  away: the host may hold them up to 8 s past the last lease.

## Phase 3: LINK lives in SETTINGS - waits for 0.3.2 (2026-10-04)

Strato: LINK goes on the settings page as a word ("LINK", maybe a small
connection icon - no tile, no new art), NOT the toolbox. The 0.3.2 patch
(a parallel session) is condensing the settings page, so the LINK row and
link mode's page wait until that lands on main ("let the 0.3.2 update cook
so we don't collide"). Until then: the parts with no page - the ESP-NOW port
(replacing the spike), the sim's UDP link, and director commands that drive
link mode, so the engine can run between two real boards with no UI.

## Phase 4 on the boards, round two (2026-10-05 afternoon, Strato in a meeting)

Accepted fish kept turning back on the boards while the simulator's crossed.
Three causes, each found from a board trace (link_dev now logs a crossing
twice a second and the state of a refused fish), each reproduced by a doors
selftest before the fix (old rule fails, fix passes):
1. A fish swims the way it FACES (a committed turn changes it): hovering at
   the door facing in, it swam in. Through a doorway the turn is committed
   at once.
2. Reflexes laid over the heading AFTER the doorway's steering (a hesitation's
   glance, the courtship, a finger, the greeting) pulled it back: the doorway
   now has the last word, re-asserted just before the side is decided.
3. The model wavered between the ask and the yes (goal changed -> door
   dropped -> explore chose the door again): the yes found the fish HEADING
   and was ignored; it hovered at the glass until the window closed (kelp,
   three times, x 380 of 410). The yes now counts if the fish still wants
   that door. (3c reproduces it: the old rule leaves it at x 380.)
After 1 and 2 (6a01f8e), fish sent to doors from the director crossed in
1-1.8 s (kelp, pip, halio). After 3 (ec2edf5): the natural run follows.
- A visitor wears its home tank's badge, small, above it (83075cc).
- Natural run before fix 3: 8 crossings started, 5 arrived, 3 came home
  through the door; refusals mostly FULL (reason 1), waited out 60 s.

## Phase 4 on the boards: fish through the doors (2026-10-05)

Built (2be0b2d, then 4e1d26e), flashed on the 1.8 and both watches, all three
linked by Strato from the glass:
- REAL CROSSINGS: the 1.8 took two visitors from the first watch on its own
  (Strato saw four fish on the 1.8); pip (the 1.8's) went through the left
  door into the first watch and came HOME THROUGH THE DOOR - a full round trip.
- The visitor cap at work: the 1.8 full, the watches' fish (ooqi, mira,
  halio) were refused at its doors - correct, but they retried every ~10 s:
  a refusal now carries its reason and a FULL tank is waited out 60 s.
- kelp was ACCEPTED and still turned back: hovering at the door facing in,
  it turned first and missed the 2 s crossing window. Now 6 s (LINK_CROSS_MS),
  a push of speed when accepted, the host holding the place 8 s.
- Found by 7c (a selftest written for the longer hold): a GO landing late
  with every VISIT lost started a lease nobody renewed while home brought the
  fish back at 10 s - TWO COPIES for ~4 s. A fish whose host never reported
  now waits LINK_RESTORE_NOVISIT_MS (16 s). Soaked 1500 runs, 79,200
  crossings, clean.
- DISCONNECT on the DRIFTED notice and in link mode (Strato).
- An open door outside link mode: a glow down the side glass + the badge.
- Open: one check-all run hung in the bowl's --selftest-tend (19 CPU min,
  a tight loop in game code, not symbolized under Rosetta); not reproduced
  in 5 runs alone or one full check-all. Suspect the shared
  /tmp/pocket-tank-selftest.sav; watch for it.

## Link mode on the glass (2026-10-05, built + flashed on the 1.8 and both watches)

- Settings: a LINK foot button (two-tanks glyph + "LINK") between UPDATES and
  CLOSE on the 1.8 and the bowl (x 188..280), centred above the foot on the
  watch (y 348). SET_TAP_LINK closes settings and opens link mode.
- Link mode (render_link_mode, from link_view()): over the live tank like a
  tool in hand - the chip (glyph + DONE) top left, our badge top centre, an
  arrow tab on each side glass (free: a gray chevron; ours asking: white,
  pulsing; one tank asking: its badge + a chevron in its color; two asking:
  both badges + "2"; open: the neighbor's badge + a double chevron). A tab
  fills from the bottom while held: 0.8 s asks, 1.2 s on an open door closes
  it. The hint (two plain lines) and NEARBY sit down by the floor (Strato:
  out of the fish's way; LINK_HINT_Y 284, checked on each glass).
- The DRIFTED notice (render_link_drift): four short lines + a bar that
  empties over the 30 s grace, over the tank whether the page is up or not.
- Firmware: link_dev owns the page (link_dev_page_open / _close / _touch /
  _draw); the touch port routes SET_TAP_LINK and gives link mode the glass
  (modal: no feeding, cards or light); main.c draws it after the tool chip.
  DONE with a door open: the link runs on quietly (1 beacon/s); with none:
  the radio goes down. Link mode closing itself after 2 idle minutes closes
  the page. Sleep / power-off: the page closes with the goodbye. Director:
  `link page` toggles it.
- Badges: circle coral, triangle green, square blue, diamond yellow - the
  shape is the identity.
- Checked: selftest-update taps LINK, holds an arrow (asks once at 0.8 s),
  holds an open door (nothing at 1 s, closes by 1.35 s), DONE, and the water
  (nothing) on all three worlds; check-all 14 x 3.

## Ghost fish on two real boards (2026-10-05)

Resumed after 0.3.2 (rebased onto main a47abf1; check-all 14 x 3 pass;
backup pushed). Boards: the second 1.8 (2 fish) and the
watch (4 fish), both flashed with link-0.4 0e7f43d
(Strato: no NVS backup - and note the downgrade trap, a task chip: 0.3.x
refuses the 1840 B save, so these tanks cannot go back to 0.3.x without
starting fresh until a loader reads a longer save by its prefix).

`tools/link_multi.py` with per-board steps, hands off:
- radio up: the 1.8 52 -> 37 KB internal free, the watch 45 -> 29.
- linking: the 1.8 held right; the watch's left arrow showed "a tank asks"
  within 0.1 s; the watch held left 2 s later -> LINKED on both within
  50 ms. Both had picked the circle; the higher tank ID re-picked
  (triangle) by itself.
- ghosts: each crossing ~50 ms; 15 s visits; home THROUGH THE DOOR with
  the meal eaten next door; two ghosts crossing opposite ways at once, fine.
- the radio: ~350 frames each way, 0 send failures, 0 stale, ring never full.
- the forgiven drift happened live (155 s: DRIFTED on both, steady again 3 s
  later, nothing else).
- frames + decisions (3-5 decisions a phase - rough):
  the 1.8 3.88 s / 25.2 fps radio off -> 4.03 s / 24.9 fps linked, idle;
  the watch 4.09 s / 20.2 fps -> 4.12 s / 19.0 fps. The 1.8's +150 ms
  contradicts phase 0 (no change) and needs a long controlled run; the 4 s
  budget is close either way. Levers if real: the radio's power save
  (ESP-NOW wake window), fewer beacons while linked and idle.

THE WATCH WENT DEAF (found the same day): after the ghost test the two
boards stopped hearing each other; a fresh boot of the watch heard the 1.8 at
once - the WATCH's radio had gone deaf and mute while its counters counted.
Cause: the sleep grace (a short PWR press, or anything that starts it) runs
LIGHT SLEEP in 1 s slices with the radio still up; a radio left on through
light sleep does not come back, and light sleep also drops the chip's USB
(the ports were renamed). FIX: `link_dev_sleep()` at the top of
enter_sleep_for and enter_poweroff - a goodbye (visitors poof home, ours
recalled, doors close) and the radio DOWN; the keeper links again after the
wake. Rule for phase 4: the link is never left on across a sleep.

The troubles, on the two boards (2026-10-05):
- sudden death (`link die` on the watch with a 1.8 ghost there): the ghost
  home from a bubble cloud 9 s later; the 1.8's door closed (lost) at 10 s.
- a long drift (staged 60 s on the watch, the ghost staying 120 s): DRIFTED on
  both at 1.0 / 1.4 s; at the 30 s grace the visitor headed home and swam back
  THROUGH THE DOOR with its meal; 20 s later both doors closed (drift).
- `link off` with a ghost out: FOUND - the radio went down 0.1 s after the
  RECALL and the RETURN was lost; the ghost stayed out with nothing ticking
  its restore. FIXED: the goodbye waits up to 0.4 s for the RETURNs, and the
  engine keeps ticking radio-less while a ghost is out. Re-tested: home from
  bubbles at once; with the host already dead, home 10 s after its last
  VISIT, radio off.
- arming with link mode timed out (2 idle minutes) did nothing silently:
  `link arm` now opens link mode again.

- the REAL sleep (Strato's short PWR press on the watch, a ghost out each
  way): in the same instant the watch's visitor poofed home, the 1.8's door
  closed (sleep) and the 1.8's ghost came home from bubbles. After his wake
  (a quick wake - resumed in place, no reboot: the case that broke the
  radio before) the watch's link was off and its radio released; `link on`
  + the arrows linked in 0.1 s, ghosts crossed both ways and swam home with
  their meals. The deaf radio is fixed.
- NEARBY spam: a circle out of link mode looked new on every beacon (one
  NEARBY a second) - fixed in the engine (30578ab), the selftest catches it;
  not flashed yet.

THE BUDGET, settled (20 min, alternating 5 min radio off / linked and idle,
29 decisions a block): the 1.8 3943 / 3944 ms off vs 3952 / 3948 ms linked
(+6 ms, 0.15%), 25.1 fps both; the watch 4114 / 4092 off vs 4099 / 4124
linked, ~20.5-20.9 fps both. The radio costs nothing measurable; the +150 ms
of the first run was a handful of decisions' noise.

THREE TANKS (2026-10-05, the 1.8 + watch A + a second watch B,
all flashed with the NEARBY fix):
- a row of three, every tank its own badge (square / triangle / circle), the
  middle tank with a door each side; ghosts crossed among all three and swam
  home through their doors with their meals; a ghost at a closed side was
  refused ("the right door is not open").
- two asking at once: with both watches asking, the 1.8's left arrow said
  "two tanks asking - wait" and nothing joined; when the first watch's ask
  ran out (5 s) the second was the only one left and joined - by the rule.
- the visitor cap: two ghosts from watch A filled the 1.8; watch B's ghost
  was refused ("the door is glass for now"); both visitors home on time.

Phase 3's engine + radio side is DONE on real boards. Left before the doors:
the LINK row and link mode's page on the settings page (after 0.3.2's
rework, now on main). Then phase 4: real fish through real doors.

## PAUSED 2026-10-04 night - until the 0.3.2 patch is done (Strato)

Where it stands: phases 0-2 done, the ESP-NOW port + ghost fish built (not
flashed). Nothing of this branch is on any board except the old spike build.
To resume, in order:
1. 0.3.2 merged to main -> rebase `link-0.4` onto main. Expect render.c /
   render.h settings changes (SET_ROW4_Y / SET_ROW5_Y, SET_NUM_* gone),
   tank_t gaining orient_lock / orient_inv / autofeed_off, two SAVE_ATs at
   1673 / 1674 (screen_turned's old padding - no clash with the 0.4 tails).
   Re-run check-all and the three firmware builds.
2. Ask Strato for two free boards; flash the 1.8 pair first; run the ghost
   test plan below. Read fps + decision time with the radio on.
3. The LINK row on the condensed settings page ("LINK" + maybe a small
   connection icon), then link mode's page (badge, arrows, NEARBY, DONE).

## The radio port + ghost fish (2026-10-04 evening; built, not flashed)

- `firmware/main/link_port_espnow.c`: the lean radio (`lp_radio_up` /
  `lp_radio_down`, shared with the spike), received frames into a 24-frame
  ring in PSRAM drained by the tank task (`link_port_drain` -> link_rx),
  frames older than 1 s dropped (LP_STALE_MS, the leases' tolerance), the
  sender's radio address learned per tank ID for unicast (8 kept; unknown =
  broadcast, the others ignore it).
- `firmware/main/link_dev.c`: the engine on the director, with GHOST FISH -
  two passports per tank that are not drawn and not in the tank, but cross,
  visit, get leased, poof and come home exactly as fish will, every step in
  the log. `link on|off`, `link arm left|right`, `link close ...`,
  `link ghost left|right`, `link stay <s>`, `link drift on|off|imu`,
  `link status` (also every 10 s). Drifting follows the IMU unless staged.
  The engine and the ghosts are in PSRAM; the radio's 16 KB of internal RAM
  is taken only between `link on` and `link off`.
- Internal RAM with the radio off: +0.5 KB DIRAM over main (the port's
  address table, the spike's counters, ESP-NOW's statics).
- Test plan on two boards (1.8 first): `link on` on both; `link arm right`
  on A, `link arm left` on B within 5 s -> LINKED on both; `link ghost
  right` on A -> crossed / visitor / swims home after 15 s; then the
  troubles by hand: unplug B with a ghost there (A: home from bubbles at
  ~10 s), `link drift on` on B for 5 s and 40 s, `link off` on A with a
  ghost out. Read fps and decision time with the radio on.

## Phase 2 status (2026-10-04 evening)

BUILT (not on any board; the device build compiles it, nothing calls it yet):
the protocol engine `common/link.c/.h` + `sim/selftest_link.c`.

- The engine knows no radio, no drawing and no fish_t: frames in through
  `link_rx`, out through the host's `send`, time from the caller, an event
  queue to the tank (LINKED, CROSS_OK, VISITOR_IN, VISITOR_POOF, HOMEWARD,
  FISH_BACK, FISH_RESTORE, DRIFT, ...), and calls from the tank (link_arm,
  link_close, link_moving, link_sleep, link_offer, link_cross,
  link_leaving). 7 KB a tank (PSRAM on the device). Every frame read is
  checked; a passport inside one goes through passport_read.
- Linking: an arrow held ARMS its edge for 5 s and the beacons say so; a
  JOIN goes to the one tank armed at the facing edge; two asking at once =
  nothing joins (LEV_ASKING reason 2). Badges: the lowest shape no nearby
  tank shows; a clash, the higher tank ID picks again. A one-sided link
  (a lost reply) heals by the JOIN repeating, or closes after 3 s
  (MISMATCH).
- Per-fish trips (a number per crossing) make every message about one
  crossing: stale and repeated frames do nothing.
- The rules found while building it (they refine the sections above):
  - restore is per fish, on its own VISITs (above, "RESUMED");
  - a GO is resent until the host answers VISIT or DROP - a host that never
    had the fish says DROP, and the fish is home in ~4.5 s, not 10;
  - home answers a VISIT it does not recognise with DROP (the host lets go:
    a backstop for a rebooted home);
  - the radio port must hand frames to the engine within ~1 s and DROP older
    ones: the leases tolerate up to 1.5 s of delay (soaked); at 3 s safety
    held but a fish waited 11.6 s to come home.
- `--selftest-link` (in `make check`, all three worlds, ~1 s):
  - 7 scripted stories on a clean radio: a visit and the swim home with the
    meals eaten next door (crossing in 90 ms); a 0.6 s nudge = nothing; 8 s
    of drifting = the notice on both tanks, then still again = doors open,
    the visitor never left; carried off for good = homeward at 31 s, the door
    closes 20 s later; the radio cut = poof next door at 5.8 s, bubbles home
    at 9.8 s; home sleeps = its fish home in 20 ms; two asking at once =
    nothing joins; every GO lost = home in 4.5 s.
  - 46 chaos runs (2-4 tanks; 25-35% of frames lost; one-way and two-way
    cuts; repeats; reordering; deaths and reboots; sleeps; drifts; doors
    closed; keepers joining at random): after EVERY 10 ms step no fish is
    simulated twice, none sits in limbo past 11.5 s, the tank's visitors
    and the engine's agree, meals never go backwards; at the end every fish
    is home.
  - 200k frames of noise and damaged real frames: no crash.
  - Soaked once with LINK_SOAK=3000: 250 simulated hours, 136,071
    crossings, no violation. Mutation-checked: a restore shorter than the
    lease, or a lease that never ends, fails within seconds.

## Phase 1 status (2026-10-04)

BUILT (not flashed): fish and tank IDs + the passport.

- `fish_t.uid`, `fish_t.parent_uid[2]`, `tank_t.uid`: random, never 0.
  `tank_entropy` is the source when the platform sets one (firmware:
  `esp_random`, the hardware RNG; the interactive sim: /dev/urandom), else a
  splitmix of the tank's seed, clock and a count that never draws from
  `t->rng` - so every seeded selftest draws what it always did. Made in
  `tank_make_fish`; `tank_add_fish` copies the parents' IDs; a sale moves the
  ID with the fish; `tank_ensure_ids` runs after a load and a fresh tank.
- NOT SAVED YET: a boot hands out fresh IDs. The save tail goes after
  genetics' genes (1688..1760) once genetics is on main.
- `common/passport.c/.h`: version 1 = 94 bytes, little-endian, append-only,
  CRC-16/CCITT in the last two bytes of `len`. Carries IDs, name, colors,
  the genes (classic 0s until genetics lands), drives, personality, drift,
  size, age (passed in: progression keeps it per slot, not in fish_t),
  meals, badges (never the retired bits). `passport_read` refuses a wrong
  magic / length / CRC, a zero ID or home, a stage that does not exist;
  clamps everything else (names to a-z 0-9 '?', an empty one to the
  preset's, a later roster's preset into ours, genes to known values or
  classic, size >= 0.2 so no fish is invisible, hand meals <= meals).
- `--selftest-passport` (in `make check`, all three worlds): IDs unique and
  nonzero, parents' IDs right, the RNG untouched, an ID moves with a sold
  fish; every fish round-trips byte for byte, and back onto a fish_t and out
  again; all 752 single-bit flips and all 94 short reads refused; a longer
  version-2 passport read by its prefix; hostile values clamped; 150k frames
  of noise (0 accepted) and 150k sealed noise (~120k accepted, every one in
  range, re-encoding stable, a fish_t made from it sane).
- Firmware: .bss +80 B (the IDs). The passport is not linked on the device
  until phase 2 calls it.

Found for phase 2: progression keeps per-fish state in FILE STATICS
(`s_age[]` and others in progression.c), one tank per process. The
in-process two-tank link test must either run progression for one tank only
or move that state into tank_t first. And a visitor's age cannot live in the
host's `s_age[slot]` (visitor slots are past N_FISH_MAX): the visitor keeps
its passport's age itself.

## Phase 0 results (2026-10-04, two 1.8 boards side by side on the desk)

`firmware/main/link_spike.c`, director `link spike on [hz] [bytes] [preset]` /
`rate <hz> [bytes]` / `off`. Wi-Fi in station mode with no network, ESP-NOW on
channel 1, broadcast beacons; the radio is off unless the command turns it on.

**The build's Wi-Fi settings do NOT fit beside the tank.** Preset 0 (what
update mode uses: 16 STATIC TX buffers + 6 static RX, ~1.6 KB of internal DMA
RAM each, AMPDU on, + the event loop): 53 KB free, the event loop took 4,
`esp_wifi_init` got 1 of its 6 RX buffers and failed with ESP_ERR_NO_MEM on
both boards (and a failed init leaked ~2 KB until reboot - fixed: the spike
now undoes what it did).

**The LEAN settings fit** (preset 1 - only esp_wifi_init's arguments, the
update path is untouched): TX buffers dynamic (from PSRAM), 2 static RX,
8 dynamic RX, no AMPDU / AMSDU, the fewest management buffers, no event loop
(ESP-NOW needs no events; the driver logs two harmless "failed to post WiFi
event" lines at start).

| | board 101 (4 fish) | board 2101 (2 fish) |
|---|---|---|
| internal RAM free, radio off | 53 KB (largest block 42) | 53 KB (42) |
| radio up (lean) | 37 KB (largest block 29), low-water 34 | 37 KB (29), low-water 33 |
| radio cost | 16 KB the first time, 15 after | same |
| after `off` | 52 KB (1 KB stays after the first start, once) | same |
| 5 on / off cycles | 15 KB out, 15 back each time, no creep | same |
| beacons 2/s x 32 B | 0-1 lost of 20 per 10 s | 0-1 lost of 20 |
| stream 20/s x 200 B | 0-3% lost, heap steady | 0-3% lost |
| signal, as they sat | -53 dBm average | -49 dBm |
| decision, radio never on | 4006 ms | 4038 ms |
| decision, 2/s | 3993 ms | 4047 ms |
| decision, 20/s stream | 4055 ms | 4075 ms |
| decision, after off | 4083 ms | 4041 ms |
| fps never on / 2/s / stream / after off | 22.5 / 22.5 / 21.7 / 21.7 | 25.0 / 24.9 / 25.0 / 25.1 |

Reading it: the radio's cost to frames and decisions is inside the run-to-run
noise (board 101 moves 4.0-4.08 s and 21.7-22.5 fps with the radio on, off,
or never started). Linking ESP-NOW into the image costs ~3.8 KB of internal
RAM even with the radio off (.bss +0.5 KB, .data +3.3 KB vs the 0.3.1 build) -
that is already inside the 53 KB above. 34 KB low-water with the radio up is
comfortably over the ~20 KB margin the design asked for.

**Verdict: GO for ESP-NOW on the 1.8.**

### Signal vs distance (2026-10-04, Strato with a tape measure)

Two 1.8s, 10 beacons/s of 32 B, 40 s at each spot, the windows while the
boards were being moved left out (3 windows = 300 frames per spot per board):

| spot | board 101 hears | board 2101 hears | frames lost |
|---|---|---|---|
| touching, edge to edge | -11 dBm avg (-24 .. -6) | -7 (-20 .. -6) | 0 of 600 |
| 4 in apart | -31 (-50 .. -23) | -26 (-50 .. -23) | 0 |
| 12 in | -36 (-58 .. -26) | -30 (-55 .. -22) | 0 |
| 39 in | -50 (-54 .. -44) | -46 (-51 .. -41) | 1 |
| 70 in (as far as the cables go) | -38 (-67 .. -32) | -35 (-59 .. -29) | 0 |

Reading it:
- **Touching is unmistakable - FOR THIS PAIR ONLY** (the bowl and the watch, below, say the opposite): ~20 dB louder than 4 in apart, on both
  boards. That is the one distance the link cares about.
- **Past a foot the signal says nothing about distance**: 70 in was LOUDER
  than 39 in (reflections, which way the boards face). An "arm's length"
  NEARBY threshold cannot be built on it.
- Range is no concern: nothing lost at 70 in.
- Single readings dip 20-30 dB for a moment even when nothing moves (4 in:
  min -50 with an average of -31): every decision uses an average over ~1 s,
  never one frame.

### The bowl and the watch: signal strength cannot see "touching" (same day)

The first pair (two 1.8s) suggested links only between touching tanks
(better than ~-20 dBm) and closing the door when they part. The bowl and the
watch, 10 beacons/s, 40 s a spot, 300 frames per spot per board:

| spot | bowl hears | watch hears |
|---|---|---|
| touching | -51 dBm (-60 .. -39) | -53 (-63 .. -43), 2% lost |
| 4 in apart | -34 (-47 .. -32) | -35 (-61 .. -29) |
| touching, the watch turned around | -33 (-35 .. -32) | -33 (-39 .. -31) |

Touching was the WEAKEST reading; 4 in apart was 17 dB louder. Where each
board's antenna sits, and which way it faces, decides the number - the cases
touching does not. The two 1.8s happened to line up antenna to antenna.
**Signal strength is not used for distance or touching anywhere in the
design.** It stays a diagnostic.

What that means for the design:
1. **NEARBY = every tank in link mode that is heard** (no threshold). Link
   mode is a deliberate act on both glasses.
2. **Which edges touch comes from the keeper**, as in Strato's idea: the
   holds on the two arrows. The pairing is the two holds that overlap in
   time (or fall within a few seconds of each other). If two tanks ask at
   once, the arrow shows both badges and nothing links until one asker lets
   go - no guessing.
3. The holds are the proof (Strato: no knock - the arrows are reliable).
4. **Moving is forgiven** (the DRIFTED notice under "Linking"): the door
   closes only on the keeper's close, a sleep, or the radio silent for 10 s.
5. The watch receives worse than it sends at middling signal (15-30% of a
   20/s stream lost around -62 to -70 dBm, ~0% around -48; the bowl hearing
   the watch at the same strength lost almost nothing). Everything that
   matters is acknowledged and resent; the 20/s position stream of the seam
   crossing must survive a 30% loss (it is drawing only).

### Memory on the bowl and the watch (lean radio, same day)

| | bowl | watch |
|---|---|---|
| internal RAM free, radio off | 43 KB | 45 KB |
| radio up | 27 KB (low-water 23, largest block 19) | 29 KB (low-water 26, largest block 20) |
| decision: off / 2 per s / 20 per s stream | 4.12 / 4.18 / 4.15 s | 4.08 / 4.23 / 4.16 s |
| frames lost, 20/s x 200 B | 6 of 2,366 | 345 of 2,367 (the signal, above) |

The radio costs the same 16 KB on every board, but the bowl and the watch
start ~10 KB lower than the 1.8, so the bowl's low-water mark is 23 KB -
right at the margin. Before visitors (two more fish_t in internal .bss) the
link must find internal RAM to give back. Decisions move 50-150 ms with the
radio on, on boards that were already at 4.1-4.2 s: inside the noise of 8-12
samples, but the number to watch in phase 4. The bowl logs "I2C transaction
failed" a few times a minute in the baseline too, before the radio - not the
link's; noted separately.

Still open in phase 0:
- battery draw with the radio on (on battery, gauge mA, linked vs not);

## Strato's answers (2026-10-04)

1. **Radio**: his question first - "when the board uses Wi-Fi it takes users
   out of the tank; are we sure Wi-Fi can be active running ESP-NOW with the
   tank up and running?" Not sure; that is phase 0. Update mode leaves the
   tank because a station + lwIP + TLS took ~73 KB of internal RAM and the
   running tank has ~54 KB. ESP-NOW needs the Wi-Fi driver but not lwIP,
   DHCP, TLS or a router, so it needs less - how much less is the unknown.
   Bluetooth LE has the same problem at about the same size, so it is no
   escape. The spike decides; radio choice follows it.
2. **Edges**: left and right, a row of tanks.
3. **Cross-tank fry**: a fry in each tank that has an open slot; cross-tank
   fry worth 0 sand dollars and not sellable (above).
4. **Order**: visits first, then cross-tank fry, then giving.

## Later: sounds for the link (Strato 2026-10-05)

"I'll probably want to add some sounds to some of these new things for an
extra layer of feedback." Candidates, each one engine event so a cue maps
1:1: LINKED (doors open), a fish through a door (out / in), the bubble-cloud
poof (VISITOR_POOF / FISH_RESTORE), DRIFT (the notice), STEADY (doors open
again - only fires while a door is linked, see a4 fix), UNLINKED (keeper /
drift), a spooked visitor's bolt. Levels go in assets/sounds/gains.csv like
the rest.

## Phase 5 grown up, and the visitor collection (2026-10-06)

- **Time together** (Strato: 30 min of visiting and no breeding - the fry
  only rode the host's own checklist). A visitor beside one of our grown
  fish (120 px) builds time with it, 0.3x anywhere in the same water, kept
  across visits while awake (tank_t.bond[6], not saved). At a random goal
  of 5-10 min the pair courts when the glass is clean, a nursery bed stands
  and either tank has room. The old 50/50 coin (join OUR staged fry) is only
  tossed when our fry IS staged - tossed at 60 s for every visitor it made a
  partner for nothing and blocked time together (the first board run).
  First natural cross-tank fry on the boards: "sol" on the circle watch,
  parents mira (host) + zola (from the 1.8), 38 min into the run, worth 10.
- **Passport buttons**: DEPORT (home, briskly, no fright; LINK_NO_DEPORTED:
  our door refuses it LINK_DEPORT_MS, its home keeps it off that door as
  long) | COLLECT (the COLLECTED stamp; the button dims).
- **The VISITOR COLLECTION** (common/collection.c): 12 entries, each the
  fish's look and IDs as last seen here plus its history (first met,
  visits, time in this water from its very first visit, fry it had here).
  Its own NVS blob ('visitors', the tank namespace: a reset erases it), a
  versioned record with its size, so later builds append. The milestones
  foot slides once one is collected (arrow by CLOSE: VISITORS); a grid,
  and a tap shows the fish's passport with its history.
- **PWR key**: the square watch powers off on USB on
  presses nobody makes - a periodic (~3.5 min) false LONG press from the
  PMIC, the key line never down. Short and long presses now need the key
  line seen down (2 / 8 frames); 4 ignored in the first 40 min.
- CI: a local sim/lvgl symlink committed in 7b5f2e2 broke every checks run
  until b02709b (stage named files, never a whole folder).


## HOME WATERS and the passport's v2 (2026-11-01, Strato)

The link's badge shapes (circle, triangle...) are who is next door NOW: a
handful of shapes, reused, so "the circle tank" can be many tanks over a
collection's life. Where a fish came from, for good, is now its home's
**HOME WATERS**: two words from the tank ID (`passport_home_waters`, 64 x 64
kid-safe words, 15 characters at most - AMBER COVE, HIDDEN SHALLOWS), the
same on every board, stored nowhere, never typed, so a fish collected before
this build shows its waters too. Strato swapped out the words that sound like
a gadget: CHANNEL, STREAM, CURRENT, BANK, DOCKS, POINT (-> SHALLOWS, GROVE,
SURF, TIDEPOOL, MOORINGS, COVES).

A tank its keeper RENAMED (MY TANKS) sends that name with its fish: passport
v2 appends `home_name` (16 bytes; a v1 reader takes the prefix, a v1 passport
reads as no name). A default name - MY TANK, TANK <n> - never travels
(`shelf_keeper_name`). The passports' HOME (FROM on a gift) reads the
keeper's name with its waters small in the label ("HOME - DREAMY COVE /
SAM'S REEF"), or the waters alone; the badge stays beside it while the fish
is here (visitor and gift cards), never on a collected one. The name rides
the visitor slot (`tank_t.vis_home_name`), the collection record (76 bytes;
a 64-byte record still loads, with no name) and the gift notes.

Same day, the visitor card: VISITING / HERE N MIN moved down clear of
STAGE's value, and CLOSE TO <fish> + five boxes became INTERESTED IN <fish>
+ five hearts (filled or hollow - the shape, never the color).

Seen on the glass: the square's TANK 2 (a default name) sent bolt to the 1.8
as DREAMY COVE alone; renamed SAM'S REEF, its sol came in as SAM'S REEF -
DREAMY COVE.
