#!/Users/strato/.espressif/python_env/idf5.4_py3.13_env/bin/python
"""shot.py PORT x0 y0 w h out.png [scale] - ask the tank for the last frame's pixels in a box (director `shot`)
and write them as a PNG, enlarged `scale` times (default 4). Opens the port like director.py (no reset)."""
import sys, time, zlib, struct
import serial

port, x0, y0, w, h, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6]
scale = int(sys.argv[7]) if len(sys.argv) > 7 else 4
s = serial.Serial(); s.port, s.baudrate, s.timeout = port, 115200, 0.2
s.open(); s.reset_input_buffer()
s.write(("shot %d %d %d %d\n" % (x0, y0, w, h)).encode()); s.flush()
rows = {}; end = time.time() + 30; hdr = None
while time.time() < end:
    line = s.readline()
    if not line: continue
    t = line.decode("utf-8", "replace").strip()
    if t.startswith("shot end"): break
    if t.startswith("shot ") and not t.startswith("shotrow"):
        hdr = [int(v) for v in t.split()[1:5]]; continue
    if t.startswith("shotrow "):
        parts = t.split(" ", 2)
        if len(parts) == 3:
            rows[int(parts[1])] = parts[2]
s.close()
if not hdr: sys.exit("no shot header - is the board awake, and the build new enough for `shot`?")
X0, Y0, W, H = hdr
def px(hx):
    v = int(hx, 16); r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
    return bytes((r * 255 // 31, g * 255 // 63, b * 255 // 31))
img = []
for y in range(Y0, Y0 + H):
    hexrow = rows.get(y, "0000" * W)
    row = b"".join(px(hexrow[4 * x:4 * x + 4]) * scale if len(hexrow) >= 4 * x + 4 else b"\xff\x00\xff" * scale for x in range(W))
    for _ in range(scale): img.append(row)
raw = b"".join(b"\x00" + r for r in img)
def chunk(tg, d): return struct.pack(">I", len(d)) + tg + d + struct.pack(">I", zlib.crc32(tg + d) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W * scale, H * scale, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
open(out, "wb").write(png)
print("%s: %dx%d box at %d,%d -> %s (%d of %d rows received)" % (port, W, H, X0, Y0, out, len(rows), H))
