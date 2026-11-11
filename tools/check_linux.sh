#!/bin/bash
# The five-world simulator check on LINUX, as CI's `checks` workflow runs it
# (.github/workflows/checks.yml: ubuntu, gcc, glibc), in a container on this
# Mac. A PRE-TAG STEP since 2026-10-24: v0.3.9's public commit failed CI on
# two selftests that pass on macOS in every world - the margins sat exactly
# where glibc's float math rounds a px or a tick the other way, and nothing
# had run the suite on Linux before the tag. The syntax check of 0.3.6
# (`cc -std=c11 -D_ANSI_SOURCE -fsyntax-only`) catches glibc's hidden names
# only; this catches what the tests do.
#
#   tools/check_linux.sh            # the tree as committed (git archive HEAD), all five worlds, stops at the first failure like CI
#   tools/check_linux.sh --all      # every selftest in every world, going on past failures: the whole list at once
#                                   # (the list from make itself: a grep took the NOMODEL line along and ran a
#                                   # garbled flag as a plain sim forever, 11-03)
#   tools/check_linux.sh --dirty    # the working tree instead of HEAD (uncommitted edits included)
#
# Needs Docker Desktop running (`open -a Docker`). The first run pulls
# ubuntu:26.04 (the runner label moved to Ubuntu 26 on 2026-10-19) and
# clones LVGL v9.2.2 into the copy; ~5 min a run on four cores. The tree is
# COPIED to a scratch folder: nothing here is touched, no sim/lvgl symlink is
# ever added to the checkout (docs/HANDOFF: that broke dev CI once).
set -euo pipefail
cd "$(dirname "$0")/.."
ALL=0; DIRTY=0
for a in "$@"; do case "$a" in --all) ALL=1;; --dirty) DIRTY=1;; *) echo "usage: tools/check_linux.sh [--all] [--dirty]"; exit 2;; esac; done
docker info >/dev/null 2>&1 || { echo "check_linux: Docker is not running (open -a Docker)"; exit 1; }
W=$(mktemp -d /tmp/pt-linux-check.XXXXXX)
if [ "$DIRTY" = 1 ]; then rsync -a --exclude .git --exclude 'sim/build*' --exclude sim/lvgl --exclude 'sim/fishsim*' ./ "$W/"
else git archive HEAD | tar -x -C "$W"; fi
# the model: the public tree tracks model/out, dev does not - CI runs NOMODEL=1 without it. The LLM selftests read
# model.bin and tokenizer.bin, not only model_q4.bin (2026-10-25: a worktree with model/out linked to the main
# checkout copied model_q4.bin alone, so the check ran --selftest-llm without its files and stopped there); cp follows
# the links
mkdir -p "$W/model/out"
for f in model_q4.bin model.bin tokenizer.bin; do [ -f "$W/model/out/$f" ] || [ ! -f "model/out/$f" ] || cp "model/out/$f" "$W/model/out/"; done
NOMODEL=""; { [ -f "$W/model/out/model.bin" ] && [ -f "$W/model/out/tokenizer.bin" ]; } || NOMODEL="NOMODEL=1"
echo "check_linux: $W ($([ "$DIRTY" = 1 ] && echo the working tree || echo HEAD $(git rev-parse --short HEAD)), $([ -n "$NOMODEL" ] && echo no model || echo with the model))"
if [ "$ALL" = 1 ]; then
  SCRIPT='tests=$(printf "print-tests:\n\t@echo \$(SELFTESTS)\n" | make -s -C sim -f Makefile -f - print-tests);
    [ -n "$NOMODEL" ] && tests=$(echo "$tests" | sed "s/--selftest-llm//; s/--selftest-model//");
    rc=0; for w in "" ROUND=1 ROUND146=1 WATCH=1 SQUARE=1; do out=fishsim; case $w in ROUND=1) out=fishsim-round;; ROUND146=1) out=fishsim-round146;; WATCH=1) out=fishsim-watch;; SQUARE=1) out=fishsim-square;; esac;
      echo "=== WORLD ${w:-RECT}"; make -C sim -j"$(nproc)" $w $out >/dev/null 2>&1 || { echo "BUILD FAILED"; rc=1; continue; };
      for t in $tests; do r=$(cd sim && ./$out $t 2>&1 | grep -E "^FAIL" | head -3); [ -n "$r" ] && { echo "  $t: $r"; rc=1; }; done; done; exit $rc'
else
  SCRIPT='make -C sim -j"$(nproc)" $NOMODEL check-all 2>&1 | grep -v "^cc \|^  ok\|tick \|^census" | grep -E "RUN |FAIL|rror|Error" ; exit ${PIPESTATUS[0]}'
fi
docker run --rm -e NOMODEL="$NOMODEL" -v "$W":/w -w /w ubuntu:26.04 bash -c "export DEBIAN_FRONTEND=noninteractive; apt-get update -qq >/dev/null 2>&1 && apt-get install -y -qq build-essential libsdl2-dev git >/dev/null 2>&1 && git clone -q --depth 1 --branch v9.2.2 https://github.com/lvgl/lvgl.git sim/lvgl 2>/dev/null && gcc --version | head -1 && $SCRIPT"
rc=$?
rm -rf "$W"
[ $rc = 0 ] && echo "check_linux: PASS" || echo "check_linux: FAIL ($rc)"
exit $rc
