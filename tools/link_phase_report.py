#!/usr/bin/env python3
"""link_phase_report.py - per board and step of a link_multi.py log: decision
time, fps, internal heap, frames heard / lost and signal (the first 10 s line
of each step dropped). docs/tank-link-0.4.md, phase 0.

    python3 pocket-tank/tools/link_phase_report.py run.log
"""

import re, sys, collections
rows = collections.OrderedDict()
for l in open(sys.argv[1]):
    m = re.search(r"\[(\d+) (\w+)\]", l)
    if not m: continue
    k = (m.group(1), m.group(2))
    r = rows.setdefault(k, {"ms": [], "fr": [], "n": set(), "heap": [], "rx": 0, "lost": 0, "rssi": []})
    d = re.search(r"(\d+) fish goals.*decisions \d+ last (\d+) ms.*heap int (\d+) KB", l)
    if d: r["ms"].append(int(d.group(2))); r["n"].add(d.group(1)); r["heap"].append(int(d.group(3)))
    f = re.search(r"display: frame ([\d.]+) ms", l)
    if f: r["fr"].append(float(f.group(1)))
    p = re.search(r"rx (\d+) lost (\d+) \| rssi avg (-?\d+)", l)
    if p: r["rx"] += int(p.group(1)); r["lost"] += int(p.group(2)); r["rssi"].append(int(p.group(3)))
for (board, ph), r in sorted(rows.items()):
    ms = r["ms"][1:] or r["ms"]; fr = r["fr"][1:] or r["fr"]
    if not ms: continue
    link = f"  rx {r['rx']} lost {r['lost']} rssi {sum(r['rssi'])/len(r['rssi']):.0f}" if r["rssi"] else ""
    print(f"usbmodem{board:5} {ph:7} fish {','.join(sorted(r['n']))}  decision {sum(ms)/len(ms):5.0f} ms (n={len(ms):2})  "
          f"{1000/(sum(fr)/len(fr)) if fr else 0:4.1f} fps  heap {min(r['heap'])}-{max(r['heap'])} KB{link}")
