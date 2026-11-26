#!/usr/bin/env python3
"""link_distance_report.py - signal and loss per spot of a link_multi.py
distance run (one step per spot, moved at each `snd confirm` chime): only the
10 s windows wholly inside a spot count, the first MOVE_S seconds after its
chime skipped while hands move the boards. docs/tank-link-0.4.md, phase 0.

    python3 pocket-tank/tools/link_distance_report.py dist.log
"""

import re, sys
MOVE_S = 6.0
steps, peers = [], []
for l in open(sys.argv[1]):
    m = re.match(r"\s*([\d.]+) ==== (\w+):", l)
    if m: steps.append((float(m.group(1)), m.group(2))); continue
    m = re.match(r"\s*([\d.]+) \[(\d+) \w+\] .*?([0-9a-f:]{17}): rx (\d+) lost (\d+) \| rssi avg (-?\d+) min (-?\d+) max (-?\d+)", l)
    if m: peers.append((float(m.group(1)), m.group(2), int(m.group(4)), int(m.group(5)), int(m.group(6)), int(m.group(7)), int(m.group(8))))
bounds = [(t, steps[i + 1][0] if i + 1 < len(steps) else 1e9, lab) for i, (t, lab) in enumerate(steps)]
print(f"{'position':8} {'board':6} {'windows':7} {'rx':>5} {'lost':>5} {'loss%':>6} {'rssi avg':>8} {'min':>5} {'max':>5}")
for t0, t1, lab in bounds:
    if lab in ("done", "off"): continue
    start = t0 + (MOVE_S if lab != steps[0][1] else 0)
    for board in sorted({p[1] for p in peers}):
        ws = [p for p in peers if p[1] == board and p[0] - 10.5 >= start and p[0] <= t1 + 0.5]
        if not ws: print(f"{lab:8} {board:6} {'none':7}"); continue
        rx = sum(w[2] for w in ws); lost = sum(w[3] for w in ws)
        avg = sum(w[4] * w[2] for w in ws) / max(rx, 1)
        print(f"{lab:8} {board:6} {len(ws):7} {rx:5} {lost:5} {100 * lost / max(rx + lost, 1):6.1f} {avg:8.1f} {min(w[5] for w in ws):5} {max(w[6] for w in ws):5}")
