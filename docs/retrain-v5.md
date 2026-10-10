# Schema v5 retrain (species) - runbook (prepared 2026-10-05; run 2026-10-06/07, v5m SHIPPED - results in docs/stats.md)

**Do not start this without Strato's go: the Mac Mini Ollama (192.168.0.139)
is shared production.** Everything below is built and was run end to end
with a stand-in rule teacher (see "Smoke test" at the bottom); only the
teacher data is missing.

## Why v5

Nine creatures joined the classic fish (docs/species.md): seahorse, octopus,
pufferfish, anglerfish, electric eel, hammerhead, squid, crab, lobster. The
shipped v4 model cannot see a species - an unknown word would be `<unk>`,
which it never trained on - so today every creature is decided as a fish
with its species' traits (an anglerfish is a bold, unsocial, incurious fish;
a seahorse a timid, social one). That is a good start but not the animal:
an anglerfish should lie in wait, a hammerhead should never stop
patrolling, a lobster should wake at night. v5 tells the model the species
and the teacher what each species is like.

## The v5 line

v4 + `species <word>` right after `stage`, so the traits read together:

```
zone <1-6> hunger <0-9> energy <0-9> stress <0-9> curiosity <0-9>
bold <0-9> social <0-9> stage fry|juv|adult|elder species <species> trust <0-9> bored <0-9>
food <dist> <clock>|none friend <dist> <clock>|none
bubble <dist> <clock>|none reef <dist> <clock>|none wall <dist> <clock>|clear
last <goal> time day|night
```

`<species>` is `SPECIES[f->species].token` (common/tank.c): `fish` (the
classic fish) `seahorse octopus puffer angler eel shark squid crab lobster`.
Example (an anglerfish lying in wait on the floor):

```
zone 5 hunger 2 energy 7 stress 0 curiosity 3 bold 6 social 1 stage adult species angler trust 5 bored 1 food none friend mid 3 bubble far 10 reef mid 7 wall near 6 last rest time day
```

- **Vocab 65** (`train_tokenizer.py --schema 5`): the v4 54 + `species` + the
  ten species words, APPENDED (ids 0..53 are exactly v4's, 54..64 new).
- **Length:** at most 41 words (v4: 39); with `->` and BOS a prompt is at
  most 43 tokens, + 3 generated = 46 of the 64-position KV cache
  (docs/memory_budget.md) and under advisor_core_infer's 60-token cap.
  Measured prefill: 42 vs 40 tokens for the same state, ~5% more per decision
  (~3.7 s -> ~3.9 s on the device; host q4_host 49 -> 52 ms).
- **Goals and reply unchanged** (`<goal> urgency <0-9>`; the teacher's JSON
  enum is the v4 one, no flee_shadow).
- **Detection:** `advisor_core_init` picks v5 when the tokenizer has
  ` species` (and ` bored`). The v4 model and tokenizer keep the v4 line
  byte for byte (the sim's LLM warm-up decisions are identical to the
  pre-v5 build's).
- **Mixed files fail safe one way:** a v5 tokenizer with the v4 model reads
  only the model's 54 ids - exactly the v4 vocab - and runs as v4 (checked:
  `AQUA_PETS_TOKENIZER=tokenizer_v5.bin ./fishsim --selftest-llm` -> "schema
  v4"). The other way - a v4 tokenizer with a v5 model - is refused ("bad
  model/tokenizer", advisor off). So the firmware's tokenizer may go out
  before the model, never after it.

### The shrimp (docs/retrain-next.md #1) - NOT batched

It stays queued. The shrimp school has shipped, but the item still has an
open design question it asks to settle first - a new goal (a schema change
to the goal list, the C enum, the reflex layer) or an existing one used near
the school - and its acceptance numbers are "to write before generating".
Neither is decided, and choosing a new goal is a game-design call, not a
data-pipeline one. If it is settled before this run, it is a small add on
top: a `shrimp <N> out` field next to the sightings (3 more tokens: 49 of 64
still fit), its words in `lexicon_for(5)`, the field in `render_v5` /
`advisor_core_encode` / `encoder_agree.py`, and a panel case in
prompt_check.

## What is built (all in this tree)

| piece | what |
|---|---|
| `common/llm/advisor_core.c` | v5 detection + encoding; `advisor_core_init_encoder` (tokenizer only, for tests); `advisor_core_unknown_words` |
| `sim/main.c` | `--selftest-encoder [tok]` (every species, every word known, `species` only for v5; in `make check`); `--selftest-llm` takes `AQUA_PETS_MODEL` / `AQUA_PETS_TOKENIZER` and, with a v5 model, adds anglerfish, squid and crab pairs |
| `model/train_tokenizer.py` | `--schema 5`, `SPECIES_WORDS` |
| `model/gen_traces.py` | `--schema 5`: tanks of 2-10 creatures, species in pairs (the shop sells pairs), the classic fish ~1/3 (`--fish-share`), the new nine from a shuffled bag; traits rolled inside each species' range (tank.c, widened 0.1 as inheritance can); curiosity drawn to the species' value; species situations (anglerfish / crab / lobster / octopus on the floor with food drifting down, seahorse in the grass, the eel rising for air, the hammerhead never stopping, squid with a squid close by, the night hunters' nights). `SYSTEM_PROMPT_V5` = the v4 prompt verbatim + a species paragraph. `--teacher rules` (a rule policy, smoke tests only). `num_ctx` 2048 for v5 (the v5 prompt is ~1,400 tokens; a smaller window makes Ollama cut the prompt's middle) and a one-time warning if a reply says the prompt filled the window |
| `model/convert_to_v5.py` | every older pair as `species fish` (v2/v3 go through `convert_to_v4.to_v4` first) |
| `model/encoder_agree.py` | the C lines vs gen_traces' `render_v5` / `parse_v5`, value domains, closed vocabulary, all ten species; in CI |
| `model/fold_traces.py` | `--schema 5`: share and top goals per species |
| `model/prompt_check.py` | `--schema 5`: the v4 panel (as `species fish`) + one case per species; `--teacher rules` dry run |
| `model/eval.py` | `--schema 5`: agreement per species, `--min-per-species`, `--accept` (exit 1 below it), `--teacher-kind rules` |
| `model/probe_dist.py` | `--schema 5`: the species panel (content day, night, starving, friend near per species) |
| `model/run_v5_overnight.sh` | the night, as run_v4_overnight.sh |
| `model/export_q4.py` | refuses a dim / FFN size the device engine would refuse (multiples of 64) |

## 0. Preflight (~20 min of teacher time, the required gate)

```bash
cd "/Volumes/Local/Projects/LLM Fish Tank/aqua-pets/model"
python3 gen_traces.py --count 10 --dry-run --schema 5 --seed 1          # v5 lines print, no network
python3 train_tokenizer.py --schema 5                                   # out/tokenizer_v5.bin, vocab 65
(cd ../sim && make && ./fishsim --selftest-encoder ../model/out/tokenizer_v5.bin) | python3 encoder_agree.py
curl -s http://192.168.0.139:11434/api/tags | grep -o gemma4:26b       # teacher reachable
python3 prompt_check.py --schema 5 --workers 2 | tee out/prompt_check_v5.log   # 790 calls
```

(Or `--host http://localhost:11434` for this machine's own gemma4:26b, as the
2026-09-14 checks did.) Watch stderr for `[warn] the prompt used N of
num_ctx 2048 tokens` - it must not appear; if it does, raise `num_ctx()` in
gen_traces.py. prompt_check now prints `teacher prompt: up to N tokens
evaluated` - expect ~1,300-1,500 for v5.

**Read this before comparing with v4:** the v2-v4 runs asked Ollama for
`num_ctx 512`, but the v4 prompt is ~760 tokens by a chars/4 estimate (v3
~570). If Ollama kept the first and last tokens and cut the middle there,
the v4 labels were made from a partial prompt. Not verifiable from here;
one call settles it: `python3 prompt_check.py --schema 4 --n 4 --workers 1`
and read the `teacher prompt:` line (512 or just under = it was cut). If
it was, the v5 teacher (2048, the whole prompt) may label the classic fish
differently from the v4 teacher - which is what the "v4 checks did not
move" comparison below is for. num_ctx is a per-request option; a value
other clients do not use may make Ollama reload the model (~12 s) when
requests alternate, so tell whoever else uses the shared host.

The v5 panel keeps every v4 check (the classic fish's social / bold cliffs,
starving, lonely, boredom, night, flee 0, the content attractor) and adds:

| check | want | why |
|---|---|---|
| P(rest \| angler, content day) | >= 0.40 | it lies in wait |
| P(rest \| shark, content day) | <= 0.10 | it never stops |
| P(explore \| shark, content day) | >= 0.40 | it patrols |
| P(follow \| squid, friend near) | >= 0.40 | the school |
| P(reef + explore \| octopus) | >= 0.60 | the curious loner |
| P(follow \| octopus) | <= 0.05 | |
| P(dart \| seahorse, energetic) | <= 0.03 | a poor swimmer |
| P(explore + reef \| puffer) | >= 0.50 | it pokes at everything |
| P(seek_food \| crab, hunger 4-5, food mid) | >= 0.60 | the eager scavenger |
| P(rest \| lobster, day) | >= 0.40 | in its den |
| P(rest \| lobster, night) | <= 0.40 | nocturnal |
| P(follow \| lobster, day) | <= 0.05 | solitary |
| P(rest \| eel, night) | <= 0.50 | the night hunter |
| P(seek_food \| any species, starving) | >= 0.95 | temperament never beats hunger |

**If a check is BAD, edit ONE sentence of `SPECIES_PROMPT_V5` and re-run;
never touch the v4 part** (it is the classic fish's DNA - the v3 flattening
came from a "small" rewrite), and never fix it in the sim. Then confirm the
v4 checks did not move against the 2026-09-14 try 3 numbers in
retrain-v4.md (follow | social 9 0.72, dart | bold 9 0.50, dart | bold 1 0.00,
night rest 0.90, bored 9 -> explore ~1.0, bored 0 keeps bubbles 0.57).

## 1-4. The night (unattended)

```bash
cd "/Volumes/Local/Projects/LLM Fish Tank/aqua-pets/model"
nohup ./run_v5_overnight.sh > /dev/null 2>&1 &          # HOST=... WORKERS=2 to change
tail -f out/v5_overnight.log
```

`run_v5_overnight.sh`: 3 workers x 8 h (`--schema 5`, seeds 51-53,
`MINUTES` / `COUNT` / `WORKERS` / `FISH_SHARE` overrides) -> fold + verify
(`out/v5_clean.jsonl`, with the per-species table) -> `convert_to_v5.py`
re-encodes the old labels as `species fish` (`out/v4m_clean.jsonl` if it is
there, else v2 + v3 + v4_clean) -> fold the mix (`out/v5m_clean.jsonl`:
~48K old + the night's) -> train 14M (dim 384, 8 layers, 8 heads, seq 64,
batch 64, lr 6e-4, `ITERS` 7000 - v4m was 5000 iters on ~48K pairs, v5m has
~65K) -> `ckpt_v5m.pt`, `model_q4_v5m.bin`, `model_v5m.bin` ->
`probe_dist.py --schema 5`.

Expect ~15-20K new pairs (the v4 night made ~20K; the v5 prompt is longer,
but Ollama reuses the cached system prompt between calls). At a 1/3 fish
share that is ~1,200-1,500 per new species. If the morning's fold shows any
species under ~1,000, run a second night with `FISH_SHARE=0.15` (the old
data already carries ~48K fish labels) before training for real - the
script's train step can simply be re-run on the bigger fold.

Peek at the first ~200 lines of one worker before bed:
```bash
python3 fold_traces.py --schema 5 out/v5_traces_a.jsonl --out /tmp/v5_peek.jsonl
```
No goal over 40%; every species present; and each species' top goals should
look like its paragraph (angler: rest; shark: explore; squid: follow;
lobster: rest by day). A species whose goals look exactly like the fish's is
a prompt the teacher did not take in - check for the num_ctx warning first.

## Acceptance (morning)

Build the q4 reader once (`cc -O3 -o runq4 runq4.c -lm`; eval of the file that
ships, not the fp32 one), then:

```bash
python3 eval.py --schema 5 --teacher --count 400 --min-per-species 30 --seed 5 \
    --run-bin ./runq4 --model-bin out/model_q4_v5m.bin --tok-bin out/tokenizer_v5.bin | tee out/eval_v5.log
```

- **teacher agreement >= 72% overall AND for every species** (the last lines
  of the log; the script exits 1 below the bar). ~450 teacher calls, ~10 min.
  With 30 states a species the per-species number is +-8 points: a species at
  65-72% is re-run with `--min-per-species 80` before it is called a fail.
- probe (`out/v5_overnight.log`, "species (v5)" panel): angler and lobster
  rest by day, the hammerhead's rest ~0 by day, squid follow a friend in view,
  lobster and eel awake at night, octopus and puffer on the reef and explore,
  seahorse dart ~0, crab seeks food earliest; **starving >= 0.85 for every
  species**; P(flee_shadow) ~0
- **the classic fish did not regress** (the probe's v4 panels, `species fish`):
  boredom sweep as v4m (bubbles again <= 0.15 at bored 8-9, explore up),
  night + bored 9 rests, starving + bored 9 seeks food >= 0.85, personality
  cliffs no worse than v3m/v4m (social 0->9 P(follow) 0.01->0.52, bold 0->9
  P(dart) 0.02->0.40)
- `AQUA_PETS_MODEL=../model/out/model_q4_v5m.bin AQUA_PETS_TOKENIZER=../model/out/tokenizer_v5.bin
  ./fishsim --selftest-llm` prints "schema v5", passes, and its census is no
  worse than v4m's (landmark time, zones per fish-minute, longest one-goal
  stretch, mean bored - the numbers in retrain-v4.md / docs/stats.md)

## 5. Ship it (sim + firmware) - manual

```bash
cd "/Volumes/Local/Projects/LLM Fish Tank/aqua-pets/model"
cp out/model_q4.bin out/model_q4_v4m_shipped.bin            # the rollback copy
cp out/model_q4_v5m.bin out/model_q4.bin
cp out/tokenizer_v5.bin out/tokenizer.bin
cp out/tokenizer_v5.bin ../firmware/main/tokenizer.bin
cd ../sim && make && ./fishsim --selftest-llm && ./fishsim --selftest-encoder   # "schema v5" both
make check-all
```

Then, in the same commit:

- `common/version.h`: `PT_MODEL_TAG "v5m"`, `PT_MODEL_LEN` (bytes of the new
  model_q4.bin, ~7,559,884), `PT_MODEL_SHA256` (`shasum -a 256
  model/out/model_q4.bin`); `python3 tools/model_trailer.py --check` must pass
  (the tank heals its trailer from these, and the OTA manifest says
  `needs_cable` for a new model tag until model updates over the air land -
  docs/OTA.md).
- `.github/workflows/checks.yml`: the q4_host prompt becomes a v5 line (add
  `species fish` after `stage adult`).
- Firmware: `cd ../firmware && . ~/esp/esp-idf/export.sh && idf.py -B
  ~/.cache/aqua-pets/fw-build build`, then `tools/flash.sh --model` (the
  model partition at 0x290000 and its trailer) - for every board on the
  bench (flash_round.sh / flash_watch.sh). The firmware's tokenizer and the
  partition's model must go together (see "Mixed files" above: a v5
  tokenizer alone just stays v4; a v5 model with the old tokenizer turns the
  advisor off).
- schema.md (promote v5 to SHIPPED), docs/stats.md (a v5 table: the
  eval per species, the probe), docs/DEVICE.md (model partition: v5m),
  docs/retrain-next.md (empty the queue of what v5 took).

## Rollback

Nothing in C needs reverting: the encoder speaks whatever the tokenizer is.

```bash
git checkout <the v4 commit> -- model/out/model_q4.bin model/out/tokenizer.bin \
    firmware/main/tokenizer.bin common/version.h
```
rebuild, `tools/flash.sh --model` (both files go back together). A tank
that took only the new app (firmware) over the air but kept its v4 model runs
as v4 by itself.

## If the night goes wrong

- `[warn] OSError: curl failed` filling `out/v5_gen_*.log`: the teacher is
  queueing; `WORKERS=2` (as for v4).
- `[warn] the prompt used N of num_ctx ...`: the species paragraph is being
  cut. Stop, raise `num_ctx()` in gen_traces.py, regenerate (the labels made
  with a cut prompt are not species labels).
- A worker dies: the others keep going; re-run it with a new seed. Files
  append; fold de-duplicates.
- A species' labels look like the fish's or one goal owns a species: it is
  the prompt - edit one sentence of `SPECIES_PROMPT_V5`, regenerate that
  worker's file.

## Smoke test (2026-10-05, no Ollama)

Run in a scratch folder with `--teacher rules` (a rule policy, never data for
a shipped model) and torch on CPU:

- gen_traces `--schema 5`: 2 x 2,500 pairs; the v2/v3/v4 dry runs are byte
  identical to the pre-v5 script (same seeds). Species shares in a 6,000-line
  dry run: fish 29%, the nine 6-11% each; floor dwellers' states in the floor
  row 98-99%; every bold / social inside its species' range.
- convert_to_v5 on v3 + v4 stand-in traces (1,232 kept, 368 dropped for a
  shadow in view, as convert_to_v4 does) -> fold -> tokenizer verify: closed
  vocabulary OK.
- train: toy students (dim 128 x 4 layers, dim 192 x 3; 4,000 iters, ~4 min
  CPU) and `run_v5_overnight.sh` itself with `TEACHER=rules ITERS=60` at the
  real 14.19M size (0.6 s/iter CPU): export_q4 7.56 MB (7,559,884 bytes, fits
  the 8 MB partition).
- eval per species (q4 through runq4) against the rule teacher: the dim-128
  toy 78% overall, 60-93% per species (the rule teacher samples, so a perfect
  student scores well under 100%) - the report and the exit code work; the
  numbers mean nothing about the real run. The toy learned species-shaped
  answers (probe: P(rest at night) 0.13 for the hammerhead, 0.47 eel, ~0.98
  for the rest).
- C: q4_host loads the v5 toy and the 14M v5 export (batched == sequential);
  `fishsim --selftest-llm` with `AQUA_PETS_MODEL` / `AQUA_PETS_TOKENIZER` prints
  "schema v5" and passes; `--selftest-encoder` + encoder_agree.py: 60 C lines
  (10 species) and 3,000 Python lines agree.

## The jellyfish (2026-10-09): one species on top of a shipped model

A species added after v5m: no new schema, one word. The recipe, for the next
one:

1. The word: `train_tokenizer.py` `SPECIES_WORDS` + `"jellyfish"` (APPENDED -
   id 65, vocab 66) and `gen_traces.py` `SPECIES` (its traits from tank.c),
   `_habitat` (where it lives), `rules_goal` (the smoke teacher), one
   sentence in `SPECIES_PROMPT_V5` (never touch the rest); `prompt_check.py`
   cases + checks; `probe_dist.py` `RANGES`.
2. The gate: `prompt_check.py --schema 5` (854 calls, ~25 min on the 1070 box):
   the jellyfish checks pass and the old ones did not move.
3. Its own labels: `out/run_label_v5j.sh` - `--focus jellyfish --focus-share 0.8
   --fish-share 0.25` (~55 % of the states are jellyfish), 6,000 states over
   the two teachers.
4. `out/run_train_v5j.sh`: fold -> `v5f_train.jsonl` + the jellyfish ->
   `tokenizer_v5j.bin` -> train 7,000 iters on CPU -> export -> probe -> eval
   (>= 72 % overall and per species, the jellyfish included).
5. Ship as v5m was (RESUME_v5.md step 5): `model_q4.bin`, `tokenizer.bin`,
   `firmware/main/tokenizer.bin`, `PT_MODEL_*`. The tokenizer may go out
   before the model: the C advisor sends `species jellyfish` only when the
   loaded vocab has the word (`advisor_core_species_word`), else `species fish`.

Numbers: docs/stats.md "The jellyfish, v5j".
