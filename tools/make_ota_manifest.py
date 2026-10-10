#!/usr/bin/env python3
"""make_ota_manifest.py - the over-the-air release: the signed image renamed
by its release, and the small manifest the tank fetches (docs/OTA.md,
common/update.h update_manifest_t; the tank parses it in
firmware/main/net_port_esp.c).

  tools/make_ota_manifest.py --base-url https://aquapets.com/install/releases/v0.3.0 --out ota/
      -> ota/aqua_pets-v0.3.0-amoled18.bin  (a copy of the build's signed image)
         ota/latest-amoled18.json

One run per board (2026-10-02: 0.3.0 ships three - docs/BOARDS.md). The
board is read from the image's own marker (tools/pt_boards.py), never from
a flag, and names both files; a tank fetches latest-<its board>.json and
refuses a manifest or an image for another board.

  --note "One plain line for the offer page"   (default: the annotated tag's
         first line, else "A new version of the tank")
  --min-from 0.2.0     the oldest release this one can update from
  --model-url URL      phase two: the model is offered over the air

The manifest, by field: release "0.3.0" and release_num (PT_RELEASE_NUM's
encoding), board (the image's marker), build (the git build id), min_from, app {url, size, sha256},
note, needs_cable, model {version, size[, url]}. The tank offers the update
only when release_num is higher than its own, and says "needs the cable"
when needs_cable is set, when min_from is above its release, or when the
model's version is not the one it carries (until phase two).

The build must be SIGNED (tools/ota_key.sh; the tank refuses anything else)
and its common/version.h must agree with the model file (model_trailer.py
--check runs first). Refuses an unsigned image."""
import argparse, hashlib, json, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pt_boards import board_of_image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def version_h():
    src = open(os.path.join(ROOT, "common", "version.h")).read()
    g = lambda k: re.search(rf'#define {k}\s+"([^"]+)"', src).group(1)
    rel, stage, tag = g("PT_RELEASE"), g("PT_RELEASE_STAGE"), g("PT_MODEL_TAG")
    ln = int(re.search(r'#define PT_MODEL_LEN\s+(\d+)u?', src).group(1))
    return rel, stage, tag, ln

def release_num(s):
    a, b, c = (list(map(int, s.split("."))) + [0, 0, 0])[:3]
    return (a << 16) | (b << 8) | c

def git(*args):
    try: return subprocess.check_output(["git", "-C", ROOT, *args], stderr=subprocess.DEVNULL, text=True).strip()
    except Exception: return ""

def is_signed(path):
    """an ESP-IDF secure-boot-v2 signature block sits in the image's last 4 KB sector: magic 0xE7"""
    with open(path, "rb") as f:
        f.seek(-4096, os.SEEK_END); blk = f.read(4096)
    return len(blk) == 4096 and blk[0] == 0xE7 and blk[1] == 0x02

def main():
    a = argparse.ArgumentParser()
    a.add_argument("--build-dir", default=os.path.expanduser("~/.cache/aqua-pets/fw-build") if os.path.isdir(os.path.expanduser("~/.cache/aqua-pets/fw-build")) else os.path.join(ROOT, "firmware", "build"))
    a.add_argument("--base-url", required=True, help="where the .bin will be served from (no trailing slash)")
    a.add_argument("--out", default=os.path.join(ROOT, "ota"))
    a.add_argument("--note", default=None)
    a.add_argument("--min-from", default="0.2.0")
    a.add_argument("--model-url", default=None)
    a.add_argument("--needs-cable", action="store_true")
    a = a.parse_args()
    rel, stage, tag, model_len = version_h()
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "model_trailer.py"), "--check", "--build-dir", a.build_dir,
                    "--out", os.devnull], check=True)   # the check alone: CI's build dirs are root-owned (the IDF action's docker), nothing may be written there
    img = os.path.join(a.build_dir, "aqua_pets.bin")
    if not os.path.isfile(img): sys.exit(f"no {img}")
    if not is_signed(img): sys.exit(f"{img} carries no signature block - the build must sign (tools/ota_key.sh, CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES)")
    board = board_of_image(img)
    data = open(img, "rb").read()
    name = f"aqua_pets-v{rel}-{board}.bin"
    os.makedirs(a.out, exist_ok=True)
    open(os.path.join(a.out, name), "wb").write(data)
    note = a.note
    if not note:
        msg = git("tag", "-l", "--format=%(contents:subject)", f"v{rel}")
        note = msg.strip() or "A new version of the tank"
    note = note[:70]
    m = {
        "release": rel, "release_num": release_num(rel), "stage": stage, "board": board,
        "build": git("describe", "--always", "--dirty", "--exclude=*") or "dev",
        "min_from": a.min_from,
        "app": {"url": f"{a.base_url}/{name}", "size": len(data), "sha256": hashlib.sha256(data).hexdigest()},
        "note": note,
        "needs_cable": bool(a.needs_cable),
        "model": {"version": tag, "size": model_len, **({"url": a.model_url} if a.model_url else {})},
    }
    manifest = f"latest-{board}.json"
    json.dump(m, open(os.path.join(a.out, manifest), "w"), indent=2)
    print(f"ota: {a.out}/{name} ({len(data):,} B, signed) + {manifest} -> release {rel} {stage} for {board}, build {m['build']}, model {tag}\n     note: {note}")

if __name__ == "__main__":
    main()
