# Display themes

Open **Settings → SETTINGS / THEMES** (the title button, in every theme). Tap Original, Quiet Lagoon, Tidepool Club or Blackwater. The selection applies immediately and is saved; Done returns to settings. The four tiles share the page above DONE (a 62 px pitch at most: 53 on the 1.8, 47 on the bowl).

Original is the default for new tanks and existing saves. It keeps the original tank art, fonts, menus and character wheel, with a theme entry added to the settings heading. Quiet Lagoon implements the approved Living Lagoon direction with layered teal water, shaded naturalistic creatures, soft baked light shafts and dark-glass controls. Tidepool Club uses pale water, cream panels, green controls and bolder lettering. Blackwater (2026-10-10) is the realistic one: near-black water over dark volcanic sand, the creatures drawn as the real animals in vivid colours, a Mayan step pyramid and a fighter-plane wreck (its own section below). All four retain the live procedural creatures, their growth stages, animations and individual saved colours.

Since 2026-10-10 the two new themes run about 20 % darker than their design studies
(Quiet Lagoon's water, sand and panels scaled by 0.8; Tidepool Club's by 0.82 / 0.78 / 0.72
on red / green / blue so it keeps its warmth; that evening the club's water went another 16 % and its sand 12 % darker, so the creatures and decorations pop against it): an AMOLED spends power on every lit pixel,
and a bright water field shown all day risks burn-in. Text, accents and gold are unchanged.
Also since that day a lagoon creature keeps its body through a turn: `lagoon_volume` draws
a shaded cross-section that widens as it faces the glass (the species sheet's turns page,
`fishsim --theme 1 --species-sheet <prefix>`, shows every creature side-on, mid-turn and head-on).

The modern settings and seven-character naming wheel use device-specific safe bounds for the 448×368 rectangle, 466×466 circular pendant and 410×502 watch. Naming still uses the original character roll: tap a slot, swipe vertically or tap its arrows, then Done; Cancel restores the previous name.

## Assets and rebuilds

`assets/themes/quiet-lagoon/` and `assets/themes/tidepool-club/` contain editable SVGs and native-size RGBA PNGs. Each theme includes 50 UI/creature icons and a picker fish. `assets/themes/blackwater/` holds only its picker fish and its wreck / frogman thumbnails: a full icon set and font atlas cost ~160 KB of flash each and the app slot has ~120 KB left, so Blackwater draws its icons and text with the lagoon's set (`theme_asset_set()` in theme.c; the jellyfish thumbnail is the lagoon's moon jelly too). The bundled DejaVu fonts and their license produce four fixed-cell antialiased glyph atlases. Existing Original assets are untouched.

Regenerate with `python3 tools/gen_theme_assets.py`; verify with `python3 tools/gen_theme_assets.py --check`. Generation requires Pillow, librsvg-2 and libcairo. It does not require network access. Firmware and simulator use the checked-in `common/theme_assets.c`, so normal builds need none of those generation dependencies. Pixel/font arrays consume 325,176 bytes of flash; there is no runtime SVG or font engine.

The persisted theme ID is appended at byte 3592 of the save, leaving every previous field at its original offset. Missing/unknown IDs fall back to Original. Do not reorder the IDs in `common/theme.h`.

## Verification

`make -C sim check-all` runs the shared-code tests for all three device layouts, including `--selftest-themes`: touch routing, circle-safe settings bounds, character rolling, save migration, persistence, asset dimensions and cache invalidation when restoring Original. Run `./sim/fishsim-round --selftest-themes /tmp/pendant` to export native PPM screenshots; equivalent options are available on `fishsim` and `fishsim-watch`.

Run `node tools/export_living_lagoon_art.cjs` before regeneration when updating the approved Lagoon shop thumbnails from its HTML vector sources. Lagoon uses DejaVu Sans; Tidepool retains DejaVu Sans Mono Bold. Native body shading uses a 24-tone light ramp and bounded scanline spans, retaining dirty-region tracking and night dimming without per-frame allocation.

The native review gallery is `docs/design/implemented-themes.html`; the earlier interactive concept is `docs/design/aqua-pets-preview.html`. Simulator checks do not replace an ESP-IDF firmware build or a visual/touch check on physical hardware before release.

## Native art and occasional behaviours

Both modern themes now render their own live procedural artwork, rather than only
tinting the original creatures. `common/living_lagoon.inc` contains the ten shaded Lagoon species;
`common/theme_creatures.inc` contains the Tidepool species; the classic fish, snail, shrimp, urchin, vegetation, decorations,
food, bubbles, battery and menu treatments live alongside their Original paths in
`common/render.c`. They need no additional runtime bitmap allocation.

| Element | Quiet Lagoon | Tidepool Club | Blackwater (2026-10-10) |
|---|---|---|---|
| Creatures | Shaded natural profiles, fin rays, scales, gills and shell detail | Rounder bodies, spots, bold fins and claws | The real animals: countershaded bodies with a specular band, translucent rayed fins, real eyes, scale lattices, gill covers, suckers, segmented shells, antennae; colours lifted to vivid |
| Sword plant / weeds | Veined leaves and delicate stems | Scalloped leaves and fuller blades | Dark kelp greens (the lagoon's veined leaves in the dark palette) |
| Castle | A French chateau (2026-10-10): a limestone facade with two pepper-pot corner towers under conical slate roofs, a main block with two rows of tall windows and a balustrade, a taller central pavilion with quoins, a hipped slate roof and a spire, dormers, chimneys, the grand arched entrance with a tall window and an oculus above it | A fat single pineapple castle (2026-10-10): one wide pineapple in a diamond skin with an eye in every cell, a swept crown of eleven leaves with a pennant in it, three pineapple-slice ring windows that are holes the fish pass behind, the arched gate with a drawbridge on two chains, a starfish on the mound | A sunken Mayan step pyramid in pale limestone blocks with dark joints (redrawn the same night to read at a glance): receding tiers each with a bright ledge, a lit central stair between dark ramps up to a temple house with a dark doorway and a roof comb, a bold carved band per tier, moss in the lower joints, lit voussoirs round the shared arched gate |
| Shipwreck (2026-10-10) | An old sailing ship listing on the sand: the stern sunk and its ribs bared, the bow risen with a bowsprit, two holes in the side, a tilted broken mast with a yard and a rag of sail, a stay, a deckhouse, an anchor off the bow | A cheerful upright tug: cream topsides, a red boot stripe, a teal bottom, brass-rimmed porthole holes, a wheelhouse with lit windows, an orange funnel, a cyan pennant, a life ring, an anchor off the bow | A bare-aluminium WWII fighter on the sand, nose up a little, every piece outlined: the near wing down on the sand with a big blue-white-red roundel, the far wing rising behind, a fin with a red rudder and a white stripe, a three-blade propeller with yellow tips, a yellow band aft, a dark cowl mouth and a pale spinner; the cockpit (a pale frame ring) and a ragged gap aft are the two holes |
| Submarine (2026-10-10 night) | A moss-green navy boat with brass fittings | A red-and-white toy with a cyan tower and gold propeller | A grey steel boat with a black waterline and rust (Original: the yellow sub with black trim) |
| Frogman (2026-10-10) | Turquoise suit with coral bands, violet fins, a cream tank, a green mask | Hot pink suit with lime bands, cyan fins, an orange tank, a purple mask (Original: yellow with black bands, red fins) | Black neoprene, high-vis amber bands and fins, an aluminium tank, a black mask |
| Coral / reef | The keeper's coral / tube / brain hues on a dark basalt rock (2026-10-10; the first cut pulled every piece toward the water's tone and read as twigs and slabs): a ridged brain, streaked tubes, deep rims | Thick finger coral, stacked sponges, dotted dome | Its own reef (redrawn the same night): a deep-red mesh sea fan on a stalk at the left, four tube sponges with white mouth rims, a big staghorn coral with ivory tips in the middle, an anemone with pale-tipped tentacles at the right, on mid-grey basalt with a mossy crown; the keeper's coral / tube / brain hues stay the inputs |
| Snail / companions | Warm shaded shell whorls, soft shrimp, fine urchin spines | Warm striped shell, segmented shrimp, rounded urchin tips | The lagoon's shaded snail; the Original's pixel cherry shrimp (legs, antennae, tail fan) and fine-spined urchin |
| Food / bubbles | Small flakes and pearl rings | Round tablets and outlined bubbles | Tan flakes with a lit edge and a crease; real bubbles - a thin rim, a dark glassy inside, the sun's window top-left |
| Battery / menus | Liquid gauge; the settings page is the one shared layout (2026-10-10) in the lagoon's dark-glass greens and its font | Four-cell gauge; the same shared settings layout in the club's cream and teal with its pill-shaped buttons | Liquid gauge; the shared layout in near-black panels with cyan controls, the lagoon's font |
| Ink / electricity | Black clouds with a whisper of green, fine mint arcs | Black cloud lobes with a whisper of blue, gold zigzags | Black clouds, pale arcs |

All themes share the same behaviour. Lobsters, crabs, anglerfish, octopuses and
pufferfish occasionally visit the upper water. Pufferfish inflate for their trip.
Squid and octopuses squirt a black ink cloud every 50-120 idle seconds (the octopus
makes every third idle turn a surface trip instead); eels spark. Ink is black in
every theme since 2026-10-10 - the tinted clouds read as water.
The first eligible idle event is staggered by creature (45–124 seconds), then
waits 100–210 eligible idle seconds between events. Surface trips last up to
40 seconds followed by an 18-second return phase. Rest, night, urgent hunger,
flight, naming, touch and active spawning take priority. These are stylized game
behaviours, not aquarium-care guidance.

The scheduling fields are transient and do not change the persisted save layout.
`--selftest-themes` checks actual surface arrival, trip completion, puff/ink/spark
activation, naming/hunger priority, and restoration of decoration geometry after
switching themes. The original species tests isolate baseline locomotion by
pausing these optional episodes. The native gallery includes all ten species,
decorations, companions, battery states and animated effect samples.

## Jellyfish (2026-10-09)

Jellyfish is species ID 10, appended after lobster. Buy one juvenile for 10 sand
dollars from **Upgrades → page 5 → Jellyfish**. Two of the species can breed.
The four designs are Peach, Pearl, Rose and Blue; naming uses the existing wheel.
Original follows the approved flat bell/white-eye design, Quiet Lagoon uses a
translucent moon jelly, and Tidepool Club uses a scalloped bell and ribbon arms.
As with every other creature, the design's saved body, arm and marking colours are
the inputs in all three themes: the theme changes the material (flat, translucent,
scalloped) and applies the same light tint the other species get, so a Rose or Blue
jellyfish stays recognisable in Quiet Lagoon and Tidepool Club and matches its card.
The shared native pulse phase drives locomotion, bell contraction and the rise: each
contraction lifts the jellyfish a few pixels and it settles as the bell opens, the
arms trailing with a slight lag. Rest slows the pulse, flight accelerates it. The
drawing is centred on the whole body (bell plus arms), so selection rings, cards
and the milestones rows frame it. Jellyfish have no ink or electric effects.

`common/jellyfish.inc` is the live geometry. Shop thumbnails are generated from
the approved HTML study by `python3 tools/gen_jellyfish_assets.py` (or `--check`).
The wreck's, the frogman's and the submarine's thumbnails (one per theme, matching the four
hulls, the four suits and the four subs) come from `tools/gen_wreck_assets.py`,
`tools/gen_frogman_assets.py` and `tools/gen_sub_assets.py`.
The three thumbnails add 9,216 bytes of pixel data. Existing persisted IDs and
save offsets are unchanged; the shop presence flag uses the next unused bit.

The model knows the jellyfish since **v5j** (docs/species.md "The model"):
the word `jellyfish` is id 65 of the 66-word tokenizer, the teacher prompt has
a jellyfish sentence, and the student was retrained on the v5 data plus a
jellyfish-focused run. A board still on v5m (65 words) hears a jellyfish as
`species fish` until it is flashed; the encoder self-test covers both.

## Blackwater (2026-10-10)

Alvin asked for "a realistic version of the creatures", darker water and sand, brighter creatures,
and decorations unlike the other themes'. Blackwater is the diver's name for exactly that look - black water, vivid life.

**The light** was a physics-based caustic pass (four deep-water waves, every ray refracted and
deposited, ten depth bands, a net on the sand, a mirror line) for a few hours on 2026-10-10;
Alvin had it removed the same night ("remove the moving water reflective light feature"). The
water is now a plain dark gradient like the other themes'; the code is gone (commit history has it).

**The look:** water 0x0a2434 → 0x02080c, dark volcanic sand with shell grit and the odd
rounded pebble, cyan accents, amber gold, a 120-strength vignette. `theme_creature_color` lifts
every saved creature colour so its brightest channel is ~240 and pushes the saturation a
quarter: the keeper's hue, vivid. The creatures live in `common/blackwater.inc`, the pyramid
in `common/blackwater_castle.inc`, the plane in `common/blackwater_wreck.inc`. The theme
test adds a `night` shot for every theme.
