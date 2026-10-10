# Schema v3 retrain — runbook (prepared 2026-08-21; RUN 2026-08-22, outcome at the bottom)

**Point `HOST` at an Ollama machine you can monopolize for the run** - a
generation cycle keeps the teacher busy for hours. Everything below is
prepared and dry-run; the only thing missing is the data.

## Why v3 (one data cycle, three wins)

1. **Drop fish names** from the state line — measured zero decision signal
   (`model/probe_dist.py`: identical distributions for mira/bolt/kelp/nori).
   Frees the population cap from the 4 trained name tokens (today fish 5–6
   reuse a name token as a placeholder; v3 makes the line clean and 2 tokens
   shorter → faster decisions).
2. **Add `trust 0-9`** so the model shades decisions by relationship (a
   trusting fish settles faster / is bolder near the surface; a wary one flees
   at less) instead of only the reflex approach-to-finger we have now.
3. **Fix the flee cue**: the v2 student learned *stress* (not shadow distance)
   as its reason to flee because 80% of shadow-near training states had stress
   ≥ 7. v3 adds a `shadow_calm` perturbation (shadow in view, stress 0–3) and a
   `lonely` perturbation (`friend none`, 17/28K in v2 → the v2 model breaks on
   it), and generates tanks of **2–6 fish** so friend distances match the new
   population range.

The v3 line (see schema.md "v3 DRAFT"):
```
zone 2 hunger 7 energy 5 stress 2 curiosity 8 bold 4 social 6 stage adult trust 5 food near 12 shadow far 6 friend mid 3 bubble mid 10 reef far 7 wall clear last explore time day
```
Vocab 54 (v2: 58). The C encoder (`common/llm/advisor_core.c`) already emits v3
when the loaded tokenizer contains ` trust` — no firmware change needed beyond
swapping the two model files.

## 0. Preflight (5 min, no Ollama load)

```bash
cd "/Volumes/Local/Projects/LLM Fish Tank/aqua-pets/model"
python3 gen_traces.py --count 5 --dry-run --schema 3 --seed 1     # v3 lines print, no network
python3 train_tokenizer.py --schema 3                              # out/tokenizer_v3.bin, vocab 54
curl -s http://localhost:11434/api/tags | head -c 300          # Ollama reachable, gemma4:26b listed
```

## 1. Generate (the 8 hours) — 3 workers, distinct seeds, hard wall-clock cap

Run each in its own terminal (or `&`); `--max-minutes` stops them cleanly.
v2 ran ~36–51 decisions/min with 2–3 workers → expect ~20K pairs in 8 h.

```bash
cd "/Volumes/Local/Projects/LLM Fish Tank/aqua-pets/model"
python3 gen_traces.py --schema 3 --count 12000 --max-minutes 480 --seed 31 --out out/v3_traces_a.jsonl
python3 gen_traces.py --schema 3 --count 12000 --max-minutes 480 --seed 32 --out out/v3_traces_b.jsonl
python3 gen_traces.py --schema 3 --count 12000 --max-minutes 480 --seed 33 --out out/v3_traces_c.jsonl
```

Watch the first ~200 lines of one file for a prompt-is-DNA skew before letting
it run all night:
```bash
python3 fold_traces.py --schema 3 out/v3_traces_a.jsonl --out /tmp/v3_peek.jsonl
```
(`follow_friend` should be well under 30%; `flee_shadow` 15–25%; if a goal is
> 40%, stop, fix `SYSTEM_PROMPT_V3` in gen_traces.py, restart.)

## 2. Fold + verify (1 min)

```bash
python3 fold_traces.py --schema 3 out/v3_traces_*.jsonl --out out/v3_clean.jsonl
python3 train_tokenizer.py --schema 3 --verify "out/v3_clean.jsonl"     # closed vocabulary OK
```

## 3. Train (~30 min on the Mac, MPS) + export 4-bit + probe

```bash
AQUA_PETS_SCHEMA=3 ~/.venvs/aquapets/bin/python train.py --data "out/v3_clean.jsonl" \
  --dim 384 --n-layers 8 --n-heads 8 --max-seq-len 64 --batch 64 --iters 4000 --lr 6e-4 --out out/ckpt_v3.pt
~/.venvs/aquapets/bin/python export_q4.py out/model_q4_v3.bin --checkpoint out/ckpt_v3.pt
~/.venvs/aquapets/bin/python probe_dist.py --schema 3            # distribution probe (see below)
python3 eval.py --schema 3 --count 60 --teacher --host http://localhost:11434 \
  --model-bin out/model_v3.bin --tok-bin out/tokenizer_v3.bin       # agreement vs teacher (needs fp32 export too)
```
(For eval's fp32 path: `~/.venvs/aquapets/bin/python llama2.c/export.py out/model_v3.bin --checkpoint out/ckpt_v3.pt`.)

Acceptance (compare with v2 in docs/stats.md):
- teacher agreement ≥ 72% (v2: 75%, ceiling 82%)
- probe: starving min P(seek_food) ≥ 0.85 across identities; **shadow near at
  stress 1 → P(flee) ≥ 0.7** (v2: 0.19); **friend none no longer flips to
  flee_shadow**; trust sweep shows a visible but modest shift (not a cliff
  that dominates hunger/shadow)
- sampling safety unchanged (teacher label in top-2 ≥ 90%)

## 4. Ship it (sim + firmware)

```bash
cp out/model_q4_v3.bin out/model_q4.bin          # the sim and run_qemu.sh read these names
cp out/tokenizer_v3.bin out/tokenizer.bin
cp out/tokenizer_v3.bin ../firmware/main/tokenizer.bin   # embedded in the app image
cd ../sim && make && ./fishsim --selftest-llm           # prints "schema v3, sampled decoding"
cd ../firmware && idf.py build && ./run_qemu.sh          # log line "schema v3"
```
Then update docs/stats.md (v3 table), schema.md (promote the v3 draft to
FROZEN), and HANDOFF.md.

## If the night goes wrong

- Ollama unreachable / Little Snitch: gen_traces uses curl (see the comment in
  `ask_ollama`); `curl .../api/tags` first.
- A worker dies: the others keep going; just re-run it with a new seed. Files
  append; fold_traces de-duplicates.
- Skewed labels: it is the prompt, not the sim — edit one sentence, regenerate
  only that worker's file.


## Outcome (2026-08-22)

Run as written (`run_v3_overnight.sh`): 22,850 pairs, trained, exported, probed.
The pure-v3 student lost the personality cliffs (prompt-is-DNA again; numbers in
docs/stats.md). Fix that shipped the same morning: `run_v3m_mixed.sh` — convert
the v2 dataset to the v3 line and train on both (51,162 pairs) → **v3m**, which
ships (sim + firmware). Lessons folded into tooling:

- `model/prompt_check.py` — test a prompt (or a teacher) in ~7 minutes before any
  overnight run. Use it first, always.
- Old datasets don't expire with a schema change: re-encode them.
- A targeted top-up (~3K pairs, 1 h) with the current v3 prompt for
  lonely+shadow and shadow-near-calm is the only generation still worth doing.
