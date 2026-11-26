# 0.4.2 boards, frozen (the 0.4.3 upgrade)

Whole sim boards written by the **0.4.2 build itself** (main 4dae3468, the source of public v0.4.2), in the rectangle's
world, on 2026-11-22. `./fishsim --selftest-upgrade-042` (run from `sim/`, in `make check`) boots each one on today's
build and checks it against 0.4.2's own read of it. Never edit or delete a file here: a board that stops loading is a
bug in the code, not in the fixture.

Each directory is everything a sim board keeps: `tank.sav` (and `tank.sav.visitors`), `storage.bin.gz` (the shelf's
storage: every tank on it and its save; gzipped, the test unpacks a copy), `frame/` (the NVS records), and what the
writer left beside it:

- `want.txt`: 0.4.2's read of the board after a boot: the tank on the glass, then each tank on the shelf by SWAP IN,
  then back (`sim/upgrade042_dump.inc`: the same text the test writes from its own read). The first line is the
  writer's clock; the test sets the sim clock there, so no tank lives the days since.
- `sav-<shelf id>.bin`: each tank's save as 0.4.2 wrote it after that boot (2200 bytes).

| board | what is on it |
| --- | --- |
| `plain` | a new tank: the two starter fish named, an hour lived |
| `full` | five named fish, every item 0.4.2 sold (the pumpkin was out of the shop), the critters named, the castle LOCKED, the coral SHELVED, two backgrounds, SELECTIVE BREEDING with a pair set, a day lived, one fish NEXT DOOR |
| `jelly` | a crew board: the fish tank on the glass (plant and cluster shelved, volcano locked, the crab named, 14 visitors collected: a page and the book), a SUNSET jelly tank and a PIRANHA tank on the shelf |
| `jellyon` | the same board, the jelly tank on the glass |

To make them again: in a checkout of 0.4.2, copy `write042.c` and `../../upgrade042_dump.inc` into `sim/`, add
`APP_SRCS += write042.c` to the Makefile and
`if (strcmp(argv[a], "--write-042") == 0 && a + 1 < argc) { extern int write_042(const char *); return write_042(argv[a + 1]); }`
to main.c's options, build, and run `./fishsim --write-042 <dir>` with HOME and TMPDIR pointed at a scratch folder; then
`gzip -9` each `storage.bin`. New fixtures go in a new directory (`upgrade-043/`, ...), never over these.
