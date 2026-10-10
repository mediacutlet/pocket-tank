#!/usr/bin/env python3
"""Fold raw trace files into one clean training set. Stdlib only.

  python3 fold_traces.py --schema 3 out/v3_traces_*.jsonl --out out/v3_clean.jsonl

- drops malformed lines and any line with a word outside the schema's closed
  vocabulary (train_tokenizer.lexicon_for)
- de-duplicates exact (state, goal) pairs; for a state seen with several goals
  keeps them all (the teacher's own variance is signal - the student learns it
  as a soft distribution and the device samples from it)
- prints the goal distribution so a prompt-is-DNA skew (docs/stats.md) is
  caught before a 30-minute train
"""
import argparse
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import train_tokenizer as tok  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inputs", nargs="+", help="raw jsonl files or globs")
    ap.add_argument("--schema", type=int, choices=(2, 3, 4, 5), default=3)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    tok.set_schema(args.schema)
    words = set(tok.LEXICON)

    paths = [p for g in args.inputs for p in sorted(glob.glob(g))]
    if not paths:
        sys.exit("no input files")
    seen, kept, bad_vocab, bad_json, dups = set(), [], 0, 0, 0
    goals = {}
    for path in paths:
        with open(path) as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                try:
                    o = json.loads(line)
                    state, goal = o["state"], o["goal"]
                except (ValueError, KeyError, TypeError):
                    bad_json += 1
                    continue
                if any(w not in words for w in (state + " " + goal).split()):
                    bad_vocab += 1
                    continue
                key = (state, goal)
                if key in seen:
                    dups += 1
                    continue
                seen.add(key)
                kept.append({"state": state, "goal": goal})
                g = goal.split()[0]
                goals[g] = goals.get(g, 0) + 1
    with open(args.out, "w") as out:
        for o in kept:
            out.write(json.dumps(o) + "\n")
    n = len(kept)
    print(f"{len(paths)} files -> {n} clean pairs -> {args.out}")
    print(f"  dropped: {bad_json} malformed, {bad_vocab} off-vocabulary, {dups} exact duplicates")
    print("  goal distribution:")
    for g, c in sorted(goals.items(), key=lambda kv: -kv[1]):
        flag = "  <-- check the prompt (v2 incident was 45%)" if c / n > 0.40 else ""
        print(f"    {g:14s} {c:6d}  {100 * c / n:4.1f}%{flag}")
    if args.schema >= 4:
        bored = sum(1 for o in kept if any(f" bored {d} " in o["state"] for d in "6789"))
        rut = sum(1 for o in kept if any(f" bored {d} " in o["state"] for d in "789")
                  and o["goal"].split()[0] == o["state"].split(" last ")[1].split()[0])
        print(f"  v4 coverage: bored 6-9 {bored} ({100 * bored / n:.1f}%); bored 7-9 that REPEAT last: {rut} "
              f"(the teacher should rarely do this)")
    if args.schema >= 5:
        # per species: its share and its top goals (a species whose labels look
        # like the classic fish's is a prompt that did not reach the teacher)
        by = {}
        for o in kept:
            sp = o["state"].split(" species ", 1)[1].split()[0] if " species " in o["state"] else "?"
            by.setdefault(sp, {}).setdefault(o["goal"].split()[0], 0)
            by[sp][o["goal"].split()[0]] += 1
        print("  v5 species (share; top goals):")
        for sp, d in sorted(by.items(), key=lambda kv: -sum(kv[1].values())):
            m = sum(d.values())
            top = ", ".join(f"{g} {100 * c / m:.0f}%" for g, c in sorted(d.items(), key=lambda kv: -kv[1])[:4])
            print(f"    {sp:9s} {m:6d} {100 * m / n:4.1f}%   {top}")
    if args.schema >= 3:
        none = sum(1 for o in kept if " friend none " in o["state"] + " ")
        calm = sum(1 for o in kept if "shadow near" in o["state"] and any(f"stress {d}" in o["state"] for d in "0123"))
        print(f"  v3 coverage: friend none {none} ({100 * none / n:.1f}%), shadow near + calm {calm}")


if __name__ == "__main__":
    main()
