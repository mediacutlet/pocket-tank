#!/usr/bin/env python3
"""make_sounds.py - the sound pipeline (docs/AUDIO.md).

Two steps, both repo-relative to this file:

  import <dir>   Strato's exports (any WAV: float/int, stereo, any rate) ->
                 assets/sounds/<cue>[-<n>].wav, the canonical 16 kHz mono
                 s16 files. Per file: fold to mono, 600 Hz high-pass (the
                 12 mm speaker's floor; below it only headroom is spent),
                 7 kHz low-pass, resample, trim silence below -60 dBFS at
                 both ends, 5 ms fades, peak-normalize to -1 dBFS. A loop
                 cue gets a 20 ms equal-power crossfade across the seam
                 instead of trim + fades. gains.csv gets a row per new cue
                 seeded with the export's own speaker-band level, so the
                 first bank plays at the relative loudness Strato exported.
  build          assets/sounds/*.wav + gains.csv -> assets/sounds/sounds.bin
                 (the bank: every clip's s16 samples back to back) and
                 common/sounds.h + sounds.c (ids, clip table, cue table).
                 The firmware embeds the .bin (EMBED_FILES); the sim reads
                 it from disk.

File naming in the export folder: sound-<cue>.wav. A trailing _v<N> is a
revision (highest wins, older ignored, nothing to delete); a trailing -<N>
is a VARIANT (the player picks one at random). Names that are not a cue
are skipped with a note. CUES below is the enum order and the contract
with the firmware; a cue with no file (deferred) keeps its id and plays
nothing.

    ~/.venvs/aquapets/bin/python tools/make_sounds.py import \\
        "/Volumes/Local-1/Projects/LLM Fish Tank/working-assets/sounds/exports"
    ~/.venvs/aquapets/bin/python tools/make_sounds.py build
"""
import csv, math, os, re, struct, subprocess, sys, wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DST = os.path.join(ROOT, "assets", "sounds")
GAINS = os.path.join(DST, "gains.csv")
BANK = os.path.join(DST, "sounds.bin")
OUT_H = os.path.join(ROOT, "common", "sounds.h")
OUT_C = os.path.join(ROOT, "common", "sounds.c")

RATE = 16000
HP_HZ, LP_HZ = 600, 7000
TRIM_DB, FADE_MS, XFADE_MS = -60.0, 5, 20
NORM_DB = -1.0

# enum order (docs/AUDIO.md section 4). (id, loop?)
CUES = [
    ("tap", 0), ("feed", 0), ("light_on", 0), ("light_off", 0), ("wipe", 0),
    ("snip", 0), ("card_open", 0), ("card_close", 0), ("wheel_tick", 0),
    ("confirm", 0), ("bubbles_loop", 1),
    ("eat", 0), ("spook", 0), ("investigate", 0), ("bubbles", 0), ("beg", 0),
    ("welcome", 0), ("arrival", 0), ("milestone", 0), ("stage_up", 0),
    ("sleep", 0), ("wake", 0), ("low_battery", 0), ("error", 0),
]
CUE_IDS = [c for c, _ in CUES]
LOOPS = {c for c, l in CUES if l}
# export stems that don't spell the cue id
ALIASES = {"bubble_cluster": "bubbles", "fry_arrival": "arrival",
           "plant_snip": "snip", "spook_dash": "spook"}
IGNORE = {"confusion"}
DEFERRED = {"beg", "bubbles"}   # cues kept in the enum, files present but not shipped (Strato 2026-09-15: beg on the fence; bubbles too frequent to be worth hearing)


def db(x):
    return 20 * math.log10(x) if x > 0 else -99.0


# ---------------------------------------------------------------- import ----
def parse_export_name(fn):
    """'sounds-light_off.wav' -> ('light_off', rev 1, var 1); None if not ours."""
    m = re.match(r"^sounds?-(.+?)(?:_v(\d+))?(?:-(\d+))?\.wav$", fn, re.I)
    if not m:
        return None
    stem, rev, var = m.group(1).lower(), int(m.group(2) or 1), int(m.group(3) or 1)
    return ALIASES.get(stem, stem), rev, var


def decode(path):
    """ffmpeg: any WAV -> float32 mono at RATE, band-limited to the speaker."""
    af = (f"highpass=f={HP_HZ}:poles=2,highpass=f={HP_HZ}:poles=2,"
          f"lowpass=f={LP_HZ}:poles=2,aresample={RATE}")
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-af", af,
                          "-ac", "1", "-f", "f32le", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(out, dtype=np.float32).astype(np.float64)


def trim_fade(x):
    thr = 10 ** (TRIM_DB / 20)
    idx = np.where(np.abs(x) > thr)[0]
    if len(idx) == 0:
        return x[:0]
    x = x[max(0, idx[0] - RATE * 2 // 1000): idx[-1] + 1 + RATE * 10 // 1000].copy()
    n = min(RATE * FADE_MS // 1000, len(x) // 2)
    if n:
        ramp = np.linspace(0, 1, n)
        x[:n] *= ramp
        x[-n:] *= ramp[::-1]
    return x


def loop_xfade(x):
    """equal-power crossfade of the tail into the head; the file gets XFADE
    shorter and wraps sample-continuous (end -> start)."""
    L = RATE * XFADE_MS // 1000
    if len(x) < 4 * L:
        sys.exit("loop clip too short for a crossfade")
    t = np.linspace(0, 1, L, endpoint=False)
    head = x[:L] * np.sin(0.5 * math.pi * t) + x[-L:] * np.cos(0.5 * math.pi * t)
    y = x[:-L].copy()
    y[:L] = head
    return y


def write_wav(path, x):
    pk = np.abs(x).max() if len(x) else 0
    if pk > 0:
        x = x * (10 ** (NORM_DB / 20) / pk)
    s16 = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(s16.tobytes())
    return pk


def read_gains():
    rows = {}
    if os.path.exists(GAINS):
        with open(GAINS, newline="") as f:
            for r in csv.DictReader(f):
                rows[r["cue"]] = float(r["gain_db"])
    return rows


def write_gains(rows):
    with open(GAINS, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["cue", "gain_db"])
        for c in CUE_IDS:
            if c in rows:
                w.writerow([c, f"{rows[c]:.1f}"])


def do_import(src):
    files = {}
    for fn in sorted(os.listdir(src)):
        if not fn.lower().endswith(".wav"):
            continue
        p = parse_export_name(fn)
        if not p:
            print(f"  skip {fn}: not sound-<cue>.wav"); continue
        cue, rev, var = p
        if cue in IGNORE:
            print(f"  skip {fn}: ignored"); continue
        if cue in DEFERRED:
            print(f"  skip {fn}: deferred"); continue
        if cue not in CUE_IDS:
            print(f"  skip {fn}: '{cue}' is not a cue in docs/AUDIO.md"); continue
        k = (cue, var)
        if k not in files or files[k][0] < rev:
            files[k] = (rev, fn)
    os.makedirs(DST, exist_ok=True)
    for old in os.listdir(DST):                       # rebuild the canonical set
        if old.endswith(".wav"):
            os.remove(os.path.join(DST, old))
    gains = read_gains()
    measured = {}
    print(f"{'cue':14s} {'from':30s} {'dur':>6s} {'export pk':>9s}")
    for (cue, var), (rev, fn) in sorted(files.items()):
        x = decode(os.path.join(src, fn))
        x = loop_xfade(x) if cue in LOOPS else trim_fade(x)
        out = os.path.join(DST, f"{cue}{'' if var == 1 else f'-{var}'}.wav")
        pk = write_wav(out, x)
        measured.setdefault(cue, []).append(pk)
        print(f"{cue:14s} {fn:30s} {len(x) / RATE:6.2f} {db(pk):9.1f}")
    # seed gains: an export's own speaker-band peak, relative to 0 dBFS
    for cue, pks in measured.items():
        if cue not in gains:
            gains[cue] = round(db(max(pks)), 1)
    write_gains(gains)
    missing = [c for c in CUE_IDS if c not in measured]
    print(f"\n{len(measured)} cues imported; no file for: {', '.join(missing)}")
    print(f"gains.csv: {len(gains)} rows (edit gain_db to retune; never re-export for level)")


# ----------------------------------------------------------------- build ----
def do_build():
    gains = read_gains()
    clips = []          # (cue, var, samples)
    for fn in sorted(os.listdir(DST)):
        m = re.match(r"^([a-z_]+?)(?:-(\d+))?\.wav$", fn)
        if not m:
            continue
        cue, var = m.group(1), int(m.group(2) or 1)
        if cue not in CUE_IDS:
            sys.exit(f"{fn}: not a cue")
        with wave.open(os.path.join(DST, fn)) as w:
            assert (w.getnchannels(), w.getsampwidth(), w.getframerate()) == (1, 2, RATE), fn
            data = w.readframes(w.getnframes())
        clips.append((cue, var, data))
    clips.sort(key=lambda c: (CUE_IDS.index(c[0]), c[1]))
    bank, table, cue_rows = bytearray(), [], []
    for cue in CUE_IDS:
        mine = [c for c in clips if c[0] == cue]
        first = len(table)
        for _, var, data in mine:
            table.append((len(bank) // 2, len(data) // 2))
            bank += data
        g = gains.get(cue, 0.0)
        q8 = int(round(256 * 10 ** (g / 20))) if mine else 0
        cue_rows.append((cue, first, len(mine), min(q8, 65535), 1 if cue in LOOPS else 0))
    with open(BANK, "wb") as f:
        f.write(bank)
    with open(OUT_H, "w") as f:
        f.write("/* sounds.h - GENERATED by tools/make_sounds.py; do not edit.\n"
                " * Cue ids + the clip/cue tables over the sound bank\n"
                " * (assets/sounds/sounds.bin: 16 kHz mono s16, clips back to back).\n"
                " * A cue with n_var == 0 is deferred: valid id, plays nothing. */\n"
                "#ifndef SOUNDS_H\n#define SOUNDS_H\n#include <stdint.h>\n\n"
                f"#define SND_RATE {RATE}\n"
                f"#define SND_BANK_SAMPLES {len(bank) // 2}u\n"
                f"#define SND_BANK_BYTES {len(bank)}u\n\n"
                "enum {\n")
        for c in CUE_IDS:
            f.write(f"    SND_{c.upper()},\n")
        f.write("    SND_COUNT\n};\n\n"
                "typedef struct { uint32_t off, len; } snd_clip_t;   /* samples into the bank */\n"
                "typedef struct {\n"
                "    const char *name;\n"
                "    uint8_t  first, n_var;    /* SND_CLIPS[first .. first + n_var) */\n"
                "    uint8_t  loop;            /* seamless wrap (bubbles_loop) */\n"
                "    uint16_t gain_q8;         /* playback gain, 256 = 0 dB (gains.csv) */\n"
                "} snd_cue_t;\n\n"
                f"#define SND_N_CLIPS {len(table)}\n"
                "extern const snd_clip_t SND_CLIPS[SND_N_CLIPS];\n"
                "extern const snd_cue_t  SND_CUES[SND_COUNT];\n\n#endif\n")
    with open(OUT_C, "w") as f:
        f.write("/* sounds.c - GENERATED by tools/make_sounds.py; do not edit. */\n"
                "#include \"sounds.h\"\n\n"
                f"const snd_clip_t SND_CLIPS[SND_N_CLIPS] = {{\n")
        for off, n in table:
            f.write(f"    {{ {off}u, {n}u }},\n")
        f.write("};\n\nconst snd_cue_t SND_CUES[SND_COUNT] = {\n")
        for cue, first, n, q8, loop in cue_rows:
            f.write(f"    {{ \"{cue}\", {first}, {n}, {loop}, {q8} }},\n")
        f.write("};\n")
    print(f"bank {len(bank) / 1024:.0f} KB, {len(table)} clips, {sum(1 for r in cue_rows if r[2])} of {len(CUE_IDS)} cues present")
    for cue, first, n, q8, loop in cue_rows:
        secs = sum(table[i][1] for i in range(first, first + n)) / RATE
        print(f"  {cue:14s} {'x' + str(n) if n else '-':4s} {secs:5.2f} s  gain {db(q8 / 256) if q8 else 0:6.1f} dB{'  loop' if loop else ''}")


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "import":
        do_import(sys.argv[2])
    elif len(sys.argv) == 2 and sys.argv[1] == "build":
        do_build()
    else:
        sys.exit(__doc__)
