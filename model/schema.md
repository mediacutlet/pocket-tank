# State & Goal Schema — v2, FROZEN

**Status: v5 SHIPS (2026-10-07, model v5m; model v5j 2026-10-09 adds the jellyfish word; the species - the v5 section at the bottom,
docs/retrain-v5.md, docs/stats.md); v4, v3 and v2 remain readable by their kept
artifacts.** (v1 approved 2026-08-19; v2 personality/stage
fields approved 2026-08-20 for the progression layer — see `docs/progression.md`.) The tokenizer and
all training data depend on this exact encoding. Do not change field order,
vocabulary, or value ranges without regenerating every trace and retraining.
`gen_traces.py` implements this spec and must stay in sync.

**v2 change:** three identity fields inserted after `curiosity`:
`bold <0-9> social <0-9> stage <fry|juv|adult|elder>`. v1 traces (no identity
fields) are incompatible with v2 training; v2 datasets are generated fresh.

This is the browser prototype's perception packet (see the handoff doc §2) flattened to
a single lowercase, space-delimited line, plus its 8-goal enum. No new encoding was
invented; fields map 1:1 to the v2 packet.

---

## Tank model

- **4 fish:** `mira`, `bolt`, `kelp`, `nori`. The advisor is queried one fish at a
  time; the state line is egocentric to that fish (same single-shared-advisor shape
  as the prototype).
- **6 zones:** the tank (448×368 landscape) is a 3-wide × 2-tall grid, numbered
  left-to-right, top row first:

  ```
  1 2 3
  4 5 6
  ```

- **Distances** are bucketed exactly as the prototype: `none` / `near` / `mid` / `far`.
- **Directions** are clock positions `1`–`12` relative to the fish's heading
  (`12` = dead ahead, `6` = behind), as in the prototype.
- **Drives** are integers `0`–`9` (prototype used 0–10; clamped to a single digit so
  every value is one short token).

## State encoding (model input, one line)

Fixed field order. A sighting is `<key> <dist> <clock>`, or `<key> none` when absent.

```
fish <name> zone <1-6> hunger <0-9> energy <0-9> stress <0-9> curiosity <0-9>
bold <0-9> social <0-9> stage fry|juv|adult|elder
food <dist> <clock>|none shadow <dist> <clock>|none friend <name> <dist> <clock>|none
bubble <dist> <clock>|none reef <dist> <clock>|none wall <dist> <clock>|clear
last <goal> time day|night
```

(Line breaks above are for readability only; the real encoding is one line.)

Example:

```
fish mira zone 2 hunger 7 energy 5 stress 2 curiosity 8 bold 4 social 6 stage adult food near 12 shadow far 6 friend bolt mid 3 bubble mid 10 reef far 7 wall clear last explore time day
```

Identity fields (v2): `bold`/`social` are slow-drifting personality traits
(progression layer); `stage` is the life stage. The model conditions on them —
one model expresses every personality and age.

Field notes:

- `friend` reports only the **nearest** other fish (`friend none` if none within `far`).
- `wall` is `clear` or the nearest wall when within `near`/`mid` (e.g. `wall near 9`).
- `last` is the fish's current goal (hysteresis signal, mirrors the prototype's `LAST:` line).
- `time` covers the day/night cycle called out in the brief's edge cases.

Worst case ~30 space-delimited words — comfortably inside the brief's 40–60 token input
budget even with a naive tokenizer.

## Goal output (model output, one line)

The prototype's 8-goal enum, lowercased, plus an urgency digit:

```
<goal> urgency <0-9>
```

where `<goal>` ∈

```
seek_food  flee_shadow  visit_bubbles  follow_friend
explore    rest         dart_play      inspect_reef
```

Example: `seek_food urgency 8`

The enum fuses intent+target (per the prototype); the reflex layer resolves the concrete
target (nearest pellet, nearest friend, etc.). Urgency scales steering gain / speed.
The on-device parser stays tolerant: unknown goal → keep previous goal; missing urgency → 5.

## Trace format (JSONL, one object per line)

```json
{"state": "fish mira zone 2 ... time day", "goal": "seek_food urgency 8"}
```

Ollama is queried with structured output constraining the reply to
`{"goal": <enum>, "urgency": 0-9}`, which gen_traces.py renders into the goal line.

**Training serialization** (a tokenizer-stage detail, not part of the frozen
encoding): each trace becomes the document `<BOS>state -> goal`, documents
concatenated BOS-separated. At inference the prompt is `state ->` and the model
completes the goal, then emits BOS (stop). The tokenizer is **word-level**: one
token per whitespace word of this closed vocabulary (58 ids incl. specials, see
`train_tokenizer.py`) — a v2 state+goal doc is ~46 tokens. It replaced the
original char-level tokenizer (vocab 355, ~200 tokens/doc) on 2026-08-21 because
decision latency scales with token count (~6s/decision at the ESP32's 30 tok/s
target with char tokens; ~1.5s with words). Encoding is split-and-lookup in C
(`model/runw.c`, `sim/advisor_llm.c`); llama2.c's BPE encoder is not used.

## Decisions locked at approval

- Zones: 6, as a 3×2 grid matching the landscape aspect.
- `wall` stays as a field (`clear` or `<dist> <clock>` when within mid range).
- Drives are single-digit 0–9 (not the prototype's 0–10).
- Only the nearest friend is reported, by name.
- Urgency is a single digit 0–9.


---

## v3 — SHIPPED 2026-08-22 (model v3m; see docs/stats.md "Schema v3 data cycle")

Three changes, one data cycle. Encoders: `gen_traces.py --schema 3` and
`common/llm/advisor_core.c` (selected automatically when the loaded tokenizer
contains ` trust`). Tokenizer: `train_tokenizer.py --schema 3` → vocab **54**.

1. **Fish names dropped** (`fish <name>` prefix and the `friend <name>` token).
   Measured: names carry zero decision signal (model/probe_dist.py), and the
   population cap (6) outgrew the four trained name tokens.
2. **`trust <0-9>`** inserted after `stage`: the keeper relationship (reflex
   layer trust 0..10, clamped to one digit). Teacher prompt gains one sentence
   (shades calm/flight/surface comfort; never overrides hunger or a predator).
3. **Data coverage**: tanks of 2–6 fish; `shadow_calm` (shadow in view, stress
   0–3) and `lonely` (`friend none`) perturbations.

```
zone <1-6> hunger <0-9> energy <0-9> stress <0-9> curiosity <0-9>
bold <0-9> social <0-9> stage fry|juv|adult|elder trust <0-9>
food <dist> <clock>|none shadow <dist> <clock>|none friend <dist> <clock>|none
bubble <dist> <clock>|none reef <dist> <clock>|none wall <dist> <clock>|clear
last <goal> time day|night
```

Example:

```
zone 2 hunger 7 energy 5 stress 2 curiosity 8 bold 4 social 6 stage adult trust 5 food near 12 shadow far 6 friend mid 3 bubble mid 10 reef far 7 wall clear last explore time day
```

Goal output unchanged. v2 data converts mechanically to the v3 line (drop `fish
<name>` and the friend name, insert `trust 5`): the shipped v3m model trains on
v2-converted + v3 (`AQUA_PETS_SCHEMA=3 train.py`). **v3 is now the encoding the sim
and firmware emit** (`common/llm/advisor_core.c`, selected by the ` trust` token in
the loaded tokenizer); v2 remains readable by the v2 artifacts kept in model/out
(`*_v2.bin`).

---

## v4 — SHIPPED 2026-09-15 (model v4m; the boredom cycle; shadow out, `bored` in)

Two changes, one data cycle. Encoders: `gen_traces.py --schema 4` and
`common/llm/advisor_core.c` (selected automatically when the loaded tokenizer
contains ` bored`). Tokenizer: `train_tokenizer.py --schema 4` → vocab **54**
(one word out, one in).

1. **`shadow` dropped.** The predator left the game on 2026-09-13 (the state
   line has read `shadow none` ever since). The `flee_shadow` goal token STAYS
   in the lexicon - it is never a v4 label, so the student learns ~0 for it -
   because `tank.c` keeps `GOAL_FLEE_SHADOW` and `advisor_core_init` wants a
   token id for every goal; one idle embedding row beats a C special case.
2. **`bored <0-9>`** inserted after `trust`: how stale the fish's current
   pastime is (reflex layer `fish_t.bored` 0..10, clamped). Rises while the
   fish keeps the same leisure goal (a minute to the top), relieved by a goal
   that is not the one just left (BORED_NEW_GOAL) or by entering a zone unseen
   for 20 s (BORED_NEW_ZONE); eating and night rest never bore; a night's sleep
   resets it. Its band is in the re-ask signature. Why: the device's fish sat
   in follow-the-friend and bubble-column loops (Strato, 2026-09-14) - the
   teacher's own labels put visit_bubbles at 50-67% of every content state,
   and nothing in the line said "you have been doing this for ages".

```
zone <1-6> hunger <0-9> energy <0-9> stress <0-9> curiosity <0-9>
bold <0-9> social <0-9> stage fry|juv|adult|elder trust <0-9> bored <0-9>
food <dist> <clock>|none friend <dist> <clock>|none
bubble <dist> <clock>|none reef <dist> <clock>|none wall <dist> <clock>|clear
last <goal> time day|night
```

Example:

```
zone 2 hunger 7 energy 5 stress 2 curiosity 8 bold 4 social 6 stage adult trust 5 bored 3 food near 12 friend mid 3 bubble mid 10 reef far 7 wall clear last explore time day
```

Goal output unchanged (`<goal> urgency <0-9>`; `flee_shadow` never emitted by
the v4 teacher - its JSON enum excludes it). Old data converts mechanically
(`convert_to_v4.py`): names / friend name dropped, `trust 5` where missing, the
shadow field removed, every pair with a shadow IN VIEW or a flee label dropped
(28,247 of the 51,162 v2+v3 pairs survive), `bored 0-2` inserted - those labels
were made under "keep the last goal while it makes sense", the fresh-fish rule.
Bored 3-9 behaviour comes only from v4 teacher data. Runbook: docs/retrain-v4.md.

---

## v5 — SHIPPED 2026-10-07 (model v5m; v5j 2026-10-09 with the jellyfish; species; docs/species.md)

One change. Encoders: `gen_traces.py --schema 5` (`render_v5` / `parse_v5`, the
field list `V5_FIELDS`) and `common/llm/advisor_core.c` (selected automatically
when the loaded tokenizer contains ` species`). Tokenizer: `train_tokenizer.py
--schema 5` → vocab **65**: the v4 54 + `species` + ten species words, APPENDED
(ids 0..53 are v4's, so a v5 tokenizer read against a v4 model's 54 ids is
exactly the v4 vocab and the advisor stays v4).

1. **`species <word>`** inserted after `stage` (the traits read together):
   `SPECIES[f->species].token` - `fish` (the classic fish) `seahorse octopus
   puffer angler eel shark squid crab lobster`. Why: nine creatures joined the
   fish (2026-10-05); each has a real animal's temperament (the anglerfish
   lies in wait, the hammerhead never stops, the lobster wakes at night) that
   no trait number carries.

```
zone <1-6> hunger <0-9> energy <0-9> stress <0-9> curiosity <0-9>
bold <0-9> social <0-9> stage fry|juv|adult|elder species <species> trust <0-9> bored <0-9>
food <dist> <clock>|none friend <dist> <clock>|none
bubble <dist> <clock>|none reef <dist> <clock>|none wall <dist> <clock>|clear
last <goal> time day|night
```

Example:

```
zone 5 hunger 2 energy 7 stress 0 curiosity 3 bold 6 social 1 stage adult species angler trust 5 bored 1 food none friend mid 3 bubble far 10 reef mid 7 wall near 6 last rest time day
```

**2026-10-09, the jellyfish:** one more species word, `jellyfish`, APPENDED as
id 65 (vocab 66; `train_tokenizer.py --schema 5` writes it). The line, the
field order and every other id are unchanged. The C advisor sends `species
jellyfish` only when the loaded tokenizer has the word (v5m's 65-word vocab
hears `species fish` for it), so a tokenizer may again go out before its model.

At most 41 words (v4: 39): a prompt of 43 tokens with `->` and BOS, 46 with
the reply, of the 64-position KV cache. `friend` is still the nearest other
creature of any species. Goal output unchanged. Old data converts mechanically
(`convert_to_v5.py`): every older label was about the classic fish, so it gains
`species fish` (v2 / v3 lines go through `convert_to_v4.to_v4` first). The C
and Python lines are checked against each other by `./fishsim
--selftest-encoder <v5 tokenizer> | model/encoder_agree.py` (in CI). Runbook:
docs/retrain-v5.md.
