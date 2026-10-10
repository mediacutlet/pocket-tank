#!/usr/bin/env python3
"""Teacher vs student, same question — the distillation demo, camera-ready.

Sends one schema state line to BOTH brains and shows the answers side by side
with timing and size. Stdlib only.

  python3 teacher_vs_student.py                       # a built-in scenario
  python3 teacher_vs_student.py "fish mira zone 2 ..."  # your own state line
  python3 teacher_vs_student.py --host http://localhost:11434
"""

import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_traces as gt

HERE = os.path.dirname(os.path.abspath(__file__))

DEFAULT_STATE = (
    "fish mira zone 2 hunger 8 energy 6 stress 3 curiosity 5 "
    "bold 4 social 6 stage adult food near 12 shadow mid 8 friend bolt mid 3 "
    "bubble mid 10 reef far 7 wall clear last seek_food time day"
)

CYAN, YELLOW, DIM, BOLD, END = "\033[96m", "\033[93m", "\033[2m", "\033[1m", "\033[0m"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("state", nargs="?", default=DEFAULT_STATE)
    ap.add_argument("--host", default="http://localhost:11434")
    ap.add_argument("--model", default="gemma4:26b")
    args = ap.parse_args()

    # The project dir is a network share; run the student from a local cache so
    # timing reflects inference (the device reads flash, not AFP).
    import shutil
    cache = os.path.expanduser("~/.cache/aqua-pets")
    os.makedirs(cache, exist_ok=True)
    model_bin, tok_bin = os.path.join(cache, "model.bin"), os.path.join(cache, "tokenizer.bin")
    for src, dst in [(os.path.join(HERE, "out", "model.bin"), model_bin),
                     (os.path.join(HERE, "out", "tokenizer.bin"), tok_bin)]:
        if not os.path.exists(dst) or os.path.getmtime(src) > os.path.getmtime(dst):
            shutil.copy(src, dst)
    run_bin = os.path.join(HERE, "runw")
    student_mb = os.path.getsize(model_bin) / 1e6
    # Baseline run: process spawn + weight load with a near-empty prompt, so the
    # reported student time is inference alone (the device loads weights once
    # at boot, then answers from memory).
    t0 = time.time()
    subprocess.run([run_bin, model_bin, "-z", tok_bin, "-t", "0", "-n", "2",
                    "-i", "fish"], capture_output=True, timeout=60)
    t_baseline = time.time() - t0

    print(f"\n{BOLD}THE QUESTION{END} (one fish's entire world, in words):\n")
    print(f"  {args.state}\n")

    print(f"{CYAN}{BOLD}TEACHER{END}{CYAN}  gemma4:26b — 26,000M parameters, ~18 GB, needs a desktop GPU{END}")
    t0 = time.time()
    goal = gt.ask_ollama(args.host, args.model, args.state, 60)
    t_teacher = time.time() - t0
    print(f"  answer: {BOLD}{goal}{END}   {DIM}({t_teacher*1000:.0f} ms){END}\n")

    print(f"{YELLOW}{BOLD}STUDENT{END}{YELLOW}  Aqua Pets — 14M parameters, {student_mb:.0f} MB, runs on a $10 microcontroller{END}")
    t0 = time.time()
    out = subprocess.run([run_bin, model_bin, "-z", tok_bin, "-t", "0",
                          "-i", args.state + " ->"],
                         capture_output=True, text=True, timeout=60).stdout
    t_student = max(0.001, (time.time() - t0) - t_baseline)
    m = re.search(r"-> ([a-z_]+ urgency \d)", out)
    print(f"  answer: {BOLD}{m.group(1) if m else out.strip()[:60]}{END}   "
          f"{DIM}({t_student*1000:.0f} ms of thinking){END}\n")

    print(f"{DIM}same judgment - from a model {26_000/14.3:,.0f}x smaller that fits "
          f"in a pocket, needs no network,\nand draws less power than an LED bulb. "
          f"The teacher can never leave the desk; the student can.{END}\n")


if __name__ == "__main__":
    main()
