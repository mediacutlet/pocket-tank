# Species (2026-10-05)

Ten new creatures join the classic fish: **seahorse, octopus, pufferfish,
anglerfish, electric eel, hammerhead shark, squid, crab, lobster**, and since
2026-10-09 the **jellyfish**. Each one breeds, comes
in several designs, rolls its own personality inside its species' range, and
moves the way the real animal does. The distilled model still chooses every
creature's goal; the species decides how that goal is carried out.

## Where they come from

- **The shop:** each species is a sand-dollar item that brings **one
  juvenile** (`SD_ITEM_SP_*`, ten items, `progression_buy` →
  `tank_add_species_n(.., 1)`; one a purchase since 2026-10-07, a pair before).
  It needs one free place in the tank (`progression_has_room_one`) and can be
  bought as often as there is room: two of a kind are what breed, so a keeper
  buys a second. A newcomer's boldness contrasts with the last of its kind
  already here, the way the founding pair's does. (The director's `spawn`
  still stages a free pair, `progression_spawn_pair`.)
- **A birth:** an arrival is the species of its parents. With a small chance
  (`SP_MUTATE_P`, 4 %) a fry of the classic fish hatches as a random new
  species - the rare surprise.
- **Breeding is within a species.** `pick_parents` picks a courting pair of
  the same species, so a tank of two species has two families.

### The keeper's side (the shop, the birth flow, the pages)

- **Prices** (`SD_PRICE_SP_*`): every creature is 10 sand dollars (since
  2026-10-07; before that a pair, on an exotic ladder from crabs 140 to
  hammerheads 400), and sells back from its card for 10 at most
  (`progression_fish_value`). The items are 7..16 in species
  order (`SD_ITEM_SP_FIRST` + species - 1), on the shop's pages 2..5 (the
  jellyfish, item 16 / bit 16, is the fifth page's one row).
- **The bit** of a species' item means "some of them are in the tank"
  (`progression_species_sync`, run on load, after a sale, a birth and a
  buy): it clears when the last one is sold and a surprise hatched in the
  tank sets it. Since 2026-10-07 it no longer locks the row: a species' row
  always shows its price and UNLOCK, and its modal says how many of its kind
  are here. No room: the row's button and the modal say NO ROOM / THE TANK
  IS FULL. Bought juveniles start at STAGE_JUV_AGE and are not paid the
  juvenile stage's dollars.
- **Which pair courts:** each species' two best (grown first, then trust,
  as before), and of those pairs the one whose weaker parent ranks highest
  (the classic fish on a tie). A tank without two of one kind lists no
  NEW FRY row, never courts and has no arrival until a second of one kind is bought.
- **The gates past five** (`care_gates`): the five's three (the youngest
  grown to adult, meals, trust) with the meals at 200 / 270 / 350 / 440 for
  6 / 7 / 8 / 9 creatures and the trust bar at 9 from seven on.
- **The birth flow:** "A NEW SEAHORSE!" (the species' name; "A NEW FRY!" for
  the classic fish), "NAME THE NEW SEAHORSE"; a surprise (the fry's species
  is not its parents') reads "A SURPRISE!" and "AS AN OCTOPUS!".
- **The colour page** belongs to the first run's founding pair, always
  classic fish. Should a species' creature ever be on it (a director's
  tank), it offers that species' four designs (the body swatches are the
  designs' bodies; a design brings its own markings) instead of LOOK_BODY.
- **The milestones page** holds six rows a page (`MSP_FISH_ROWS`); with more
  (up to 25 creatures and the NEW FRY row) a chevron beside the rows - the
  TANK row's arrow, a pip per page - or a sideways swipe across them turns
  the page, and a card opened or stepped to brings its row's page. The TANK
  row's population strip is three rows of nine dots; its tally modal shows
  the school in two staggered rows. A big creature's row portrait is held
  to `MSP_ROW_SP_MAX`.
- **The card** names a creature under its name in the 8 px font: "SEAHORSE -
  GOLDEN", or the design first, or the token ("EEL - SPOTTED") when the
  card is too narrow; the milestones card says "ADULT LOBSTER".
- **A tap** reaches a creature by `tank_fish_hit_r`: its species' `hit_r` at
  its size, never under `TANK_HIT_MIN_R` (32 px, a fingertip); the nearest
  by its own reach wins (the sim and the touch port alike).
- **The director:** `buy <species>` (token, name or the shop's plural),
  `spawn <species>` (a staged pair, free), and `state` prints each
  creature's species / design and a species count.

## The cap

`N_FISH_MAX` is 25 since 2026-10-07 (6 before the species, 10 on
2026-10-05), on every board (`POP_CAP` = `N_FISH_MAX`; the device shipped 5
"until advisor latency is measured"). Every creature adds a turn to the
advisor's queue: at ~3.7 s a decision on the device, ten creatures get a
fresh decision every ~37 s and a full tank of 25 every ~90 s, against ~22 s
at six. The reflex layer keeps everyone moving in between; the decisions
are just older. The save's `fish_ext` tail holds fish 7..25 (19 x 100 B,
3,592 B in all, inside the 4,000 B NVS budget); a 10-place build reading a
save with more than ten falls back to the core's six, as an older build
always has. The gates past ten keep the ladder going (~+70 meals a place,
trust 9); the TANK row's tally is three rows of nine dots and the tally
modal's school two rows.

## Species table (`SPECIES[]`, tank.c)

| species | token | size | bold | social | curiosity | lazy | turn | moves as |
|---|---|---|---|---|---|---|---|---|
| fish | `fish` | roster | 0.10-0.90 | 0.10-0.90 | roster | roster | roster | `LOCO_FIN` |
| seahorse | `seahorse` | 0.80-1.00 | 0.08-0.35 | 0.50-0.85 | 4.0 | 0.70 | 1.6 | `LOCO_UPRIGHT` |
| octopus | `octopus` | 1.00-1.25 | 0.40-0.80 | 0.05-0.25 | 8.5 | 0.40 | 3.0 | `LOCO_JET` (crawls the floor) |
| pufferfish | `puffer` | 0.85-1.05 | 0.25-0.60 | 0.20-0.50 | 7.0 | 0.50 | 4.6 | `LOCO_HOVER` |
| anglerfish | `angler` | 0.95-1.20 | 0.50-0.80 | 0.05-0.20 | 2.5 | 0.90 | 1.4 | `LOCO_AMBUSH` |
| electric eel | `eel` | 0.98-1.23 | 0.60-0.90 | 0.10-0.30 | 4.5 | 0.60 | 1.8 | `LOCO_UNDULATE` |
| hammerhead | `shark` | 1.12-1.37 | 0.80-0.95 | 0.50-0.80 | 5.0 | 0.10 | 1.5 | `LOCO_CRUISE` |
| squid | `squid` | 0.90-1.10 | 0.30-0.60 | 0.70-0.95 | 6.0 | 0.30 | 3.5 | `LOCO_JET` (hovers in open water) |
| crab | `crab` | 0.80-1.00 | 0.40-0.80 | 0.20-0.50 | 6.5 | 0.50 | 3.5 | `LOCO_SIDEWALK` |
| lobster | `lobster` | 1.10-1.40 | 0.50-0.85 | 0.05-0.25 | 5.5 | 0.60 | 2.0 | `LOCO_WALK` |
| jellyfish | `jellyfish` | 0.95-1.15 | 0.10-0.40 | 0.40-0.75 | 5.0 | 0.70 | 1.6 | `LOCO_HOVER` (pulses, drifts) |
| swordfish | `swordfish` | 1.10-1.35 | 0.70-0.95 | 0.30-0.60 | 5.5 | 0.10 | 1.3 | `LOCO_CRUISE` (the hammerhead's way, faster; 2026-10-10 night) |

Sizes are tank-scaled (a "pup" hammerhead, a dwarf seahorse): 1.0 is the
classic fish's ~42 px. A newborn's size, bold and social are rolled inside
its species' range (inherited: the parents' mean ± 0.15, then clamped to
the range widened by 0.1).

## Designs (`SPECIES[].var[SP_VARIANTS]`)

Four designs a species, each a body / fin / accent and a pattern the
renderer draws for it (`fish_t.variant` 0..3). An arrival takes one
parent's design (80 %) or a random one (20 %), and its colours come from
the parents as the classic fish's do (body from one, markings from the
other).

- **Seahorse:** 0 golden (bands), 1 crimson (speckles), 2 black (white spots), 3 lavender (spines)
- **Octopus:** 0 common red-brown (mottled), 1 blue-ringed (rings), 2 mimic (stripes), 3 violet (plain, spots)
- **Pufferfish:** 0 spotted, 1 dogface grey (dark mask), 2 saddled white (black saddles), 3 golden (plain)
- **Anglerfish:** 0 abyss black (cyan lure), 1 frogfish orange (warts), 2 mottled brown (blotches), 3 pink warty (yellow lure)
- **Electric eel:** 0 olive (orange belly), 1 charcoal (yellow belly), 2 bronze, 3 spotted green
- **Hammerhead:** 0 grey, 1 bronze, 2 slate blue, 3 pale scalloped
- **Squid:** 0 pink (chromatophore dots), 1 firefly blue (glowing dots), 2 bigfin white, 3 reef amber
- **Crab:** 0 red rock, 1 blue (orange-tipped claws), 2 Sally Lightfoot (orange, blue flecks), 3 green shore crab
- **Lobster:** 0 common (dark olive, orange antennae), 1 rare blue, 2 spiny (teal, gold spots), 3 calico (red, cream patches)
- **Jellyfish:** 0 peach, 1 pearl, 2 rose, 3 blue - a bell and arms in the
  body / fin colours; the theme decides the look (docs/THEMES.md "Jellyfish")
- **Swordfish (2026-10-10 night, 10 sand dollars):** 0 steel, 1 cobalt (bars),
  2 sunset (bars), 3 ghost - a long slim body, the bill out front, a sickle
  dorsal, a lunate tail on a keel, a silver belly; drawn four ways (Original
  flat, Quiet Lagoon shaded, Tidepool Club round and bold, Blackwater real).
  Its word is not in any shipped vocabulary, so every model hears it as
  `species fish` (its traits still make it a cruiser).

## How they move (the reflex layer, tank.c)

The model picks one of the 8 goals; `target_for_goal` and the locomotion
step turn it into the animal's own motion.

- **Seahorse (`LOCO_UPRIGHT`)** - swims upright, slowly (dorsal fin flutter,
  the slowest swimmer: speed ×0.35), climbs and sinks more than it travels.
  REST: wraps its tail around the nearest grass frond and sways with it.
  Feeding: drifts close, then a quick head snick. Startle: clings and
  freezes instead of bolting.
- **Octopus (`LOCO_JET`, crawler)** - EXPLORE / REST / INSPECT on the floor
  and the reef: crawls on its arms. DART_PLAY and a startle: jets
  mantle-first, arms trailing, in pulses, and a startle leaves an ink cloud.
  Left to itself by day it squirts a black cloud of its own every 50-120 s
  (two squirts in three; the third idle turn is a trip to the surface), and
  two quick taps on it squirt one on demand.
  REST: a den at the reef cluster, castle or grass foot, and it **takes on
  the colour of what it sits on** (`fish_t.camo`).
- **Pufferfish (`LOCO_HOVER`)** - slow, boxy, fins sculling: it can stop,
  turn on the spot and back up (no committed U-turn needed). Startle:
  **inflates** into a spiny ball for a few seconds, then deflates. Two quick
  taps on it (2026-10-10, `tank_poke`) puff it up the same way, without a
  startle's stress or lost trust.
- **Anglerfish (`LOCO_AMBUSH`)** - an ambush hunter: it barely moves, stays
  low, waits with its lure bobbing, and lunges short and fast at food that
  comes near. The lure **glows**, brightest at night. Frogfish-style it can
  "walk" the floor on its pectoral fins when it explores.
- **Electric eel (`LOCO_UNDULATE`)** - a long body that ripples from head to
  tail, swims backward as easily as forward, and lies along the floor to
  rest. It **breathes air**: every 1-2 minutes it rises to the surface for a
  gulp. Startle: a harmless spark.
- **Hammerhead (`LOCO_CRUISE`)** - never stops (it breathes by swimming): a
  floor of speed, wide smooth turns, the head sweeping side to side. REST
  is a slow patrol lap, not a stop.
- **Squid (`LOCO_JET`, hoverer)** - hovers in open water with its fins
  rippling, moves forward or backward, and jets in pulses to dart or flee
  (ink on a startle, and a black cloud of its own every 50-120 idle seconds
  by day; two quick taps on it squirt one too). Social: squid hold station
  near each other.

- **Crab (`LOCO_SIDEWALK`)** - a floor walker that moves **sideways**, legs
  stepping in a ripple; it climbs the reef cluster, castle and rocks, and
  picks at food on the floor with its claws (two-handed). It never swims up:
  a goal in open water becomes the nearest point on the floor or a rock below
  it. REST: tucked under the reef / castle edge. Startle: claws up, then a
  fast sideways scuttle away.
- **Lobster (`LOCO_WALK`)** - walks the floor head first on its legs, long
  antennae sweeping; REST in a den under rock. Startle: the **tail-flip** -
  a few fast backward strokes of its tail that shoot it backward (burst ×2.2),
  then it walks again. Like the crab it stays on the floor and rocks.
- **Jellyfish (`LOCO_HOVER`, pulsing)** - an upright bell that never turns
  on its side: `fish_t.jet` is its pulse phase, and one phase drives both
  the thrust (the speed swells and fades with each contraction, ~0.6 Hz
  exploring) and the bell the renderer draws, so motion and picture agree.
  It drifts slowly in the open water (speed ×0.40, a gentle bob, climbs as
  readily as it travels) and keeps out of the floor band and the surface.
  REST: it hangs still in mid-water near the middle, the pulse slowed to
  ~0.2 Hz. A startle quickens the pulse (~1.3 Hz) and nothing else: no ink,
  no spark, no puff, no idle flourish (it never visits the surface).

## The gentle tank

No creature harms another. The small ones (fish, seahorse, pufferfish,
squid, crab, lobster) keep their distance from the big ones (hammerhead, eel): a stronger
separation push inside `SP_AVOID_R`. Every creature eats pellets.

## Persistence

- `fish_save_t.pad` (2 bytes a fish, written 0 by every older build) holds
  **species** and **variant**: an older save reads every creature as the
  classic fish, design 0.
- Fish 7..10 live in a new save tail (`fish_ext`, from offset 1688). The
  core count `n_fish` stays ≤ 6 and the tail holds the true count, so an
  older build (an OTA rollback) still loads the tank: its first six, as fish.
- The species' live state (puff, ink, camo, the eel's air, the seahorse's
  frond) is not saved.

## The model (schema 5)

The v4 model has no word for a species: an unknown word becomes `<unk>`,
which it never trained on. So the advisor sends `species <token>` only to a
**schema-5** model (detected by ` species` in its tokenizer), and the v4
model keeps deciding as before, from each creature's species-shaped traits.
The retrain runbook is `docs/retrain-v5.md`; the schema-5 model **v5m** ships
since 2026-10-07 (its numbers: docs/stats.md, "Schema v5 data cycle").

**A species added after a model** (the jellyfish, 2026-10-09): its word is
appended to the tokenizer (`jellyfish`, id 65; vocab 66) and the advisor
sends it only when the loaded vocabulary has it (`advisor_core_species_word`,
checked at init); a model whose vocabulary stops at 65 words hears the
jellyfish as `species fish` - its traits and the reflex layer still make it
a jellyfish. The jellyfish's own labels (a `--focus jellyfish` run of
`gen_traces.py`) and the retrained model are **v5j** (docs/stats.md,
"The jellyfish, v5j").
