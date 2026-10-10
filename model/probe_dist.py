"""probe_dist.py — probe the student's goal *distribution* (not just argmax) on real and synthetic states.
Runs on CPU with the shipped word-level v2 checkpoint (ckpt_v2w.pt), ~1 min.
Numbers feed docs/progression-next.md. Run: ~/.venvs/aquapets/bin/python model/probe_dist.py"""
import sys, os, json, random, math, argparse
MODEL_DIR = os.path.dirname(os.path.abspath(__file__))
_ap = argparse.ArgumentParser()
_ap.add_argument("--schema", type=int, choices=(2, 3, 4, 5), default=2)
_ap.add_argument("--ckpt", default=None, help="checkpoint (default: ckpt_v2w.pt / ckpt_v3.pt)")
_ap.add_argument("--data", default=None, help="clean jsonl to sample real states from")
_args = _ap.parse_args()
os.environ["AQUA_PETS_SCHEMA"] = str(_args.schema)
sys.path.insert(0, MODEL_DIR); sys.path.insert(0, os.path.join(MODEL_DIR, "llama2.c"))
import torch
from export import load_checkpoint
import train_tokenizer as tok

torch.manual_seed(0); random.seed(0)
model = load_checkpoint(_args.ckpt or os.path.join(MODEL_DIR, "out", {2: "ckpt_v2w.pt", 3: "ckpt_v3m.pt", 4: "ckpt_v4m.pt", 5: "ckpt_v5j.pt"}[_args.schema]))
GOALS = ["seek_food","flee_shadow","visit_bubbles","follow_friend","explore","rest","dart_play","inspect_reef"]
GID = [tok.encode(g)[0] for g in GOALS]

@torch.no_grad()
def goal_probs(state, T=1.0):
    ids = tok.encode(state + " ->", bos=True)
    x = torch.tensor([ids], dtype=torch.long)
    logits = model(x)[0, -1, :]            # next-token logits
    g = logits[GID] / T
    p = torch.softmax(g, dim=0)
    mass = torch.softmax(logits, dim=0)[GID].sum().item()   # how much prob sits on goal tokens at all
    return p.tolist(), mass

def ent(p): return -sum(q*math.log(q+1e-12) for q in p)
def tv(p, q): return 0.5*sum(abs(a-b) for a,b in zip(p,q))
def fmt(p): return " ".join(f"{GOALS[i][:6]}={p[i]:.2f}" for i in sorted(range(8), key=lambda i:-p[i])[:4])

# ---------- 1. real dataset states ----------
lines = open(_args.data or os.path.join(MODEL_DIR, "out", f"v{_args.schema}_clean.jsonl")).read().splitlines()
sample = random.sample(lines, min(800, len(lines)))
top1s, margins, ents, masses = [], [], [], []
t_in_top1 = t_in_top2 = 0
for ln in sample:
    o = json.loads(ln); p, mass = goal_probs(o["state"])
    order = sorted(range(8), key=lambda i: -p[i])
    top1s.append(p[order[0]]); margins.append(p[order[0]]-p[order[1]]); ents.append(ent(p)); masses.append(mass)
    tg = o["goal"].split()[0]
    if GOALS[order[0]] == tg: t_in_top1 += 1
    if tg in (GOALS[order[0]], GOALS[order[1]]): t_in_top2 += 1
n = len(sample)
print(f"== {n} real states ==")
print(f"goal-token mass (sanity, should be ~1): mean {sum(masses)/n:.3f} min {min(masses):.3f}")
print(f"top-1 prob: mean {sum(top1s)/n:.2f}; >0.9: {sum(t>0.9 for t in top1s)/n:.0%}; <0.6: {sum(t<0.6 for t in top1s)/n:.0%}")
print(f"'torn' (top-2 margin <0.2): {sum(m<0.2 for m in margins)/n:.0%}")
print(f"entropy mean {sum(ents)/n:.2f} nats (max {math.log(8):.2f})")
print(f"teacher label == greedy: {t_in_top1/n:.0%}; teacher label in top-2: {t_in_top2/n:.0%}")
print(f"P(sample != greedy) at T=1: {1-sum(top1s)/n:.0%}")

# ---------- 2. synthetic probes ----------
def S(name="mira", zone=2, hunger=3, energy=6, stress=1, curiosity=6, bold=5, social=5, stage="adult",
      food="none", shadow="none", friend="bolt mid 3", bubble="mid 10", reef="far 7", wall="clear", last="explore", time="day",
      trust=5, bored=1, species="fish"):
    if _args.schema >= 5:
        fr = friend if friend == "none" else friend.split(" ", 1)[1]
        return (f"zone {zone} hunger {hunger} energy {energy} stress {stress} curiosity {curiosity} "
                f"bold {bold} social {social} stage {stage} species {species} trust {trust} bored {bored} food {food} "
                f"friend {fr} bubble {bubble} reef {reef} wall {wall} last {last} time {time}")
    if _args.schema >= 4:
        fr = friend if friend == "none" else friend.split(" ", 1)[1]
        return (f"zone {zone} hunger {hunger} energy {energy} stress {stress} curiosity {curiosity} "
                f"bold {bold} social {social} stage {stage} trust {trust} bored {bored} food {food} friend {fr} "
                f"bubble {bubble} reef {reef} wall {wall} last {last} time {time}")
    if _args.schema >= 3:
        fr = friend if friend == "none" else friend.split(" ", 1)[1]     # v3: no friend names
        return (f"zone {zone} hunger {hunger} energy {energy} stress {stress} curiosity {curiosity} "
                f"bold {bold} social {social} stage {stage} trust {trust} food {food} shadow {shadow} friend {fr} "
                f"bubble {bubble} reef {reef} wall {wall} last {last} time {time}")
    return (f"fish {name} zone {zone} hunger {hunger} energy {energy} stress {stress} curiosity {curiosity} "
            f"bold {bold} social {social} stage {stage} food {food} shadow {shadow} friend {friend} "
            f"bubble {bubble} reef {reef} wall {wall} last {last} time {time}")

print("\n== starving (hunger 9, food near 12, no shadow): P(seek_food) across identities ==")
worst = 1.0
for bold in (0,5,9):
    for social in (0,9):
        for stage in ("fry","adult","elder"):
            p,_ = goal_probs(S(hunger=9, food="near 12", bold=bold, social=social, stage=stage))
            worst = min(worst, p[0])
print(f"  min P(seek_food) over 18 identities: {worst:.2f}")
p,_ = goal_probs(S(hunger=9, food="near 12")); print(f"  adult/5/5: {fmt(p)}  H={ent(p):.2f}")
p,_ = goal_probs(S(hunger=9, food="near 12", bold=0, stage="fry", stress=6)); print(f"  timid stressed fry: {fmt(p)}  H={ent(p):.2f}")
for T in (0.5, 0.8, 1.0):
    p,_ = goal_probs(S(hunger=9, food="near 12", bold=0, stage="fry", stress=6), T=T); print(f"    T={T}: P(not seek_food)={1-p[0]:.2f}")

print("\n== content fish (hunger 2, energy 7, stress 0, curiosity 7, nothing urgent) ==")
base = S(hunger=2, energy=7, stress=0, curiosity=7)
p,_ = goal_probs(base); print(f"  adult bold5 social5: {fmt(p)}  H={ent(p):.2f}")
print("  -- bold sweep 0..9 (TV distance from previous step; smooth = drift is continuous) --")
prev=None
for b in range(10):
    p,_ = goal_probs(S(hunger=2, energy=7, stress=0, curiosity=7, bold=b))
    print(f"    bold {b}: {fmt(p)}" + (f"   dTV={tv(p,prev):.2f}" if prev else "")); prev=p
print("  -- social sweep --")
for s in (0,3,6,9):
    p,_ = goal_probs(S(hunger=2, energy=7, stress=0, curiosity=7, social=s)); print(f"    social {s}: {fmt(p)}")
print("  -- stage sweep --")
for st in ("fry","juv","adult","elder"):
    p,_ = goal_probs(S(hunger=2, energy=7, stress=0, curiosity=7, stage=st)); print(f"    {st:5}: {fmt(p)}  H={ent(p):.2f}")

if _args.schema >= 4:
    print("\n== boredom (v4): content fish at the bubbles, bored 0..9 -> P(bubbles again) / P(explore) ==")
    for b in range(10):
        p,_ = goal_probs(S(hunger=2, energy=7, stress=0, curiosity=7, bubble="near 12", last="visit_bubbles", bored=b))
        print(f"    bored {b}: P(bubbles)={p[2]:.2f} P(explore)={p[4]:.2f}  {fmt(p)}")
    print("  social 8 fish following a friend, bored 0 / 5 / 9:")
    for b in (0, 5, 9):
        p,_ = goal_probs(S(hunger=2, energy=7, stress=0, curiosity=6, social=8, friend="bolt near 12", last="follow_friend", bored=b))
        print(f"    bored {b}: P(follow)={p[3]:.2f} P(explore)={p[4]:.2f}  {fmt(p)}")
    print("  night, elder, bored 9, last rest (boredom must not wake the tank):")
    p,_ = goal_probs(S(hunger=2, energy=5, stress=0, time="night", stage="elder", last="rest", bored=9)); print(f"    {fmt(p)}")
    print("  starving, bored 9 (boredom never outranks hunger):")
    p,_ = goal_probs(S(hunger=9, food="near 12", bored=9, last="seek_food")); print(f"    P(seek_food)={p[0]:.2f}  {fmt(p)}")
    print("  P(flee_shadow) anywhere above (should be ~0): the goal token is in the vocab, never a label")
if _args.schema >= 5:
    # v5: each species at its own mid-range traits (common/tank.c SPECIES[]),
    # the same content / night / starving states - the species word must move
    # the distribution its own way, never past hunger
    RANGES = {"fish": (5, 5, 6), "seahorse": (2, 6, 4), "octopus": (5, 1, 8), "puffer": (4, 3, 7), "angler": (6, 1, 2),
              "eel": (7, 2, 4), "shark": (8, 6, 5), "squid": (4, 7, 6), "crab": (5, 3, 6), "lobster": (6, 1, 5),
              "jellyfish": (2, 5, 5)}
    print("\n== species (v5): content day / content night / starving, each at its species' typical bold social curiosity ==")
    for sp, (b, so, cu) in RANGES.items():
        kw = dict(species=sp, bold=b, social=so, curiosity=cu, zone=5 if sp in ("octopus", "angler", "crab", "lobster", "eel") else 2)
        pd_, _ = goal_probs(S(hunger=2, energy=7, stress=0, **kw))
        pn, _ = goal_probs(S(hunger=2, energy=6, stress=0, time="night", last="rest", **kw))
        ps, _ = goal_probs(S(hunger=9, food="near 12", **kw))
        pf, _ = goal_probs(S(hunger=2, energy=7, stress=0, friend="bolt near 12", **dict(kw, social=max(so, 7))))
        print(f"  {sp:9s} day: {fmt(pd_)}")
        print(f"  {'':9s} night: P(rest)={pn[5]:.2f}  starving: P(seek_food)={ps[0]:.2f}  friend near: P(follow)={pf[3]:.2f}")
    print("  (want: angler / lobster day rest high, shark rest ~0, squid follow high, lobster / eel night awake,")
    print("   octopus / puffer reef + explore, seahorse dart ~0, crab seek_food early; starving >= 0.85 for all)")

print("\n== shadow far 6 (distant predator): P(flee) by identity ==" if _args.schema < 4 else "\n== (shadow probes skipped: no shadow in v4) ==")
for bold,stage in ((0,"fry"),(0,"adult"),(5,"adult"),(9,"adult"),(9,"juv")) if _args.schema < 4 else ():
    p,_ = goal_probs(S(hunger=3, shadow="far 6", bold=bold, stage=stage)); print(f"  bold {bold} {stage:5}: P(flee)={p[1]:.2f}  {fmt(p)}")
if _args.schema < 4: print("  shadow mid 12:")
for bold in (0,5,9) if _args.schema < 4 else ():
    p,_ = goal_probs(S(hunger=3, shadow="mid 12", bold=bold)); print(f"  bold {bold}: P(flee)={p[1]:.2f}  {fmt(p)}")

print("\n== night, content ==")
for st in ("juv","elder"):
    p,_ = goal_probs(S(hunger=2, energy=5, stress=0, time="night", stage=st)); print(f"  {st}: {fmt(p)}")

print("\n== OOD check: 'friend none' (17/28312 in training) vs friend present ==")
for fr in ("bolt mid 3", "none"):
    p,mass = goal_probs(S(hunger=2, energy=7, stress=0, curiosity=7, friend=fr)); print(f"  friend {fr:10}: {fmt(p)}  H={ent(p):.2f} goalmass={mass:.3f}")
    p,mass = goal_probs(S(hunger=9, food="near 12", friend=fr)); print(f"  starving, friend {fr:10}: {fmt(p)} goalmass={mass:.3f}")
    if _args.schema < 4:
        p,mass = goal_probs(S(hunger=3, shadow="near 12", friend=fr)); print(f"  shadow near, friend {fr:10}: {fmt(p)} goalmass={mass:.3f}")

# ---------- 3. shadow / hunger / name probes ----------
def P(state): return goal_probs(state)[0]
if _args.schema < 4: print("shadow near, varying stress / clock / bold / last:")
for kw in [] if _args.schema >= 4 else [dict(shadow="near 12"), dict(shadow="near 12", stress=5), dict(shadow="near 12", stress=8),
           dict(shadow="near 6"), dict(shadow="near 3", stress=4), dict(shadow="near 12", bold=0),
           dict(shadow="near 12", bold=9), dict(shadow="near 12", last="flee_shadow"),
           dict(shadow="near 12", stress=5, bold=0, stage="fry"), dict(shadow="mid 12", stress=5),
           dict(shadow="mid 12", stress=5, bold=0), dict(shadow="far 12", stress=5, bold=0)]:
    p = P(S(**kw)); print(f"  {str(kw):60} P(flee)={p[1]:.2f}  {fmt(p)}")
print("\nhunger mid-range (5,6,7) with food near -> P(seek_food):")
for h in (4,5,6,7,8):
    p = P(S(hunger=h, food="near 12")); print(f"  hunger {h}: P(seek)={p[0]:.2f} {fmt(p)}")
print("\nname swap (names carry no signal?) content state:")
for nm in ("mira","bolt","kelp","nori"):
    p = P(S(name=nm, hunger=2, energy=7, stress=0, curiosity=7)); print(f"  {nm}: {fmt(p)}")


# ---------- 4. personality across several content states (is conditioning present, or is one goal an attractor?) ----------
print("\n== personality over 6 content states: mean P(follow) social 0 vs 9; mean P(dart) bold 0 vs 9; mean TV(social0,social9), TV(bold0,bold9) ==")
states=[dict(hunger=2,energy=7,stress=0,curiosity=7), dict(hunger=3,energy=9,stress=1,curiosity=3), dict(hunger=1,energy=8,stress=0,curiosity=5,friend="bolt near 2"),
        dict(hunger=2,energy=6,stress=2,curiosity=8,zone=5,reef="mid 10",bubble="far 3"), dict(hunger=4,energy=9,stress=0,curiosity=6,last="dart_play"), dict(hunger=0,energy=9,stress=0,curiosity=9,zone=1,reef="near 12",bubble="far 6")]
fs0=fs9=db0=db9=tvs=tvb=0
for st in states:
    p0,_=goal_probs(S(social=0,**st)); p9,_=goal_probs(S(social=9,**st))
    b0,_=goal_probs(S(bold=0,**st)); b9,_=goal_probs(S(bold=9,**st))
    fs0+=p0[3]; fs9+=p9[3]; db0+=b0[6]; db9+=b9[6]; tvs+=tv(p0,p9); tvb+=tv(b0,b9)
    print(f"  {str({k:v for k,v in st.items()})[:70]:70} social0->9: {fmt(p0)[:40]:40} | {fmt(p9)[:40]}")
n=len(states)
print(f"  MEAN: P(follow) social0 {fs0/n:.2f} -> social9 {fs9/n:.2f};  P(dart) bold0 {db0/n:.2f} -> bold9 {db9/n:.2f};  TV social {tvs/n:.2f}, TV bold {tvb/n:.2f}")
