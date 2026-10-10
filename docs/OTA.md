# Over-the-air updates: the plan

Written 2026-09-30 from a planning session with Strato. Nothing here is
built yet. Every number that came from a measurement says so; every number
that is an estimate says so. When the work starts, the first two steps are
measurements (see "Build order"), and this file gets corrected from them.

**The goal.** A keeper opens settings, taps CHECK FOR UPDATES, and the tank
updates itself. The cable is for the first install only. The Wi-Fi radio is
on while checking or installing and off at every other moment: a tank that
is not being asked for an update never touches the network.

## Status (2026-09-30, night): the tank updated itself. Twice.

Strato's tank went 0.2.1 -> 0.3.0 over his Wi-Fi, from the glass: the wizard,
the password on the keyboard, JOIN, CHECK, UPDATE; 1,970,176 bytes in 14 s,
verified, booted from ota_1, rollback cancelled once the save loaded, the
UPDATED card. Then again hands-free (director `ota check` + `ota tap`) to the
final 0.3.0 build. What the night found and fixed, on top of the evening's
list below:

- **Taps never completed in update mode**: the release handed the page a zero
  position. The flow keeps the last position now (as the tank's port does).
- **The glass was off by a scale, not a bias.** Nine crosshairs (`ota calib`)
  showed the panel's report stretched ~14% about the top left: reported =
  1.142 x - 33, 1.138 y - 22. A tap at the bottom right read 25 px right and
  low ("aim slightly left", "fingers land low near the bezel"). The touch port
  now maps every tap through the inverse, tank and update mode alike (V2 /
  CST816 only; V1 keeps the old 10 px bias). Verified: nine crosses within a
  few pixels, "not a single mistake" on the keyboard.
- **The keyboard's function row**: MORE, DEL, SPACE at the left; CAPS, 123;
  BACK (quieter) and JOIN at the right - MORE beside BACK was unusable. BACK
  and a refused try keep the text. A refused password offers TRY AGAIN first
  (five connect attempts, growing pauses); the saved network is kept.
- **The test channel** needs build-named files and a cache-busting manifest
  name (`manifest-<build>.json`): the public site's proxy cache serves the old
  `manifest.json` (and `latest.json` is 403 there, by name).
- **Never log the typed field**: on the saved-network path it is the passphrase.
- **Untested still**: the public repo's own first run of the pipeline (the
  private rehearsal of 2026-10-03 ran it on the dev repo; Improv is tested).

## Status (2026-09-30, evening): built, verified on the board up to the network

Everything in the build order below except the live connect / check / install
exists and runs; the sim walks the whole flow (`--selftest-update`,
`--snapshot` writes every page). On Strato's tank (cable-flashed with the
new table at 16:41): update mode starts from `ota check`, scans, lists 8
networks, and BACK TO TANK hands the board back with the radio off. The
saved-network path, the manifest, the download and the signed install wait
for a network's password (typed on the glass, or `wifi set ssid=... pass=...`
on the director) and a test channel to fetch from.

Measured on the board (the numbers this file was waiting for):

| | KB |
|---|---|
| internal RAM free at the top of app_main | 240 |
| in update mode, before the radio | 192 |
| with the radio up (station started) | 119 |
| back in the tank: at the tank task's start / running | 76 / 54 |
| the signed 0.3.0 image (flash) | 1,924 of 2,560 (25% free) |

What changed against the plan while building:

- **The tank task fits again, with one trade.** The Wi-Fi stack's statics took
  ~36 KB of internal RAM and the tank task (a 12 KB internal stack) no longer
  started. Now the advisor's ~70 KB of hot buffers are taken first, the card
  cache stays internal only while that leaves a 52 KB margin (on this build it
  lands in PSRAM: ~2 ms more per frame, only while a stats card is up), and
  the tank task starts from a one-shot timer after app_main's own 16 KB stack
  is freed.
- **The model trailer sits at the END of the model partition** (0xA8F000), so
  the body never moves and the cable update still skips the 7.5 MB write. A
  tank without one hashes the body once at boot (~1 s) and writes it when the
  body is the shipped model (PT_MODEL_* in version.h).
- **Improv-serial: provisioning mode at install** (2026-10-03, update_mode.c
  `provision_mode_run`). A boot caused by a reset over USB (the installer
  page's, `ESP_RST_USB`) on a tank with NO network saved does not start the
  tank: the glass stays dark, the radio is free, the page's scan gets the
  real list and the network it sends is connected to (2 tries, inside the
  page's 45 s) before it is stored - a wrong passphrase is an error on the
  page, nothing saved. It ends when a network is stored, when the page
  never asks (8 s), when the page goes quiet (25 s: its open form rescans
  every 3 s, so quiet = skipped or closed) or at a touch; it restarts if
  the radio was used. A RUNNING tank asked for the networks (the page's
  "Connect to Wi-Fi" / "Change Wi-Fi") saves and restarts into the same
  mode (`provision_mode_request`, a word in RTC memory as update mode's),
  which answers that very scan; a refused or abandoned change keeps the
  network it had. Only update mode still answers a scan with an empty list. Tested on the pendant with the
  page and on the watch with a scripted client (never asked / wrong
  passphrase / quiet).
- **One manifest per board** (2026-10-02, docs/BOARDS.md "Releases"):
  `latest-amoled18.json`, `latest-round175c.json`, `latest-watch206.json`,
  each with a `board` field; the tank refuses another board's manifest and
  another board's image (its marker, read back at the first 4 KB).
- **The test channel** is a private folder on the project's site
  (a manifest + the signed image, from
  `tools/make_ota_manifest.py`), reached with the director's `ota url`;
  the public repo's Release assets are the keeper's channel.
- **The console works in update mode** (`wifi set`, `ota tap <x> <y>`,
  `ota page`), so the flow can be driven over serial without hands.

Still to do: the live test above; the public resync (with the
`OTA_SIGNING_KEY` secret set on the public repo BEFORE the first tagged
build, or the cable image ships unsigned and can never update); the
updates-page copy; phase two.

## Strato's decisions (2026-09-30)

| question | answer |
|---|---|
| Where the update files live | GitHub, but ONLY once he has released (the tag is the gate; see "Hosting") |
| Password entry | on the glass, the paged big-key keyboard grown to cover every WPA character; plus Improv-serial in the web installer |
| Model updates | 0.3.0 ships the app path only, with the hooks for the model in place; 0.4.0 will likely carry a new model, so the model path is phase two, not "someday" |
| Version | 0.3.0, "the last time you need the cable", with the asterisk that a future brain may want it until phase two lands |

## What the board fixes

Measured on the 2026-09-30 build (`idf.py size`, `size-files`) unless noted.

- ESP32-S3, 16 MB flash, octal PSRAM (8 MB on this module; confirm the free
  amount at boot, it has not been measured).
- Partition table today: ONE factory app slot, 2.5 MB at 0x10000; the model,
  8 MB at 0x290000 (7,557,640 bytes used, 830 KB spare tail); `storage`, 5.4 MB
  at 0xA90000, holding only the NVS rescue copy at its front (main.c
  `nvs_start`); NVS at 0x9000, which holds the keeper's tank and which
  `tools/make_installer.py` refuses to move.
- The app is 1,222,640 bytes: 530 KB of sound cues, 90 KB of icon tables,
  483 KB of code (about 116 KB of it the tank's own code in common/), the rest
  constant data.
- Wi-Fi is not linked (`sdkconfig.defaults` sets it off). Linking the station
  driver, lwIP, TLS and the HTTPS OTA client is an ESTIMATED 550 to 600 KB.
- Internal RAM: the link leaves 231 KB unused at startup; the running tank
  leaves about 23 KB (memory_budget.md). A Wi-Fi station with lwIP and one
  TLS session wants roughly 50 to 80 KB of internal, DMA-capable RAM. The
  driver cannot run beside the live tank. It can run before the tank exists.
- No secure boot, no flash encryption, no signed-app check. Correct for a kit
  people flash themselves; it means OTA needs its own signature check.

Three consequences drive the whole design:

1. **One last cable update.** Two app slots plus an otadata partition need a
   new partition table, and the table can only change over USB. So the first
   OTA-capable build is itself a USB release. It ships as 0.3.0.
2. **Update mode runs at boot, before the tank.** Not beside it.
3. **A 7.5 MB model cannot be double-buffered** in 16 MB (two app slots plus
   two model slots do not fit). A model update is an in-place write, made
   safe by staging and by a validity marker written last.

## The partition table

The model does not move, so the 0.3.0 cable update writes the bootloader,
the table, the app and a 4 KB model trailer, and skips the 7.5 MB model.
The second slot is carved from the front of `storage`, which is nearly
empty. App slots must be 0x10000 aligned.

| name | type | today | proposed | note |
|---|---|---|---|---|
| nvs | data | 0x9000, 0x6000 | unchanged | the save; make_installer enforces the address |
| phy_init | data | 0xF000, 0x1000 | unchanged | |
| ota_0 | app | (was `factory`) 0x10000, 0x280000 | same address, same size | every cable install writes here; with an empty otadata the bootloader boots ota_0 |
| model | data 0x40 | 0x290000, 0x800000 | unchanged | trailer sector at its END, see "Model updates" |
| otadata | data ota | none | 0xA90000, 0x2000 | which slot boots, rollback state |
| ota_1 | app | none | 0xAA0000, 0x280000 | the other slot (0xA92000..0xAA0000 stays a gap, or a small spare data row) |
| storage | data | 0xA90000, 0x560000 | 0xD20000, 0x2E0000 (2.9 MB) | the NVS rescue copy moves with it; it is found by name |

Both slots are 2.5 MB; an image must fit the smaller one. ota_0 is pinned
by the model behind it. If the app ever nears 2.5 MB the lever is the sound
cues (43% of the app): move them to their own data partition and the app
drops to about 700 KB. Not needed for years of shop items (see "Budget").

## Update mode: at boot, before the tank

The settings tap does not start Wi-Fi. It saves the tank, sets a magic word
in RTC memory (RTC_NOINIT, the batlog's pattern: magic plus checksum, cleared
by a true power-off so a request can never get stuck) and restarts. Cost to
the keeper: two short reboots, which the tank already lives as an absence.

At the top of `app_main`, before the LLM, audio, IMU or the tank are touched,
the firmware checks the word. If set, it clears it and runs update mode with
the display, touch and Wi-Fi only, so the driver has the whole internal heap.
Its outcomes:

- **Cancel, up to date, or any failure**: stop and de-initialize Wi-Fi, then
  fall through into the normal boot. No second restart.
- **Success**: the boot slot is switched and the board restarts. The new
  build boots normally and never turns the radio on unless asked. "The radio
  is off when it comes back up" is a property of the structure, not a cleanup
  step.

sdkconfig for the driver in this mode: Wi-Fi IRAM and RX-IRAM optimizations
OFF (they buy throughput with internal RAM; a download needs neither), small
buffer counts, lwIP and Wi-Fi allocations allowed in PSRAM, mbedTLS on
external memory. The download is a trickle by Wi-Fi standards.

## The keeper's flow

1. **Settings** gains an UPDATES row: CHECK FOR UPDATES, the running version
   (already on the page), and the saved network's name or NO NETWORK. Once a
   network is saved, a FORGET NETWORK row.
2. **Battery gate** before anything starts: below about 20% and not charging,
   the tank says to plug in first. A model update (phase two) requires the
   charger regardless.
3. **Save, restart, update mode.** A plain page: no fish are drawn, the tank
   is not running.
4. **No network saved: the wizard.** Scan and list SSIDs (never typed; hidden
   networks are not in v1), pick one, type the password, connect. Wrong
   password or no signal ends in a short message and the offer to try again
   or go back to the tank.
5. **Connected: fetch the manifest** (a few hundred bytes), compare the
   release number with the running one. Nothing new: "YOUR TANK IS UP TO
   DATE", radio off, normal boot.
6. **Something new**: "VERSION 0.4 IS READY" with the changelog's one-line
   note, the size and a rough time, and YES / NOT NOW. NOT NOW: radio off,
   normal boot.
7. **YES**: stream the image into the other slot with a progress bar, verify
   (signature, hash, size), switch the boot slot, restart.
8. **First boot of the new build**: an "UPDATED TO 0.4.0" card once, using
   the notice cards the tank already has; the app marks itself valid for
   rollback (see "Safety").

Every failure leaves the old tank untouched: wrong password, no signal,
server unreachable, interrupted download, bad signature. A short message
names it, the radio goes off, the old tank boots.

### The password keyboard

The wheel that shipped for names is lovely for six letters and miserable for
a 16-character passphrase. Of the two rejected 09-13 designs (`setup.h`,
director `kbd`), the paged one is the base: 5 x 3 keys of 66 x 74 px, a
corner key flipping A-M / N-Z, the nav on top. It lost to the wheel because
its panel hid the fish; in update mode there are no fish, so the keys may
take the whole screen.

A WPA passphrase is 8 to 63 characters from the printable ASCII set, any of
them. So the keyboard needs: two letter pages, a case toggle (shift, shown on
the keys), a digits page, and two symbol pages that cover all 32 printable
symbols plus space. The typed password shows in the clear (it is the
keeper's own desk); a counter stops at 63. The same widget serves any later
text entry.

### Credentials

One network in v1, in its own NVS namespace, never in the save (a save
migration or rescue must not carry a password). Plaintext in NVS, like every
hobby ESP device; NVS encryption is a later option and not a v1 promise.
FORGET NETWORK erases the namespace.

## Safety

- **Signed images without secure boot.** ESP-IDF's "signed app verification
  without secure boot" (its own menu: the build appends a signature block,
  `esp_ota_end` verifies on update). OTA accepts only images signed with
  Strato's key. USB flashing still accepts anything, so forks and community
  ports keep working; they just cannot push into a stock tank over the air.
  Secure boot proper burns eFuses and is wrong for a kit.
- **HTTPS with the IDF certificate bundle.** No certificate pinning: pinned
  certificates rot on a device that sits on a desk for a year. GitHub's
  download hosts redirect; the HTTP client follows redirects, and the bundle
  covers both hosts.
- **Rollback.** Bootloader app rollback ON. A fresh image boots as "pending";
  the app marks itself valid once the save has loaded and the tank has drawn
  its first frames. A crash before that boots the previous slot.
- **Rollback and the save.** The save is append-only and the loader accepts
  any OLDER length. A rollback reverses that: the older build reads a save
  the newer one wrote. Rule for 0.3.0 onward: the loader reads its own prefix
  of a LONGER save too, and a new build marks itself valid BEFORE its first
  save write.
- **Size and hash before the write** (from the manifest), the signature after.
- **Battery gate**, above.

## Hosting and the release pipeline

The public repo's `installer` workflow deploys GitHub Pages on EVERY push to
main that touches the firmware. An OTA manifest riding it would offer
whatever was last pushed. So the OTA files do not ride it.

- **A second workflow on `push: tags: v*`** builds the firmware with the
  signing key (a repository secret; fork PRs never see it, and PR builds stay
  unsigned), produces the signed image and the manifest, and attaches both
  as assets of the GitHub Release for that tag. Releases are not wiped by
  Pages deploys and have stable URLs, including a "latest" URL that skips
  pre-releases.
- **The tag is the gate**, exactly as it is for the installer today: the
  release recipe in `batch-public-releases` gains "the tag workflow ran green
  and the release is published".
- **Three locks behind the tag.** The tank offers only a HIGHER release number
  than the one it runs (`common/version.h`, PT_RELEASE_NUM); the key is used
  by the tag workflow alone; a pre-release is invisible to "latest".
- **Strato's test channel.** A release marked pre-release is reachable by its
  own URL. The director gets `ota url <manifest>` so his tank checks that
  one; an OTA is tested end to end before anyone else can see it.
- **The public site** keeps `/updates/` for humans (the changelog entry is
  written anyway, per release) and links the release. `site/` stays dev-only.

### The manifest

Small JSON, one per release, fetched before anything is downloaded.

| field | meaning |
|---|---|
| release, release_num | "0.4.0" and its number; the tank compares the number |
| build | the git build id, for the settings page and reports |
| min_from | the oldest release this one can update from (a save-format floor) |
| app: url, size, sha256 | the signed image |
| model: version, sha256, url, size | the model this app needs; url present only when it is offered over the air (phase two) |
| note | one plain line for the YES / NOT NOW page, from the changelog |
| needs_cable | true when the model changed and the running build cannot fetch it |

## Model updates (phase two, 0.4.0)

Possible, in place, made safe in three ways. The hooks (marked *) ship in
0.3.0 so that phase two needs no second cable release.

1. **Stage the whole model in PSRAM first.** Update mode has the PSRAM to
   itself. Download the full 7.5 MB into it, verify hash and signature there,
   and only then erase and write the model partition. The danger window
   shrinks from a whole download over someone's Wi-Fi to about 30 seconds of
   flash writing. A failed or interrupted download changes nothing.
2. **A validity trailer, written last.*** The model partition's LAST sector
   (0xA8F000, the body stays at offset 0 so nothing moves) holds magic, the
   model version, the byte length and the sha256. An OTA model write erases
   the trailer first, writes the body, writes the trailer last. At boot the
   loader checks it before the mmap. The 0.3.0 cable installer writes the
   trailer as a 4 KB part (make_installer gains the row). A tank without a
   trailer (a 0.3.0 cable install that somehow skipped it) hashes the body
   once, compares it with the hash of the model the build shipped with
   (`version.h` gains PT_MODEL_SHA256*), and writes the trailer itself.
3. **A missing model is plain, never masked.** Bad trailer: the tank runs with
   the advisor visibly off, a card says the brain needs restoring, and the
   next check re-offers the model. No silent fallback (the model owns the
   decisions).

Order in one update: model first, then the app, the boot slot switched only
when both verified. The one unavoidable failure is a model write that dies
after its erase; then the old app boots without a model and re-offers it.
The charger rule and the PSRAM staging make it rare.

The check page reports both when both are waiting: "VERSION 0.4, AND A NEW
BRAIN, 8 MB, ABOUT TWO MINUTES". Model-only updates become possible, which
suits the retraining loop. The loader's mmap does not change (body at 0).

## Budget

Flash, per slot, with the estimate marked:

| | KB |
|---|---|
| slot | 2,560 |
| app today (measured) | 1,222 |
| Wi-Fi, lwIP, TLS, HTTPS OTA client (ESTIMATE) | 550 to 600 |
| left for features | about 750 |

What a shop item costs (measured on the shrimp school, commit cfd5928): about
390 lines across tank, render and progression at 16 to 18 bytes per line,
so about 7 KB of code, plus a 447-byte PNG icon that becomes a 5 KB pixel
table: about 12 KB. The reef cluster is the heaviest so far, in the same
band. Ten shrimp-class items: about 120 KB. Ten items at three times the
shrimp: about 360 KB. Procedural creatures are cheap because the art is
code. Headroom is not a concern; the sound-cue lever exists if it ever is.

RAM in update mode: 231 KB of internal RAM is free at link time, before any
startup allocation; the exact free number at the top of boot is the first
measurement. PSRAM free in update mode is the second (it decides phase two's
staging).

## Improv-serial in the web installer

ESP Web Tools speaks Improv Wi-Fi over the serial port after a flash: the
page asks for a network and password while the cable is still in, and the
firmware stores them. That gives a new install its credentials with no
typing on the glass, and then the cable is done. The firmware listens for
Improv frames on the USB serial it already uses for the director; the frames
land in the same NVS store the wizard uses.

Two things to re-check when it lands: the vendored dialog edit in
`make_installer.py` (never_erase) was made against a device that does NOT
speak Improv, and ESP Web Tools changes its post-install flow for one that
does; and the installer's hosted copy
gains one sentence about Wi-Fi.

## Testing

- **Director**: `wifi set <ssid> <password>`, `wifi forget`, `ota check`,
  `ota url <manifest>` (the test channel), `ota mode` (restart into update
  mode). Strato never types a password on the glass unless he wants to.
- **Sim**: no Wi-Fi. The update pages and the keyboard render on the desktop
  against a fake manifest and a fake scan list, the pattern everything else
  uses; selftest covers the keyboard's pages and the YES / NOT NOW page.
- **On the device**: the first end-to-end run is against a pre-release.
  Every flash still goes through `tools/flash.sh` (DEVICE.md).

## Not in v1

Hidden SSIDs; WPA2-Enterprise; captive-portal networks; more than one saved
network; scheduled or automatic checks (the radio is on only when asked;
people learn of updates from the public site, the same as today); Improv over
BLE; a phone-in-the-middle access point portal (the tank has a screen; the
door stays open for screenless boards); NVS encryption.

## Build order

0. **Measure**: free internal RAM and free PSRAM at the top of `app_main`;
   link the Wi-Fi stack once to read the real flash delta. Correct this file.
1. **Partition table + installer**: the new table, the model trailer part,
   make_installer's checks, a cable install on the trial board.
2. **Update mode skeleton**: the RTC word, the boot branch, a page with a
   message and a button, the fall-through to the normal boot.
3. **Wi-Fi store + wizard + keyboard**: NVS namespace, scan list, the paged
   keyboard, connect, FORGET NETWORK; director `wifi` commands; sim pages.
4. **Check + install**: manifest fetch and compare, the YES / NOT NOW page,
   the download with progress, signed verification, rollback, the first-boot
   card, the save prefix rule.
5. **Pipeline**: the tag workflow, the signing secret, Release assets, the
   manifest generator, the release recipe line, the updates page copy ("after
   this one, updates arrive on the tank itself" with the model asterisk).
6. **Improv-serial** in the firmware and the installer page.
7. **Ship 0.3.0** by cable. From then on, the app path is over the air.
8. **Phase two (0.4.0)**: PSRAM staging, the model download, the trailer
   write, the combined check page; the model asterisk comes off the site.
