#!/Users/strato/.espressif/python_env/idf5.4_py3.13_env/bin/python
"""preflight.py - archive what the tank knows BEFORE anything resets it.

Run before every flash (tools/flash.sh does), and any morning after a night
on battery. It asks the director for `batlog` and `state` and writes both to
docs/batlog/<YYYY-MM-DD_HHMM>.txt (committed history: every night's numbers
stay readable after the log itself is gone). Exits 1 if the tank is not on
USB, if the device on the port does not answer as the tank's director, or if
several boards are plugged in and TANK_PORT does not name the tank (2026-10-01:
a second board took the tank's old port, and first-by-name was the wrong one) - a flash with no preflight is how the first full deep-sleep night was
lost on 2026-09-15 (the RTC log was wiped by the reflash before anyone read
it). The log now survives resets (batlog.c, RTC_NOINIT), but a power-off or
a PMIC cut still clears it; the archive is the record.

  tools/preflight.py              # archive + print the summary lines
  tools/preflight.py --quiet      # archive only (flash.sh)
"""
import glob, os, sys, time, datetime
import serial

quiet = "--quiet" in sys.argv
ports = sorted(glob.glob("/dev/cu.usbmodem*"))
port = os.environ.get("TANK_PORT") or (ports[0] if len(ports) == 1 else None)
if not port:
    sys.exit("preflight: no /dev/cu.usbmodem* - wake the tank (BOOT) and plug it in; refusing to continue" if not ports else
             "preflight: more than one board on USB (" + ", ".join(ports) + ") - say which is the tank: TANK_PORT=/dev/cu.usbmodemNNN")
here = os.path.dirname(os.path.abspath(__file__))
out_dir = os.path.join(here, "..", "docs", "batlog")
os.makedirs(out_dir, exist_ok=True)
stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H%M")
path = os.path.join(out_dir, f"{stamp}.txt")

s = serial.Serial(); s.port, s.baudrate, s.timeout = port, 115200, 0.1
s.open(); s.reset_input_buffer()          # a plain open: DTR/RTS untouched, the tank keeps running
lines = []
for cmd, secs in (("batlog", 4), ("state", 3)):
    s.write((cmd + "\n").encode()); s.flush()
    end = time.time() + secs
    while time.time() < end:
        raw = s.readline()
        if raw:
            lines.append(raw.decode("utf-8", "replace").rstrip())
s.close()
keep = [l for l in lines if "batlog:" in l or "director:" in l]
if not any("director:" in l for l in keep):   # another board on that port, or the tank asleep: nothing to archive, nothing to flash
    sys.exit(f"preflight: no director on {port} - not the tank, or it is asleep; refusing to continue")
with open(path, "w") as f:
    f.write(f"# Aqua Pets preflight {stamp} on {port}\n")
    f.write("\n".join(keep) + "\n")
n_samples = next((l for l in keep if "samples (h:mm" in l), "no samples")
print(f"preflight: {len(keep)} lines -> {os.path.relpath(path, os.getcwd())} | {n_samples.split('batlog: ')[-1]}")
if not quiet:
    for l in keep:
        if "batlog:" in l and ("awake:" in l or "asleep:" in l or "| sleep" in l or "| wake" in l):
            print("  " + l.split("batlog: ")[-1])
