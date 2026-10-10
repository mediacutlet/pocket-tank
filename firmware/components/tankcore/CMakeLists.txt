# Platform-agnostic reflex layer + renderer + progression, shared verbatim with
# sim/ via ../../../common, plus this platform's implementation of the
# progression ports (NVS + wall clock). The ports live here, not in main, so
# the static-archive link order resolves (tankcore -> nvs_flash).
set(COMMON ${CMAKE_CURRENT_LIST_DIR}/../../../common)
idf_component_register(
    SRCS "${COMMON}/tank.c" "${COMMON}/advisor.c" "${COMMON}/render.c" "${COMMON}/theme.c" "${COMMON}/theme_assets.c" "${COMMON}/icons.c" "${COMMON}/jellyfish_assets.c" "${COMMON}/wreck_assets.c" "${COMMON}/frogman_assets.c" "${COMMON}/sub_assets.c" "${COMMON}/progression.c" "${COMMON}/setup.c"
         "${COMMON}/audio.c" "${COMMON}/notice.c" "${COMMON}/sounds.c" "${COMMON}/battery.c" "${COMMON}/update.c"
         "persist_port_esp.c" "rtc_port_pcf85063.c"
    INCLUDE_DIRS "${COMMON}" "."
    PRIV_REQUIRES nvs_flash esp_driver_i2c
)
# POP_CAP: the population ceiling, N_FISH_MAX = 25 on every board since
# 2026-10-07 (10 with the species on 2026-10-05; the device shipped 5 until
# then). Each creature is a turn in the advisor's queue: at ~3.7 s a decision
# on the device, ten creatures each get a fresh decision every ~37 s, a full
# tank of 25 every ~90 s (six: ~22 s); the reflex layer keeps them moving in
# between, on older decisions.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffast-math -DPOP_CAP=25)
target_compile_definitions(${COMPONENT_LIB} PUBLIC POP_CAP=25)
# the world's shape is compiled in (common/tank.h): the bowl for the round board.
# PUBLIC: every component that sees tank.h must see the same world.
if(CONFIG_AQUA_PETS_ROUND)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC TANK_ROUND)
endif()
if(CONFIG_AQUA_PETS_WATCH)   # the watch's 410 x 502 portrait tank
    target_compile_definitions(${COMPONENT_LIB} PUBLIC TANK_WATCH)
endif()
