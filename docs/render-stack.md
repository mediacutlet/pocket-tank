# The render stack: who is painted over whom (the contract)

The order the frame is painted in, back to front, and the rule for every
actor that stands on the sand. `fishsim --selftest-stack` (in `make check`,
so all four worlds) derives its checks from this table: for every pair of
actors that can overlap it renders both, each alone and neither, and checks
the occlusion runs the way the table says. **A new critter or decoration
is not done until it has its row here and the matrix passes in four
worlds** (docs/BOARDS.md rule 6). The matrix runs the board's path (the
scene cache, pieces baked); a placement page and the bare test path draw a
live piece over everything and keep the critters in the scene pass.

## The order (render_tank, 2026-10-24)

| rank | painted | notes |
|---|---|---|
| 0 | the SCENE: the water, the sand, the pieces BEHIND (baked, with their contact shadows and the night glow), then the back grass - every frond of the seagrass (0.3.9.3: no weave; the seagrass always BEHIND, the sword plant BEHIND or IN FRONT), laid from THE GRASS LOOP's band and fine moment for the frame (render.c) | the scene rebuilt only on a bake; the loop rebaked when the grass's look changes, drawn live meanwhile |
| 1 | the DEEP crab in a shelter BEHIND (ROOST / SHELTER, backed in or not) | drawn, then the back fronds of every bed crossing its rect painted over it again: under every frond |
| 2 | (gone in 0.3.9.3: the early arch pass drew a MIDDLE bed's late fronds inside a castle IN FRONT's opening; a bed BEHIND shows through the open arch from rank 0) | a bed placed IN FRONT takes no part (it stands before the castle) |
| 3 | the DEEP crab in a castle IN FRONT's arch | over the fronds seen through the opening |
| 4 | the shrimp | |
| 5 | the seahorse's back lane | |
| 6 | the fish | |
| 7 | the floor critters in their BACK lane: the crab (walk / rest), then the snail, then the urchin | the crab lowest: whatever passes is over it |
| 8 | the pieces IN FRONT restored over all of the above (the castle: its front row only - the arch stays open) | |
| 9 | the FRONT SLOT: the crab (in a volcano IN FRONT's cave or on its flank; its front lane on the sand; a dig, always), then the snail, then the urchin in their front lane | the crab lowest here too |
| 10 | the front grass: the sword plant placed IN FRONT with its every frond, WHOLE - across the arch too, unshaded (drawn live every frame, at the loop's moment) | a bed IN FRONT keeps off only the pieces ranked above it |
| 11 | a placement page's live piece | over everything; the front slot is off while one is up - a critter in its front lane goes to the TOP lane instead, so it stays before the live piece as it was before the baked one |
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
- A bed placed IN FRONT is a front-layer thing like a fish or a frond
  (Strato: "in front of" applies globally to fish and fronds): over every
  critter in every state, the crab in a front castle's arch included, and
  over every piece it is ranked above - with no tunnel shade on it, since
  it stands before the castle.

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
