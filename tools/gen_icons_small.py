#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Leonid Moiseichuk
# SPDX-FileCopyrightText: 2026 MediaCutlet
# MediaCutlet's are a few lines from upstream's MIT releases (v0.3.9.2 and
# earlier); LICENSE-MIT-CYD says which.
"""gen_icons_small.py - the small copies of upstream's art a 320 x 240 board
draws, appended to upstream's generated common/icons.c and common/icons.h.

A 320 x 240 board (the CYD, the Touch-LCD-2) cannot fit the milestones
page's rows of 32 px badges, and pixel art drawn at 32 px has no clean half
size: box-filtered to 24 px, the art survives the step down. render.c's
badge_art() pairs each ms_* badge with its copy, icon_ms_<name>_24; a tall
tank never references the copies and the linker drops them.

The foot's, the shop tabs' and the fish card's button sprites (v0.4: 32 to
36 px, for squares of 44) get a copy at half their size, icon_<name>_half:
a 2:1 box filter, the one clean step down for pixel art, so they fit the
22 px squares a 240 px page has room for. The fish card's three button
sprites are icons4 art upstream (4-bit palettes, tools/gen_icons4.py); their
halves are ordinary icon_t, since the filter's in-between shades do not fit
fifteen colours.

Upstream's icons.c and icons.h stay as upstream generated them, and
tools/gen_icons.py as upstream wrote it. Since v0.4.3 upstream's icons.c
holds icons whose PNGs are not in assets/icons (the castle's three stages and
the octopus), so running gen_icons.py again would drop them. This script
makes the copies with upstream's own generator instead, run in a scratch
directory on the resized PNGs, and appends what that wrote between two
marker lines - replacing the block an earlier run left, so a second run
changes nothing. At a sync: take upstream's icons.c and icons.h, then

    python3 tools/gen_icons_small.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets", "icons")
PREFIX, SIZE = "ms_", 24          # which art gets a small copy, and its size
HALVES = {                        # the art that gets a half-size copy: its folder, its names
    "icons": ("foot_close", "foot_my_tanks", "foot_passport", "foot_sand_dollar", "foot_settings",
              "settings_link", "settings_updates", "shop_tab_bg", "shop_tab_decor", "shop_tab_earn", "shop_crab"),
    "icons4": ("card_give", "card_more", "card_rename"),
}
ICONS_C = os.path.join(ROOT, "common", "icons.c")
ICONS_H = os.path.join(ROOT, "common", "icons.h")
BEGIN = "/* ---- this fork: the ms_* badges' 24 px copies, appended by tools/gen_icons_small.py ---- */"
END = "/* ---- end of the 24 px copies ---- */"
# (the markers keep their first words: a block an earlier run appended is found by them)


def without_block(text):
    """The text without the block an earlier run appended."""
    start = text.find(BEGIN)
    if start < 0:
        return text
    stop = text.find(END, start)
    if stop < 0:
        sys.exit(f"{BEGIN!r} with no end marker after it: mend the file by hand")
    return text[:start].rstrip("\n") + "\n" + text[stop + len(END):].lstrip("\n")


def generate(badges):
    """Upstream's gen_icons.py run on the small copies alone: its icons.c and icons.h."""
    with tempfile.TemporaryDirectory() as scratch:
        for part in ("assets/icons", "tools", "common"):
            os.makedirs(os.path.join(scratch, part))
        for fn in badges:
            art = Image.open(os.path.join(SRC, fn)).convert("RGBA")
            # Pillow resizes RGBA through premultiplied alpha, so the
            # transparent edge does not bleed dark into the art
            small = art.resize((SIZE, SIZE), Image.BOX)
            stem = os.path.splitext(fn)[0]
            small.save(os.path.join(scratch, "assets", "icons", f"{stem}_{SIZE}.png"))
        for folder, names in HALVES.items():
            for name in names:
                art = Image.open(os.path.join(ROOT, "assets", folder, name + ".png")).convert("RGBA")
                half = art.resize((art.width // 2, art.height // 2), Image.BOX)
                half.save(os.path.join(scratch, "assets", "icons", f"{name}_half.png"))
        shutil.copy(os.path.join(ROOT, "tools", "gen_icons.py"), os.path.join(scratch, "tools"))
        subprocess.run([sys.executable, os.path.join(scratch, "tools", "gen_icons.py")],
                       check=True, stdout=subprocess.DEVNULL)
        with open(os.path.join(scratch, "common", "icons.c")) as c_file, \
             open(os.path.join(scratch, "common", "icons.h")) as h_file:
            return c_file.read(), h_file.read()


def main():
    badges = sorted(f for f in os.listdir(SRC) if f.startswith(PREFIX) and f.endswith(".png"))
    if not badges:
        sys.exit(f"no {PREFIX}*.png in {SRC}")
    small_c, small_h = generate(badges)

    # icons.c: the arrays and the icon_t lines - everything between the include
    # and the scratch run's own icons_all table, which is not wanted here
    head, separator, rest = small_c.partition('#include "icons.h"\n')
    body, separator_2, _ = rest.partition("const icon_t *const icons_all")
    if not separator or not separator_2:
        sys.exit("upstream's gen_icons.py wrote an icons.c of a shape this script does not know")
    body = body.strip("\n")
    # one declaration per icon ("extern const icon_t icon_..."), not the icons_all table's
    externs = [line for line in small_h.splitlines() if line.startswith("extern const icon_t icon_")]
    expected = len(badges) + sum(len(names) for names in HALVES.values())
    if len(externs) != expected or not all(line.endswith((f"_{SIZE};", "_half;")) for line in externs):
        sys.exit(f"expected {expected} declarations of *_{SIZE} and *_half icons, found {len(externs)}")

    with open(ICONS_C) as c_file:
        icons_c = without_block(c_file.read())
    with open(ICONS_H) as h_file:
        icons_h = without_block(h_file.read())
    icons_c = icons_c.rstrip("\n") + "\n\n" + BEGIN + "\n" + body + "\n" + END + "\n"
    endif = icons_h.rfind("#endif")
    if endif < 0:
        sys.exit("icons.h has no #endif to put the declarations before")
    icons_h = (icons_h[:endif].rstrip("\n") + "\n\n" + BEGIN + "\n" + "\n".join(externs) + "\n"
               + END + "\n\n" + icons_h[endif:])
    with open(ICONS_C, "w") as c_file:
        c_file.write(icons_c)
    with open(ICONS_H, "w") as h_file:
        h_file.write(icons_h)
    print(f"appended {len(badges)} {SIZE} px copies and {expected - len(badges)} half-size copies"
          " to common/icons.c and common/icons.h")


if __name__ == "__main__":
    main()
