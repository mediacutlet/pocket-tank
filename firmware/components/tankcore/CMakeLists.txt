# Platform-agnostic reflex layer + renderer + progression, shared verbatim with
# sim/ via ../../../common, plus this platform's implementation of the
# progression ports (NVS + wall clock). The ports live here, not in main, so
# the static-archive link order resolves (tankcore -> nvs_flash).
set(COMMON ${CMAKE_CURRENT_LIST_DIR}/../../../common)
idf_component_register(
    SRCS "${COMMON}/tank.c" "${COMMON}/advisor.c" "${COMMON}/render.c" "${COMMON}/icons.c" "${COMMON}/progression.c" "${COMMON}/setup.c"
         "${COMMON}/audio.c" "${COMMON}/notice.c" "${COMMON}/sounds.c" "${COMMON}/battery.c" "${COMMON}/update.c" "${COMMON}/advanced.c" "${COMMON}/shelf.c" "${COMMON}/boot_show.c" "${COMMON}/passport.c" "${COMMON}/link.c" "${COMMON}/tank_link.c" "${COMMON}/collection.c" "${COMMON}/gift.c"
         "persist_port_esp.c" "rtc_port_pcf85063.c"
    INCLUDE_DIRS "${COMMON}" "."
    PRIV_REQUIRES nvs_flash esp_driver_i2c esp_partition
)
# POP_CAP (the keeper's 5-fish cap) is common/tank.h's, the same for the sim and every board: never defined here
# (tank.h stops a build that does). N_FISH_MAX stays 6, the array bound.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffast-math)
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
