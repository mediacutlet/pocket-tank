#!/usr/bin/env python3
"""tab5_console.py - send one director console line to a Tab5 and read the reply.

The tank's `director.c` is a serial console (snd/battery/touch/... commands), and
the log reader (tools/tab5_boot_log.py) only listens. This is the other half:
open the port, write one line, read for a few seconds, print what came back.

    python tools/tab5_console.py COM9 "snd list" 5
    python tools/tab5_console.py COM9 "snd 3" 8 --log docs/batlog/tab5-snd.log

It does NOT reset the board (use tab5_boot_log.py for that): the console is
already there, and a reset would throw away the state you are poking at.
"""
import argparse
import sys
import time

import serial

# the tank's console prints box-drawing and emoji; a GBK console would die on the
# first replacement character (tab5_boot_log.py does the same)
sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("line", help="the console command, without the newline")
    ap.add_argument("seconds", nargs="?", type=float, default=5.0)
    ap.add_argument("--baud", type=int, default=115200, help="native USB-CDC ignores it")
    ap.add_argument("--log", help="also write everything read to this file")
    ap.add_argument("--wait-for", dest="wait_for", default=None,
                    help="wait until this regex appears before sending the command "
                         "(opening the port can reset a P4, so this is how you wait for the boot)")
    ap.add_argument("--wait-timeout", dest="wait_timeout", type=float, default=25.0)
    args = ap.parse_args()

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 0.2
    ser.dsrdtr = False
    ser.rtscts = False
    ser.open()
    # The P4's native USB-Serial/JTAG resets the chip when DTR or RTS is
    # asserted - and pyserial asserts both on open. Let go of them before
    # writing, or this tool reboots the tank you are poking at.
    ser.dtr = False
    ser.rts = False
    time.sleep(0.2)
    try:
        ser.reset_input_buffer()
        chunks = []
        if args.wait_for:
            import re
            pat = re.compile(args.wait_for)
            deadline = time.time() + args.wait_timeout
            seen = ""
            while time.time() < deadline:
                data = ser.read(4096)
                if data:
                    text = data.decode("utf-8", errors="replace")
                    chunks.append(text); seen += text
                    sys.stdout.write(text); sys.stdout.flush()
                    if pat.search(seen):
                        break
            print(f"\n[--wait-for matched or timed out; sending now]", file=sys.stderr)
        ser.write((args.line + "\n").encode())
        ser.flush()
        deadline = time.time() + args.seconds
        while time.time() < deadline:
            data = ser.read(4096)
            if data:
                text = data.decode("utf-8", errors="replace")
                chunks.append(text)
                sys.stdout.write(text)
                sys.stdout.flush()
        if args.log:
            with open(args.log, "w", encoding="utf-8", errors="replace") as f:
                f.write("".join(chunks))
            print(f"\n[{len(chunks)} chunks -> {args.log}]", file=sys.stderr)
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
