# Platform-agnostic reflex layer + renderer + progression, shared verbatim with
# sim/ via ../../../common, plus this platform's implementation of the
# progression ports (NVS + wall clock). The ports live here, not in main, so
# the static-archive link order resolves (tankcore -> nvs_flash).
set(COMMON ${CMAKE_CURRENT_LIST_DIR}/../../../common)
idf_component_register(
    SRCS "${COMMON}/tank.c" "${COMMON}/advisor.c" "${COMMON}/render.c" "${COMMON}/backgrounds.c" "${COMMON}/icons.c" "${COMMON}/icons4.c" "${COMMON}/progression.c" "${COMMON}/setup.c"
         "${COMMON}/audio.c" "${COMMON}/notice.c" "${COMMON}/sounds.c" "${COMMON}/battery.c" "${COMMON}/update.c" "${COMMON}/wifi_nets.c" "${COMMON}/advanced.c" "${COMMON}/shelf.c" "${COMMON}/boot_show.c" "${COMMON}/passport.c" "${COMMON}/link.c" "${COMMON}/tank_link.c" "${COMMON}/collection.c" "${COMMON}/gift.c" "${COMMON}/sdgift.c" "${COMMON}/jelly.c" "${COMMON}/school.c" "${COMMON}/piranha_spike.c" "${COMMON}/lib_thumbs.c" "${COMMON}/lib_pics.c"
         "${COMMON}/lang.c" "${COMMON}/lang_de.c"
         "persist_port_esp.c" "rtc_port_pcf85063.c"
    INCLUDE_DIRS "${COMMON}" "."
    PRIV_REQUIRES nvs_flash esp_driver_i2c esp_partition
)
# POP_CAP (the keeper's 5-fish cap) is common/tank.h's, the same for the sim and every board: never defined here
# (tank.h stops a build that does). N_FISH_MAX stays 6, the array bound.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffast-math)
# the frame's fine split on the glass (measurement builds only: `PT_PERF=1 idf.py ...`; never a release build)
if(DEFINED ENV{PT_PERF})
    target_compile_definitions(${COMPONENT_LIB} PUBLIC PT_PERF)
endif()
# THE APP'S ROOM (0.4.2): the code that never draws a pixel or runs the model - the save, the shelf's books, the tank
# link and its gifts, the passport's data, ADVANCED - is compiled for size (~25 KB of the app slot); the renderer, the
# reflexes and the setup page keep -O2 (a frame never waits on these). Source options come after the target's -O2.
set_source_files_properties("${COMMON}/progression.c" "${COMMON}/advanced.c" "${COMMON}/shelf.c" "${COMMON}/passport.c"
    "${COMMON}/link.c" "${COMMON}/tank_link.c" "${COMMON}/collection.c" "${COMMON}/gift.c" "${COMMON}/sdgift.c"
    PROPERTIES COMPILE_OPTIONS "-Os")
# the world's shape is compiled in (common/tank.h): the bowl for the round board.
# PUBLIC: every component that sees tank.h must see the same world.
if(CONFIG_POCKET_TANK_ROUND)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC TANK_ROUND)
endif()
# the frame in the panel's byte order on every board (2026-10-23, render.h PT_FB_SWAP): the SPI DMA
# sends it straight from PSRAM on the flush task; the sim builds without it (its pixel tests)
target_compile_definitions(${COMPONENT_LIB} PUBLIC PT_FB_SWAP=1)
if(CONFIG_POCKET_TANK_WATCH)   # the watch's 410 x 502 portrait tank
    target_compile_definitions(${COMPONENT_LIB} PUBLIC TANK_WATCH)
endif()
if(CONFIG_POCKET_TANK_SQUARE)  # the square's 480 x 480 tank
    target_compile_definitions(${COMPONENT_LIB} PUBLIC TANK_SQUARE)
endif()
