# 0.4 — Genetics and heredity (framing, 2026-10-04)

Branch `genetics-0.4` (worktree `~/.cache/pocket-tank/wt-genetics`, off main
44e0249 = v0.3.1). Nothing is built yet; this is the frame to argue with.

Strato's brief: take cues from Spore. A new tank's fry customization goes
deeper than color: **body** (3 shapes, 1 exists), **pattern** (3, 1 exists),
**fins** (3 arrangements, 1 exists). Any of them passes down to fry, and a fry
has a **5% chance of a mutation** neither parent had.

Sketch: [media/genetics-sketch.png](media/genetics-sketch.png) (made by a
script from render.c's own local coordinates, so the C polygons will match).

## What exists today (the starting point)

| | where | today |
|---|---|---|
| body color | `fish_t.color`, `LOOK_BODY[8]` | the keeper picks at setup; a fry takes one parent's |
| fin color | `fish_t.fin` | derived from the body (`fin_for`), not a gene |
| accent color | `fish_t.accent`, `LOOK_ACCENT[8]` | a "?" at setup (a surprise at juvenile); a fry takes the other parent's |
| body shape | `draw_fish_core` 10-point polygon | one shape |
| fins | triangle tail + dorsal crest | crest grows in at ADULT, taller at ELDER, tail 1.22x at ELDER |
| pattern | three accent ellipses (vertical bars) | come in at JUVENILE, fade with the yaw turn |
| personality | `bold`, `sociable` | parents' average +-0.15 |

The model never sees a fish's looks (names carry no signal either), so **every
look gene is render-only and needs no retrain**: the decision budget and the
schema are untouched.

## The genes

Five look genes, one byte each:

| gene | values | 0 (= every fish today, every old save) |
|---|---|---|
| `shape` | CLASSIC, SLENDER, BULBOUS | CLASSIC |
| `fins` | CLASSIC, VEIL, SAIL | CLASSIC |
| `pattern` | BARS, MASK, EYE BAND, SOLID | BARS |
| `body` | LOOK_BODY index 0..7 | (today stored as RGB; kept) |
| `accent` | LOOK_ACCENT index 0..7 | (today stored as RGB; kept) |

The two color genes stay RGB in the save (they already are) so nothing about
0.3 fish changes; the new three are new.

### The new shapes (sketch row 1-3)

- **SLENDER**: long and low, pointed nose (danio / rasbora). Half-height 6.5
  vs 10, length 42 vs 34. Reads fast.
- **BULBOUS**: tall and round (discus / angelfish body). Half-height 15, short.
- **VEIL** fins: a long two-lobed trailing tail (betta-ish), a trailing anal
  fin, a low dorsal fold. Flows with a lag behind the tail phase.
- **SAIL** fins: a tall swept-back dorsal sail + a mirrored anal fin, a small
  lyre tail. The silhouette changes most when seen side-on vs turned.
- **MASK**: the accent covers the whole face, with a curved gill edge just
  behind the eye; the eye stays on top. It reads at tank size and stays clear
  of every top fin's root, so the fins keep the fin color - no fin rule.
- **SOLID** (Strato 10-04): no markings - a gene value like the others
  (inherited, carried, a mutation can land on it: "SOMETHING NEW: NO
  MARKINGS"). The editor keeps three tiles; tapping the picked one again
  deselects it to SOLID (the MARKINGS row then stands back), any tile picks
  a pattern again; ROLL can land on it.
- **EYE BAND**: a 5.5 px band leaning back through the eye (a bandit stripe).
  Neither face pattern fades with the turn: a face shows head-on.
  (Strato 10-04: SPOTS dropped after seeing it in the sim; the patterns are
  BARS, MASK, EYE BAND.)
  (Strato 10-04, round 4. Tried and dropped on the way: STRIPE (round 1);
  TWO-TONE BACK with a "the top fin wears the accent" rule (round 3) - an
  accent fin merged into the back, a darker accent
  ([media/genetics-fin-shade.png](media/genetics-fin-shade.png)) vanished on
  a dark accent against the black tank. Round 2's other candidates - two-tone
  half, eye band - are on [media/genetics-patterns-2.png](media/genetics-patterns-2.png).)

Fins and patterns anchor to the BODY (tail root x, half-height), so all 27
combinations work - the sketch's bottom row is three of them. The fill
routine is convex-only, so the veil and lyre tails are drawn as two quads.

### Stages (the reveal stays)

- **FRY**: shape shows from birth (it is the body), small tail only.
- **JUVENILE**: the pattern comes in (as the bars do today).
- **ADULT**: the fin arrangement grows in (as the dorsal crest does today).
- **ELDER**: the fins lengthen (the elder tail 1.22x / taller crest, per
  arrangement).

So a newborn's family page can say what it WILL become (dimmed icons for the
fins and pattern it carries) and the keeper watches it arrive.

## Decisions (Strato, 2026-10-04)

1. **Carried genes**, rolled for 0.3 fish at the upgrade.
2. **The keeper picks the accent** (and the pattern) at setup; the editor
   shows the grown-up preview. The surprise moves to inheritance and mutation.
3. **Looks + motion feel**: shape and fins tune the tail beat and turn rate
   (about +-10%); no effect on hunger, the economy or the model.
4. **NAME / FORM / COLORS**, three pages a fish (first run 7 -> 9 pages).

## Inheritance (carried genes)

Every fish has two copies of each of the five genes: the one it SHOWS and one
it CARRIES (hidden). Tunables, first guesses:

- A parent passes its shown copy 75%, its carried copy 25%
  (`GENE_PASS_CARRIED 0.25`).
- The fry holds one copy from each parent; a coin decides which it shows, the
  other it carries. So a fry shows something its parents only carry about 1
  time in 4 per gene - family resemblance holds, and traits skip generations.
- **Founders** (first run): shown = the keeper's picks; carried = rolled at
  random. The keeper designs what they see, the tank keeps a secret.
- **0.3 fish at the upgrade**: shown = what they are (CLASSIC / CLASSIC /
  BARS, their colors); carried = rolled once at the first 0.4 boot. They look
  exactly the same; their fry can surprise.
- Personality stays the parents' average + noise.

**Mutation**: 5% of fry. One gene, picked at random among the five, shows a
value that **neither parent shows** - preferring one neither carries, when one
is left (with 3-value genes there is always at least one value no parent
shows). The fry keeps a flag for which gene mutated, so the birth flow and the
card can say so. A selftest pins it (10k simulated fry: 5% +- 0.5, never a
value a parent shows) and the carried-copy rate.

At 5%, a keeper sees a mutation about every 20 births: rare enough to be an
event.

## Motion feel (decision 3) - BUILT 10-04 (tail beat in render.c FORMS, turn rate in tank.c tank_form_turn)

Render and steering only; the model's inputs and every economy rate stay as
they are. First guesses, to tune on the glass:

| | tail beat | turn rate |
|---|---|---|
| SLENDER | 1.15x | 1.10x |
| BULBOUS | 0.90x | 1.05x (a short body pivots) |
| VEIL | 0.92x, the fins trail with a lag | 0.95x |
| SAIL | - | - (the sail leans into the turn) |

## The setup editor (first run) - BUILT 10-04

First run (reordered 10-04 on the glass, Strato: design the fish, then name
it once its chosen form is in view, both fish, then the tank): WELCOME ->
COLORS -> FORM -> NAME (first fish) -> COLORS -> FORM -> NAME (second) ->
BUBBLES -> CARE, 9 pages. The editor pages say "... FOR THE FIRST FISH"
(not named yet). A name page for a fish not yet grown stages it at NOW and
shows its GROWN UP form beside it; the letter wheel moved down 20 px
(SETUP_SLOT_Y 170) to make the room. Both editor pages sit over the live
tank with BACK / ROLL / NEXT on top:

- A band under the buttons: the fry ringed on stage at the left (**NOW** -
  the live fish, wearing its body and shape) and the grown fish on a dark
  plate at the right (**GROWN UP** - fins come in at adult, markings at
  juvenile, so the keeper sees what they are choosing).
- **FORM**: SHAPE and FINS, three named tiles each, every tile a small grown
  fish of that option in the fish's own colors.
- **COLORS**: BODY (8 swatches), PATTERN (3 tiles, the current pattern's name
  in the label), MARKINGS (8 swatches; the one matching the body is struck
  out and takes no tap - markings that color would vanish).
- **ROLL** picks the page's genes at random (never the carried ones).
- A row's band runs from its label to halfway into the gap under it; the
  nearest choice along x. Fits the 1.8, the bowl's circle and the watch's
  narrow page (checked from the sim's snapshots of all three).
- The keeper picks what the fish SHOWS; the copies it carries stay the
  tank's secret (rolled at tank_new_population).

## One palette, nine colors, SWAP - BUILT 10-04 (Strato on the glass)

Black fish wanted, a SWAP (body <-> markings) button wanted. SWAP is only
exact if both rows share one list, so they do: LOOK_COLORS[9] = the six
roster bodies, blue, silver, charcoal (LOOK_BODY / LOOK_ACCENT name it).
The markings' own shades went (orange, cream, white, red, dark ink - silver
and charcoal stand in for white and ink); the roster's orange / cream
markings became gold / silver (mira, bolt, sol; the hero shot moved ~200
px). Nine swatches at 44 px apart, 40 wide (the bowl 40 / 36: its circle
is narrower at the markings row). A dark body's fins go LIGHTER (18% toward
white), or they vanish in the water. SWAP sits beside ROLL on the COLORS
page - since 10-04 eve a tall arrow key (up at the body row, down at the markings row) at the pattern row's right end, the pattern tiles narrowed to 100 px for it; the struck-out X is light on a dark swatch.

## The birth flow - BUILT 10-04

Four pages now (was three): A NEW FRY! -> NAME -> FAMILY -> NATURE (DONE).

- **A NEW FRY!**: a mutation adds a gold line ("SOMETHING NEW: VEIL FINS",
  "... A SLENDER BODY", "... AN EYE BAND", "... ITS BODY COLOR") and the
  outer ring turns gold.
- **FAMILY**: the fry NOW (ringed) > the fish it becomes; then SHAPE, FINS,
  PATTERN (names), BODY, MARKINGS (swatches), each with FROM: the parent's
  name in its body color, `*` when it was that parent's hidden copy (a
  footnote says so - the font gained a `*`), or NEW! in gold.
- **NATURE**: BOLD / SOCIAL bars with the parents' ticks, PARENTS legend
  (moved from the old family page).

**Fish card** (BUILT 10-04): the milestones card shows the fish's form
under its stage - "SLENDER  SAIL  BARS" - for every stage (a fry's is what
it will grow into). The card is 20 px taller and always keeps the line's
room, so RENAME / SELL never move; it fits the 1.8, the bowl and the watch.

**Badge** (BUILT 10-04): TANK milestone "first mutation" (TMS_FIRST_MUTATION,
bit 13), Strato's helix (working-assets/dna-mutation-badge-v1 ->
assets/icons/ms_first_mutation.png). Shown in the tank row once the tank has
bred (a trio), earned the first time a fry is born with a mutation; its
notice waits for the birth flow like every notice.

Still to do: the director / b-roll scenes; the 1.8 glass (fps + decision
time).

## Fish and tank IDs - BUILT 10-04 (for the tank link)

Lifted from the tank link's branch (link-0.4, 5d1230a "phase 1", the
identity hunks only - not the passport) with its names, so that branch
rebases clean: `fish_t.uid`, `parent_uid[2]`, `tank_t.uid` / `uid_n`,
`tank_entropy` (esp_random on the device, /dev/urandom in the interactive
sim; the selftests keep a seeded mix that never touches `t->rng`),
`tank_new_uid` (never 0, never an ID already in the tank),
`tank_ensure_ids`. A fish keeps its ID for life - a sale, a rename, a
move to another tank; internal only, never on a card. Saved at the link's
offsets: tank uid @1760, fish uid[6] @1764, parent_uid[6][2] @1788 - save
1840 B (1836 of fields, padded to 8). A pre-0.4 save's fish get fresh IDs,
their parents' from the slots. Two changes from the link's 5d1230a:
`tank_init` clears the tank ID and `progression_reset` hands out a new one
(a reset is a new tank); fixture `2026-10-04-1840-ids.sav`.

## Engineering notes

- **Save** (BUILT 10-04): `fish_save_t` is frozen (68 B); the genes are a
  new tail, `genes[N_FISH_MAX][12]` at 1688 (save 1760 B, 2240 B under the
  NVS budget): a has-genes flag, shown shape / fins / pattern, the five
  carried copies, mutated gene + 1, from_b, from_carried. The shown colors
  stay in body / accent (RGB, as before). A save without the tail (every
  pre-0.4 tank) loads its fish CLASSIC and rolls their carried copies once;
  the next save keeps them. Fixture `sim/testdata/saves/2026-10-04-1760-genes.sav`.
- **Heredity** (BUILT 10-04, tank.c `tank_add_fish`): per gene, a copy from
  each parent (shown, or carried at GENE_PASS_CARRIED 0.25), a coin for which
  shows; GENE_MUTATION 0.05 of fry show one gene's value no parent shows (one
  none carries either, when left; never a markings color that matches the
  body). Genes draw on their own stream (`tank_t.gene_rng`), so the tank's rng
  and every seeded run are unchanged. `--selftest-genes` (20k fry): 5.05%
  mutations evenly over the five genes, a secret shown in 24.8% of genes.
  The family page already says whose copy each color is, or NEW.
- **RAM**: 11 B a fish in `fish_t`; nothing near the 23 KB internal margin.
- **Frames**: the face patterns are one clipped polygon each, the new fins
  2-4 polygons; budget
  check on the 1.8 after (report fps AND decision time, the 25 fps ceiling).
- **Three boards**: common/ only; the editor laid out in the PAGE safe box;
  `make check-all`.
- **Tools**: director `gene <slot> shape|fins|pattern <v>` and `mutate`, sim
  keys, b-roll scenes; selftest for inheritance and the mutation rate.
- **Preset**: a fry still draws a random free roster preset (its default name,
  size, curiosity, lazy, turn rate). Not a gene now; could be later.

## Proposed order

1. Render the three shapes, fins and patterns (sim, then the 1.8), sign off
   the silhouettes on the glass.
2. Genes in `fish_t` + save tail + inheritance + mutation + selftest.
3. Setup editor (FORM / COLORS pages).
4. Birth flow + family page + card + milestones.
5. Director / b-roll, three-board pass, changelog.
