#!/bin/bash
# run_v3m_mixed.sh — train a v3-format student on v2 (converted: names dropped,
# trust 5) + v3 data. Cheap local experiment after the v3 probe showed the v3
# prompt flattened the teacher's personality conditioning (docs/stats.md).
# Outputs are suffixed _v3m so the pure-v3 artifacts stay intact.
set -u
cd "$(dirname "$0")"
PY=~/.venvs/aquapets/bin/python
LOG=out/v3m_mixed.log
log() { echo "[$(date '+%F %T')] $*" | tee -a "$LOG"; }
log "=== v3m mixed start ==="
python3 - <<'EOF' 2>&1 | tee -a out/v3m_mixed.log
import json, re
n=0
with open('out/v2_as_v3.jsonl','w') as out:
    for line in open('out/v2_clean.jsonl'):
        o=json.loads(line); s=o['state']
        s=re.sub(r'^fish \w+ ', '', s)                                  # drop "fish <name> "
        s=re.sub(r'stage (\w+) ', r'stage \1 trust 5 ', s, count=1)     # neutral trust
        s=re.sub(r'friend \w+ (near|mid|far) ', r'friend \1 ', s)       # drop friend name
        out.write(json.dumps({"state": s, "goal": o['goal']})+"\n"); n+=1
print(f"converted {n} v2 pairs -> out/v2_as_v3.jsonl")
EOF
python3 fold_traces.py --schema 3 out/v2_as_v3.jsonl out/v3_clean.jsonl --out out/v3m_clean.jsonl 2>&1 | tee -a "$LOG"
log "--- train (14M, mixed) ---"
AQUA_PETS_SCHEMA=3 $PY train.py --data "out/v3m_clean.jsonl" --dim 384 --n-layers 8 --n-heads 8 \
    --max-seq-len 64 --batch 64 --iters 5000 --lr 6e-4 --out out/ckpt_v3m.pt 2>&1 | tail -6 | tee -a "$LOG"
log "--- export + probe ---"
$PY export_q4.py out/model_q4_v3m.bin --checkpoint out/ckpt_v3m.pt 2>&1 | tee -a "$LOG"
$PY llama2.c/export.py out/model_v3m.bin --checkpoint out/ckpt_v3m.pt 2>&1 | tail -1 | tee -a "$LOG"
$PY probe_dist.py --schema 3 --ckpt out/ckpt_v3m.pt --data out/v3m_clean.jsonl 2>&1 | grep -v Warning | tee -a "$LOG"
log "=== v3m DONE ==="
