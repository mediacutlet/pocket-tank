# Platform-agnostic reflex layer + renderer + progression, shared verbatim with
# sim/ via ../../../common, plus this platform's implementation of the
# progression ports (NVS + wall clock). The ports live here, not in main, so
# the static-archive link order resolves (tankcore -> nvs_flash).
set(COMMON ${CMAKE_CURRENT_LIST_DIR}/../../../common)
idf_component_register(
    SRCS "${COMMON}/tank.c" "${COMMON}/advisor.c" "${COMMON}/render.c" "${COMMON}/icons.c" "${COMMON}/progression.c" "${COMMON}/setup.c"
         "${COMMON}/audio.c" "${COMMON}/notice.c" "${COMMON}/sounds.c" "${COMMON}/battery.c" "${COMMON}/update.c" "${COMMON}/advanced.c"
         "${COMMON}/lang.c" "${COMMON}/lang_de.c"
         "persist_port_esp.c" "rtc_port_pcf85063.c"
    INCLUDE_DIRS "${COMMON}" "."
    PRIV_REQUIRES nvs_flash esp_driver_i2c
)
# POP_CAP: the device ships a 5-fish ceiling until advisor latency is measured on
# hardware (docs/progression-next.md); the sim shows all 6. N_FISH_MAX stays 6.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffast-math -DPOP_CAP=5)
target_compile_definitions(${COMPONENT_LIB} PUBLIC POP_CAP=5)
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
