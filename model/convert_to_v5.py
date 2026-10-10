#!/usr/bin/env python3
"""Re-encode older datasets as v5 lines (schema.md v5). Stdlib only.

Every pair the teacher ever labelled was about the classic fish, so v5 reuses
them all as `species fish`: the v5 line is the v4 line with `species fish`
inserted after `stage <x>`. v2 / v3 lines go through convert_to_v4.to_v4 first
(names out, trust 5, the shadow field and every shadow-in-view or flee pair
dropped, bored 0-2); v4 lines (the 2026-09-14 teacher data and the converted
v4m mix) only gain the species field.

  python3 convert_to_v5.py out/v4m_clean.jsonl --out out/old_as_v5.jsonl
  python3 convert_to_v5.py out/v2_clean.jsonl out/v3_clean.jsonl out/v4_clean.jsonl --out out/old_as_v5.jsonl

Feed it v4m_clean.jsonl OR its parts (v2 + v3 + v4_clean), not both - the fold
de-duplicates exact pairs, but the v2/v3 conversion draws a random bored 0-2,
so the same old pair converted twice is two near-duplicates.
"""
import argparse
import json
import random
import re

from convert_to_v4 import to_v4


def to_v5(state, rng):
    """the v5 line for an older state, or None if the pair must be dropped"""
    if " species " in f" {state} ":
        return state                                                    # already v5
    if " bored " not in state:
        state = to_v4(state, rng)                                       # v2 / v3 first
        if state is None:
            return None
    s, n = re.subn(r"(^| )stage (fry|juv|adult|elder) ", r"\1stage \2 species fish ", state, count=1)
    return s if n == 1 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inputs", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=5)
    args = ap.parse_args()
    rng = random.Random(args.seed)
    kept = dropped = flee = bad = 0
    with open(args.out, "w") as out:
        for path in args.inputs:
            for line in open(path):
                try:
                    o = json.loads(line)
                    state, goal = o["state"], o["goal"]
                except (ValueError, KeyError, TypeError):
                    bad += 1
                    continue
                if goal.startswith("flee_shadow"):
                    flee += 1
                    continue
                s = to_v5(state, rng)
                if s is None:
                    dropped += 1
                    continue
                out.write(json.dumps({"state": s, "goal": goal}) + "\n")
                kept += 1
    print(f"kept {kept} v5 pairs (species fish) -> {args.out}; dropped {dropped} (a shadow in view / "
          f"unreadable line), {flee} flee labels, {bad} malformed")


if __name__ == "__main__":
    main()
