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
| 0 | the SCENE: the water, the sand, the pieces BEHIND (baked, with their contact shadows and the night glow), then the back grass - every frond of the seagrass (0.3.9.3: no weave; the seagrass always BEHIND) and the sword plant (one depth among the pieces since 0.4; against the seagrass, since 0.4.1 (Strato 11-05), the keeper's pick on its page: IN FRONT OF THE SEAGRASS - bed 3 painted last, 0.4's one look and every older plant's - or BEHIND THE SEAGRASS - bed 3 painted FIRST (render.c bed_ord, THE BED ORDER) in every back pass: the loop's bake and its box patch, the live grass, the crab's weave, the grass cache. Either way it is back grass: a fish IN FRONT is over both, one BEHIND under both, one THROUGH (and the seahorse coiled on a blade) under the odd fronds of both. `--selftest-plant-under`, its stack cells in `--selftest-stack`), laid from THE GRASS LOOP's band and fine moment for the frame (render.c) | the scene rebuilt only on a bake; the loop rebaked when the grass's look changes, drawn live meanwhile |
| 0b | TWILIGHT DEEP's drift (0.4.1): the far water's specks | painted late (render_tank, after the frame's own layers) but only on a pixel the frame still shows as the scene AND no piece covers (pc_covers: the castle's mask, every baked or live piece's) - so it lands on the water, the backdrop and the pieces' contact shadows only: under every frond, piece, critter and fish, never in the critters' sand lane. Before the 0.4.1 audit (P9, Strato: "only in the background") the scene test alone let ~5-7% of the specks onto the pieces, which are baked into the scene. `--selftest-backgrounds` (each frame against the same frame without the drift, over the castle, the coral and the cluster) |
| 1 | the DEEP crab in a shelter BEHIND (ROOST / SHELTER, backed in or not) | under every frond. With the frame primed from THE GRASS LOOP (every board since 0.3.9.3) it is drawn through dep_draw at depth BEHIND inside its shelter (DEP_NOBED): the loop's grass comes back over it from the frame's mask, every row, the shelter's rock stays as it is; with the grass live, drawn plain and the back fronds of every bed crossing its rect painted over it again. (0.3.9.4: the loop path was missing - the crab sat on the fronds, C1704 on Discord; `--selftest-cave`, loop on) |
| 2 | (gone in 0.3.9.3: the early arch pass drew a MIDDLE bed's late fronds inside a castle IN FRONT's opening; a bed BEHIND shows through the open arch from rank 0) | a bed placed IN FRONT takes no part (it stands before the castle) |
| 3 | the DEEP crab in a castle IN FRONT's arch | over the fronds seen through the opening |
| 4-7 | THE ANIMAL PASS, DEEPEST FIRST (0.4, Strato 11-02: "the deeper animal is always under"): the shrimp, the seahorse's back lane, the fish and the floor critters' back lane drawn in ONE pass, sorted back to front by their layer (render.c anim_layer), not by kind. The layers: L0 behind the grass and the pieces (a fish or the school at BEHIND / BEHIND); L1 behind the grass, before the pieces BEHIND (BEHIND / IN FRONT; an animal in its back lane behind the grass); L2 through the grass (whatever its piece depth: through is behind every piece IN FRONT and before every piece BEHIND); L3 before the grass, behind the pieces IN FRONT (IN FRONT / BEHIND; an animal in its back lane before the grass); L4 before the grass and the pieces (IN FRONT / IN FRONT). At one layer the old ranks hold, stable: the shrimp (4), the seahorse's back lane (5), the fish by slot (6), the crab, the snail, the urchin (7). Why: an animal's put-back lays the grass and the pieces it is behind over it from what was drawn before it, so an animal drawn after one in front of it went over that one everywhere but on the fronds - the front one in frond-shaped stripes (the pendant, two fish; 0a337b8 sorted the fish, this the rest). `--selftest-fish-pair` (every pair of animals at different layers, live and from the loop; PT_FISH_PAIR_OLD=1 the control: the ranks alone) | the rows 4-7 below are each kind's own rules, inside the pass |
| 4 | the shrimp | at the school's own SWIM DEPTH against the grass and the pieces (0.3.9.4, dep_draw); the school at its layer in the pass |
| 5 | the seahorse's back lane | at its SWIM DEPTH against the grass (its lane: the pieces). Coiled on a frond, THE BED'S STACK (0.3.9.7): every piece that stands before that frond's bed (veg_skip_for) is put back WHOLE over it from what was under it - the castle's keep and towers too, a piece BEHIND ranked above the sword plant too; in its front lane (13) the same, for the pieces IN FRONT ranked above the plant. Taken up only clear of every piece, or as it settles. `--selftest-seahorse-stack` (PT_SH_STACK_OFF=1 the control) |
| 6 | the fish | each at its own SWIM DEPTH against the back grass (0.3.9.4, docs/swim-depth.md): IN FRONT as before; BEHIND - the grass's own pixels back over it inside its box, every row down to the sand (0.3.9.6: no cut at the roots); THROUGH - the odd fronds' only. Chosen by its goal and temperament, changed only clear of every bed. `--selftest-depth`, all four worlds. At its layer in the pass (0.4, the pendant); a visitor too - its depth counts toward the frame's grass masks like a home fish's (render.c dep_any: until 0.4 only the home fish's did, so a visitor behind the grass with every home fish in front was drawn over the fronds) |
| 7 | the floor critters in their BACK lane: the crab (walk / rest / a climb on a piece BEHIND), then the snail, then the urchin | each at its SWIM DEPTH against the back grass (0.3.9.4), at its layer in the pass (L1-L3: the back lane is behind the pieces IN FRONT); at one layer the crab lowest. A fish or the school before the grass and the pieces (L4) passes OVER a critter in its back lane (until 0.4 the critter was drawn over it); in the front slot (9) the critters stay over every fish |
| 8 | the pieces IN FRONT restored over all of the above (the castle: its front row only - the arch stays open) | SWIM DEPTH phase 2 (0.3.9.4): a fish's own depth against the pieces decides - one passing BEHIND any piece (whatever its depth) has its body put back over it at rank 6 (pc_body_covers: the castle's front row, any other piece whole); one IN FRONT of a piece IN FRONT marks its pixels (g_keep) and this restore, the overlays (rank 12), the arch's shade (14) and a crab in a front volcano's cave (9) keep off them - and so does the put-back of a later animal passing BEHIND the piece (0.3.9.6: there the earlier fish's own pixels come back, not the scene's; `--selftest-front-keep`) |
| 9 | the FRONT SLOT: the crab (in a volcano IN FRONT's cave or on its flank; its front lane on the sand; a dig, always), then the snail, then the urchin in their front lane | the crab lowest here too |
| 10 | RETIRED 0.4 (the sword plant has one depth, BEHIND - no bed IN FRONT is left; the matrix skips its cells). Was: the front grass: the sword plant placed IN FRONT with its every frond, WHOLE - across the arch too, unshaded (drawn live every frame, at the loop's moment) | a bed IN FRONT keeps off only the pieces ranked above it - and (SWIM DEPTH, 0.3.9.4) off any animal IN FRONT of it (every leaf) or THROUGH it (the even leaves): the fish, the shrimp, the seahorse, the crab, the snail, the urchin by their own choice; a crab in a shelter (cave, arch) stays under it. A live piece (11) ranked UNDER it is drawn just before it, so its leaves fall over that piece too |
| 11 | a placement page's LIVE piece - the page's own, once it has been changed on the page (dragged, re-ranked, re-depthed, recolored; until then it stays baked like the rest) | at its rank: a piece IN FRONT ranked above the sword plant IN FRONT comes here, after the leaves; one ranked under the plant was drawn before the leaves (10); one BEHIND right over the scene (0) - under the grass, so the sword plant (over the seagrass since 0.4) keeps its leaves off it where it is ranked above the plant, by the live piece's own mask (pm_live: never its baked slot, so a CANCEL leaves the scene's masks as they were; `--selftest-place`, the pumpkin BEHIND dragged). The baked pieces ranked above it come back from the scene inside its box (pc_cover_live: a piece whole, the castle's keep and wall), keeping off a fish IN FRONT of the pieces and the leaves of a bed ranked over the covering piece. The front slot is off while a piece IN FRONT is live - a critter in its front lane goes to the TOP lane instead, so it stays before the live piece as it was before the baked one |
| 12 | the pieces IN FRONT's overlays: crowns, the candle, the volcano's lava and embers, the pumpkin's face and flame | the lava and the pumpkin's face keep off every critter in the front slot - the crab, the snail, the urchin (g_guard / slot_covers in vo_store and pk_store) |
| 13 | the TOP lane: the seahorse's front lane, the crab holding a sand dollar up; with a page up, the crab's, the snail's and the urchin's front lane | over the fronds, so the keeper sees the find |
| 14 | the arch's shade through a castle IN FRONT | what was drawn in the opening this frame and left marked (a fish); a bed IN FRONT's fronds are final and keep their color |

## What the matrix enumerates (2026-10-24, the second pass)

- Subjects: the CRAB in nine states (walk in either lane, rest, dig, the
  walk into a shelter, roost, backed in, climb, holding a find); the
  URCHIN and the SNAIL in either lane, at a piece's flank and at its
  middle (a cave mouth, the arch, the lantern's face).
- Pieces: none; the castle, the volcano, the coral, the cluster and the
  pumpkin, each BEHIND and IN FRONT. (Two pieces at once round the crab's
  shelter - the castle with the volcano, either shelter with the coral, the
  cluster or the pumpkin, each order: `--selftest-roost`.) The coral, the cluster and the pumpkin
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

- A FROND IS WHOLE over an animal (0.3.9.6): an animal BEHIND the grass has
  every frond before all of it, its foot at the sand included; THROUGH, the odd
  fronds whole before it and the even ones whole behind. 0.3.9.4's put-back
  stopped at DEP_ROOT_Y (8 rows above the sand) so a critter kept its feet over
  the roots - and a critter sitting at the roots had each frond over its top
  and its lower body over the frond's foot, which Strato read as a frond
  "pasted on" the urchin (the square, 10-31 night; the snail and the crab the
  same). `--selftest-depth` counts the roots apart for the three floor critters.
- A decoration placed IN FRONT is in front of the FISH and the FRONDS,
  always. The critters that roam the sand take a LANE per move (tank.h THE
  LANES): 60 in front / 40 behind, changed only in the clear of every piece
  IN FRONT, so none pops through one. In the front lane a critter stands on
  the piece's own base line (DECOR_FY_FRONT).
- An animal holding a frond is in that frond's stack (0.3.9.7, a keeper's
  square on #help "Seahorse hold": coiled on a seagrass blade behind a castle
  IN FRONT it sat on the tower's cone; on a sword plant's leaf ranked under the
  volcano it sat on the peak). Whatever hides the frond hides what is coiled on
  it: its lane alone is not enough, since the castle's front row is all the
  back lane gets put back, and a piece BEHIND is behind every lane.
- An animal never pops behind or out from behind a piece it is on (0.3.9.7,
  #help: "they choose to be on a layer behind the volcano while they're in
  front of the volcano. So they just poof disappear", the shrimp mostly). Both
  of its depths decide a piece: behind or through the grass is behind a piece
  IN FRONT, behind the grass AND the pieces is behind a piece BEHIND (tank.h
  depth_behind_piece, the one rule render.c draws by). The grass depth waited
  only for the beds, so over the volcano a fish or the school went at once;
  now a change of either depth that would flip a piece it is on waits until it
  is off that piece (tank.c pieces_keep). `--selftest-depth-pop` (the volcano
  BEHIND and IN FRONT, PT_DEPTH_POP_OLD=1 the control), `--depth-pop-hunt`
  the long version, director `poptest <i> [old|new] [piece]` on the glass.
- A fish behind or through the grass is behind the WHOLE castle IN FRONT,
  its keep (the rear tower) included (0.3.9.7, #help "Castle Tower Oddness",
  a square: a fish over the rear tower with fronds over it - the grass stands
  behind the keep, so that was a loop). Only a fish in front of the grass but
  behind the pieces swims between the keep and the front row. tank.h
  depth_piece_cover() gives 0 / 1 (the front row) / 2 (all of it); the
  grass put-back leaves a piece put back whole to the piece. A change between
  1 and 2 over the keep waits too (pieces_keep). `--selftest-castle-keep`
  (cone and window, every depth pair, the grass loop off and on;
  PT_CASTLE_ROW_ONLY=1 the control), `--castle-shot` the sheet, director
  `keeptest <i> front|through|behind [old|new]` on the glass.
- A fish called by a held finger is in sight (0.4, Strato 11-01: "the fish
  would not be able to see my finger through the volcano"): it wants the front
  of the grass and the pieces, and a piece hiding it is swum out from round
  its nearest side (left, right or over the top: tank.c piece_exit) before the
  front is taken - never a pop through a piece. Its jump to the front may cross
  the seagrass. The seahorse too (Strato 11-01): called, it swims out from
  behind a piece IN FRONT (its back lane) or the bed's stack it was coiled in,
  then takes the front lane and the front of the grass. Its lane for each hold
  (sh_front_want) is taken up clear of every piece, or as it settles on the
  hold - never through a piece. `--selftest-hold-emerge` (four fish behind the
  volcano BEHIND and IN FRONT, the seahorse behind it IN FRONT, then the finger
  lifted; PT_HOLD_EMERGE_OFF=1 the control), director `holdtest [old|new]
  [piece]`.
- A seahorse holding a piece's post is in that piece's stack (0.4, a keeper's
  pendant: on the castle's rear tower it was drawn under the volcano the castle
  stands before): right before that piece, under every piece that stands
  before it (tank_decor_above), the castle's rear tower under the castle's own
  front row too (tank.h sh_opost / sh_orow, taken up as sh_obed; render.c
  sh_stack + g_dep_over_row). The rear tower's post is in the castle's lane
  now, not always the back. `--selftest-seahorse-post` (PT_SH_STACK_OFF=1 the
  control), director `postshot [old|new]`.
- The crab digging for sand dollars is in front of every decoration.
- A crab in its cave or the castle's arch is the deepest thing on the sand:
  the fish, the shrimp, the snail, the urchin and the fronds that stand
  before its shelter all pass in front of it. A shelter BEHIND stands
  behind the fronds (the crab under them all); a shelter IN FRONT stands
  before them (the crab over the fronds seen through the arch, over the
  volcano's rock, lava and embers).
- THE CRAB'S SHELTER IS THE FRONTMOST, AND THE CRAB IN IT STANDS AT ITS PLACE
  IN THE STACK (crab-roost, 2026-11-02; Strato on the watch: the crab "roosting
  in the volcano" with the castle standing in front of it - "the crab should
  always choose the uppermost roosting spot"). Of the castle and the volcano in
  the water it roosts in the one painted over the other (tank_decor_above: the
  greater depth, then the higher rank), wherever they stand - never the nearer
  (tank.c crab_cave; a new shelter joins CR_SHELTERS). A change while it roosts
  or walks in (a piece bought, back from the shelf, re-depthed, re-ranked, a
  SWAP IN; its piece moved) sends it over, once out of the dark after a scare.
  In its shelter it is INSIDE that piece: every piece ranked above the shelter
  (the coral, the cluster, the pumpkin too) covers it - the crab is not painted
  under those (g_cr_clip) - and every piece ranked below stays behind it: a
  piece IN FRONT under a castle IN FRONT keeps its restore (rank 8) and its
  overlay (rank 12) off the crab in the arch; a volcano IN FRONT's cave keeps
  its own lava off it as before. `--selftest-roost` (PT_ROOST_OLD=1 the pick's
  control, PT_CRAB_STACK_OLD=1 the render's).
  The same holds for an ANIMAL'S PUT-BACK (0.4, abumaia's pendant on #help
  11-03 "Shrimp shadows?": the castle IN FRONT over the volcano, the shrimp
  behind the castle or the volcano "cast a shadow on the resting crab, hiding
  it"): an animal passing behind a piece IN FRONT has the piece put back over
  it from the scene, which has no crab - so where that piece is the crab's
  castle or one ranked below it, the crab's pixels come back from what was
  under the animal (the crab), not the scene (render.c dep_draw, g_dp_crab).
  A piece ranked above the castle still comes back over the crab. And an
  animal behind the WHOLE castle (behind or through the grass) is behind the
  crab everywhere: the arch is open to what is behind the castle, so it showed
  over the crab in the opening - with the volcano BEHIND nothing hid it - and
  the grass's put-back brought the crab back in frond-shaped stripes. One in
  front of the grass but behind the pieces still swims through the arch over
  the crab (the crab is the deepest thing on the sand).
  `--selftest-roost-pass` (in `make check`: both pieces in every depth order,
  the shrimp and a fish swept across the crab at every depth pair - no crab
  pixel may show what is behind it; with the crab in a castle IN FRONT, behind
  the castle its front row stays exact and behind the whole castle the crab
  stays exact; PT_ROOST_PASS_OLD=1 the control), director
  `rpasstest [old|new|off] [front|through|behind] [pfront|pbehind]`.
- OUT OF ITS SHELTER, IN FRONT, THE FRONT LANE (0.4, Strato on the square
  11-04: "i absolutely saw volcano clipping over the crab"). In its shelter the
  crab is inside that piece, in no lane; leaving it, it kept the lane it came
  in with (inside a footprint a lane never changes), so one that came in by its
  back lane walked out of a castle IN FRONT behind every piece IN FRONT - the
  volcano behind the castle restored over it in the arch (rank 8), all of it,
  until it was clear; out of a volcano IN FRONT's cave the volcano's own rock.
  Out of a shelter IN FRONT (the roost's end, a pellet, a tap as it walks in)
  it takes the FRONT lane - out of the opening toward the glass - and the
  lanes' rule holds it until it is clear of every piece (tank.c crab_mode).
  A shelter BEHIND changes nothing: the crab in it was behind every piece IN
  FRONT already. A piece ranked above a shelter IN FRONT and standing over
  its opening (a coral before the arch) has the crab step out over it.
  `--selftest-roost-exit` (PT_EXIT_LANE_OLD=1 the control), director
  `crab out [old|new]`.
- IN ITS SHELTER MEANS AT THE OPENING (0.4, the layer hunt `--layer-hunt`:
  in its front lane before a coral IN FRONT, the crab headed for a volcano
  BEHIND across the tank and vanished behind the coral at once, 518 px -> 0).
  Walking in counted as inside the shelter from the first step. Now the crab
  is drawn inside its shelter (rank 1 / 3, the front volcano's cave in the
  front slot) only roosting, or walking in and at the opening - the castle's
  arch (26 px of its middle), the cave mouth (CR_NEAR_PX) - tank.c
  tank_crab_in_shelter, the one test render.c draws by. Until then it walks
  in its lane like any crab. Heading home from the open sand it takes the lane
  of its shelter's depth: IN FRONT, the front lane (in view, into the
  opening; as it comes out); BEHIND, the back lane (behind a piece IN FRONT
  standing before the opening, not through it at the door). Already inside a
  footprint its lane holds; heading for a shelter BEHIND in its front lane
  from inside a piece IN FRONT's footprint (a cluster IN FRONT before a
  castle BEHIND's arch: the hunt's tank 97, 528 px -> 0 at the door) it
  first steps out to the nearest clear sand, takes the back lane there and
  walks back in behind the piece (tank.c crab_home_lane). The other way
  round needs no step: a crab in its back lane goes into a shelter IN FRONT
  from behind, through the arch or into the cave's dark.
  `--selftest-roost-exit`'s way in: from a coral IN FRONT to each shelter in
  either lane, no pop but the frame it goes in, at the opening.
- A DEPTH CHANGES ONLY WHERE NOTHING SHOWS IT (0.4, the layer hunt's last
  four - each popped in one frame). A floor critter's lane (tank.c lane_try)
  changes only clear of every piece IN FRONT, as since 10-24, and now also
  with no other animal over it (the urchin stepping forward onto the crab,
  127 -> 387 px), and in the grass only from in front of it: the front lane
  is drawn over every frond (DEP_*_F take FISH_DEPTH_FRONT), so a critter in
  its back lane behind or through a bed waits for the open sand to step
  forward, and one stepping back inside a bed keeps FISH_DEPTH_FRONT - its
  depth eases to its want clear of the beds, as it always has (the crab
  dropping behind the sword plant's leaves, 509 -> 153 px). The seahorse's
  lane and stacks change clear of every piece or as it settles on its hold;
  heading for a hold in another stack with its way inside the pieces, it
  reached the hold still in the old one and changed there (coiled in the
  sword plant's stack behind the volcano, onto a frond of the next bed:
  0 -> 395 px). Now it swims out round the pieces first (exit_round, the
  finger's way out) and changes in the clear. `--selftest-pop-lanes`: the
  urchin on the crab, the crab's lane in a bed exact (the frame against the
  same frame with the old lane put back: 0 px), the walk home, the seahorse
  bed to bed in three layouts; PT_POP_OLD=1 / 2 / 4 the controls, each
  FAILS its part.
- THE ORDER IS PART OF THE GRASS LOOP'S LOOK (0.4, Strato on the square
  11-04: the sword plant raised over the volcano on its page - IN FRONT lit
  - its leaves stayed cut round the volcano; toggling flickered). The loop
  bakes each back bed's leaves cut round the pieces ranked above it
  (veg_skip_for); its look (render.c gl_key) held the beds' kind, place and
  fronds but not which pieces they keep off, so a re-rank of the plant was
  never a change: the loop served the old order on the page, after DONE,
  until a restart - against every piece BEHIND. The kept-off set is in the
  look now; the patch repaints the pieces' boxes (gl_window). `--selftest-
  plant-order` (the board's path - the loop on, its slices between frames -
  against loops baked fresh with each order, every moment exact, the plant
  against the volcano, castle, pumpkin, coral, cluster; PT_GL_SKIP_OLD=1 the
  control). `--selftest-place` measures with the loop bypassed: it never saw
  this.
- Among the floor critters at one layer the crab is lowest, then the snail,
  then the urchin, in either lane; across lanes or layers the one in front is
  over (0.4: in the back lane, a critter behind the grass is under one before it).
- The deeper animal is always under (0.4, Strato on the pendant 11-02: "one
  fish behind the seagrass and one fish in front. when they layer, the
  seagrass gets the drawing wrong"). An animal's put-back lays the grass and
  the pieces it is behind over it from what was drawn before it, so whatever
  stands before it has to be drawn after it: THE ANIMAL PASS (ranks 4-7) goes
  back to front by layer (render.c anim_layer), never by kind or slot - the
  fish first (0a337b8), then the shrimp, the seahorse's back lane and the
  floor critters' back lane with them. The deep crab in a shelter (1, 3), the
  front slot (9) and the top lane (13) are outside the pass, as before.
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
- THE BAKE IS PAINTED IN ORDER, AND NOTHING IN IT CLIPS BY A MASK OF A PIECE NOT YET PAINTED (coral-sliver,
  2026-11-02). A piece's mask is this bake's only once the bake has painted it; before that it holds the last
  bake's shape. The lantern's store kept off the pieces above it by their masks - right for its face and flame,
  drawn every frame after the bake - in the bake too, so a coral or a reef cluster above it that changed shape
  left the lantern holes where only the old shape covered, the water showing through, until the next bake: a
  growth step (the cluster on most of its 64 steps, up to ~20 px; the coral on a few), and worst the first bake
  after masks of other shapes (a tank staged after another: up to ~870 px of the lantern missing, the old
  cluster's silhouette). In the bake the order alone decides; a piece above is painted over the lantern after it.
  `--selftest-bake-order` (in `make check`): the coral and the cluster above the lantern at each depth, every
  growth step of their size phase and a step back, each scene against the same tank baked again twice.
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
