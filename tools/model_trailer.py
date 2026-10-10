#!/usr/bin/env python3
"""model_trailer.py - the model partition's validity marker, as a file.

The tank's model partition ends with a 4 KB trailer (firmware/main/
model_trailer.c): magic, version, the body's length and sha256, a tag, a
crc. The cable installer writes it as its own part at the partition's last
sector, and flash.sh writes it beside the model; a tank with none heals it
at boot from the values in common/version.h.

  tools/model_trailer.py                       # model/out/model_q4.bin -> model_trailer.bin (in the build dir)
  tools/model_trailer.py --check               # also refuse if version.h disagrees with the file
  tools/model_trailer.py --model X --out Y --tag v3m

--check is what a release runs: PT_MODEL_TAG, PT_MODEL_LEN and
PT_MODEL_SHA256 in common/version.h must describe the model file that
ships, or the tank would heal a wrong trailer and, later, refuse a good
model update. Update version.h when the model changes."""
import argparse, hashlib, os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAGIC = 0x54524D50           # "PMRT"
SIZE = 0x1000

def fnv1a(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h

def version_h():
    src = open(os.path.join(ROOT, "common", "version.h")).read()
    tag = re.search(r'#define PT_MODEL_TAG\s+"([^"]+)"', src).group(1)
    ln = int(re.search(r'#define PT_MODEL_LEN\s+(\d+)u?', src).group(1))
    sha = re.search(r'#define PT_MODEL_SHA256\s+"([0-9a-f]{64})"', src).group(1)
    return tag, ln, sha

def build(model_path, tag):
    body = open(model_path, "rb").read()
    sha = hashlib.sha256(body).digest()
    head = struct.pack("<III32s16s", MAGIC, 1, len(body), sha, tag.encode()[:15].ljust(16, b"\0"))
    blob = head + struct.pack("<I", fnv1a(head))
    return blob.ljust(SIZE, b"\xff"), len(body), sha.hex()

def main():
    a = argparse.ArgumentParser()
    a.add_argument("--model", default=os.path.join(ROOT, "model", "out", "model_q4.bin"))
    a.add_argument("--out", default=None, help="default: <build dir>/model_trailer.bin")
    a.add_argument("--build-dir", default=os.path.expanduser("~/.cache/aqua-pets/fw-build"))
    a.add_argument("--tag", default=None, help="default: PT_MODEL_TAG from common/version.h")
    a.add_argument("--check", action="store_true", help="refuse if common/version.h disagrees with the model file")
    a = a.parse_args()
    tag, ln, sha = version_h()
    if a.tag: tag = a.tag
    blob, got_len, got_sha = build(a.model, tag)
    if a.check and (got_len != ln or got_sha != sha):
        sys.exit(f"model_trailer: common/version.h says {tag} {ln} B {sha[:16]}..., the file is {got_len} B {got_sha[:16]}... - update PT_MODEL_LEN / PT_MODEL_SHA256")
    out = a.out or os.path.join(a.build_dir, "model_trailer.bin")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "wb").write(blob)
    print(f"model_trailer: {out} - {tag}, {got_len:,} B, sha256 {got_sha[:16]}...{' (matches version.h)' if (got_len, got_sha) == (ln, sha) else ' (version.h DIFFERS)'}")

if __name__ == "__main__":
    main()
