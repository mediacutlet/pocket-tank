#!/usr/bin/env python3
"""board_sheet.py - the same pictures on all three glasses, side by side.

Runs one picture-writing mode of the simulator on each board's build and
pastes the pictures of the same name into one PNG per name: the rectangle
(the 1.8), the bowl (the 1.75C) and the watch (the 2.06), each cut to the
shape of its glass, so what the bezel hides is hidden here too. Look at the
sheets before anything is flashed (docs/BOARDS.md): a page that is right on
one board and cut, shifted or crowded on another shows at a glance.

  aqua-pets/tools/board_sheet.py --selftest-card       # the fish card, armed, the rename page
  aqua-pets/tools/board_sheet.py --snapshot 20         # every page the snapshot mode draws
  aqua-pets/tools/board_sheet.py --out /tmp/x --selftest-card

The mode is any simulator flag that takes a path prefix as its next argument
and writes <prefix>_<name>.ppm files; anything after it is passed on. The
three simulators must be built (`make`, `make ROUND=1`, `make WATCH=1` in
sim/, or `make check-all`). Sheets land in sim/sheets/ (not tracked) unless
--out says otherwise. No dependencies beyond the standard library.
"""
import glob
import os
import struct
import subprocess
import sys
import tempfile
import zlib

SIM = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sim"))
# the board, its simulator, the corner radius of its glass (None = a circle)
BOARDS = [("1.8", "fishsim", 40), ("1.75C", "fishsim-round", None), ("2.06", "fishsim-watch", 100)]
GAP, BACK, OFF = 16, (30, 30, 30), (70, 0, 70)     # between glasses; the sheet; what the bezel hides


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts, pos = [], 0
    while len(parts) < 4:                          # P6, width, height, maxval
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        parts.append(data[pos:end])
        pos = end
    w, h = int(parts[1]), int(parts[2])
    return w, h, data[pos + 1:pos + 1 + w * h * 3]


def on_glass(x, y, w, h, radius):
    if radius is None:                             # the bowl: the circle inscribed in the frame
        r = w / 2.0
        return (x + 0.5 - r) ** 2 + (y + 0.5 - h / 2.0) ** 2 <= r * r
    cx = radius if x < radius else w - radius if x >= w - radius else None
    cy = radius if y < radius else h - radius if y >= h - radius else None
    if cx is None or cy is None:
        return True
    return (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= radius * radius


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    args = sys.argv[1:]
    out = os.path.join(SIM, "sheets")
    if args[:1] == ["--out"]:
        out, args = args[1], args[2:]
    if not args:
        sys.exit(__doc__)
    flag, rest = args[0], args[1:]
    os.makedirs(out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="board-sheet-")
    shots = {}                                     # name -> {board: (w, h, rgb)}
    for board, exe, _ in BOARDS:
        if not os.path.exists(os.path.join(SIM, exe)):
            sys.exit("board_sheet: sim/%s is not built (make check-all in sim/)" % exe)
        prefix = os.path.join(tmp, board)
        run = subprocess.run(["./" + exe, flag, prefix] + rest, cwd=SIM, capture_output=True, text=True)
        if run.returncode:
            print("board_sheet: %s %s FAILED on the %s:\n%s" % (exe, flag, board, (run.stdout + run.stderr)[-600:]))
        for p in sorted(glob.glob(prefix + "*.ppm")):
            shots.setdefault(os.path.basename(p)[len(board):].lstrip("_")[:-4] or "frame", {})[board] = read_ppm(p)
    if not shots:
        sys.exit("board_sheet: %s wrote no pictures" % flag)
    for name, per in sorted(shots.items()):
        W = sum(per[b][0] for b, _, _ in BOARDS if b in per) + GAP * (len(per) - 1)
        H = max(v[1] for v in per.values())
        rows = [bytearray(bytes(BACK) * W) for _ in range(H)]
        x0 = 0
        for board, _, radius in BOARDS:
            if board not in per:
                continue
            w, h, rgb = per[board]
            for y in range(h):
                row = rows[y]
                for x in range(w):
                    o = (x0 + x) * 3
                    row[o:o + 3] = rgb[(y * w + x) * 3:(y * w + x) * 3 + 3] if on_glass(x, y, w, h, radius) else bytes(OFF)
            x0 += w + GAP
        path = os.path.join(out, "%s.png" % name)
        write_png(path, W, H, rows)
        missing = [b for b, _, _ in BOARDS if b not in per]
        print("%s%s" % (path, "  (no picture from: %s)" % ", ".join(missing) if missing else ""))


if __name__ == "__main__":
    main()
