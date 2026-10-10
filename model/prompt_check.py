#!/usr/bin/env python3
"""Check a teacher prompt BEFORE the overnight run. Stdlib only.

The v3 overnight taught us the expensive way that one sentence in the teacher
prompt reshapes every label (P(follow | social>=8) fell 0.50 -> 0.18). This
asks the teacher ~300 questions on a fixed diagnostic panel and prints the
conditional rates the student will inherit - ~7 minutes with 3 workers, not
8 hours. Use it to compare prompts, or teachers (--model qwen3.5:27b ...).

  python3 prompt_check.py --schema 3                       # current v3 prompt
  python3 prompt_check.py --schema 3 --prompt-file cand.txt # a candidate prompt
  python3 prompt_check.py --schema 3 --model qwen3.5:27b   # another teacher

Panel (per schema line, identity randomized unless the case pins it):
  social9   content fish, social 9 (expect follow_friend high)     x40
  social1   content fish, social 1 (expect follow_friend ~0)        x40
  bold9     content + energetic, bold 9 (expect dart_play present)  x40
  bold1     same, bold 1 (expect dart_play ~0)                      x40
  content   content, mid identity (watch for a single attractor)    x40
  shadowcalm shadow near, stress 0-3 (expect flee high)             x40
  lonely    friend none, content (must not flee / seek food)        x30
  starving  hunger 9, food near (expect seek_food ~1.0)             x30
  trust     content, trust 9 vs trust 0 (v3 only; expect a shade, not a flip) x40
  v4 (--schema 4; the shadow cases are skipped, bored cases added):
  bored9bub  content, bored 9, last visit_bubbles (expect a change, explore up) x40
  bored9fol  social 8, friend near, bored 9, last follow_friend (expect a change) x40
  bored0bub  content, bored 0, last visit_bubbles (expect last mostly KEPT)     x40
  nightbored night, bored 9, last rest (expect rest - boredom never wakes the tank) x30
  v5 (--schema 5; every v4 case stays, about the classic fish (`species fish`), plus
  one panel per species, its bold / social / curiosity rolled inside its range):
  sp_angler   content, day (expect rest - lying in wait - high)               x30
  sp_shark    content, day (expect explore high, rest ~0)                      x30
  sp_squid    friend near (expect follow_friend high - the school)             x30
  sp_octopus  content (expect inspect_reef + explore, follow ~0)               x30
  sp_seahorse energetic (expect dart_play ~0, rest / reef high)                x30
  sp_puffer   content (expect explore + inspect_reef)                          x30
  sp_crab     hunger 4-5, food mid (expect seek_food - the eager scavenger)    x30
  sp_lobster  day (expect rest - the den) / sp_lobster_n night (expect awake)  x30 / x30
  sp_eel_n    night (expect awake more than resting)                           x30
  sp_jelly    energetic (expect dart ~0, rest + explore) / sp_jelly_b bubbles near x30 / x30
  sp_starving every species, hunger 9, food near (expect seek_food ~1.0)       x40
  --teacher rules runs the panel against gen_traces' rule policy (a dry run of
  this script, no Ollama).
"""
import argparse
import concurrent.futures as cf
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_traces as gt  # noqa: E402


def state(rng, schema, **pin):
    f = dict(zone=rng.randint(1, 6), hunger=rng.randint(0, 3), energy=rng.randint(5, 9), stress=rng.randint(0, 2),
             curiosity=rng.randint(3, 8), bold=rng.randint(2, 7), social=rng.randint(2, 6),
             stage=rng.choice(["juv", "adult", "adult", "elder"]), trust=rng.randint(2, 7),
             bored=rng.randint(0, 2), food="none", shadow="none", friend=f"{rng.choice(['near','mid'])} {rng.randint(1,12)}",
             bubble=f"{rng.choice(['near','mid','far'])} {rng.randint(1,12)}",
             reef=f"{rng.choice(['mid','far'])} {rng.randint(1,12)}", wall="clear",
             last=rng.choice(["explore", "inspect_reef", "visit_bubbles", "rest"]), time="day")
    if schema >= 5:
        f["species"] = pin.get("species", "fish")
        if f["species"] != "fish":         # its traits inside its species' range unless the case pins them
            b0, b1, s0, s1 = gt.SPECIES[f["species"]][:4]
            f["bold"] = gt.trait9(rng.uniform(max(0.0, b0 - 0.1), min(1.0, b1 + 0.1)))
            f["social"] = gt.trait9(rng.uniform(max(0.0, s0 - 0.1), min(1.0, s1 + 0.1)))
            f["curiosity"] = gt.species_curiosity(f["species"], rng)
    f.update(pin)
    ident = f"hunger {f['hunger']} energy {f['energy']} stress {f['stress']} curiosity {f['curiosity']} bold {f['bold']} social {f['social']} stage {f['stage']}"
    tail = (f"food {f['food']} shadow {f['shadow']} friend {f['friend']} bubble {f['bubble']} reef {f['reef']} "
            f"wall {f['wall']} last {f['last']} time {f['time']}")
    if schema >= 5:
        return gt.render_v5(dict(f, food=f["food"], friend=f["friend"]))
    if schema >= 4:
        return (f"zone {f['zone']} {ident} trust {f['trust']} bored {f['bored']} food {f['food']} friend {f['friend']} "
                f"bubble {f['bubble']} reef {f['reef']} wall {f['wall']} last {f['last']} time {f['time']}")
    if schema >= 3:
        return f"zone {f['zone']} {ident} trust {f['trust']} {tail}"
    name = rng.choice(["mira", "bolt", "kelp", "nori"])
    fr = f['friend'] if f['friend'] == "none" else f"{rng.choice(['mira','bolt','kelp','nori'])} {f['friend']}"
    return f"fish {name} zone {f['zone']} {ident} {tail.replace('friend ' + f['friend'], 'friend ' + fr, 1)}"


def panel(rng, schema, n):
    P = []
    def add(case, k, **pin):
        for _ in range(int(n * k / 40)):
            P.append((case, state(rng, schema, **pin)))
    add("social9", 40, social=9)
    add("social1", 40, social=1)
    add("bold9", 40, bold=9, energy=9, hunger=rng.randint(0, 2))
    add("bold1", 40, bold=1, energy=9, hunger=rng.randint(0, 2))
    add("content", 40)
    if schema < 4:
        add("shadowcalm", 40, shadow=f"near {rng.randint(1,12)}", stress=rng.randint(0, 3))
    add("lonely", 30, friend="none")
    add("starving", 30, hunger=9, food=f"near {rng.randint(1,12)}")
    if schema >= 3:
        add("trust9", 20, trust=9)
        add("trust0", 20, trust=0)
    if schema >= 5:
        add("sp_angler", 30, species="angler", zone=rng.randint(4, 6))
        add("sp_shark", 30, species="shark")
        add("sp_squid", 30, species="squid", friend=f"near {rng.randint(1,12)}")
        add("sp_octopus", 30, species="octopus", zone=rng.randint(4, 6))
        add("sp_seahorse", 30, species="seahorse", energy=9, hunger=rng.randint(0, 2))
        add("sp_puffer", 30, species="puffer")
        add("sp_crab", 30, species="crab", hunger=rng.choice([4, 5]), food=f"mid {rng.randint(1,12)}", zone=rng.randint(4, 6))
        add("sp_lobster", 30, species="lobster", zone=rng.randint(4, 6))
        add("sp_lobster_n", 30, species="lobster", zone=rng.randint(4, 6), time="night", last="rest", energy=rng.randint(4, 8))
        add("sp_eel_n", 30, species="eel", zone=rng.randint(4, 6), time="night", last="rest", energy=rng.randint(4, 8))
        add("sp_jelly", 30, species="jellyfish", energy=rng.randint(7, 9), hunger=rng.randint(0, 2), bold=rng.randint(1, 4))
        add("sp_jelly_b", 30, species="jellyfish", bubble=f"near {rng.randint(1,12)}", hunger=rng.randint(0, 3))
        for sp in gt.SPECIES_TOKENS:
            add("sp_starving", 4, species=sp, hunger=9, food=f"near {rng.randint(1,12)}")
    if schema >= 4:
        add("bored9bub", 40, bored=9, last="visit_bubbles", bubble=f"near {rng.randint(1,12)}")
        add("bored9fol", 40, bored=9, last="follow_friend", social=8, friend=f"near {rng.randint(1,12)}")
        add("bored0bub", 40, bored=0, last="visit_bubbles", bubble=f"near {rng.randint(1,12)}")
        add("nightbored", 30, bored=9, last="rest", time="night", energy=rng.randint(3, 6), reef=f"near {rng.randint(1,12)}")
    return P


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--schema", type=int, choices=(2, 3, 4, 5), default=3)
    ap.add_argument("--teacher", choices=("ollama", "rules"), default="ollama",
                    help="rules = gen_traces' rule policy: exercises this script without Ollama")
    ap.add_argument("--host", default="http://192.168.0.139:11434")
    ap.add_argument("--model", default="gemma4:26b")
    ap.add_argument("--prompt-file", default=None, help="candidate system prompt (default: gen_traces' prompt for the schema)")
    ap.add_argument("--n", type=int, default=40, help="calls per major case (default 40 -> ~320 calls)")
    ap.add_argument("--workers", type=int, default=3)
    ap.add_argument("--seed", type=int, default=11)
    args = ap.parse_args()
    gt.SCHEMA = args.schema
    gt.TEACHER = args.teacher
    if args.prompt_file:
        txt = open(args.prompt_file).read().strip()
        gt.SYSTEM_PROMPT = txt
        gt.SYSTEM_PROMPT_V3 = txt
        gt.SYSTEM_PROMPT_V4 = txt
        gt.SYSTEM_PROMPT_V5 = txt
    rng = random.Random(args.seed)
    P = panel(rng, args.schema, args.n)
    who = "the RULE teacher (a dry run)" if args.teacher == "rules" else f"{args.model} @ {args.host}"
    print(f"{len(P)} calls to {who} ({args.workers} workers) ...", flush=True)

    def ask(item):
        case, st = item
        try:
            g = gt.ask_teacher(args.host, args.model, st, 30)
        except Exception as e:  # noqa: BLE001
            g = None
        return case, (g.split()[0] if g else None)

    results = {}
    fails = 0
    with cf.ThreadPoolExecutor(args.workers) as ex:
        for case, goal in ex.map(ask, P):
            if goal is None:
                fails += 1
                continue
            results.setdefault(case, {}).setdefault(goal, 0)
            results[case][goal] += 1

    def rate(case, goal):
        d = results.get(case, {}); n = sum(d.values())
        return (d.get(goal, 0) / n if n else float("nan")), n

    def top(case):
        d = results.get(case, {}); n = sum(d.values()) or 1
        return ", ".join(f"{g} {c/n:.2f}" for g, c in sorted(d.items(), key=lambda kv: -kv[1])[:3])

    print(f"\nfailures: {fails}")
    if gt.MAX_PROMPT_TOKENS:
        # a prompt that fills the window was cut by Ollama: the teacher never read all of it
        print(f"teacher prompt: up to {gt.MAX_PROMPT_TOKENS} tokens evaluated, num_ctx {gt.num_ctx()}"
              f"{'  <-- TRUNCATED?' if gt.MAX_PROMPT_TOKENS >= gt.num_ctx() - 16 else ''}")
    checks = [
        ("P(follow | social 9)", *rate("social9", "follow_friend"), ">= 0.40", lambda v: v >= 0.40),
        ("P(follow | social 1)", *rate("social1", "follow_friend"), "<= 0.05", lambda v: v <= 0.05),
        ("P(dart | bold 9, energetic)", *rate("bold9", "dart_play"), ">= 0.10", lambda v: v >= 0.10),
        ("P(dart | bold 1, energetic)", *rate("bold1", "dart_play"), "<= 0.03", lambda v: v <= 0.03),
        ("P(seek_food | starving)", *rate("starving", "seek_food"), ">= 0.95", lambda v: v >= 0.95),
        ("P(seek_food | lonely, not hungry)", *rate("lonely", "seek_food"), "<= 0.05", lambda v: v <= 0.05),
    ]
    if args.schema < 4:
        checks += [
            ("P(flee | shadow near, calm)", *rate("shadowcalm", "flee_shadow"), ">= 0.80", lambda v: v >= 0.80),
            ("P(flee | lonely, no shadow)", *rate("lonely", "flee_shadow"), "== 0", lambda v: v <= 0.02),
        ]
    else:
        checks += [
            ("P(bubbles again | bored 9, last bubbles)", *rate("bored9bub", "visit_bubbles"), "<= 0.15", lambda v: v <= 0.15),
            ("P(explore | bored 9, last bubbles)", *rate("bored9bub", "explore"), ">= 0.25", lambda v: v >= 0.25),
            ("P(follow again | bored 9, last follow)", *rate("bored9fol", "follow_friend"), "<= 0.15", lambda v: v <= 0.15),
            ("P(bubbles kept | bored 0, last bubbles)", *rate("bored0bub", "visit_bubbles"), ">= 0.40", lambda v: v >= 0.40),
            ("P(rest | night, bored 9, last rest)", *rate("nightbored", "rest"), ">= 0.70", lambda v: v >= 0.70),
            ("P(flee | any v4 case)", (sum(d.get("flee_shadow", 0) for d in results.values()) / max(1, sum(sum(d.values()) for d in results.values()))), sum(sum(d.values()) for d in results.values()), "== 0", lambda v: v <= 0.001),
        ]
    if args.schema >= 5:
        def rate2(case, *gs):
            d = results.get(case, {}); n = sum(d.values())
            return (sum(d.get(g, 0) for g in gs) / n if n else float("nan")), n
        checks += [
            ("P(rest | angler, content day)", *rate("sp_angler", "rest"), ">= 0.40", lambda v: v >= 0.40),
            ("P(rest | shark, content day)", *rate("sp_shark", "rest"), "<= 0.10", lambda v: v <= 0.10),
            ("P(explore | shark, content day)", *rate("sp_shark", "explore"), ">= 0.40", lambda v: v >= 0.40),
            ("P(follow | squid, friend near)", *rate("sp_squid", "follow_friend"), ">= 0.40", lambda v: v >= 0.40),
            ("P(reef+explore | octopus)", *rate2("sp_octopus", "inspect_reef", "explore"), ">= 0.60", lambda v: v >= 0.60),
            ("P(follow | octopus)", *rate("sp_octopus", "follow_friend"), "<= 0.05", lambda v: v <= 0.05),
            ("P(dart | seahorse, energetic)", *rate("sp_seahorse", "dart_play"), "<= 0.03", lambda v: v <= 0.03),
            ("P(explore+reef | puffer)", *rate2("sp_puffer", "explore", "inspect_reef"), ">= 0.50", lambda v: v >= 0.50),
            ("P(seek_food | crab, hunger 4-5, food mid)", *rate("sp_crab", "seek_food"), ">= 0.60", lambda v: v >= 0.60),
            ("P(rest | lobster, day)", *rate("sp_lobster", "rest"), ">= 0.40", lambda v: v >= 0.40),
            ("P(rest | lobster, night)", *rate("sp_lobster_n", "rest"), "<= 0.40", lambda v: v <= 0.40),
            ("P(follow | lobster, day)", *rate("sp_lobster", "follow_friend"), "<= 0.05", lambda v: v <= 0.05),
            ("P(rest | eel, night)", *rate("sp_eel_n", "rest"), "<= 0.50", lambda v: v <= 0.50),
            ("P(dart | jellyfish, energetic)", *rate("sp_jelly", "dart_play"), "<= 0.03", lambda v: v <= 0.03),
            ("P(rest+explore | jellyfish, content)", *rate2("sp_jelly", "rest", "explore"), ">= 0.55", lambda v: v >= 0.55),
            ("P(bubbles | jellyfish, bubble near)", *rate("sp_jelly_b", "visit_bubbles"), ">= 0.20", lambda v: v >= 0.20),
            ("P(seek_food | any species, starving)", *rate("sp_starving", "seek_food"), ">= 0.95", lambda v: v >= 0.95),
        ]
    for name, v, n, want, ok in checks:
        print(f"  {'OK ' if ok(v) else 'BAD'}  {name:36s} = {v:.2f}  (n={n}, want {want})")
    # attractor check: no single goal should own the content fish
    d = results.get("content", {}); n = sum(d.values()) or 1
    mx = max(d.values()) / n if d else 0
    print(f"  {'OK ' if mx < 0.45 else 'BAD'}  {'max single goal | content':36s} = {mx:.2f}  (want < 0.45)   [{top('content')}]")
    if args.schema >= 3:
        print(f"       trust 9: [{top('trust9')}]\n       trust 0: [{top('trust0')}]")
    print("\n  per case:")
    for case in ("social9", "social1", "bold9", "bold1", "content", "shadowcalm", "lonely", "starving", "trust9", "trust0",
                 "bored9bub", "bored9fol", "bored0bub", "nightbored",
                 "sp_angler", "sp_shark", "sp_squid", "sp_octopus", "sp_seahorse", "sp_puffer", "sp_crab",
                 "sp_lobster", "sp_lobster_n", "sp_eel_n", "sp_jelly", "sp_jelly_b", "sp_starving"):
        if case in results:
            print(f"    {case:10s} {top(case)}")


if __name__ == "__main__":
    main()
