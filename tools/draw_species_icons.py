#!/usr/bin/env python3
"""draw_species_icons.py - the shop's nine species icons (2026-10-05), drawn
as 32x32 pixel art from shapes: each layer a flat colour with a light edge
up-left and a dark edge down-right, and a dark outline round the whole -
the three-tone style of the hand-drawn assets/icons. Writes
assets/icons/shop_<species>.png (then run tools/gen_icons.py) and a contact
sheet at 4x for a look:

    python3 tools/draw_species_icons.py assets/icons /tmp/sheet.png
"""
import sys, math
from PIL import Image, ImageDraw
OUT = sys.argv[1]
N = 32
def hexrgb(h): return ((h >> 16) & 255, (h >> 8) & 255, h & 255)
def mul(c, k): return tuple(max(0, min(255, int(v * k))) for v in c)
def mix(a, b, k): return tuple(int(a[i] + (b[i] - a[i]) * k) for i in range(3))

class Icon:
    def __init__(s): s.layers = []   # (mask, color, shade)
    def layer(s, color, shade=True):
        m = Image.new('1', (N, N), 0); s.layers.append((m, hexrgb(color), shade))
        return ImageDraw.Draw(m)
    def render(s, outline=None):
        img = Image.new('RGBA', (N, N), (0, 0, 0, 0)); px = img.load()
        union = [[False] * N for _ in range(N)]
        darkest = None
        for m, col, shade in s.layers:
            mp = m.load()
            for y in range(N):
                for x in range(N):
                    if not mp[x, y]: continue
                    union[y][x] = True
                    c = col
                    if shade:
                        ul = (x == 0 or not mp[x - 1, y]) or (y == 0 or not mp[x, y - 1])
                        dr = (x == N - 1 or not mp[x + 1, y]) or (y == N - 1 or not mp[x, y + 1])
                        if ul and not dr: c = mix(col, (255, 255, 255), 0.35)
                        elif dr and not ul: c = mul(col, 0.62)
                    px[x, y] = c + (255,)
            if shade and (darkest is None or sum(col) < sum(darkest)): darkest = col
        oc = outline or mul(darkest, 0.35)
        for y in range(N):
            for x in range(N):
                if union[y][x]: continue
                if any(0 <= x + dx < N and 0 <= y + dy < N and union[y + dy][x + dx] for dx, dy in ((1,0),(-1,0),(0,1),(0,-1))):
                    px[x, y] = oc + (255,)
        return img

def dots(d, pts):
    for x, y in pts: d.point((x, y), 1)

icons = {}
# seahorse: golden, bands, facing right, tail curled
I = Icon()
d = I.layer(0xd9962a)                        # dorsal fin
d.polygon([(4, 12), (8, 10), (8, 18), (4, 17)], 1)
d = I.layer(0xffc93c)
d.ellipse((9, 3, 19, 11), 1)                 # head
d.rectangle((17, 6, 25, 9), 1)              # snout
d.ellipse((7, 9, 19, 23), 1)                # body, belly forward
d.polygon([(8, 18), (15, 18), (14, 25), (10, 25)], 1)
d.arc((8, 22, 18, 30), 90, 360, 1, width=3) # the curled tail
d.arc((10, 24, 16, 29), 180, 360, 1, width=2)
d.polygon([(10, 2), (12, 0), (13, 3)], 1)   # coronet
d = I.layer(0xff8a3d, shade=False)          # bands
for y in (13, 16, 19): d.line((12, y, 18, y - 1), 1)
d = I.layer(0x1a1410, shade=False); dots(d, [(15, 6)])
d = I.layer(0xffffff, shade=False); dots(d, [(14, 5)])
icons['shop_seahorse'] = I
# octopus: red-brown mantle, six curling arms
I = Icon()
d = I.layer(0xc0583a)
d.ellipse((7, 2, 25, 18), 1)
d.rectangle((9, 12, 23, 19), 1)
for x0, x1 in ((7, 2), (11, 8), (15, 14), (17, 19), (21, 24), (25, 30)):
    d.line((x0 + 1, 18, (x0 + x1) // 2 + 1, 24, x1, 27), 1, width=3)
d.arc((0, 24, 6, 30), 270, 90, 1, width=2); d.arc((26, 24, 32, 30), 90, 270, 1, width=2)
d = I.layer(0xf2b48a, shade=False)           # spots
dots(d, [(12, 6), (19, 5), (16, 9), (21, 10), (10, 11)])
d = I.layer(0xffffff, shade=False); d.rectangle((11, 13, 13, 15), 1); d.rectangle((19, 13, 21, 15), 1)
d = I.layer(0x101010, shade=False); dots(d, [(12, 14), (12, 15), (20, 14), (20, 15)])
icons['shop_octopus'] = I
# pufferfish: a spiny ball, spotted
I = Icon()
d = I.layer(0x9e8a5a)                         # tail and fin
d.polygon([(0, 11), (6, 16), (0, 21)], 1)
d = I.layer(0xd8c48a)
d.ellipse((5, 5, 27, 27), 1)
for a in range(0, 360, 30):
    r1, r2 = 11, 14.5; ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
    d.line((16 + ca * r1, 16 + sa * r1, 16 + ca * r2, 16 + sa * r2), 1)
d = I.layer(0xf6ecd0, shade=False); d.chord((8, 14, 26, 27), 0, 180, 1)   # pale belly
d = I.layer(0x9e8a5a, shade=False); d.polygon([(13, 18), (17, 17), (16, 22)], 1)
d = I.layer(0x3a3226, shade=False)
dots(d, [(10, 9), (14, 8), (18, 9), (9, 13), (13, 12), (16, 11), (12, 16), (8, 17)])
d = I.layer(0xffffff, shade=False); d.rectangle((20, 10, 23, 13), 1)
d = I.layer(0x101010, shade=False); d.rectangle((21, 11, 22, 12), 1)
d = I.layer(0x7a3a30, shade=False); d.line((25, 17, 27, 17), 1)
icons['shop_puffer'] = I
# anglerfish: dark body, a wide toothy mouth, a glowing lure
I = Icon()
d = I.layer(0x5a5470)
d.ellipse((4, 9, 26, 29), 1)
d.polygon([(0, 13), (6, 18), (0, 25)], 1)      # tail
d = I.layer(0x2e2a36, shade=False)            # the mouth
d.polygon([(17, 20), (29, 16), (29, 26), (19, 25)], 1)
d = I.layer(0xffffff, shade=False)            # teeth
dots(d, [(21, 19), (24, 18), (27, 17), (22, 25), (25, 25), (28, 25)])
d = I.layer(0x8a84a6, shade=False)            # the lure's stalk
d.line((16, 9, 17, 4), 1); d.line((18, 3, 23, 2), 1); d.line((24, 3, 25, 5), 1)
d = I.layer(0x7ffcff)
d.ellipse((23, 5, 28, 10), 1)
d = I.layer(0xffffff, shade=False); dots(d, [(25, 7)])
d = I.layer(0xe8f4ff, shade=False); d.rectangle((17, 12, 19, 14), 1)
d = I.layer(0x101010, shade=False); dots(d, [(18, 13)])
icons['shop_angler'] = I
# electric eel: a long ripple, olive with an orange belly, a spark
I = Icon()
d = I.layer(0x7a8a4a)
pts = [(1 + i, 20 + 4.5 * math.sin(i / 2.9 + 0.6)) for i in range(0, 25)]
d.line(pts, 1, width=6, joint='curve')
d.ellipse((23, 14, 31, 21), 1)                    # head
d = I.layer(0xff8a3d, shade=False)
d.line([(x, y + 2) for x, y in pts[2:]], 1, width=1)
d = I.layer(0xffd23f, shade=False)               # the spark
d.line((16, 2, 19, 6, 17, 7, 21, 11), 1)
d = I.layer(0xffffff, shade=False); dots(d, [(28, 16)])
d = I.layer(0x101010, shade=False); dots(d, [(29, 16)])
icons['shop_eel'] = I
# hammerhead: seen from above, the hammer, the fins, the tail
I = Icon()
d = I.layer(0x8a9aa6)
d.polygon([(3, 5), (29, 5), (29, 9), (19, 10), (13, 10), (3, 9)], 1)   # the hammer
d.ellipse((11, 7, 21, 23), 1)                                          # the body
d.polygon([(13, 20), (19, 20), (17, 28), (15, 28)], 1)
d.polygon([(11, 13), (3, 20), (12, 17)], 1)                            # pectorals
d.polygon([(21, 13), (29, 20), (20, 17)], 1)
d.polygon([(16, 26), (11, 31), (16, 29), (21, 31)], 1)                 # tail
d = I.layer(0xe8eef2, shade=False); d.line((15, 12, 15, 21), 1)
d = I.layer(0x101010, shade=False); dots(d, [(4, 7), (28, 7)])
d = I.layer(0x5e6e7a, shade=False); d.line((16, 7, 16, 9), 1)
icons['shop_shark'] = I
# squid: a pointed mantle with fins, eyes, arms and two long tentacles
I = Icon()
d = I.layer(0xf2a6c0)
d.polygon([(16, 0), (22, 8), (22, 18), (10, 18), (10, 8)], 1)
d.polygon([(10, 4), (5, 9), (10, 10)], 1); d.polygon([(22, 4), (27, 9), (22, 10)], 1)   # fins
d.ellipse((10, 15, 22, 21), 1)
for x0, x1 in ((11, 9), (14, 13), (18, 19), (21, 23)): d.line((x0, 20, x1, 27), 1, width=2)
d.line((13, 20, 10, 31), 1); d.line((19, 20, 22, 31), 1)
d.rectangle((8, 29, 10, 31), 1); d.rectangle((22, 29, 24, 31), 1)
d = I.layer(0xe0443a, shade=False)
dots(d, [(14, 6), (17, 9), (13, 12), (19, 13), (16, 15), (18, 4)])
d = I.layer(0xffffff, shade=False); d.rectangle((11, 16, 13, 18), 1); d.rectangle((19, 16, 21, 18), 1)
d = I.layer(0x101010, shade=False); dots(d, [(12, 17), (20, 17)])
icons['shop_squid'] = I
# crab: a wide shell, claws up, legs out, eyes on stalks
I = Icon()
d = I.layer(0xd8402a)
d.ellipse((7, 13, 25, 25), 1)
for side in (-1, 1):
    for k in range(3):
        y = 18 + k * 3
        x0 = 16 + side * 8; x1 = 16 + side * 14
        d.line((x0, y, x1, y + 2, x1 + side, y + 5), 1, width=1)
    d.line((16 + side * 7, 15, 16 + side * 10, 9), 1, width=2)            # the arm
    cx = 16 + side * 11
    d.ellipse((cx - 4, 2, cx + 4, 10), 1)                                  # the claw
d = I.layer(0x04141a, shade=False)                                         # the claws' notch
for side in (-1, 1):
    cx = 16 + side * 11; d.polygon([(cx - 1, 2), (cx + 1, 2), (cx, 6)], 1)
d = I.layer(0xffe0c0, shade=False); dots(d, [(12, 17), (19, 16), (15, 20)])
d = I.layer(0x9a2a1c, shade=False); d.line((13, 13, 13, 10), 1); d.line((19, 13, 19, 10), 1)
d = I.layer(0x101010, shade=False); d.rectangle((12, 9, 13, 10), 1); d.rectangle((19, 9, 20, 10), 1)
icons['shop_crab'] = I
# lobster: the rare blue, a segmented tail, big claws ahead, antennae
I = Icon()
d = I.layer(0x2a6aff)
d.ellipse((12, 8, 20, 18), 1)                                   # the carapace
for k in range(4): d.ellipse((12, 16 + k * 3, 20, 21 + k * 3), 1)   # tail segments
d.polygon([(16, 27), (10, 31), (22, 31)], 1)                    # the fan
for side in (-1, 1):
    d.line((16 + side * 3, 11, 16 + side * 7, 7), 1, width=2)
    cx = 16 + side * 9; d.ellipse((cx - 3, 0, cx + 3, 8), 1)    # claws
    for k in range(3): d.line((16 + side * 4, 13 + k * 2, 16 + side * 8, 15 + k * 3), 1)
d = I.layer(0x1a44b0, shade=False)
for k in range(4): d.line((13, 19 + k * 3, 19, 19 + k * 3), 1)
d = I.layer(0xa8d0ff, shade=False)                              # antennae
d.line((15, 8, 11, 2), 1); d.line((17, 8, 21, 2), 1)
d = I.layer(0x101010, shade=False); dots(d, [(14, 9), (18, 9)])
icons['shop_lobster'] = I

# swordfish (2026-10-10 night): side-on, the long bill out front, the sickle dorsal, the lunate tail
I = Icon()
d = I.layer(0x5f7f99)
d.polygon([(6, 17), (10, 12), (17, 10), (22, 12), (25, 16), (22, 20), (17, 22), (10, 21)], 1)   # the body
d.polygon([(10, 12), (12, 4), (15, 3), (17, 10)], 1)                                           # the sickle dorsal
d.polygon([(6, 17), (2, 11), (4, 17), (2, 24)], 1)                                              # the lunate tail
d.polygon([(16, 19), (13, 26), (19, 21)], 1)                                                    # the pectoral
d = I.layer(0x3d5468, shade=False); d.line((25, 16, 31, 15), 1, width=2)                        # the bill
d = I.layer(0xe9eef2, shade=False); d.polygon([(9, 19), (17, 21), (22, 19), (17, 18)], 1)       # the silver belly
d = I.layer(0xffffff, shade=False); dots(d, [(21, 14)])
d = I.layer(0x101010, shade=False); dots(d, [(22, 14)])
icons['shop_swordfish'] = I

big = Image.new('RGBA', (len(icons) * 140, 140), (4, 20, 26, 255))
for i, (name, ic) in enumerate(icons.items()):
    im = ic.render(); im.save(f"{OUT}/{name}.png")
    big.alpha_composite(im.resize((128, 128), Image.NEAREST), (i * 140 + 6, 6))
big.save(sys.argv[2])
