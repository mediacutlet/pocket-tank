#!/usr/bin/env python3
"""link_multi.py - drive several tanks at once from their director consoles and
keep the lines that matter (link spike, the 10 s decision line, the frame line,
errors), each prefixed with seconds since start, the port and the step's label.
Written for the tank link's radio spike (docs/tank-link-0.4.md, phase 0).

    . ~/esp/esp-idf/export.sh      # its python has pyserial
    python pocket-tank/tools/link_multi.py /dev/cu.usbmodem101 /dev/cu.usbmodem2101 \
        '[["base","",60],["on","link spike on 2 32 1",90],["stream","link spike rate 20 200",120],["off","link spike off",30]]' > run.log

Steps are [label, director line ("" = none), seconds, (port index)]; the line
goes to every port at the step's start, or only to the port at that index
(0 = the first on the command line) when a fourth item is given. A step that sends `snd confirm` makes both tanks
chime - the cue to move the boards in a distance test. Plain pyserial open
(no DTR/RTS toggling: that resets the chip). With several boards on USB,
check which port is which by USB serial first.
"""

import sys, time, threading, json, serial, re

ports = sys.argv[1:-1]
steps = json.loads(sys.argv[-1])        # [["label", "cmd or ''", seconds], ...]
KEEP = re.compile(r"\blink:|director|fish goals|display: frame|E \(|assert|Guru|abort|Rebooting")
conns = {}
for p in ports:
    s = serial.Serial(); s.port, s.baudrate, s.timeout = p, 115200, 0.1; s.open(); s.reset_input_buffer()
    conns[p] = s
label = ["start"]
T0 = time.time()
stop = False
def reader(p, s):
    tag = p.split("usbmodem")[-1]
    while not stop:
        line = s.readline()
        if not line: continue
        t = line.decode("utf-8", "replace").rstrip()
        if KEEP.search(t):
            print(f"{time.time()-T0:7.1f} [{tag} {label[0]}] {t}", flush=True)
th = [threading.Thread(target=reader, args=(p, s), daemon=True) for p, s in conns.items()]
for t in th: t.start()
for step in steps:
    lab, cmd, secs = step[0], step[1], step[2]
    only = ports[step[3]] if len(step) > 3 else None
    label[0] = lab
    print(f"{time.time()-T0:7.1f} ==== {lab}: '{cmd}'{' on ' + only if only else ''} for {secs} s", flush=True)
    if cmd:
        for p, s in conns.items():
            if only and p != only: continue
            s.write((cmd + "\n").encode()); s.flush()
    time.sleep(secs)
stop = True
time.sleep(0.3)
for s in conns.values(): s.close()
