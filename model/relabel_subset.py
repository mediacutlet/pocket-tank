#!/usr/bin/env python3
"""Relabel a subset of an already-folded v5 file with the CURRENT teacher prompt.

Used for the dart fix (2026-10-07): the classic fish's bold / energetic / calm
daytime states are re-asked after the identity paragraph gained its dart
sentence (bold 7-9 only: elsewhere the sentence changes nothing it should); every other label is kept as it was. Writes the merged file and a
side file of (state, old, new) for the record.

  python3 relabel_subset.py out/v5_clean.jsonl --out out/v5f_clean.jsonl \
      --hosts http://127.0.0.1:11435 http://192.168.89.1:11434
"""
import argparse, json, re, sys, collections, concurrent.futures as cf
import gen_traces as gt

num = lambda s, k: int(re.search(rf"\b{k} (\d)", s).group(1))

def wanted(s):
    # only the states the dart sentence is about: bold 7-9, rested, calm, by day
    return ("species fish " in s and num(s, "bold") >= 7 and num(s, "energy") >= 6
            and num(s, "hunger") <= 4 and num(s, "stress") <= 3 and " time day" in s)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src"); ap.add_argument("--out", required=True)
    ap.add_argument("--hosts", nargs="+", required=True)
    ap.add_argument("--per-host", type=int, default=2)
    ap.add_argument("--model", default="gemma4:26b")
    args = ap.parse_args()
    gt.SCHEMA = 5; gt.TEACHER = "ollama"
    rows = [json.loads(l) for l in open(args.src)]
    idx = [i for i, r in enumerate(rows) if wanted(r["state"])]
    print(f"{len(rows)} labels, relabelling {len(idx)}", flush=True)
    slots = [h for h in args.hosts for _ in range(args.per_host)]
    def ask(k_i):
        k, i = k_i
        for attempt in range(3):
            try:
                g = gt.ask_teacher(slots[(k + attempt) % len(slots)], args.model, rows[i]["state"], 300)
                if g: return i, g
            except Exception as e:
                print(f"[warn] {type(e).__name__}: {e}", file=sys.stderr)
        return i, None
    changed = kept = 0; moves = collections.Counter()
    side = open(args.out + ".relabelled", "w")
    with cf.ThreadPoolExecutor(len(slots)) as ex:
        for n, (i, g) in enumerate(ex.map(ask, enumerate(idx)), 1):
            old = rows[i]["goal"]
            if g is None: kept += 1; continue
            side.write(json.dumps({"state": rows[i]["state"], "old": old, "new": g}) + "\n")
            moves[(old.split()[0], g.split()[0])] += 1
            rows[i]["goal"] = g; changed += 1
            if n % 250 == 0: print(f"  {n}/{len(idx)}", flush=True)
    with open(args.out, "w") as f:
        for r in rows: f.write(json.dumps(r) + "\n")
    print(f"relabelled {changed}, kept old on failure {kept} -> {args.out}")
    old = collections.Counter(o for (o, _), v in moves.items() for _ in range(v))
    new = collections.Counter(n for (_, n), v in moves.items() for _ in range(v))
    for g in sorted(set(old) | set(new)):
        print(f"  {g:14s} {old[g]:5d} -> {new[g]:5d}")

if __name__ == "__main__":
    main()
