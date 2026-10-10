#!/usr/bin/env python3
"""make_installer.py - assemble the browser installer (ESP Web Tools).

Gathers the firmware build's bootloader / partition table / app, the
shipped model_q4.bin, the installer page and the vendored ESP Web Tools
bundle into ONE static folder that any HTTPS host can serve as-is:

    installer/dist/
        index.html          the page (version stamped in)
        manifest.json       what to flash where (ESP Web Tools format); the
                            Install button: NEVER erases, so a tank already
                            on the board (its save lives in NVS at 0x9000,
                            which no part touches) carries on after an update
        manifest-erase.json the same parts behind the "start over" button:
                            ESP Web Tools' erase question first
        manifest-<board>.json, manifest-<board>-erase.json
                            the same pair for each other board built
                            (--board-build; 2026-10-02, 0.3.0 ships three):
                            the 1.8 keeps the plain names every page
                            already points at
        firmware/*.bin      bootloader, partition table, app, model; a board's
                            file that differs from the 1.8's is <name>-<board>.bin
        vendor/esp-web-tools-<tag>/*.js   the flasher (Apache-2.0, vendored so the
                                    page has no third-party runtime deps;
                                    the install dialog gets the one-line
                                    never_erase patch below at assembly)

Offsets come from the build's flasher_args.json (bootloader / partition
table / app) and from firmware/partitions.csv (the model partition), so a
layout change can't silently ship a stale offset.

    tools/make_installer.py                      # default build dir
    tools/make_installer.py --build-dir path     # another idf.py -B dir
    tools/make_installer.py --version 1.2.0      # instead of git describe
    tools/make_installer.py --build-dir B18 --board-build BROUND --board-build BWATCH   # all three boards

Each build's board is read from its app image's marker (tools/pt_boards.py),
so a build dir in the wrong slot can never ship under another board's name.

Web Serial needs a secure context: serve the folder over HTTPS (or from
http://localhost for a local check: `python3 -m http.server -d installer/dist`)."""
import argparse, datetime, hashlib, json, os, re, shutil, struct, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pt_boards import BOARDS, board_of_image, name_of, build_of_image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BUILD = os.path.expanduser("~/.cache/aqua-pets/fw-build")

# ESP Web Tools (10.4.0) has no manifest option for "install without erasing"
# on a device that does not speak Improv: with new_install_prompt_erase the
# dialog asks (checkbox off by default), without it the dialog ERASES first,
# unconditionally. The tank's save would go with it. So the vendored dialog
# gets one edit at assembly: a manifest with "never_erase": true skips the
# question and starts a plain install (bootloader / partition table / app /
# model written at their offsets, NVS untouched). The edit is a literal
# replace of the two click handlers; a vendor upgrade that changes the text
# fails the build here instead of quietly shipping an erasing page.
DIALOG_ERASE_CLICK = 'this._manifest.new_install_prompt_erase?this._state="ASK_ERASE":this._startInstall(!0)'
DIALOG_NEVER_ERASE = 'this._manifest.new_install_prompt_erase?this._state="ASK_ERASE":this._startInstall(!this._manifest.never_erase)'
WIFI_FORM_TEXT = ("Connect your tank to Wi-Fi so it can download future updates. Wi-Fi is only on while it checks "
                  "for an update, or on the pendant to set its clock. Your tank sends nothing about you or your fish.")
# Three more, the same way (2026-10-04, Strato's first install on a brand-new board): each is
# (the text, how often it must occur, its replacement).
DIALOG_EDITS = [
    # after an install, Next opens the Wi-Fi form for a tank with no network (it did only after an
    # ERASING install, which this page never does; the keeper met the menu, and the tank its 25 s)
    ('this._state=i&&this._installErase?"PROVISION":"DASHBOARD"', 1,
     'this._state=i&&(this._installErase||this._showsProvisionForm)?"PROVISION":"DASHBOARD"'),
    # a tank already on this version says so (the menu's update item is hidden for it by the
    # dialog itself: the tank reports the manifest's own version string - director.c)
    ('<div slot="headline">Connected to ${this._info.name}</div>', 1,
     '<div slot="headline">${this._isSameVersion?"Your tank is up to date":"Connected to "+this._info.name}</div>'),
    # the dialog's "Visit Device" item (the link the tank hands the page; it read "See what's new"
    # until 2026-10-10, when Alvin had it removed): never shown, in the menu or after an install
    ('void 0===this._client.nextUrl?"":', 2, '!0?"":'),
    # a tank on this version got an "Erase User Data" item in the menu (an ERASING reinstall): this
    # page's button never erases - the "start over" button below it is the only way to a wipe
    ('this._isSameVersion?s`', 1, '!1?s`'),
    # connecting to a board: the dialog gave it 1.5 s to answer Improv, and the connect RESETS the
    # board - a tank answers ~0.7 s after the reset, but when the reset lands late the tank showed
    # as an unknown device ("Install Aqua Pets" on a tank that has it). 4 s; the dialog asks
    # again every second inside it. A board without Improv waits those 4 s once, at connect.
    (':1e4:1500;this._info=await t.initialize(i)', 1, ':1e4:4e3;this._info=await t.initialize(i)'),
    # the Wi-Fi form says what Wi-Fi is for, and what the tank does not do with it (Strato, 2026-10-04)
    ('<div>Connect your device to the network to start using it.</div>', 1, '<div>' + WIFI_FORM_TEXT + '</div>'),
]


def patch_dialog(vendor_dir):
    """apply the never_erase edit to the copied install dialog bundle"""
    names = [n for n in os.listdir(vendor_dir) if n.startswith("install-dialog") and n.endswith(".js")]
    if len(names) != 1:
        sys.exit(f"{vendor_dir}: expected one install-dialog*.js, found {names}")
    path = os.path.join(vendor_dir, names[0])
    js = open(path).read()
    n = js.count(DIALOG_ERASE_CLICK)
    if n != 2:
        sys.exit(f"{path}: the erase click handler occurs {n} times, expected 2 - ESP Web Tools "
                 "changed; re-check the never_erase patch before shipping the installer")
    js = js.replace(DIALOG_ERASE_CLICK, DIALOG_NEVER_ERASE)
    for old, want, new in DIALOG_EDITS:
        if js.count(old) != want:
            sys.exit(f"{path}: {old[:60]!r} occurs {js.count(old)} times, expected {want} - ESP Web Tools "
                     "changed; re-check the dialog edits before shipping the installer")
        js = js.replace(old, new)
    open(path, "w").write(js)
    return names[0], hashlib.sha1(js.encode()).hexdigest()[:8]


def model_part():
    """the 'model' row of firmware/partitions.csv -> (offset, size)"""
    with open(os.path.join(ROOT, "firmware", "partitions.csv")) as f:
        for line in f:
            cols = [c.strip() for c in line.split(",")]
            if len(cols) >= 5 and cols[0] == "model":
                return int(cols[3], 0), int(cols[4], 0)
    sys.exit("partitions.csv: no 'model' partition")
def model_offset():
    return model_part()[0]
# the model trailer (docs/OTA.md; firmware/main/model_trailer.c): the LAST
# sector of the model partition, written by the installer as its own part so
# a cable install leaves the model marked good
TRAILER_SIZE = 0x1000
def model_trailer_offset():
    off, size = model_part()
    return off + size - TRAILER_SIZE


# The tank's save lives in NVS (firmware/partitions.csv: nvs at 0x9000, 0x6000
# long), and "updating never erases" holds only while it stays there: the
# page writes the partition table too, so a moved or resized nvs row would
# boot the update on blank NVS (the tank gone), and a part that grew into
# 0x9000..0xF000 would overwrite it. So the build fails on either, reading the
# partition table the page actually ships.
NVS_OFFSET, NVS_SIZE = 0x9000, 0x6000


def check_nvs_untouched(parts, ptable):
    rows = {}
    raw = open(ptable, "rb").read()
    for i in range(0, len(raw) - 31, 32):
        magic, ptype, sub, off, size = struct.unpack_from("<HBBII", raw, i)
        if magic != 0x50AA:                  # 0xEBEB = the md5 row, 0xFFFF = the end
            break
        rows[raw[i + 12:i + 28].split(b"\0")[0].decode()] = (ptype, sub, off, size)
    nvs = [(label, r) for label, r in rows.items() if r[:2] == (1, 2)]   # data, nvs
    if len(nvs) != 1 or nvs[0][1][2:] != (NVS_OFFSET, NVS_SIZE):
        sys.exit(f"{ptable}: the nvs row must stay at 0x{NVS_OFFSET:x}, 0x{NVS_SIZE:x} long - found "
                 + (", ".join(f"{l} 0x{r[2]:x} 0x{r[3]:x}" for l, r in nvs) or "none")
                 + "; every keeper's tank lives there and an update would leave it behind")
    for off, src, pub in parts:
        end = off + os.path.getsize(src)
        if off < NVS_OFFSET + NVS_SIZE and end > NVS_OFFSET:
            sys.exit(f"{pub}: 0x{off:x}..0x{end:x} overlaps nvs 0x{NVS_OFFSET:x}..0x{NVS_OFFSET + NVS_SIZE:x} "
                     "- installing it would overwrite the keeper's tank")
    if "model" not in rows or rows["model"][2:4] != tuple(model_part()):
        sys.exit(f"{ptable}: its model row disagrees with firmware/partitions.csv - rebuild the firmware")
    # over-the-air (docs/OTA.md): two app slots and an otadata row, or the tank can never update itself
    apps = [l for l, r in rows.items() if r[0] == 0]
    if not any(r[0] == 1 and r[1] == 0 for r in rows.values()) or len(apps) < 2:
        sys.exit(f"{ptable}: OTA needs otadata + two app slots (found apps {apps})")


def release_version(build_id=None):
    """common/version.h's release (2026-09-29): "v0.2.0 alpha" - the number
    the tank's settings page shows - with the build id beside it. The id is
    the IMAGE's own (2026-10-04: the tank reports this very string to the
    page over Improv, and the page hides its update item when the two are
    equal - an id taken from git at assembly differed whenever the checkout
    had moved since the build)."""
    h = open(os.path.join(ROOT, "common", "version.h")).read()
    rel = re.search(r'#define PT_RELEASE\s+"([^"]+)"', h).group(1)
    stage = re.search(r'#define PT_RELEASE_STAGE\s+"([^"]*)"', h).group(1)
    return f"v{rel} {stage}".strip() + f" (build {build_id or git_version()})"


def git_version():
    try:
        out = subprocess.run(["git", "-C", ROOT, "describe", "--always", "--dirty", "--exclude=*"],   # the hash, never a tag
                             capture_output=True, text=True, check=True).stdout.strip()
        return out
    except Exception:
        return "dev"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=DEFAULT_BUILD if os.path.isdir(DEFAULT_BUILD)
                    else os.path.join(ROOT, "firmware", "build"))
    ap.add_argument("--model", default=os.path.join(ROOT, "model", "out", "model_q4.bin"))
    ap.add_argument("--out", default=os.path.join(ROOT, "installer", "dist"))
    ap.add_argument("--version", default=None)
    ap.add_argument("--manifest-url", default="manifest.json",
                    help="what the page's install button points at: the relative default for a "
                         "self-contained folder, or an absolute URL (e.g. the GitHub Pages copy) "
                         "for a page hosted somewhere else")
    ap.add_argument("--board-build", action="append", default=[],
                    help="another board's idf.py -B dir (repeat per board); its board comes from its image")
    a = ap.parse_args()

    def build_parts(build_dir):
        """(offset, source path, bare published name) for one board's build"""
        fa_path = os.path.join(build_dir, "flasher_args.json")
        if not os.path.isfile(fa_path):
            sys.exit(f"{fa_path}: not a firmware build dir (run idf.py build first)")
        fa = json.load(open(fa_path))
        parts = []
        for key, pub in (("bootloader", "bootloader.bin"), ("partition-table", "partition-table.bin"),
                         ("app", "aqua_pets.bin"), ("otadata", "ota_data_initial.bin")):
            if key not in fa:
                sys.exit(f"{fa_path}: no '{key}' - the build has no OTA layout (firmware/partitions.csv)")
            ent = fa[key]
            parts.append((int(ent["offset"], 0), os.path.join(build_dir, ent["file"]), pub))
        # the otadata initial image: a cable install always boots ota_0 again,
        # whatever slot the tank had updated itself into
        parts.append((model_offset(), a.model, "model_q4.bin"))
        if os.path.getsize(a.model) > model_part()[1] - TRAILER_SIZE:
            sys.exit(f"{a.model}: {os.path.getsize(a.model):,} B does not leave the model partition's last sector for the trailer")
        # the trailer is made in a scratch folder, never in the build dir: in CI the build dirs are
        # root-owned (the ESP-IDF action's docker) and the public repo's first 0.3.0 installer run
        # died here (2026-10-04; make_ota_manifest.py had the same fault at the rehearsal)
        trailer = os.path.join(tempfile.mkdtemp(prefix="pt-trailer-"), "model_trailer.bin")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "model_trailer.py"), "--model", a.model, "--out", trailer, "--check"], check=True)
        parts.append((model_trailer_offset(), trailer, "model_trailer.bin"))
        parts.sort()
        for off, src, pub in parts:
            if not os.path.isfile(src):
                sys.exit(f"missing: {src}")
        check_nvs_untouched(parts, os.path.join(build_dir, fa["partition-table"]["file"]))
        return parts

    builds = {}                                   # board id -> parts, from each image's own marker
    for d in [a.build_dir] + a.board_build:
        parts = build_parts(d)
        board = board_of_image(next(src for _, src, pub in parts if pub == "aqua_pets.bin"))
        if board in builds:
            sys.exit(f"{d}: a second build for {board}")
        builds[board] = parts
    if "amoled18" not in builds:
        sys.exit("no 1.8 build: manifest.json (the name every install page points at) is the 1.8's")
    order = [b[0] for b in BOARDS if b[0] in builds]

    app18 = next(src for _, src, pub in builds["amoled18"] if pub == "aqua_pets.bin")
    version = a.version or release_version(build_of_image(app18))   # the page's line: the 1.8's
    date = datetime.date.today().isoformat()
    out = a.out
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "firmware"))
    published = {}                                 # published name -> bytes' sha256: one copy of what the boards share
    def publish(src, pub, board):
        digest = hashlib.sha256(open(src, "rb").read()).hexdigest()
        if published.get(pub, digest) != digest:   # the 1.8 has this name with other bytes: the board's own copy
            stem, ext = os.path.splitext(pub)
            pub = f"{stem}-{board}{ext}"
        if pub not in published:
            shutil.copyfile(src, os.path.join(out, "firmware", pub))
            published[pub] = digest
        return pub
    board_files = {b: [(off, publish(src, pub, b), src) for off, src, pub in builds[b]] for b in order}
    total = sum(os.path.getsize(src) for _, _, src in board_files["amoled18"])
    shutil.copytree(os.path.join(ROOT, "installer", "vendor"), os.path.join(out, "vendor"))
    media = os.path.join(ROOT, "installer", "media")        # the page's own pictures (2026-10-07: the species)
    if os.path.isdir(media):
        shutil.copytree(media, os.path.join(out, "media"))
    dialog, tag = patch_dialog(os.path.join(out, "vendor", "esp-web-tools"))
    # The bundle's file names are content hashes of the PRISTINE vendor, and
    # hosts serve .js with a year's max-age: a patched dialog under the old
    # path stays the old, erasing one in every CDN and browser cache (it
    # happened, 2026-09-18). So the folder carries the patched dialog's hash -
    # the imports inside are relative, a new folder is a new URL for all of it.
    vendor = f"vendor/esp-web-tools-{tag}"
    os.rename(os.path.join(out, "vendor", "esp-web-tools"), os.path.join(out, vendor))

    page_boards = []                         # what the page's board picker offers, in BOARDS order
    for b in order:
        build = {"chipFamily": "ESP32-S3",
                 "parts": [{"path": f"firmware/{pub}", "offset": off} for off, pub, _ in board_files[b]]}
        manifest = {
            "name": "Aqua Pets",
            "version": a.version or release_version(build_of_image(next(src for _, _, src in board_files[b] if os.path.basename(src) == "aqua_pets.bin"))),
            "built": date,                       # read by the page (ESP Web Tools ignores extra keys)
            "board": b, "board_name": name_of(b),
            "new_install_prompt_erase": False,
            "never_erase": True,                 # the patched dialog: no erase question, no erase - the
                                                 # tank on the board (NVS) survives; a blank board boots fresh
            "builds": [build],
        }
        stem = "manifest" if b == "amoled18" else f"manifest-{b}"
        json.dump(manifest, open(os.path.join(out, f"{stem}.json"), "w"), indent=2)
        erase = dict(manifest, name="Aqua Pets (fresh)", new_install_prompt_erase=True)
        del erase["never_erase"]                 # the "start over" button: the dialog asks, checkbox off by default
        json.dump(erase, open(os.path.join(out, f"{stem}-erase.json"), "w"), indent=2)
        page_boards.append({"id": b, "name": name_of(b),
                            "manifest": a.manifest_url.replace("manifest.json", f"{stem}.json"),
                            "erase": a.manifest_url.replace("manifest.json", f"{stem}-erase.json")})
    # Apache / LiteSpeed hosts sometimes refuse .bin or serve .json as text;
    # harmless elsewhere. The page itself must never come out of a host's
    # page cache: it names the vendor folder, and a stale page is a stale
    # (once: erasing) dialog.
    open(os.path.join(out, ".htaccess"), "w").write(
        "AddType application/octet-stream .bin\nAddType application/json .json\n"
        "AddType text/javascript .js\n"
        "<IfModule mod_headers>\n<FilesMatch \"\\.html$\">\n"
        "Header set Cache-Control \"no-cache, must-revalidate\"\n</FilesMatch>\n</IfModule>\n")

    page = open(os.path.join(ROOT, "installer", "index.html")).read()
    page = page.replace("{{VERSION}}", version).replace("{{DATE}}", date)
    page = page.replace("{{TOTAL_MB}}", f"{total / 1e6:.1f}")
    page = page.replace("{{VENDOR}}", vendor)
    page = page.replace("{{MANIFEST}}", a.manifest_url)
    page = page.replace("{{MANIFEST_ERASE}}", a.manifest_url.replace("manifest.json", "manifest-erase.json"))
    page = page.replace("{{BOARDS}}", json.dumps(page_boards))
    open(os.path.join(out, "index.html"), "w").write(page)
    open(os.path.join(out, ".nojekyll"), "w").close()   # GitHub Pages: serve the folder as-is

    print(f"installer -> {out}  (version {version}, {date}; manifest {a.manifest_url}; "
          f"never_erase patch in {vendor}/{dialog})")
    for b in order:
        print(f"  {b} ({'manifest' if b == 'amoled18' else 'manifest-' + b}.json):")
        for off, pub, src in board_files[b]:
            print(f"    0x{off:06x}  {os.path.getsize(src):>9,} B  {pub}")
    print(f"  {total:,} B to flash (the 1.8)")


if __name__ == "__main__":
    main()
