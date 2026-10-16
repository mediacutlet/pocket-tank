# tab5_boot_log.py - reset the Tab5 (ESP32-P4) and capture its boot log.
# Windows/PowerShell, no bash, no interactive monitor: the board is on a native
# USB-serial-JTAG port, so a reset goes through esptool and the log is just read
# off the same COM port afterwards.
#
#   python tools/tab5_boot_log.py COM9 25 [out.log]
#
# The log is printed as it arrives and (with a third argument) written to a file
# with UTF-8, no BOM - the tank's own console prints box-drawing and emoji.
import subprocess
import sys
import time

import serial

# the tank's console prints box-drawing and emoji, and a crash prints a raw
# backtrace: the Windows console code page (GBK) cannot encode all of it, so
# anything undecodable is replaced rather than killing the read
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

port = sys.argv[1] if len(sys.argv) > 1 else "COM9"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0
out = sys.argv[3] if len(sys.argv) > 3 else None

reset = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32p4", "-p", port,
                        "--after", "hard_reset", "read_mac"], capture_output=True, text=True)
if reset.returncode != 0:
    print("(the reset before the read failed: %s)" % reset.stderr.strip()[-300:])

lines = []
raw = bytearray()
t0 = time.time()
with serial.Serial(port, 115200, timeout=0.2) as s:
    while time.time() - t0 < secs:
        d = s.readline()
        if not d:
            continue
        raw += d
        t = d.decode("utf-8", "replace").rstrip("\r\n")
        print(t, flush=True)
        lines.append(t)

if out:
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    with open(out + ".raw", "wb") as f:      # the bytes as they came: a panic's own dump
        f.write(raw)
    print("(%d lines -> %s)" % (len(lines), out))
