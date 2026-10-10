#!/usr/bin/env python3
"""Do the C encoder and gen_traces.py write the same schema-5 line? Stdlib only.

The model only ever sees what gen_traces.py wrote at training time, and only
ever hears what common/llm/advisor_core.c writes on the device: the two must
agree word for word on the field order, the keys and every value's form. The
C side prints its lines (`ENC <line>`, one per creature of every species, day
and night) with a schema-5 tokenizer loaded; this script parses each one with
gen_traces.parse_v5, re-renders it with gen_traces.render_v5 (the Python
encoder's own formatter) and requires the identical string, checks every value
against the field's domain, every word against the v5 lexicon, and that every
species was heard. The Python encoder's own lines (a dry run of the v5 sim)
go through the same checks, so a drift on either side fails here.

  python3 model/train_tokenizer.py --schema 5 --out /tmp/tok5.bin
  (cd sim && ./fishsim --selftest-encoder /tmp/tok5.bin) | python3 model/encoder_agree.py
"""
import argparse
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_traces as gt  # noqa: E402
import train_tokenizer as tok  # noqa: E402

SIGHT = re.compile(r"^(none|(near|mid|far) ([1-9]|1[0-2]))$")
DOMAIN = {
    "zone": re.compile(r"^[1-6]$"),
    "stage": re.compile(r"^(fry|juv|adult|elder)$"),
    "species": re.compile(r"^(" + "|".join(gt.SPECIES_TOKENS) + r")$"),
    "food": SIGHT, "friend": SIGHT, "bubble": SIGHT, "reef": SIGHT,
    "wall": re.compile(r"^(clear|(near|mid) ([1-9]|1[0-2]))$"),
    "last": re.compile(r"^(" + "|".join(gt.GOALS) + r")$"),
    "time": re.compile(r"^(day|night)$"),
}
for k in ("hunger", "energy", "stress", "curiosity", "bold", "social", "trust", "bored"):
    DOMAIN[k] = re.compile(r"^[0-9]$")


def check(line, words):
    """None if the line is a well-formed v5 line, else why not"""
    try:
        d = gt.parse_v5(line)
    except ValueError as e:
        return str(e)
    if gt.render_v5(d) != line:
        return "re-rendered differently: " + gt.render_v5(d)
    for k, v in d.items():
        if not DOMAIN[k].match(v):
            return f"`{k} {v}` is outside the field's domain"
    bad = [w for w in line.split() if w not in words]
    if bad:
        return f"off-vocabulary words {bad}"
    if len(line.split()) + 2 > 60:
        return "longer than the C prompt cap (60 tokens with -> and BOS)"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump", nargs="?", default="-", help="fishsim --selftest-encoder output (default stdin)")
    ap.add_argument("--py-count", type=int, default=3000, help="Python encoder lines to check too")
    args = ap.parse_args()
    tok.set_schema(5)
    words = set(tok.LEXICON)
    src = sys.stdin if args.dump == "-" else open(args.dump)
    c_lines = [ln[4:].rstrip("\n") for ln in src if ln.startswith("ENC ")]
    if not c_lines:
        sys.exit("FAIL: no `ENC` lines - was the sim given a schema-5 tokenizer?")
    fails = 0
    c_species = set()
    for ln in c_lines:
        why = check(ln, words)
        if why:
            fails += 1
            print(f"FAIL C line: {why}\n  {ln}")
        else:
            c_species.add(gt.parse_v5(ln)["species"])
    missing = set(gt.SPECIES_TOKENS) - c_species
    if missing:
        fails += 1
        print(f"FAIL: the C lines never carried {sorted(missing)}")

    gt.SCHEMA = 5                                   # the Python encoder, same checks
    rng = random.Random(55)
    tank = gt.Tank(rng)
    py_species = set()
    for i in range(args.py_count):
        if i and i % 60 == 0:
            tank = gt.Tank(rng)
        for _ in range(10):
            tank.tick()
        f = tank.fish[i % len(tank.fish)]
        if rng.random() < 0.45:
            tank.perturb(f)
        ln = gt.encode(tank, f)
        why = check(ln, words)
        if why:
            fails += 1
            print(f"FAIL Python line: {why}\n  {ln}")
        py_species.add(f.species)
    if set(gt.SPECIES_TOKENS) - py_species:
        fails += 1
        print(f"FAIL: the Python sim never made {sorted(set(gt.SPECIES_TOKENS) - py_species)}")
    print(f"encoder agreement: {len(c_lines)} C lines ({len(c_species)} species), {args.py_count} Python lines "
          f"({len(py_species)} species): {'OK' if not fails else f'{fails} FAILURES'}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
