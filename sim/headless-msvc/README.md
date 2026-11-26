# sim/headless-msvc - the `--snapshot` pages on a Windows box

The sim proper is a Mac build (`make`, SDL2, LVGL). This folder builds only what
`./fishsim --snapshot` needs - `common/` + `sim/main.c` - with the Visual Studio
C++ build tools and nothing else: SDL, the LVGL calls, pthread and the LLM
worker are link-level stubs (`stubs.c`, `stub/`), none of which the snapshot
path reaches. No window, no sound, rules brain. Needs `sim/lvgl/` cloned (headers only).

    build.bat            448x368, the 1.8" board
    build.bat 480 480    any size: tank.h's TANK_W / TANK_H take a -D override

Output: `out\WxH\*.png`, one per page (41 today). Side-by-side sheets against
another run: `python topng.py out/480x480/ppm out/480x480 out/448x368/ppm`.

`stub/compat.h` is force-included: it drops `__attribute__`, supplies
`clock_gettime`, and turns `_Static_assert` off - progression.c's save-layout
asserts fail under MSVC's struct layout (at the stock size too), and a snapshot
never loads a save. Do not use this build for anything that touches a save file.
