#!/usr/bin/env python3
"""Evaluate the exported student model against fresh sim states. Stdlib only.

Samples never-seen states from the same headless sim as gen_traces.py, runs the
native llama2.c binary on each, and reports what the student decided. With
--teacher it also asks Ollama for its goal on the same states and reports
student/teacher agreement (top-line distillation metric).

  python3 eval.py --count 20                 # student only, prints a table
  python3 eval.py --count 50 --teacher       # adds gemma4:26b agreement
  python3 eval.py --schema 5 --teacher --count 400 --run-bin ./runq4 \
      --model-bin out/model_q4_v5j.bin --tok-bin out/tokenizer_v5j.bin   # + per-species agreement

With --schema 5 the agreement is also broken down per species (the v5
acceptance is >= 72% overall AND for every species); --min-per-species makes
the sampler keep going until every species has that many states.
"""

import argparse
import os
import random
import re
import subprocess
import sys

import gen_traces as gt

HERE = os.path.dirname(os.path.abspath(__file__))


def student_goal(run_bin, model_bin, tok_bin, state):
    prompt = state + " ->"
    out = subprocess.run(
        [run_bin, model_bin, "-z", tok_bin, "-t", "0", "-i", prompt],
        capture_output=True, text=True, timeout=60).stdout
    completion = out.split("->", 1)[1] if "->" in out else out
    m = re.search(r"([a-z_]+) urgency (\d)", completion)
    return f"{m.group(1)} urgency {m.group(2)}" if m else f"<unparsed: {completion.strip()[:40]}>"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=20)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--teacher", action="store_true", help="also query Ollama and score agreement")
    ap.add_argument("--host", default="http://192.168.0.139:11434")
    ap.add_argument("--model", default="gemma4:26b")
    ap.add_argument("--run-bin", default=os.path.join(HERE, "runw"))
    ap.add_argument("--model-bin", default=os.path.join(HERE, "out", "model.bin"))
    ap.add_argument("--tok-bin", default=os.path.join(HERE, "out", "tokenizer.bin"))
    ap.add_argument("--schema", type=int, choices=(2, 3, 4, 5), default=2, help="state line schema of the model under test")
    ap.add_argument("--teacher-kind", choices=("ollama", "rules"), default="ollama",
                    help="rules = gen_traces' rule policy (smoke tests of the pipeline only)")
    ap.add_argument("--min-per-species", type=int, default=0,
                    help="v5: keep sampling past --count until every species has this many states")
    ap.add_argument("--accept", type=float, default=72.0, help="agreement bar, percent (overall and per species)")
    args = ap.parse_args()
    gt.SCHEMA = args.schema
    gt.TEACHER = args.teacher_kind

    for p in (args.run_bin, args.model_bin, args.tok_bin):
        if not os.path.exists(p):
            sys.exit(f"missing {p} (build/train/export first)")

    rng = random.Random(args.seed)
    tank = gt.Tank(rng)
    for _ in range(200):
        tank.tick()

    agree = valid = 0
    per = {}                                   # species -> [states, agreements]
    i = 0
    while i < args.count or (args.schema >= 5 and args.min_per_species and
                             (len(per) < len(gt.SPECIES) or min(v[0] for v in per.values()) < args.min_per_species)):
        if i >= max(args.count, 1) * 20:
            break                              # never loop forever on a sampler that misses a species
        if args.schema >= 5 and i and i % 60 == 0:
            tank = gt.Tank(rng)                # a new population: every species gets its turn
            for _ in range(200):
                tank.tick()
        for _ in range(40):
            tank.tick()
        fish = tank.fish[i % len(tank.fish)]
        if rng.random() < 0.45:
            tank.perturb(fish)
        state = gt.encode(tank, fish)
        sp = fish.species if args.schema >= 5 else "fish"
        if args.schema >= 5 and args.min_per_species and i >= args.count and per.get(sp, [0])[0] >= args.min_per_species:
            i += 1
            continue                           # past --count: only the species still short
        s_goal = student_goal(args.run_bin, args.model_bin, args.tok_bin, state)
        per.setdefault(sp, [0, 0])
        per[sp][0] += 1
        if not s_goal.startswith("<"):
            valid += 1
        line = f"[{i+1:3d}] {s_goal:28s}"
        if args.teacher:
            t_goal = gt.ask_teacher(args.host, args.model, state, 30) or "<teacher failed>"
            hit = s_goal.split()[0] == t_goal.split()[0]
            agree += hit
            per[sp][1] += hit
            line += f" teacher: {t_goal:28s} {'MATCH' if hit else 'diff'}"
        print(line + f"  | {state}")
        if not s_goal.startswith("<"):
            fish.goal = s_goal.split()[0]
        i += 1

    n = sum(v[0] for v in per.values())
    print(f"\nvalid output: {valid}/{n}")
    if args.teacher:
        ok = 100 * agree / max(1, n) >= args.accept
        print(f"goal agreement with teacher: {agree}/{n} ({100*agree/max(1, n):.0f}%)  {'OK' if ok else 'BELOW'} {args.accept:.0f}%")
        if args.schema >= 5:
            print("per species:")
            for s in gt.SPECIES_TOKENS:
                m, a = per.get(s, [0, 0])
                pct = 100 * a / m if m else float("nan")
                good = m and pct >= args.accept
                ok = ok and bool(good)
                print(f"  {s:9s} {a:4d}/{m:<4d} {pct:5.1f}%  {'OK' if good else 'BELOW' if m else 'NO STATES'}")
            print(f"v5 acceptance (>= {args.accept:.0f}% overall and per species): {'PASS' if ok else 'FAIL'}")
            sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
