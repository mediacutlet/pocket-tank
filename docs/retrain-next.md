# Queued for the next training run

Things the model should learn at its next retrain. Each changes what the
model SEES (the state line, schema.md) or what it may CHOOSE, so none of
them can ship as a firmware-only change: the model was trained on an exact
format, and an unseen token gets unpredictable answers. Batch them into one
schema bump; run `model/prompt_check.py` before any overnight generation
(see retrain-v4.md for the last run's recipe and acceptance numbers).

**The next run is schema v5 (species), built and waiting for the teacher:
docs/retrain-v5.md.** The shrimp below did NOT go into v5 (2026-10-05): its
open question (a new goal, or an existing one at the school?) and its
acceptance numbers are still undecided. Settle both before the v5 night and it
can ride along (a `shrimp <N> out` field, 3 tokens: 49 of the 64 KV positions,
retrain-v5.md "The shrimp"); otherwise it waits for v6.

## 1. Shrimp to watch (2026-09-29, Strato)

"Mark this for a future training run: something for the fish to watch. A
short `shrimp: 6 out` line in what the model sees, so fish can choose to
watch them as a cure for boredom."

- Depends on the shrimp school shipping first (shop item, 300 SD; see the
  shrimp notes in HANDOFF.md when it lands). *(It has: SD_ITEM_SHRIMP,
  common/tank.h "the shrimp school". There is no "hiding" state in shrimp_t
  yet, so "out of cover" also needs defining - e.g. not inside the grass
  canopy.)*
- The token: `shrimp N out` (N = shrimp out of cover, 0 when none are owned
  or all are hiding) - short, one line, near the other sightings.
- What it should do: a bored fish with shrimp out sometimes picks a
  watch/visit behaviour near the school; a fish with bored 0-2 mostly
  ignores it. Decide whether that is a new goal (a schema change to the
  goal list too) or an existing one (inspect_reef-style lane at the school).
- Acceptance to write before generating: P(watching | bored high, shrimp
  out) up, P(anything else) roughly unchanged vs the v4 baseline, no
  flattening of personality (the v3 lesson).
