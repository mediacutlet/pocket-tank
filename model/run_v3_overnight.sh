#!/bin/bash
# run_v3_overnight.sh — the schema v3 data cycle, unattended (docs/retrain-v3.md
# steps 1-3). Launch with nohup; everything logs to out/v3_overnight.log.
#
#   nohup ./run_v3_overnight.sh > /dev/null 2>&1 &
#
# 1. three trace workers against the Mac Mini teacher, hard 8 h wall-clock cap
# 2. fold + closed-vocabulary verify -> out/v3_clean.jsonl
# 3. train the 14M student (AQUA_PETS_SCHEMA=3), export 4-bit + fp32, probe
# Step 4 (shipping the model into sim/firmware) is deliberately manual.
set -u
cd "$(dirname "$0")"
LOG=out/v3_overnight.log
PY=~/.venvs/aquapets/bin/python
HOST=${HOST:-http://localhost:11434}
MINUTES=${MINUTES:-480}
COUNT=${COUNT:-12000}
log() { echo "[$(date '+%F %T')] $*" | tee -a "$LOG"; }

mkdir -p out
log "=== v3 overnight start: host $HOST, $MINUTES min cap, 3 workers ==="
for s in a b c; do
  case $s in a) seed=31;; b) seed=32;; c) seed=33;; esac
  nohup python3 gen_traces.py --schema 3 --count $COUNT --max-minutes $MINUTES \
      --seed $seed --host "$HOST" --out out/v3_traces_$s.jsonl > out/v3_gen_$s.log 2>&1 &
  echo $! > out/v3_gen_$s.pid
  log "worker $s pid $! seed $seed -> out/v3_traces_$s.jsonl"
done

# progress every 30 min while any worker lives
while pgrep -f "gen_traces.py --schema 3" > /dev/null; do
  sleep 1800
  n=$(cat out/v3_traces_*.jsonl 2>/dev/null | wc -l | tr -d ' ')
  log "progress: $n pairs so far"
done
log "workers done: $(cat out/v3_traces_*.jsonl | wc -l | tr -d ' ') raw pairs"

log "--- fold ---"
python3 fold_traces.py --schema 3 out/v3_traces_*.jsonl --out out/v3_clean.jsonl 2>&1 | tee -a "$LOG"
python3 train_tokenizer.py --schema 3 --verify "out/v3_clean.jsonl" 2>&1 | tee -a "$LOG"
n=$(wc -l < out/v3_clean.jsonl | tr -d ' ')
if [ "$n" -lt 5000 ]; then log "ABORT: only $n clean pairs - not training"; exit 1; fi

log "--- train (14M, schema v3, $n pairs) ---"
AQUA_PETS_SCHEMA=3 $PY train.py --data "out/v3_clean.jsonl" --dim 384 --n-layers 8 --n-heads 8 \
    --max-seq-len 64 --batch 64 --iters 4000 --lr 6e-4 --out out/ckpt_v3.pt 2>&1 | tail -15 | tee -a "$LOG"
[ -f out/ckpt_v3.pt ] || { log "ABORT: no checkpoint"; exit 1; }

log "--- export ---"
$PY export_q4.py out/model_q4_v3.bin --checkpoint out/ckpt_v3.pt 2>&1 | tee -a "$LOG"
$PY llama2.c/export.py out/model_v3.bin --checkpoint out/ckpt_v3.pt 2>&1 | tail -3 | tee -a "$LOG"
ls -la out/model_q4_v3.bin out/model_v3.bin out/tokenizer_v3.bin | tee -a "$LOG"

log "--- probe (distribution) ---"
$PY probe_dist.py --schema 3 2>&1 | grep -v Warning | tee -a "$LOG"

log "=== v3 overnight DONE. Next: docs/retrain-v3.md step 3 eval (--teacher) and step 4 ship. ==="
