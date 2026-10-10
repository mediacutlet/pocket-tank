# Schema v4 retrain — runbook (prepared 2026-09-14; NOT yet run)

**Do not start this without Strato's go: the Mac Mini Ollama (192.168.0.139)
is shared production.** Everything below is built and dry-run; only the
teacher data is missing.

## Why v4 (one data cycle, two wins)

1. **Boredom.** Strato (2026-09-14): "the fish often seem to get stuck in loops
   following_friend or playing in the bubble column ... they don't explore the
   tank very much. They go from the bubbles, to sometimes the grasses, only
   drifting to get food." Measured causes:
   - the teacher's own taste: on every content state in the last prompt check
     (model/out/prompt_check.log) visit_bubbles was 50-67% of labels, so the
     student parks a fed, calm fish at the column;
   - nothing in the line said how long the fish had been at it - `last` plus
     "keep the last goal while it makes sense" is pure hysteresis;
   - `explore` in tank.c was a 50 px wander target ahead of the nose: a random
     walk that never left the neighbourhood, so even an explore decision
     looked like drifting. (Fixed in the reflex layer, no model needed - see
     HANDOFF 2026-09-14 "boredom".)
   The v4 line carries `bored 0-9` and the teacher is told what to do with it.
2. **The shadow is gone** (2026-09-13). The predator paragraph leaves the
   prompt, the `shadow` field leaves the line, flee_shadow is never a label
   (10,190 of the old 51K pairs were flee labels; 12,725 more had a shadow in
   view - all dropped from the converted set).

The v4 line (schema.md "v4 DRAFT"):
```
zone 2 hunger 7 energy 5 stress 2 curiosity 8 bold 4 social 6 stage adult trust 5 bored 3 food near 12 friend mid 3 bubble mid 10 reef far 7 wall clear last explore time day
```
Vocab 54. The C encoder (`common/llm/advisor_core.c`) already emits v4 when
the loaded tokenizer contains ` bored` (host-checked 2026-09-14) - shipping is
swapping the two model files, as for v3.

What the sim already does (built, all six selftests green, firmware compiles):
`fish_t.bored` rises 0.15/s on the same leisure goal, -4 for a genuinely new
goal (not a bounce back to the one just left), -1.5 for entering a zone unseen
for 20 s, -0.3/s while eating or resting at night, 0 after a sleep; its band
(<3.5 / <7 / 7+) is in the re-ask signature. Explore picks a point in one of
the two least-recently-seen zones and cruises there. With the SHIPPED v3m
model (which cannot see `bored`) the selftest-llm census already moved: fish-
time at a landmark 76% -> 59%, 3+ fish crowding one landmark 67% -> 34%. The
v4 model is what makes them *choose* to leave.

## 0. Preflight (~8 min of teacher time, the required gate)

Teacher host: the Mac Mini (default, shared - ask first) or THIS machine's own
Ollama, which also has gemma4:26b (Strato, 2026-09-14; loads in ~12 s): pass
`--host http://localhost:11434` to prompt_check / gen_traces, or
`HOST=http://localhost:11434 ./run_v4_overnight.sh`. The 2026-09-14 check ran
locally.

```bash
cd "/Volumes/Local/Projects/LLM Fish Tank/aqua-pets/model"
python3 gen_traces.py --count 5 --dry-run --schema 4 --seed 1       # v4 lines print, no network
python3 train_tokenizer.py --schema 4                                # out/tokenizer_v4.bin, vocab 54
curl -s http://192.168.0.139:11434/api/tags | grep -o gemma4:26b    # teacher reachable
python3 prompt_check.py --schema 4 --workers 2 | tee out/prompt_check_v4.log   # 450 calls
```
prompt_check's v4 panel adds: bored 9 at the bubbles (want bubbles again
<= 0.15, explore >= 0.25), bored 9 following (follow again <= 0.15), bored 0 at
the bubbles (bubbles KEPT >= 0.40 - hysteresis must survive), night + bored 9
(rest >= 0.70), and flee_shadow == 0 anywhere. The old checks stay: social /
bold cliffs, starving, lonely, and "max single goal | content < 0.45" (the
bubble attractor; the v4 prompt says the column is "one option among several,
never the default"). A single call returned `explore urgency 5` for the bored-
at-the-bubbles state three times out of three - the prompt has the idea; the
panel says whether it has the balance. **If a check is BAD, edit ONE sentence
of SYSTEM_PROMPT_V4 in gen_traces.py and re-run the check; never fix it in the
sim.**

### Preflight results (2026-09-14 evening, local gemma4:26b, 450 calls each)

| check | want | try 1 | try 2 (night + dart sentences) |
|---|---|---|---|
| P(follow \| social 9) | >= 0.40 | 0.78 | 0.82 |
| P(follow \| social 1) | <= 0.05 | 0.00 | 0.00 |
| P(dart \| bold 9, energetic) | >= 0.10 | 0.65 | 0.62 |
| P(dart \| bold 1, energetic) | <= 0.03 | 0.17 BAD | 0.23 BAD |
| P(seek_food \| starving) | >= 0.95 | 1.00 | 1.00 |
| P(bubbles again \| bored 9) | <= 0.15 | 0.00 | 0.00 |
| P(explore \| bored 9, last bubbles) | >= 0.25 | 1.00 | 1.00 |
| P(follow again \| bored 9) | <= 0.15 | 0.00 | 0.00 |
| P(bubbles kept \| bored 0) | >= 0.40 | 0.60 | 0.53 |
| P(rest \| night, bored 9) | >= 0.70 | 0.10 BAD | 0.83 |
| P(flee) anywhere | 0 | 0.00 | 0.00 |
| max single goal \| content | < 0.45 | 0.40 (bubbles) | 0.45 (reef) |

Read: the boredom idea is fully in the teacher's hands (bored 9 leaves the
bubbles or the friend every time; bored 0 keeps them about half the time, so
the hysteresis survives), the night rule took one sentence, the personality
cliffs are the strongest any prompt has produced here (v3 check: follow 0.15,
dart 0.33), and the content attractor is at the bar and flips between reef
and bubbles between runs (n = 40: noise). The timid-fish dart check has been
BAD under every prompt ever checked (v3 0.10, v3.1 0.05); the old data's bold
cliff (dart 0.02 -> 0.40 in v3m) comes along in the mix regardless.
**Try 3 (the prompt that SHIPS in gen_traces.py: "dart_play is only ever
chosen by a fish with bold 5 or more ... bold 0-3 never picks it"):** every
behaviour check OK - follow | social 9 0.72, dart | bold 9 0.50, dart | bold 1
0.00, night rest 0.90, bored 9 -> explore 1.00 / 0.97, bored 0 keeps bubbles
0.57, flee 0. Only the content attractor is over the bar (inspect_reef 0.50,
bubbles 0.28, explore 0.15): the teacher's taste for a calm fish with nothing
else to do. Left there on purpose - boredom now ends any parked pastime within
a minute, and a fourth sentence risks the prompt-is-DNA flattening v3 had.
Logs: out/prompt_check_v4_try1.log, _try2.log, _try3.log.

## 1-4. The night (unattended)

```bash
cd "/Volumes/Local/Projects/LLM Fish Tank/aqua-pets/model"
nohup ./run_v4_overnight.sh > /dev/null 2>&1 &
tail -f out/v4_overnight.log
```
`run_v4_overnight.sh`: 3 workers x 8 h (seeds 41-43, `MINUTES`/`COUNT`/`WORKERS`
env overrides) -> fold + verify -> `convert_to_v4.py` re-encodes v2 + v3 and the
fold mixes them in (`out/v4m_clean.jsonl`, expect ~28K old + ~20K new) -> train
14M (5000 iters, ~30 min MPS) -> `ckpt_v4m.pt`, `model_q4_v4m.bin`,
`model_v4m.bin` -> `probe_dist.py --schema 4` (boredom sweep + the old panels).

Watch the first ~200 lines of one worker for a skew before bed:
```bash
python3 fold_traces.py --schema 4 out/v4_traces_a.jsonl --out /tmp/v4_peek.jsonl
```
(no goal > 40%; "bored 7-9 that REPEAT last" should be a small number.)

### The night (2026-09-14, on the Mini with Strato's go)

Launched 19:22; the 200-line peek showed seek_food 41% / rest 30% and the
cross-tab blamed the sim, not the prompt: 55% of states were night (the
day/night toggle was symmetric). gen_traces now flips night -> day at
3x the rate (27% night in a dry run) and a perturbed night ends by itself;
relaunched 19:37 with the first 716 pairs kept. Expect ~20K pairs by 03:37,
the v4m model by ~04:15. Peek numbers worth knowing: day + content + bored
0-2 -> bubbles 30 / follow 26 / reef 18, explore ~0; bored 7-9 -> explore
85%. So explore is what a BORED fish does; fresh fish still pick pastimes.
That is the intended cycle (a pastime for ~a minute, then off somewhere new).

## Acceptance (morning)

- teacher agreement >= 72% (`eval.py --schema 4 --teacher --model-bin
  out/model_v4m.bin --tok-bin out/tokenizer_v4.bin`, n = 60)
- probe: bored sweep at the bubbles - P(bubbles again) falls from >= 0.4 at
  bored 0-1 to <= 0.15 at bored 8-9 while P(explore) rises; social 8 at bored 9
  leaves the friend; night + bored 9 still rests; starving + bored 9 still
  seeks food (>= 0.85); P(flee_shadow) ~0 everywhere
- personality cliffs no worse than v3m (social 0->9 P(follow) 0.01->0.52,
  bold 0->9 P(dart) 0.02->0.40 in docs/stats.md)
- `./fishsim --selftest-llm` census with the v4 model: landmark time well
  under 59%, distinct zones per fish-minute above 1.5, longest one-goal
  stretch under 59 s, mean bored under 3.3 (the v3m numbers of 2026-09-14)

## 5. Ship it (sim + firmware) - manual

```bash
cd "/Volumes/Local/Projects/LLM Fish Tank/aqua-pets/model"
cp out/model_q4_v4m.bin out/model_q4.bin
cp out/tokenizer_v4.bin out/tokenizer.bin
cp out/tokenizer_v4.bin ../firmware/main/tokenizer.bin
cd ../sim && make && ./fishsim --selftest-llm            # prints "schema v4, sampled decoding"
cd ../firmware && . ~/esp/esp-idf/export.sh && idf.py -B ~/.cache/aqua-pets/fw-build build
```
Then the model partition flash (the 7.56 MB q4 file, as for v3m), docs/stats.md
(v4 table), schema.md (promote v4 to FROZEN), HANDOFF.md.

## If the night goes wrong

- The teacher answered a plain call in 0.7 s on 2026-09-14 evening but a 4th
  request queued behind 3 busy workers waited > 60 s: with `--timeout 30`
  that is a failure per call. If `out/v4_gen_*.log` fills with `[warn] OSError:
  curl failed`, drop to 2 workers (`WORKERS=2`).
- A worker dies: the others keep going; re-run it with a new seed. Files
  append; fold_traces de-duplicates.
- Skewed labels: it is the prompt, not the sim - edit one sentence, regenerate
  only that worker's file.
