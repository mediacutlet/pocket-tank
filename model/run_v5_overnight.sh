#!/bin/bash
# run_v5_overnight.sh — the schema v5 data cycle (species), unattended (docs/retrain-v5.md).
# Launch with nohup; everything logs to out/v5_overnight.log.
#
#   nohup ./run_v5_overnight.sh > /dev/null 2>&1 &
#
# 1. trace workers against the teacher (v5 prompt: the v4 prompt + the species
#    paragraph; tanks of 2-10 creatures, ten species, the classic fish ~1/3),
#    hard wall-clock cap
# 2. fold + closed-vocabulary verify -> out/v5_clean.jsonl
# 3. re-encode every older label as `species fish` (convert_to_v5.py: the v4m
#    mix, or v2 + v3 + v4 if there is no v4m file) and fold it in ->
#    out/v5m_clean.jsonl (the personality cliffs and the boredom rules live in
#    the old labels; the species live only in the new ones)
# 4. train the 14M student (AQUA_PETS_SCHEMA=5, vocab 65) on the mix, export
#    4-bit + fp32, probe
# Step 5 (eval, then shipping) is deliberately manual.
#
# Env: HOST (teacher), MINUTES (cap per worker), COUNT (pairs per worker),
# WORKERS (2-3; 4 overloads one Ollama), ITERS (train), FISH_SHARE, TEACHER.
set -u
cd "$(dirname "$0")"
LOG=out/v5_overnight.log
PY=${PY:-~/.venvs/aquapets/bin/python}
HOST=${HOST:-http://192.168.0.139:11434}
MINUTES=${MINUTES:-480}
COUNT=${COUNT:-12000}
WORKERS=${WORKERS:-3}
ITERS=${ITERS:-7000}
FISH_SHARE=${FISH_SHARE:-0.333}
TEACHER=${TEACHER:-ollama}           # rules = a dry run of this script without Ollama (never ship it)
log() { echo "[$(date '+%F %T')] $*" | tee -a "$LOG"; }

mkdir -p out
log "=== v5 overnight start: host $HOST, $MINUTES min cap, $WORKERS workers, fish share $FISH_SHARE ==="
seeds=(51 52 53 54)
for ((w = 0; w < WORKERS; w++)); do
  s=$(printf "\\x$(printf %x $((97 + w)))")      # a b c d
  nohup python3 gen_traces.py --schema 5 --count $COUNT --max-minutes $MINUTES --fish-share $FISH_SHARE --teacher $TEACHER \
      --seed ${seeds[$w]} --host "$HOST" --out out/v5_traces_$s.jsonl > out/v5_gen_$s.log 2>&1 &
  echo $! > out/v5_gen_$s.pid
  log "worker $s pid $! seed ${seeds[$w]} -> out/v5_traces_$s.jsonl"
done

# progress every 30 min while any worker lives
while pgrep -f "gen_traces.py --schema 5" > /dev/null; do
  sleep ${POLL:-1800}
  n=$(cat out/v5_traces_*.jsonl 2>/dev/null | wc -l | tr -d ' ')
  log "progress: $n pairs so far"
done
log "workers done: $(cat out/v5_traces_*.jsonl | wc -l | tr -d ' ') raw pairs"
grep -h "truncated the system prompt" out/v5_gen_*.log | head -1 | tee -a "$LOG"

log "--- fold ---"
python3 fold_traces.py --schema 5 "out/v5_traces_*.jsonl" --out out/v5_clean.jsonl 2>&1 | tee -a "$LOG"
python3 train_tokenizer.py --schema 5 --verify "out/v5_clean.jsonl" 2>&1 | tee -a "$LOG"
n=$(wc -l < out/v5_clean.jsonl | tr -d ' ')
if [ "$n" -lt 5000 ]; then log "ABORT: only $n clean pairs - not training"; exit 1; fi

log "--- mix in every older label as species fish ---"
if [ -f out/v4m_clean.jsonl ]; then OLD="out/v4m_clean.jsonl"
else OLD="$(ls out/v2_clean.jsonl out/v3_clean.jsonl out/v4_clean.jsonl 2>/dev/null | tr '\n' ' ')"; fi
log "old data: $OLD"
python3 convert_to_v5.py $OLD --out out/old_as_v5.jsonl 2>&1 | tee -a "$LOG"
python3 fold_traces.py --schema 5 out/old_as_v5.jsonl out/v5_clean.jsonl --out out/v5m_clean.jsonl 2>&1 | tee -a "$LOG"
python3 train_tokenizer.py --schema 5 --verify "out/v5m_clean.jsonl" 2>&1 | tee -a "$LOG"
n=$(wc -l < out/v5m_clean.jsonl | tr -d ' ')

log "--- train (14M, schema v5, $n pairs, $ITERS iters) ---"
AQUA_PETS_SCHEMA=5 $PY train.py --data "out/v5m_clean.jsonl" --dim 384 --n-layers 8 --n-heads 8 \
    --max-seq-len 64 --batch 64 --iters $ITERS --lr 6e-4 --out out/ckpt_v5m.pt 2>&1 | tail -15 | tee -a "$LOG"
[ -f out/ckpt_v5m.pt ] || { log "ABORT: no checkpoint"; exit 1; }

log "--- export ---"
$PY export_q4.py out/model_q4_v5m.bin --checkpoint out/ckpt_v5m.pt 2>&1 | tee -a "$LOG"
$PY llama2.c/export.py out/model_v5m.bin --checkpoint out/ckpt_v5m.pt 2>&1 | tail -3 | tee -a "$LOG"
ls -la out/model_q4_v5m.bin out/model_v5m.bin out/tokenizer_v5.bin | tee -a "$LOG"

log "--- probe (distribution, + the species panel) ---"
$PY probe_dist.py --schema 5 --ckpt out/ckpt_v5m.pt --data out/v5m_clean.jsonl 2>&1 | grep -v Warning | tee -a "$LOG"

log "=== v5 overnight DONE. Next: docs/retrain-v5.md acceptance (eval.py per species) + ship. ==="
