# Swim depth: the animals choose front, behind or through (EXPLORATION, 2026-10-31)

Strato on Discord (mysiak's thread "Fish not swimming through leaves", 10-31):
"I'm trying to reintroduce fish swimming through the grass, but its a
decision the fish makes. To swim in front, behind, or through. And matters
less *where* you put it in the tank. The decoration depth impacts the visual
layering and stacking order of everything, but fish and critters should have
freedom to explore that '3d space' as normal animals would." And today:
"the layering then moves to only influence the stack order of decorations.
fish and critters would have their choice of where and how to swim not
globally defined by the grass". mysiak: "Maybe they could hide at least while
resting"; "Individual critters' decisions for where to swim would be awesome".

Nothing here is built for the boards. The sim proves the look
(`fishsim --depth-shot <prefix>`, branch swim-depth).

## Where it stands (docs/render-stack.md)

- The seagrass is the scene's bottom layer (rank 0), baked by THE GRASS LOOP:
  every animal is in front of every frond.
- The fish (rank 6) and the shrimp (4) sit between the pieces BEHIND (in the
  scene) and the pieces IN FRONT (restored over them at rank 8, `pc_restore`):
  a piece's depth decides for every fish at once.
- The floor critters already choose: a LANE per move, 60 in front / 40
  behind, changed only in the clear of every piece IN FRONT (tank.h THE
  LANES). That is this idea, for three animals and the pieces only.
- The old weave (2026-08-30 .. 10-30) split every bed into even and odd
  fronds and drew the odd ones after the fish - for everyone, every frame,
  live. It kept the grass from being baked: the biggest frame cost on the
  boards, and it is why it went.

## The idea

**Each animal carries a depth for what it crosses: FRONT, BEHIND or
THROUGH.** A piece's IN FRONT / BEHIND only orders the pieces (and the beds)
among themselves. THROUGH is for the grass (every other frond over the
animal); a piece is FRONT or BEHIND (the castle's arch stays a way through).

**How the board draws it - per animal, the size of the animal.** The frame
is primed with the scene and the grass as now. Before an animal is drawn the
few pixels under its box are kept (a fish ~2,000 px, 4 KB); after it is drawn,
the occluders it is BEHIND get their own pixels back inside its box:

- a piece: from the scene, where its recorded mask covers (`pc_restore`
  already does this - today for every fish at once, at rank 8);
- the grass: from the kept pixels, where they differ from the scene (they
  were grass);
- THROUGH: the same, only where an ODD frond stands: a 1-bit mask baked with
  each grass loop band (~10 KB a band on the 1.8; the fine moments use their
  band's mask - off by a pixel at a frond's edge at most; exact masks for
  every fine moment would cost ~1 MB). `--selftest-grassloop` compares the
  grass with every fish IN FRONT (render_depth_off); `--selftest-depth`
  holds the depths.

Cost: a few thousand pixel tests per animal, well under a millisecond a frame
for five fish and the shrimp - against the weave's whole grass, every frame.
Nothing about the grass loop's bake changes but the odd mask beside it.

**The proof (sim, the hero tank, these exact compositions):**
`--depth-shot` writes mira in the left bed IN FRONT / BEHIND / THROUGH, and
bolt behind the castle (as today: it stands IN FRONT) and in front of it
with the castle still IN FRONT.

## Who decides

The goal stays the model's (LLM owns decisions). The depth is how the fish
carries the goal out, chosen when a pass begins and changed only in the clear
of every occluder (the lanes' rule: nothing pops through a frond or a piece):

| goal | depth (a roll, weighted by temperament: a shy fish hides more) |
|---|---|
| REST (and the day's breather) | BEHIND or THROUGH the grass - mysiak's ask |
| INSPECT_REEF | THROUGH the grass, around and behind the pieces |
| EXPLORE | mostly FRONT, now and then THROUGH or BEHIND |
| FLEE | BEHIND the nearest cover |
| SEEK_FOOD, VISIT_BUBBLES | FRONT (the keeper sees the feeding) |
| FOLLOW_FRIEND | the friend's depth |

## Decisions for Strato

- **D1. Who picks the depth. DECIDED 2026-10-31 (Strato): the goal and the
  temperament, as above - no retraining.** (The other way was a depth in the
  model's output: schema v5 and a retrain.)
- **D2. Hidden behind a solid piece. TAKEN 2026-10-31 (phase 2 built for 0.3.9.4):** A fish behind the castle can vanish.
  The rule: behind a piece only while passing; resting hides in the grass,
  which stays see-through (the kids find their fish).
- **D3. The order of the work.** Recommended: (1) fish and the grass - FRONT
  / BEHIND / THROUGH, mysiak's ask; (2) fish and the pieces, the decoration
  depth reduced to the pieces' order; (3) the shrimp, the seahorse and the
  floor critters onto the same system (their lanes become it). Each phase:
  its rows in render-stack.md and `--selftest-stack` in all four worlds
  (BOARDS rule 6), the frame rate measured on every board.
- **D4. When. DECIDED 2026-10-31 (Strato): 0.3.9.4** - phases 1 and 2;
  phase 3 (the shrimp, the seahorse, the floor critters) later.

## Built (0.3.9.4, 2026-10-31)

Phases 1-3 shipped in 0.3.9.4 (items 5, 7, 10, 11 of docs/release-0.3.9.4.md):
the fish against the grass, the pieces and the sword plant IN FRONT; the
shrimp school against the grass and the pieces; the seahorse against the
grass (its lane: the pieces); the crab, the snail and the urchin against the
grass (their lanes: the pieces; the sword plant IN FRONT stays over them).
One routine draws them all (render.c dep_draw); `--selftest-depth` holds each.
