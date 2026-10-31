# The render stack: who is painted over whom (the contract)

The order the frame is painted in, back to front, and the rule for every
actor that stands on the sand. `fishsim --selftest-stack` (in `make check`,
so all four worlds) derives its checks from this table: for every pair of
actors that can overlap it renders both, each alone and neither, and checks
the occlusion runs the way the table says. **A new critter or decoration
is not done until it has its row here and the matrix passes in four
worlds** (docs/BOARDS.md rule 6). The matrix runs the board's path (the
scene cache, pieces baked). A placement page (0.3.9.4) changes nothing
until its piece is changed on it: the scene and the grass loop stay as they
are, every piece baked. Once the piece is dragged, re-ranked, re-depthed or
recolored it alone is drawn LIVE, by its rank (rank 11 below), and the loop
sits the page out (`--selftest-place`). The bare test path (no scene cache)
draws every piece live over everything and keeps the critters in the scene
pass.

## The order (render_tank, 2026-10-24)

| rank | painted | notes |
|---|---|---|
| 0 | the SCENE: the water, the sand, the pieces BEHIND (baked, with their contact shadows and the night glow), then the back grass - every frond of the seagrass (0.3.9.3: no weave; the seagrass always BEHIND, the sword plant BEHIND or IN FRONT), laid from THE GRASS LOOP's band and fine moment for the frame (render.c) | the scene rebuilt only on a bake; the loop rebaked when the grass's look changes, drawn live meanwhile |
| 1 | the DEEP crab in a shelter BEHIND (ROOST / SHELTER, backed in or not) | under every frond. With the frame primed from THE GRASS LOOP (every board since 0.3.9.3) it is drawn through dep_draw at depth BEHIND inside its shelter (DEP_NOBED): the loop's grass comes back over it from the frame's mask, every row (no roots cut: the shelter stands behind the roots), the shelter's rock stays as it is; with the grass live, drawn plain and the back fronds of every bed crossing its rect painted over it again. (0.3.9.4: the loop path was missing - the crab sat on the fronds, C1704 on Discord; `--selftest-cave`, loop on) |
| 2 | (gone in 0.3.9.3: the early arch pass drew a MIDDLE bed's late fronds inside a castle IN FRONT's opening; a bed BEHIND shows through the open arch from rank 0) | a bed placed IN FRONT takes no part (it stands before the castle) |
| 3 | the DEEP crab in a castle IN FRONT's arch | over the fronds seen through the opening |
| 4 | the shrimp | at the school's own SWIM DEPTH against the grass and the pieces (0.3.9.4, dep_draw) |
| 5 | the seahorse's back lane | at its SWIM DEPTH against the grass (its lane: the pieces) |
| 6 | the fish | each at its own SWIM DEPTH against the back grass (0.3.9.4, docs/swim-depth.md): IN FRONT as before; BEHIND - the grass's own pixels back over it inside its box; THROUGH - the odd fronds' only. Chosen by its goal and temperament, changed only clear of every bed. `--selftest-depth`, all four worlds |
| 7 | the floor critters in their BACK lane: the crab (walk / rest), then the snail, then the urchin | the crab lowest: whatever passes is over it. Each at its SWIM DEPTH against the back grass (0.3.9.4), in the front slot (9) too |
| 8 | the pieces IN FRONT restored over all of the above (the castle: its front row only - the arch stays open) | SWIM DEPTH phase 2 (0.3.9.4): a fish's own depth against the pieces decides - one passing BEHIND any piece (whatever its depth) has its body put back over it at rank 6 (pc_body_covers: the castle's front row, any other piece whole); one IN FRONT of a piece IN FRONT marks its pixels (g_keep) and this restore, the overlays (rank 12), the arch's shade (14) and a crab in a front volcano's cave (9) keep off them |
| 9 | the FRONT SLOT: the crab (in a volcano IN FRONT's cave or on its flank; its front lane on the sand; a dig, always), then the snail, then the urchin in their front lane | the crab lowest here too |
| 10 | the front grass: the sword plant placed IN FRONT with its every frond, WHOLE - across the arch too, unshaded (drawn live every frame, at the loop's moment) | a bed IN FRONT keeps off only the pieces ranked above it - and (SWIM DEPTH, 0.3.9.4) off any animal IN FRONT of it (every leaf) or THROUGH it (the even leaves): the fish, the shrimp, the seahorse, the crab, the snail, the urchin by their own choice; a crab in a shelter (cave, arch) stays under it. A live piece (11) ranked UNDER it is drawn just before it, so its leaves fall over that piece too |
| 11 | a placement page's LIVE piece - the page's own, once it has been changed on the page (dragged, re-ranked, re-depthed, recolored; until then it stays baked like the rest) | at its rank: a piece IN FRONT ranked above the sword plant IN FRONT comes here, after the leaves; one ranked under the plant was drawn before the leaves (10); one BEHIND right over the scene (0). The baked pieces ranked above it come back from the scene inside its box (pc_cover_live: a piece whole, the castle's keep and wall), keeping off a fish IN FRONT of the pieces and the leaves of a bed ranked over the covering piece. The front slot is off while a piece IN FRONT is live - a critter in its front lane goes to the TOP lane instead, so it stays before the live piece as it was before the baked one |
| 12 | the pieces IN FRONT's overlays: crowns, the candle, the volcano's lava and embers, the pumpkin's face and flame | the lava and the pumpkin's face keep off every critter in the front slot - the crab, the snail, the urchin (g_guard / slot_covers in vo_store and pk_store) |
| 13 | the TOP lane: the seahorse's front lane, the crab holding a sand dollar up; with a page up, the crab's, the snail's and the urchin's front lane | over the fronds, so the keeper sees the find |
| 14 | the arch's shade through a castle IN FRONT | what was drawn in the opening this frame and left marked (a fish); a bed IN FRONT's fronds are final and keep their color |

## What the matrix enumerates (2026-10-24, the second pass)

- Subjects: the CRAB in nine states (walk in either lane, rest, dig, the
  walk into a shelter, roost, backed in, climb, holding a find); the
  URCHIN and the SNAIL in either lane, at a piece's flank and at its
  middle (a cave mouth, the arch, the lantern's face).
- Pieces: none; the castle, the volcano, the coral, the cluster and the
  pumpkin, each BEHIND and IN FRONT. The coral, the cluster and the pumpkin
  have no cave or arch: the crab's shelter states cannot meet them. In
  front of any of the five a critter in its front lane stands on the
  piece's base line and shows over it; in its back lane the piece covers
  it.
- Passers: the fish, the shrimp, the snail and the urchin in either lane
  (for the crab), the even and the odd fronds of a bed over the spot, the
  same bed placed IN FRONT with its every frond, the piece itself; the
  light on and off.
- The tint: a bed placed IN FRONT of the castle, its fronds crossing the
  arch pixel for pixel the same color with the castle standing as without
  it (688 of 1323 were shaded before the fix), day and night.

- The strict rule: a subject in front of a piece IN FRONT shows the piece
  on (almost) none of its pixels - at most 1 in 20 - not just "clearly
  less" (the castle aside: its front row meets the crab in the arch).

## The rules behind it

- A decoration placed IN FRONT is in front of the FISH and the FRONDS,
  always. The critters that roam the sand take a LANE per move (tank.h THE
  LANES): 60 in front / 40 behind, changed only in the clear of every piece
  IN FRONT, so none pops through one. In the front lane a critter stands on
  the piece's own base line (DECOR_FY_FRONT).
- The crab digging for sand dollars is in front of every decoration.
- A crab in its cave or the castle's arch is the deepest thing on the sand:
  the fish, the shrimp, the snail, the urchin and the fronds that stand
  before its shelter all pass in front of it. A shelter BEHIND stands
  behind the fronds (the crab under them all); a shelter IN FRONT stands
  before them (the crab over the fronds seen through the arch, over the
  volcano's rock, lava and embers).
- Among the floor critters the crab is always lowest, then the snail, then
  the urchin, in either lane; across lanes the one in front is over.
- The held sand dollar is over everything but a page's live piece.
- A piece's CONTACT SHADOW (its body shifted 3 px down and right, hard; 6
  px, soft; pc_contact_shadow, bake only) falls on whatever stands behind
  the piece - the fronds included. A frond or a leaf that a piece in front
  of it skips (the pieces IN FRONT for the back grass; for the sword plant,
  those ranked above it) is painted over the rim and then shaded the same
  way (veg_rim_shade, 0.3.9.4), in the grass loop's bake, the live grass and
  the plant alike. `--selftest-footprint` holds it for every piece: the body
  its own, every frond on the rim darker than without the shade, never the
  bare frond.
- A frond row's spans all take ONE vignette alpha, the whole span's middle
  (span_final through veg_span's g_span_xc, 0.3.9.4) - a run a piece's mask
  splits it into, or a run clipped to a box, keeps the alpha the whole span
  has. That is THE PATCH's rule (render.c): what a piece does inside its box
  never changes a pixel outside it, so when a piece moves the grass loop
  keeps every band and difference and repaints only the boxes
  (`--selftest-grasspatch` holds the patched loop pixel for pixel against
  one baked from scratch, in all four worlds).
- A bed placed IN FRONT is a front-layer thing like a fish or a frond: over
  the crab in a shelter (a front castle's arch, a cave) and over every piece
  it is ranked above - with no tunnel shade on it, since it stands before the
  castle. Since 0.3.9.4 (SWIM DEPTH) every other animal's own depth decides:
  IN FRONT of it, THROUGH it or BEHIND it (Strato on the square, 10-31: the
  snail walking in front, the sword plant's leaves behind it).

## Where the matrix would have caught today's reports

The urchin behind a crab in its cave; the shrimp behind it; a frond behind
it (the weave, gone since 0.3.9.3); the crab behind the fronds in a front castle's arch; the
lava over the crab in a front volcano's cave; the lava over the crab
digging at the cave mouth. Each is one cell of the table. What the matrix
found on its own, the same day: the pumpkin's carved face painted over a
crab walking past a front lantern (now guarded like the lava).

What it missed, and why (2026-10-24, the 1.8): the sword plant placed IN
FRONT drawn behind the crab roosting in the castle, and its leaves across
the arch tinted. The matrix placed the plant only at the MIDDLE depth, so
a bed IN FRONT had no row; the early arch pass took every bed that reached
the arch, front beds included, and the tunnel's shade found their marked
pixels. A new axis means new rows: when a piece, a critter or a bed gains
a depth or a state, give the matrix that depth or state before calling the
layering done.

What the new row found on its own, in the other worlds (the rect alone
would have passed): on the watch and the square a front bed's fronds over
the crab backed into the castle came out a deeper green. The end-of-frame
passes that work from the dirty mask - the vignette sweep in the glass's
ring, the arch's shade - found the crab's marks still set under the final
frond and dimmed it a second time; the rect's vignette is nil there, so
it never showed. A final span now clears its marks wherever such a pass
could reach it (span_final / span_final2 in the ring, veg_put across the
arch). The lesson is the matrix's own: run it in all four worlds, every
time - docs/BOARDS.md rule 6 is not a formality.

What it missed (0.3.9.4, Strato on the square, 10-31): grass "clipping over
the edges" of the pumpkin - "small cut outs in the left and right edge" with
a frond showing through, and fronds through the volcano where the pumpkin
overlaps its foot, right after a boot, gone some minutes later. Every check
looked inside a piece's body MASK and the body was always clean, because the
mask was recorded from the same broken bake: `bake_scene` called outside a
frame (the boot show's final bake) ran with the last frame's marks still set
- a fish IN FRONT of the pieces (`g_keep`) above all - and `pk_store` /
`vo_store` kept off them, so the pieces were baked with fish-shaped holes
and the loop painted fronds into them until the next full rebake. The mask
can never prove the body: `--square-boot` compares the scene baked at the
start with a clean bake, pixel for pixel, with a fish held in front of the
pieces through the start. (The hunt also found the grass painted bright on a
piece's contact shadow's rim, outside the mask: a frond there takes the
shadow now, and `--selftest-footprint` holds it.) The same day, the
placement page: a live piece had no mask, so the sword
plant's "over" set took every unbaked piece for one under the plant and the
pumpkin's body kept off the leaves, whatever the order said - and since a
page took every piece IN FRONT out of the scene, every page showed it. The
matrix never ran the page's path; `--selftest-place` walks it now.

What it missed next (0.3.9.1, Strato on the 1.8, the light on and off):
the front volcano's lava in its cave painted over the urchin walking past
in its front lane. Two holes. The urchin and the snail stood only at a
piece's flank, where the volcano has no lava and the lantern no face; and
the 2:1 rule judged the piece's whole silhouette, so a patch of lava (66
px of 319) over the critter still read as "the urchin in front". The guard
that kept the lava and the face off the crab now keeps them off every
critter in the front slot; the matrix stands the urchin and the snail at
the middle too, and in front of a piece the piece must show on almost none
of the subject. Without the fix it fails twelve cells: the urchin and the
snail at the front volcano's cave and before the front lantern, the light
on and off.
