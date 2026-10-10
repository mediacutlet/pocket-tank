# Golden saves

Real tank saves, frozen. `./fishsim --selftest-saves` (run from `sim/`) loads
every `*.sav` here, whole and cut to every older build's length, and checks
the tank that comes back against the file's own bytes (fish, names, badges,
sand dollars, unlocks, decorations). It is the test behind the installer's
promise: updating never loses your tank.

**Before every release, run the tank a bit and add its save here as
`<date>-<size>.sav`.** Never edit or delete a file here: a save that stops
loading is a bug in the code, not in the fixture.

- From the sim (the same bytes the board writes): play a while, quit, then
  `cp ~/.cache/aqua-pets/tank.sav sim/testdata/saves/$(date +%F)-$(wc -c < ~/.cache/aqua-pets/tank.sav | tr -d ' ').sav`
- From the board: run `tools/preflight.py` first (reading the flash resets the
  chip), then `python -m esptool --chip esp32s3 read_flash 0x9000 0x6000 nvs.bin`
  and pull out NVS `tank`/`save` with ESP-IDF's parser:

      import sys; sys.path.insert(0, "<esp-idf>/components/nvs_flash/nvs_partition_tool")
      from nvs_parser import NVS_Partition
      p = NVS_Partition("nvs", bytearray(open("nvs.bin", "rb").read()))
      chunks = {e.metadata["chunk_index"]: b"".join(c.raw for c in e.children)[:e.data["size"]]
                for pg in p.pages for e in pg.entries
                if e.state == "Written" and e.key == "save" and e.metadata["type"] == "blob_data"}
      open("save.sav", "wb").write(b"".join(chunks[k] for k in sorted(chunks)))

What is here:

| file | from |
| --- | --- |
| `2026-09-04-1304-device.sav` | the board's NVS dump of 2026-09-04 (4 fish, per-frond heights, before names) |
| `2026-09-13-1432-prebubble.sav` | the first public installer's layout: seen masks at 1404, no bubble_x (built from the 09-29 save) |
| `2026-09-24-1656-sim.sav` | Strato's sim tank as played on 2026-09-24 |
| `2026-09-29-1656.sav` | a synthetic tank with every tail set: 5 fish, names, a custom color, parents and a welcome still owed, every shop item placed, a colored coral, a cluster look, 271 sand dollars |
| `2026-09-29-1664-shrimp.sav` | the 1656 tank above, loaded and saved by the shrimp build: plus a school of 6 shrimp, 7 pellets toward the next, 321 s of cooldown |
| `2026-09-29-1672-v0.2.0.sav` | the release save for **v0.2.0**: the shrimp tank above loaded and saved by the 0.2.0 build (release stamp 0x000200 at 1664) |
| `2026-10-02-1688-urchin.sav` | the v0.2.0 save above grown to 1688 (built by hand from it: the watch's SCREEN byte 0, then the urchin tail): the urchin bought (unlock bit 6), at x 212, 4321.5 px of grass eaten |
| `2026-10-05-2096-species.sav` | the species' ten (built by today's progression.c from the urchin save: one of its five fish sold, a pair of crabs and of lobsters bought, two lobster fry born, a crab renamed and re-designed): 4 fish, 2 crabs, 4 lobsters - the core says 6 (what an older build loads, as six classic fish), fish 7..10 in the tail from 1692; the tenth still owed its welcome |
