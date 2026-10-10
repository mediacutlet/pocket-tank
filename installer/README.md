# Browser installer

Plug the board in, open a page, click Install: the same "flash it from the
browser" flow ESPHome and Home Assistant use ([ESP Web
Tools](https://esphome.github.io/esp-web-tools/), Apache-2.0, vendored under
`vendor/`). Chrome or Edge on a desktop; it uses Web Serial, which Safari
and Firefox don't have. Chrome on Android works too (2026-10-10): it has WebUSB
but no Web Serial, so the page installs Google's web-serial-polyfill (vendored under
`vendor/web-serial-polyfill/`, Apache-2.0) as `navigator.serial` only when Web Serial
is missing, and ESP Web Tools runs unchanged over the board's USB CDC interface.
`?polyfill=1` forces it on a desktop for testing. iOS has neither API.

## Build the upload folder

```
idf.py -B ~/.cache/aqua-pets/fw-build build     # in firmware/ (or any -B dir)
tools/make_installer.py                            # -> installer/dist/
python3 -m http.server -d installer/dist 8765      # local check at http://localhost:8765
```

The local theme installer uses a cream and lagoon-green layout with previews of
Original, Quiet Lagoon and Tidepool Club. It requires an explicit board choice
before enabling Install, and validates the selected manifest. ESP Web Tools and
all artwork are served locally, with no external runtime dependencies.

`installer/dist/` is the whole thing: `index.html`, `manifest.json`,
`manifest-erase.json`, `firmware/*.bin` (bootloader, partition table, app,
model), `vendor/`. The offsets in the manifests come from the build's
`flasher_args.json` and from `firmware/partitions.csv`, and the page carries
the git version. It is gitignored: rebuild it for every release.

## Updating never erases (the two manifests, and one patched line)

The tank's save lives in NVS at `0x9000`; the four parts sit at `0x0`,
`0x8000`, `0x10000` and `0x290000`, so a plain write of the four leaves it
alone and the tank carries on after an update (`tools/flash.sh` does the same
writes every day). The catch was in the dialog: ESP Web Tools 10.4.0 has no
manifest option for "install, don't erase" on a device without Improv.
`new_install_prompt_erase: true` asks (checkbox off by default), and without
it the dialog erases the chip first, unconditionally. So:

- `manifest.json` (the red button) carries `"never_erase": true` and
  `make_installer.py` patches the copied `install-dialog-*.js` at assembly:
  the two click handlers that read `_startInstall(!0)` (erase) become
  `_startInstall(!this._manifest.never_erase)`. The build fails loudly if
  the handler text is not found exactly twice, so a vendor upgrade can't
  ship an erasing page by accident. `vendor/` itself stays pristine.
- `manifest-erase.json` (the "Erase the board and install fresh" button)
  is the same parts with the erase question, for a board that is stuck or
  a keeper who wants a clean flash. The on-device reset (hold BOOT, tap the
  glass) is the normal way to start over and needs no page.

The firmware side of the promise: `common/progression.c` loads a save of any
older length (the tail only ever appends) and slides the one mid-struct
insertion (bubble_x, 2026-09-14) into place for saves from the first public
builds; since 2026-09-29 it also loads the head of a NEWER build's longer
save, so rolling back to an older release keeps the tank (the newer-only
tail reads as its defaults when the newer build returns). What guards it:

- **SAVE LAYOUT LOCK** (progression.c): a compile-time assert on every
  save_t / fish_save_t offset, the magic, the NVS names, and the NVS size
  budget (4000 B, math in the comment). A field inserted mid-struct, a
  rename of `tank`/`save`, or a save too big for the partition fails the
  build, on the device and in the sim.
- **`sim/fishsim --selftest-saves`**: every save in `sim/testdata/saves/`
  (the board's, the sim's, one with every tail set) loads whole and cut to
  every older build's length, checked field by field against the file's
  own bytes, then again after a save; plus a newer build's longer save (the
  rollback). Add a save from each release there (its README says how).
- **`check_nvs_untouched`** (make_installer.py): the installer build fails
  if the partition table it ships moves or resizes nvs, or if any part it
  writes overlaps 0x9000..0xF000.
- **Boot never erases on a guess** (main.c `nvs_start`): only an NVS that
  can't mount at all ("no free pages") is erased, and a raw copy goes to the
  front of the unused `storage` partition first (`esptool read_flash
  0xA90000 0x6000`); any other init error runs the tank without saving and
  leaves the flash alone. A save that exists but won't read turns saving
  off until a reset, so it is never overwritten by a fresh tank.

## It updates itself (GitHub Pages)

`.github/workflows/installer.yml` in the PUBLIC repo builds the firmware
with ESP-IDF v5.4.1 on every push to `main` that touches `firmware/`,
`common/`, `model/out/`, `installer/` or the assembler, runs
`tools/make_installer.py`, and deploys the folder to
**https://aquapets.com/install/**. Pages serves HTTPS with
`Access-Control-Allow-Origin: *`, so the copy on stratobuilds.com points
its button at that manifest:

```
tools/make_installer.py --manifest-url https://aquapets.com/install/manifest.json --out /tmp/site
```

and that `index.html` PLUS its `vendor/esp-web-tools-<tag>/` folder live on
the site - the never-erase patch is in the vendor's dialog bundle. An old
`vendor/` next to the new manifest erased every install without asking
(the 09-11 upload, found 2026-09-18), and the host serves `.js` with a
year's max-age, so the folder name now carries the patched dialog's hash.
(The old one-command upload to stratobuilds.com, `tools/publish_site_installer.sh`,
was retired on 2026-10-04 and removed on 2026-10-10.)
The page fetches the manifest on load and shows the version and build date of what it will actually flash,
so pushing to the public repo is the whole release step: no upload, no
cache purge. (Manual failure mode: the Actions run is red - `gh run list
--repo fiatminimalist/aqua-pets`.)

## Android, and the local server's certificate (2026-10-10)

Chrome on Android installs through WebUSB (the vendored web-serial-polyfill) and
shows a USB chooser on Install - but Chrome refuses every permission prompt on a
page it opened through a certificate warning, so a self-signed server must be
TRUSTED by the phone first. The local `serve.py` now presents a CA certificate
whose Subject Alternative Names are the machine's addresses (192.168.88.216,
100.66.66.66, 127.0.0.1, flux, localhost; regenerate it with openssl and a SAN
config if the addresses change) and serves it at `/aqua-pets-installer.crt`. The
page's Android section links it with the install steps (Settings → Security →
Install a certificate → CA certificate) and has a **Test USB access** button that
runs the chooser alone and says in words what happened. A public HTTPS host with
a real certificate needs none of this.

## Host it

Web Serial needs a secure context, so the page must be on **HTTPS** (or
`localhost`). Any static host works; the manifest and the `.bin` files must be
fetchable from the page's origin (or send CORS headers).

**stratobuilds.com (WordPress behind Cloudflare):** upload `installer/dist/`
as a folder next to WordPress, e.g. `public_html/aqua-pets/`, and link
`https://stratobuilds.com/aqua-pets/`. Being a plain folder it is outside
WordPress, so themes, caching and security plugins don't touch it. Things to
check once:

- Open the page, the button must say *Install Aqua Pets*, not the red
  unsupported text. If it never appears, Cloudflare's Rocket Loader is
  rewriting the module script: exclude `/aqua-pets/*` from it (a
  Configuration Rule), or turn it off.
- In the browser's network tab `manifest.json` and `firmware/*.bin` must be
  200. A 403 on `.bin` means the host blocks the type: the `.htaccess` in the
  folder adds it for Apache/LiteSpeed; on nginx add
  `types { application/octet-stream bin; }`.
- Cloudflare caches `.bin` and `.js` by default. After uploading a new build,
  purge `/aqua-pets/*` (the manifest carries the version, so a stale
  cache shows an old version string on the page).

**GitHub Pages** (the public repo) is the other easy option: push `dist/` to
a `gh-pages` branch or a `docs/installer/` folder. Pages serves HTTPS and
`Access-Control-Allow-Origin: *`, so the button can even live on a WordPress
page (Custom HTML block with the `<script type="module">` tag and the
`<esp-web-install-button manifest="https://...manifest.json">` element)
while the binaries stay on GitHub.

## What the user sees

Click → the browser's port picker (`USB JTAG/serial debug unit`) → *Install
Aqua Pets* → "Do you want to install Aqua Pets <version>?" → a progress
bar over the four parts → *Installation complete*, and the board resets into
the tank: fresh on a blank board, the same tank on one that had it. The
"start over" button adds the erase question (tick *Erase device*). The page's
"If something's off" section covers the usual snags: a sleeping tank hides
its USB port (press PWR), holding BOOT while plugging in forces download
mode, Linux group permissions, charge-only cables, and an optional esptool
backup of the save area for the cautious.

## Verified

The artifact set in `dist/firmware/` at the manifest's offsets was written to
the real board with `esptool.py write_flash` (the exact operation ESP Web
Tools performs, from the same files), and the tank booted; the page, manifest
and vendor bundle were checked from a local server. The browser's own port
picker is a native dialog, so the click-through itself is a human test.

## Local theme build (2026-10-09)

Built all three boards with the cached `espressif/idf:v5.4.1` image. Build outputs
are in `~/.cache/aquapets-idf/build/{amoled18,round175c,watch206}`. Assemble with:

```sh
python3 tools/make_installer.py \
  --build-dir ~/.cache/aquapets-idf/build/amoled18 \
  --board-build ~/.cache/aquapets-idf/build/round175c \
  --board-build ~/.cache/aquapets-idf/build/watch206
```

The existing server at `~/.cache/aquapets-installer/serve.py` serves
`installer/dist` on `0.0.0.0:8765` (HTTP) and `0.0.0.0:8766` (HTTPS).
Use `https://100.66.66.66:8766` over Tailscale or
`https://192.168.88.216:8766` on the LAN. HTTPS uses the machine's existing
self-signed certificate; the client must accept/trust that certificate.
An alternative is `ssh -N -L 8765:127.0.0.1:8765 alvin@100.66.66.66`, then
`http://localhost:8765` on the client. Plain remote HTTP cannot use Web Serial.

The artifact set was checked for valid RSA signatures, correct board markers,
partition fit and NVS preservation. All three signed apps are 2,428,928 bytes,
leaving 192,512 bytes in each app slot. The local build is `jelly-d226e71952`;
it adds the approved jellyfish to Original, Quiet Lagoon and Tidepool Club, with pulsing movement, growth, feeding and breeding. Buy it on Upgrades page 5 for 10 sand dollars. The approved Living Lagoon artwork and other creature behaviours are retained. `installer/dist/SHA256SUMS`
records the served binary hashes. These checks do not claim a physical flash
or device boot; the USB selection and flashing are performed by the user.
