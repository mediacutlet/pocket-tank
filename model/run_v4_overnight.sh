#!/bin/bash
# run_v4_overnight.sh — the schema v4 data cycle, unattended (docs/retrain-v4.md).
# Launch with nohup; everything logs to out/v4_overnight.log.
#
#   nohup ./run_v4_overnight.sh > /dev/null 2>&1 &
#
# 1. three trace workers against the Mac Mini teacher (v4 prompt: no shadow,
#    + bored), hard wall-clock cap
# 2. fold + closed-vocabulary verify -> out/v4_clean.jsonl
# 3. re-encode the v2 + v3 datasets as v4 (convert_to_v4.py: shadow field
#    dropped, shadow-in-view and flee pairs dropped, bored 0-2) and fold them
#    in -> out/v4m_clean.jsonl (the v3m lesson: the personality cliffs live
#    in the old labels; the boredom behaviour lives only in the new ones)
# 4. train the 14M student (AQUA_PETS_SCHEMA=4) on the mix, export 4-bit + fp32,
#    probe
# Step 5 (shipping the model into sim/firmware) is deliberately manual.
set -u
cd "$(dirname "$0")"
LOG=out/v4_overnight.log
PY=~/.venvs/aquapets/bin/python
HOST=${HOST:-http://192.168.0.139:11434}
MINUTES=${MINUTES:-480}
COUNT=${COUNT:-12000}
WORKERS=${WORKERS:-3}
log() { echo "[$(date '+%F %T')] $*" | tee -a "$LOG"; }

mkdir -p out
log "=== v4 overnight start: host $HOST, $MINUTES min cap, $WORKERS workers ==="
seeds=(41 42 43 44)
for ((w = 0; w < WORKERS; w++)); do
  s=$(printf "\\x$(printf %x $((97 + w)))")      # a b c d
  nohup python3 gen_traces.py --schema 4 --count $COUNT --max-minutes $MINUTES \
      --seed ${seeds[$w]} --host "$HOST" --out out/v4_traces_$s.jsonl > out/v4_gen_$s.log 2>&1 &
  echo $! > out/v4_gen_$s.pid
  log "worker $s pid $! seed ${seeds[$w]} -> out/v4_traces_$s.jsonl"
done

# progress every 30 min while any worker lives
while pgrep -f "gen_traces.py --schema 4" > /dev/null; do
  sleep 1800
  n=$(cat out/v4_traces_*.jsonl 2>/dev/null | wc -l | tr -d ' ')
  log "progress: $n pairs so far"
done
log "workers done: $(cat out/v4_traces_*.jsonl | wc -l | tr -d ' ') raw pairs"

log "--- fold ---"
python3 fold_traces.py --schema 4 out/v4_traces_*.jsonl --out out/v4_clean.jsonl 2>&1 | tee -a "$LOG"
python3 train_tokenizer.py --schema 4 --verify "out/v4_clean.jsonl" 2>&1 | tee -a "$LOG"
n=$(wc -l < out/v4_clean.jsonl | tr -d ' ')
if [ "$n" -lt 5000 ]; then log "ABORT: only $n clean pairs - not training"; exit 1; fi

log "--- mix in the re-encoded v2 + v3 data ---"
python3 convert_to_v4.py out/v2_clean.jsonl out/v3_clean.jsonl --out out/v2v3_as_v4.jsonl 2>&1 | tee -a "$LOG"
python3 fold_traces.py --schema 4 out/v2v3_as_v4.jsonl out/v4_clean.jsonl --out out/v4m_clean.jsonl 2>&1 | tee -a "$LOG"
python3 train_tokenizer.py --schema 4 --verify "out/v4m_clean.jsonl" 2>&1 | tee -a "$LOG"
n=$(wc -l < out/v4m_clean.jsonl | tr -d ' ')

log "--- train (14M, schema v4, $n pairs) ---"
AQUA_PETS_SCHEMA=4 $PY train.py --data "out/v4m_clean.jsonl" --dim 384 --n-layers 8 --n-heads 8 \
    --max-seq-len 64 --batch 64 --iters 5000 --lr 6e-4 --out out/ckpt_v4m.pt 2>&1 | tail -15 | tee -a "$LOG"
[ -f out/ckpt_v4m.pt ] || { log "ABORT: no checkpoint"; exit 1; }

log "--- export ---"
$PY export_q4.py out/model_q4_v4m.bin --checkpoint out/ckpt_v4m.pt 2>&1 | tee -a "$LOG"
$PY llama2.c/export.py out/model_v4m.bin --checkpoint out/ckpt_v4m.pt 2>&1 | tail -3 | tee -a "$LOG"
ls -la out/model_q4_v4m.bin out/model_v4m.bin out/tokenizer_v4.bin | tee -a "$LOG"

log "--- probe (distribution) ---"
$PY probe_dist.py --schema 4 --ckpt out/ckpt_v4m.pt --data out/v4m_clean.jsonl 2>&1 | grep -v Warning | tee -a "$LOG"

log "=== v4 overnight DONE. Next: docs/retrain-v4.md acceptance + ship. ==="
