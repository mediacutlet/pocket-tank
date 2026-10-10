# Training pipeline — from teacher to flashable binary

The distillation loop end to end. The teacher is any Ollama-served model
(the shipped weights were taught by gemma4:26b); set `--host` /
`HOST` to your Ollama server (default `http://localhost:11434`).

Training needs PyTorch — use a virtualenv:

```bash
python3 -m venv .venv && .venv/bin/pip install torch
```

`model/llama2.c/` is a clone of
[karpathy/llama2.c](https://github.com/karpathy/llama2.c) (training script +
reference inference):

```bash
git clone https://github.com/karpathy/llama2.c model/llama2.c
```

## The steps

```bash
cd model/llama2.c && make run && cd ../..                # 1. native llama2.c build

python3 model/gen_traces.py --count 10 --dry-run         # 2a. sample encoded states, no network
python3 model/gen_traces.py --count 3000 --max-minutes 45 --seed 1 \
    --out model/out/traces_x1.jsonl                      # 2b. generate teacher-labeled traces
                                                         #     (run several workers with distinct seeds/outs)

python3 model/fold_traces.py                             #     dedup + vocabulary check + label distribution
python3 model/train_tokenizer.py                         # 3. word-level tokenizer, vocab 58, no deps

.venv/bin/python model/train.py --data model/out/clean.jsonl \
    --dim 384 --n-layers 8 --n-heads 8 --max-seq-len 64 \
    --batch 64 --iters 4000 --lr 6e-4                    # 4. train (torch)

.venv/bin/python model/llama2.c/export.py model/out/model.bin \
    --checkpoint model/out/ckpt.pt                       # 5. export fp32 (--version 2 for int8)
python3 model/export_q4.py                               #    → 4-bit model_q4.bin

cc -O3 -o model/runw model/runw.c -lm                    # 6a. word-tokenizing runner
./model/runw model/out/model.bin -z model/out/tokenizer.bin \
    -t 0 -i "<state line> ->"                            # 6b. test a decision

python3 model/eval.py --count 60 --teacher               # 7. goal agreement vs the teacher
```

The prompt in step 6 is any [schema.md](../model/schema.md) state line with
` ->` appended; the model completes it with `<goal> urgency <0-9>` and stops.

## Extra tools

- `model/prompt_check.py` — 7-minute teacher/prompt sanity check. **Run this
  before any long generation run**; a subtly broken prompt wastes a night.
- `model/probe_dist.py` — the student's full goal *distribution* on real and
  synthetic states (evidence behind [progression-next.md](progression-next.md))
- `model/teacher_vs_student.py` — side-by-side decisions for the same states
- Schema v3 cycle: `--schema 3` on gen_traces / train_tokenizer / eval,
  `AQUA_PETS_SCHEMA=3` for train.py — runbook in [retrain-v3.md](retrain-v3.md)
