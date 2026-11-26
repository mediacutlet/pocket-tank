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
| 0b | THE DRIFT: the far water's specks - TWILIGHT DEEP's marine snow (0.4.1); EMBERS' (the volcanic vent's) embers off each chimney's mouth and MANGROVES' silt and falling leaves (0.4.2; an ember lights itself: the night does not dim it) | painted late (render_tank, after the frame's own layers) but only on a pixel the frame still shows as the scene AND no piece covers (pc_covers: the castle's mask, every baked or live piece's) - so it lands on the water, the backdrop and the pieces' contact shadows only: under every frond, piece, critter and fish, never in the critters' sand lane. Before the 0.4.1 audit (P9, Strato: "only in the background") the scene test alone let ~5-7% of the specks onto the pieces, which are baked into the scene. `--selftest-backgrounds` (each frame against the same frame without the drift, over the castle, the coral and the cluster; 0.4.2: the same for the vent and the mangroves, and a frame after the drift is the frame that never had it - nothing left behind). The vent's glowing mouths, rims and cracks are NOT this row: they are baked into rank 0's scene with a self-lit weight (backgrounds.h bg_row_lit), so the night dims them less |
| 1 | the DEEP crab in a shelter BEHIND (ROOST / SHELTER, backed in or not) | under every frond. With the frame primed from THE GRASS LOOP (every board since 0.3.9.3) it is drawn through dep_draw at depth BEHIND inside its shelter (DEP_NOBED): the loop's grass comes back over it from the frame's mask, every row, the shelter's rock stays as it is; with the grass live, drawn plain and the back fronds of every bed crossing its rect painted over it again. (0.3.9.4: the loop path was missing - the crab sat on the fronds, C1704 on Discord; `--selftest-cave`, loop on) |
| 2 | (gone in 0.3.9.3: the early arch pass drew a MIDDLE bed's late fronds inside a castle IN FRONT's opening; a bed BEHIND showed through the open arch from rank 0. 0.4.3: the arch is a DEN with a solid back - nothing shows through it, and the pass's code, the late pass's skip and the fronds' tunnel shade are out of render.c) | - |
| 3 | the DEEP crab in a castle IN FRONT's den | over the den's back wall and floor (baked with the keep, 0.4.3: the den is in the castle's silhouette, so no frond is drawn in it); its feet on the castle's floor line (tank.c crab_tick, eased in and out) |
| 4-7 | THE ANIMAL PASS, DEEPEST FIRST (0.4, Strato 11-02: "the deeper animal is always under"): the shrimp, the seahorse's back lane, the fish and the floor critters' back lane drawn in ONE pass, sorted back to front by their layer (render.c anim_layer), not by kind. The layers: L0 behind the grass and the pieces (a fish or the school at BEHIND / BEHIND); L1 behind the grass, before the pieces BEHIND (BEHIND / IN FRONT; an animal in its back lane behind the grass); L2 through the grass (whatever its piece depth: through is behind every piece IN FRONT and before every piece BEHIND); L3 before the grass, behind the pieces IN FRONT (IN FRONT / BEHIND; an animal in its back lane before the grass); L4 before the grass and the pieces (IN FRONT / IN FRONT). At one layer the old ranks hold, stable: the shrimp (4), the seahorse's back lane (5), the fish by slot (6), the crab, the snail, the urchin (7). Why: an animal's put-back lays the grass and the pieces it is behind over it from what was drawn before it, so an animal drawn after one in front of it went over that one everywhere but on the fronds - the front one in frond-shaped stripes (the pendant, two fish; 0a337b8 sorted the fish, this the rest). `--selftest-fish-pair` (every pair of animals at different layers, live and from the loop; PT_FISH_PAIR_OLD=1 the control: the ranks alone) | the rows 4-7 below are each kind's own rules, inside the pass |
| 4 | the shrimp | at the school's own SWIM DEPTH against the grass and the pieces (0.3.9.4, dep_draw); the school at its layer in the pass |
| 5 | the seahorse's back lane | at its SWIM DEPTH against the grass (its lane: the pieces). Coiled on a frond, THE BED'S STACK (0.3.9.7): every piece that stands before that frond's bed (veg_skip_for) is put back WHOLE over it from what was under it - the castle's keep and towers too, a piece BEHIND ranked above the sword plant too; in its front lane (13) the same, for the pieces IN FRONT ranked above the plant. Taken up only clear of every piece, or as it settles. `--selftest-seahorse-stack` (PT_SH_STACK_OFF=1 the control) |
| 6 | the fish | each at its own SWIM DEPTH against the back grass (0.3.9.4, docs/swim-depth.md): IN FRONT as before; BEHIND - the grass's own pixels back over it inside its box, every row down to the sand (0.3.9.6: no cut at the roots); THROUGH - the odd fronds' only. Chosen by its goal and temperament, changed only clear of every bed. `--selftest-depth`, all four worlds. At its layer in the pass (0.4, the pendant); a visitor too - its depth counts toward the frame's grass masks like a home fish's (render.c dep_any: until 0.4 only the home fish's did, so a visitor behind the grass with every home fish in front was drawn over the fronds) |
| 7 | the floor critters in their BACK lane: the crab (walk / rest / a climb on a piece BEHIND), then the snail, then the urchin | each at its SWIM DEPTH against the back grass (0.3.9.4), at its layer in the pass (L1-L3: the back lane is behind the pieces IN FRONT); at one layer the crab lowest. A fish or the school before the grass and the pieces (L4) passes OVER a critter in its back lane (until 0.4 the critter was drawn over it); in the front slot (9) the critters stay over every fish. The crab CLIMBING a volcano BEHIND is at the VOLCANO'S DEPTH against everything (0.4.2, below): against the PIECES in the volcano's stack (every piece ranked above the volcano whole before it - a castle IN FRONT's keep too - never only the front row of rank 8); against the GRASS behind every frond painted over the volcano, whatever its own swim depth - the seagrass, and the sword plant unless the volcano is ranked above the plant; in the pass the FIRST of L1, so every animal before the pieces BEHIND is over it |
| 8 | the pieces IN FRONT restored over all of the above (the castle: its front row only - a fish in the gate swims between the keep, with its den, and the front row) | SWIM DEPTH phase 2 (0.3.9.4): a fish's own depth against the pieces decides - one passing BEHIND any piece (whatever its depth) has its body put back over it at rank 6 (pc_body_covers: the castle's front row, any other piece whole); one IN FRONT of a piece IN FRONT marks its pixels (g_keep) and this restore, the overlays (rank 12), the arch's shade (14) and a crab in a front volcano's cave (9) keep off them - and so does the put-back of a later animal passing BEHIND the piece (0.3.9.6: there the earlier fish's own pixels come back, not the scene's; `--selftest-front-keep`) |
| 9 | the FRONT SLOT: the crab (in a volcano IN FRONT's cave or on its flank; its front lane on the sand; a dig, always), then the snail, then the urchin in their front lane | the crab lowest here too. On the flank (the climb) as in the cave: in the volcano's stack (0.4.2) - not painted under a piece IN FRONT ranked above the volcano, whose overlay (12) goes over it; the volcano's lava and the pieces below keep off it |
| 10 | RETIRED 0.4 (the sword plant has one depth, BEHIND - no bed IN FRONT is left; the matrix skips its cells). Was: the front grass: the sword plant placed IN FRONT with its every frond, WHOLE - across the arch too, unshaded (drawn live every frame, at the loop's moment) | a bed IN FRONT keeps off only the pieces ranked above it - and (SWIM DEPTH, 0.3.9.4) off any animal IN FRONT of it (every leaf) or THROUGH it (the even leaves): the fish, the shrimp, the seahorse, the crab, the snail, the urchin by their own choice; a crab in a shelter (cave, arch) stays under it. A live piece (11) ranked UNDER it is drawn just before it, so its leaves fall over that piece too |
| 11 | a placement page's LIVE piece - the page's own, once it has been changed on the page (dragged, re-ranked, re-depthed, recolored; until then it stays baked like the rest) | at its rank: a piece IN FRONT ranked above the sword plant IN FRONT comes here, after the leaves; one ranked under the plant was drawn before the leaves (10); one BEHIND right over the scene (0) - under the grass, so the sword plant (over the seagrass since 0.4) keeps its leaves off it where it is ranked above the plant, by the live piece's own mask (pm_live: never its baked slot, so a CANCEL leaves the scene's masks as they were; `--selftest-place`, the pumpkin BEHIND dragged). The baked pieces ranked above it come back from the scene inside its box (pc_cover_live: a piece whole, the castle's keep and wall), keeping off a fish IN FRONT of the pieces and the leaves of a bed ranked over the covering piece. The front slot is off while a piece IN FRONT is live - a critter in its front lane goes to the TOP lane instead, so it stays before the live piece as it was before the baked one |
| 12 | the pieces IN FRONT's overlays: crowns, the candle, the volcano's lava and embers, the pumpkin's face and flame | the lava and the pumpkin's face keep off every critter in the front slot - the crab, the snail, the urchin (g_guard / slot_covers in vo_store and pk_store) |
| 13 | the TOP lane: the seahorse's front lane, the crab holding a sand dollar up; with a page up, the crab's, the snail's and the urchin's front lane | over the fronds, so the keeper sees the find |
| 14 | the den's shade in a castle IN FRONT | what was drawn in the den this frame and left marked (a fish in the gate, the crab); the den's own wall and floor carry it in the bake |

## What the matrix enumerates (2026-10-24, the second pass)

- Subjects: the CRAB in nine states (walk in either lane, rest, dig, the
  walk into a shelter, roost, backed in, climb, holding a find - the climb
  with a second piece beside the volcano: `--selftest-crab-climb`); the
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
  before them (the crab in the castle's den, where no frond is drawn since
  0.4.3; over the volcano's rock, lava and embers).
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
  crab everywhere. Until 0.4.3 the arch was open to what is behind the castle,
  so it showed over the crab in the opening and a pass of its own put the crab
  back; the den is in the castle's silhouette now, so the same put-back that
  hides the animal behind the stone hides it behind the den, and the crab's
  pixels come back from what was under it (that pass is gone; `--selftest-den`
  E holds every pixel of the roosting crab inside the silhouette). One in
  front of the grass but behind the pieces still swims through the gate over
  the crab (the crab is the deepest thing on the sand).
  `--selftest-roost-pass` (in `make check`: both pieces in every depth order,
  the shrimp and a fish swept across the crab at every depth pair - no crab
  pixel may show what is behind it; with the crab in a castle IN FRONT, behind
  the castle its front row stays exact and behind the whole castle the crab
  stays exact; PT_ROOST_PASS_OLD=1 the control), director
  `rpasstest [old|new|off] [front|through|behind] [pfront|pbehind]`.
- THE CLIMB IS IN THE VOLCANO'S STACK (0.4.2, Strato's pendant: back to front
  the volcano, the castle raised over it, the fronds - the crab climbing the
  volcano drawn over the castle's stonework on its way down; the 0.4.2 audit's
  RN-1). On the flank the crab is ON the volcano, so THE CRAB'S STACK takes the
  volcano for its shelter (render.c crab_stack, crab_shelter): the climb clears
  cr_cave, so until 0.4.2 it had no stack - over every piece IN FRONT in the
  front slot, over every piece BEHIND in the back lane, over a castle IN
  FRONT's keep (rank 8 restores its front row only). The cells, for a climbing
  crab against a second piece:

  | volcano | the piece | ranked | the crab against the piece |
  |---|---|---|---|
  | BEHIND | BEHIND | above the volcano | behind it, whole (not painted under its mask) |
  | BEHIND | BEHIND | below | over it |
  | BEHIND | IN FRONT (always above) | above | behind it, whole - the castle's keep and towers too |
  | IN FRONT | IN FRONT | above | behind it, whole; its overlay (crown, face, flame) over the crab |
  | IN FRONT | IN FRONT | below | over it; its overlay keeps off the crab |
  | IN FRONT | BEHIND (always below) | below | over it |

  Against the GRASS the same rule (0.4.2, Strato's square: "the crab is
  climbing the volcano. it is being drawn OVER the sword plant incorrectly.
  The sword plant is in front of the volcano" - "ANYTHING that is layered
  above the volcano"). The first cut of the stack was a list of PIECES; the
  sword plant is grass (bed 3, ranked among the pieces) and the climb kept
  the crab's own swim depth against every frond - in front of them more
  often than not. ON A VOLCANO BEHIND THE CRAB IS AT THE VOLCANO'S DEPTH:
  the volcano is baked under the back grass, so every frond painted over it
  is over the crab on its flank (render.c crab_on_back_volcano, drawn at
  depth BEHIND whatever cr_gdepth says):

  | on a volcano BEHIND, against | the crab |
  |---|---|
  | the seagrass (always painted over a piece BEHIND) | behind every frond (on the approach, but one it still holds: below) |
  | the sword plant ranked above the volcano (IN FRONT OF / BEHIND THE SEAGRASS) | behind every leaf |
  | the sword plant ranked below the volcano (its leaves cut round the rock) | over the leaves, as the rock is - still behind the seagrass (crab_grass_part: the put-back takes the seagrass's pixels only; with the plant IN FRONT OF THE SEAGRASS and below the volcano the order is a loop, and the crab is over every leaf) |
  | a fish, the school, the seahorse, the snail, the urchin before the pieces BEHIND (L1-L4) | under them: first of L1 in the animal pass |
  | an animal behind the grass and the pieces BEHIND (L0) | over it |

  On a volcano IN FRONT it is in the front slot, over every frond (the
  volcano stands before them all), as before. `--selftest-crab-depth` (in
  `make check`: judged against frames with the grass taken out, never the
  renderer's masks - the five plant cases, either side, sent up FRONT /
  THROUGH / BEHIND, the grass live and from the loop; with each piece above
  the volcano, the plant above and below; a fish at each of the five layers
  and behind / through tall fronds; NO POP frame by frame from the first step
  to after it steps off, from the foot and across the sand, the plant above
  and below, a fish over it as it sets out, and never a frond held on the
  flank; the loop by the clock and LOCKED; the volcano moved IN FRONT
  mid-climb and mid-run-up; the climb on a
  volcano IN FRONT and the crab on the sand pixel for pixel as the control,
  PT_CLIMB_GRASS_OLD=1, which `make check` runs and requires to FAIL).
  Where the stack meets the lanes - the foot, the frame it starts up
  and the frame it steps back onto the sand - the change shows only if another
  piece stands over the crab, so the climb starts from the near foot unless
  another piece's footprint is over it and none over the far foot: then the
  far one (tank.c crab_climb_side; the lanes' own rule, a depth changes only
  where nothing shows it). Both feet covered (a castle centred over the
  volcano): the near one, and the crab goes behind the piece above the frame
  it starts up, as at a shelter's opening. Against the GRASS the change is
  never seen at all. THE VOLCANO'S DEPTH (tank.c crab_vdepth_tick, cr_vmode)
  is one answer from the approach through the climb and back down: under
  every frond painted over the volcano, over the leaves of a plant ranked
  below it. It is taken up as the walk to the foot begins and kept, back on
  the sand, while a leaf of a plant below the volcano is still in reach.
  And A FROND CHANGES SIDES ONLY WHILE THE CRAB IS NOT TOUCHING IT - THE HELD
  FRONDS (the first cut waited for sand clear of every bed, and a lush tank
  has none: the crab snapped behind the fronds the frame it started up; the
  second let the plant below flip twice): the fronds rooted within its reach
  as it takes the volcano's depth (cr_glo..cr_ghi) stay as its own depth had
  them - in front of the grass, under it; through, the odd ones over it;
  behind, over it, the plant below's leaves too - and each is let go as the
  crab walks clear of it (render.c crab_grass_mask: frond by frond). A crab's
  width of fronds takes ~96 px of walking to let go, so one nearer the foot
  than that walks a run-up first - out from the volcano, or along its foot
  where the glass is near (the bowl, the watch) - and comes back. THE CLIMB
  NEVER STARTS WITH A FROND HELD, nor in the front lane: at the foot it turns
  for another run-up (a fish was over it; a piece IN FRONT stood over its
  sand), four at most, then gives the climb up for ten minutes. After a
  climb on a volcano BEHIND it rests (a dig is the front lane's). The keeper
  moving the volcano IN FRONT under an approaching or climbing crab (a
  placement page: no animal is drawn while it is up) sends it on to the foot
  and leaves it in the front lane, before the rock.
  `--selftest-crab-climb` (in
  `make check`: the castle, the coral, the cluster and the pumpkin beside the
  volcano in every depth order and rank, either side, from the foot it takes
  and from the near foot - never over a piece above, never hidden by one
  below, the frame against the control's the same outside the pieces above
  (so the volcano, the lava as before; tall grass with the crab sent up
  FRONT / THROUGH / BEHIND its own leg); no jump onto or off the flank that
  the piece adds; the roosting, walking and passing siblings pixel for pixel
  as before; PT_CLIMB_STACK_OLD=1 the render's control, PT_CLIMB_FOOT_OLD=1
  the foot's), `--climb-bench` the frame cost.
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

## The castle's den and its stages (0.4.3)

- THE DEN. The castle's arch is a room painted with the keep: a back wall, a
  floor. It is in the castle's silhouette masks (g_castle_mask), so every rule
  that reads them treats it as stone: a frond of a bed the castle stands
  before is cut round it (no leaf of a sword plant ranked under a castle
  BEHIND lies over the crab in it: 0.4.3 item 16, Bananna's tank), an animal
  behind the whole castle does not show in it, the contact shadow does not
  fall in it. A plant ranked ABOVE the castle still lays its leaves over the
  den and the crab. A fish with the castle's front row before it still swims
  through the gate, between the den's wall and the front row.
- What came out with the open arch: the grass's early arch pass and the late
  pass's skip (g_veg_arch_pass), the fronds' tunnel shade (veg_arch_shade),
  the arch's place in the grass loop's key, and dep_draw's pass for the crab
  under an animal behind the whole castle. What stayed: the den's shade on
  its own walls (arch_shade in the bake).
- THE DEN'S SHADE ON AN ANIMAL (Strato 11-22). Only what is INSIDE the den
  takes it - never an animal in front of the gate - by how deep it is (the
  mouth barely, the back wall fully), and none with the den's lamp on (stage
  2 and up, the tank's light off), where the lamp lights it instead. The crab
  takes it in its own colours as it draws (draw_crab -> cr_den_tone, by
  cr_inn and cr_back: free); a fish passing between the gate and the keep is
  wrapped by den_begin/den_end in dep_draw (its own changed pixels in the
  opening). The end-of-frame arch_shade_dirty pass, which darkened every
  pixel drawn in the opening of a castle IN FRONT whatever drew it, is no
  longer called (PT_DEN_OLD=1 brings it back for the sim's comparisons). The
  crab goes INSIDE only with all of it within the arch (tank.c
  crab_at_opening: CR_ARCH_HALF - CR_HALF_W); until then it walks in its lane
  in front of the gate. Tests: --selftest-den-shade, --selftest-crab-walkin. The lantern's light through the gate and the windows
  went too (Strato 11-21: realistic - a lantern behind the castle is blocked,
  and does not show in the den): the castle's whole silhouette takes the
  spill, the sand before it is dark (pk_shade).
- THE STAGES. tank_t.castle_stage 1..3 picks the geometry (castle_whole) and
  the lights. The two silhouette masks hold ONE stage's shape; pieces_build
  refills them from the geometry alone (castle_shape_fill, no pixels) whenever
  this frame's tank has another stage, before anything reads them. The stage
  rides the scene's signature (sig.q of the castle) and so the grass loop's
  key; the loop is baked whole when it holds a castle of another shape
  (gl_window).
- THE LIGHTS are painted by the bake with the tank's light off (dim < 1):
  windows and the den's lamp from stage 2, the torches at stage 3. Nothing a
  frame, at any GRAPHICS level. A castle drawn live (its placement page after
  a change) paints them every frame of that page.
- `--selftest-den` (in `make check`, with its control PT_DEN_OPEN=1 that must
  fail A and B).

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

## THE OCTOPUS (a sim prototype, branch octopus-sim; docs/octopus.md sections 25 and 29)

Not a shop critter yet: one static animal behind `octopus_proto_set`, off in every tank and every other test. Its rows,
written before the code (BOARDS.md rule 6). It paints through `px_blend_s` like every animal, so `dep_draw`, the rank 8
restore and the arch's shade all see it.

| where it is | drawn at | against the grass | against the pieces |
|---|---|---|---|
| on the sand (it stands 5 px nearer than the crab: `OCTO_FLOOR_Y`) | the last of the animal pass, `dep_draw(FISH_DEPTH_FRONT, DEP_LANE)` | before the seagrass and the back beds; under the late fronds of a MIDDLE bed | before every piece BEHIND; behind every piece IN FRONT (their base is nearer still: rank 8 puts them back) |
| on the sand in its FRONT lane (4 px nearer still: going to or from a den in a piece IN FRONT; taken and left only where no piece IN FRONT is over it) | rank 9, `dep_draw(FISH_DEPTH_FRONT, DEP_LANE_F)` | before the seagrass; under the late fronds | before every piece, IN FRONT too |
| in a den BEHIND (the castle's arch, the volcano's cave) | rank 1, `dep_draw(FISH_DEPTH_BEHIND, DEP_NOBED)`, clipped to the opening | under every frond that stands before the piece | inside the opening only: never on the wall or the rock |
| in a castle IN FRONT's arch | rank 3, clipped to the arch | over the fronds seen through the arch | under the castle's front row (rank 8); in the tunnel's shade (rank 14) |
| in a volcano IN FRONT's cave | rank 9 (the front slot), clipped to the cave | under the late fronds | over the rock; **its lava and embers are not kept off it yet** (the crab's `g_guard` / `slot_covers`) |

`--selftest-octo-den` (in `make check`): the four dens at three sizes and the sand beside a coral IN FRONT, judged from
frames with the animal, the grass and the piece each taken out in turn, never from the renderer's own masks.

**Rows it does NOT have yet** (each needs its row and its matrix leg before it is called done):
- in `--selftest-stack` as a subject and as a passer (a fish, the shrimp, the snail, the urchin and the crab passing it);
- the walk through a door frame by frame (it changes from the sand's rule to the den's at the door: a pop is possible);
- the lane change frame by frame (it is by rule only where no piece IN FRONT is over it; not yet proved pixel for pixel);
- into a volcano IN FRONT's cave from behind the rock, where a tank has no clear sand: it comes forward out of the dark (not yet seen on a glass);
- the climb on the volcano and the castle (the crab's stack: at the piece's depth, whole);
- the other three worlds for the den test (it runs on the 1.8 build);
- one opening each with the crab is by rule only (it never picks the crab's shelter); the two are not yet tested together.

### Every rule the crab has, and where the octopus stands on it (11-21)

Strato, at the lava's glow drawn over the octopus in a volcano's cave: "this is an issue we fixed with the crab. make
sure to review all the rules the crab has for layering as they all likely apply." Each crab rule in this file, in order:

| the crab's rule | the octopus | proved by |
|---|---|---|
| In a shelter BEHIND: under every frond before the piece (rank 1, `DEP_NOBED`) | the same call | `--selftest-octo-den` 3 |
| In a shelter BEHIND with the grass LIVE (no mask): the back fronds re-woven over it | the live grass's own recorded mask (`g_fl_all`) for the same put-back, exact; the weave only as the last resort (the weave is a pixel off at frond edges, and this animal fills the opening) | octo-den 3 (the hero tank's grass is live there: it failed by ~190 px with the weave) |
| In a castle IN FRONT's arch: over the fronds seen through it, under the front row (rank 3 + rank 8), in the tunnel's shade (rank 14) | the same ranks | octo-den 2 (the opening); the shade by eye only |
| In a volcano IN FRONT's cave: the front slot, over the rock (rank 9) | the same | octo-den 2 |
| THE STACK: never painted under a piece ranked above its shelter (`crab_stack`, `cr_clipped`) | `octo_stack`, `oc_stacked` | octo-den 8 (a coral over the arch, castle BEHIND and IN FRONT) |
| The overlays of pieces IN FRONT keep off it in the front slot (`g_guard`, `slot_covers`): the lava, its embers, the lantern's face, a crown | `GUARD_OCTO`, `octo_covers`: in a den IN FRONT and in its front lane | octo-den 7, and its control `PT_OCTO_NOGUARD=1` FAILS |
| ... and a piece ranked above its shelter paints its overlay OVER it (the per-piece toggle) | the same toggle | octo-den 8 (a coral over each of the four dens, BEHIND and IN FRONT, above and below it) |
| The restore of a piece IN FRONT that stands behind its castle keeps off it (`g_rs_crab`) | `g_rs_octo` | octo-den 8, the pieces ranked below its den: it loses nothing to them (no cell there stages a castle IN FRONT with a piece IN FRONT below it: the hero tank ranks the coral above) |
| A fish before the pieces stays over it in a front volcano's cave (`hold`, `g_keep`) | it skips those pixels (`g_oc_keep`) | octo-den 9 |
| An animal behind its WHOLE castle does not show over it through the arch (`g_dp_crab`, the `dpc` mask) | `g_dp_octo`: its pixels put back | octo-den 10 (a castle IN FRONT) |
| An animal's put-back never erases it in the arch (`g_dp_crab_under`: a piece IN FRONT at or below its castle restores the crab, not the scene) | `g_dp_octo_under`, the same | octo-den 10 (the same cell: the octopus's pixels unchanged by the fish) |
| Out of a shelter IN FRONT: the front lane (never vanishing behind the piece) | its front lane, taken and left only on clear sand | octo-den 6 (seen at the door) |
| A lane or a depth changes only where nothing shows it; the held fronds | the lane: by rule (clear sand). It has no grass depth of its own yet: always before the grass | **not proved frame by frame** |
| THE CLIMB is in the piece's stack; never starts with a frond held | not built (docs/octopus.md 25) | - |
| Its sand dollar over the fronds (`cr_top`) | not built | - |
| The snail, the urchin, the shrimp, a fish passing it: the matrix | **NOT IN THE MATRIX.** Octo-den 11 holds the urchin in both lanes beside it on the sand (behind it in its back lane, before it in its front lane); the snail, the shrimp, the crab and a fish passing it on the sand are not staged | octo-den 11, weakly: the hero tank's cluster covers most of the octopus at that spot |
| Dimmed by how far back it is (`cr_back`) | dimmed by how far in | by eye |

**In from behind** (a den in a piece IN FRONT, no clear sand): a path the crab does not have. Through a castle's arch
it is seen all the way; in a cave it comes forward out of the dark. Tested only as "it gets in".

### The octopus on the sand and at the door: more rows proved (11-21, "finish the layering")

- **Its layer in the animal pass.** It was drawn at the END of the pass, over every fish. Now it is in the pass by its
  layer like the crab (before the grass, behind the pieces IN FRONT: L3, the last of it), so a fish that swims before the
  pieces is over it. `--selftest-octo-den` 12: a fish before the pieces over it; a fish behind the grass under it.
- **The snail and the crab beside it,** each in its back lane (behind it) and its front lane (before it): 13. The urchin: 11.
- **Before it means nearer: the front lane's floor line (0.4.3, Strato 11-23 on the square: "i could see the octopus tentacles
  from beneath the snail").** The front lane only changed the order. The critters' feet stayed on the sand's middle line
  (TANK_BOT - 14/15) while the octopus's arm tips reach TANK_BOT - 11, so its arms showed under the foot of the snail, crab or
  urchin drawn over it. Now a floor critter in its front lane stands on the front pieces' base line, DECOR_FY_FRONT (TANK_BOT - 8),
  on the open sand as well as in a piece's footprint: snail +6 px, urchin +6, crab +7, the same on every board. The lane is a
  height now, so a lane change waits until the octopus is not beside it (lane_animal_over). The octopus in its own front lane
  (TANK_BOT - 7) is drawn over them and still stands nearer. `--selftest-floor-octo`: three sizes, both of its lanes, the three
  critters crossing both ways; no pixel of the far animal under the near one's foot; their taps; the snail off the glass.
  Round 3 of the 0.4.3 audit (R3-L26..L28, L30): the line holds from the moment a critter appears. Bought, loaded or PUT BACK
  from the shelf, it stands on its lane's line at once (tank_floor_stand; the shelf keeps its lane): the crab sank 7 px after
  every load, and put back the snail hopped 4.6 px, the urchin rose 6. The snail off the glass stands up ON its front line (the
  flat picture reaches lower than the upright sole, so nothing jumps). The urchin's shelf poof is round its middle on that line.
  The shrimp: at food the school is IN FRONT / IN FRONT, drawn over the octopus on the sand, so over the octopus (its arms'
  reach, octopus_reach) a shrimp may go down to the same front line, its lowest row on DECOR_FY_FRONT, 4 px under the sand's
  line; it goes back up over the 12 px past the arms. A pellet there is its goal at that line. Elsewhere, and over the octopus
  in its own front lane or a den, nothing changes. `--selftest-floor-octo` has all four (loads, the shelf, the landing beside
  the octopus, the shrimp at three pellet spots and three sizes in both of its lanes, the poof).
- **Its lane changes only where nothing shows it:** 14. Going home to a volcano IN FRONT from clear sand and from under
  the rock (out, round, in), frame by frame: at the frame its lane flips, what is seen of it changes by 1% at most.
- **A castle IN FRONT raised above a coral IN FRONT** (the coral showing through the arch behind it): 8b.

**A shade to chase (found by 12, not a layer):** where a fish passes BEHIND the octopus on the sand, the octopus's
pixels over the hidden fish come out one shade lighter (240,120,72 for 232,116,64): the vignette's sweep and a pixel
two animals drew this frame. 346 pixels on the adult. Not checked: whether the crab shows the same.

**Still without rows:** the octopus in `--selftest-stack` itself (these are its own frame-judged cells, not that
matrix); the shrimp's school and the seahorse beside it; the door frame by frame (going in changes from the sand's
rule to the den's on one frame, by design, as approved on the prototype); the climb. Controls that must fail: the lava
guard only.

**Its camouflage changes its colours, never its layer.** It is drawn by the same call in the same place in every row
above. What it reads is the baked scene, so no animal passing behind or before it changes its colours.
