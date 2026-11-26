# ppm -> png without Pillow; optional side-by-side sheets against a reference run.
#   python topng.py <ppm dir> <png dir>                      convert
#   python topng.py <ppm dir> <png dir> <reference ppm dir>  also <png dir>/compare/: reference | this
import zlib, struct, sys, os, glob
def read_ppm(p):
    d = open(p, 'rb').read(); parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split()); return w, h, parts[3]
def write_png(p, w, h, rgb):
    raw = b''.join(b'\0' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def ch(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    open(p, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 9)) + ch(b'IEND', b''))
def side(a, b, gap=16, bg=(40, 40, 40)):
    (wa, ha, da), (wb, hb, db) = a, b; H = max(ha, hb); fill = bytes(bg); rows = []
    for y in range(H):
        ra = da[y*wa*3:(y+1)*wa*3] if y < ha else fill * wa
        rb = db[y*wb*3:(y+1)*wb*3] if y < hb else fill * wb
        rows.append(ra + fill * gap + rb)
    return wa + gap + wb, H, b''.join(rows)
src, out = sys.argv[1], sys.argv[2]; ref = sys.argv[3] if len(sys.argv) > 3 else None
os.makedirs(out, exist_ok=True)
if ref: os.makedirs(out + '/compare', exist_ok=True)
n = 0
for p in sorted(glob.glob(src + '/s_*.ppm')):
    name = os.path.basename(p)[2:-4]; img = read_ppm(p); write_png(f'{out}/{name}.png', *img); n += 1
    if ref and os.path.exists(f'{ref}/s_{name}.ppm'): write_png(f'{out}/compare/{name}.png', *side(read_ppm(f'{ref}/s_{name}.ppm'), img))
print(n, 'pngs in', out)
