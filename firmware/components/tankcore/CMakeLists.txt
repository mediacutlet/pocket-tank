# Platform-agnostic reflex layer + renderer + progression, shared verbatim with
# sim/ via ../../../common, plus this platform's implementation of the
# progression ports (NVS + wall clock). The ports live here, not in main, so
# the static-archive link order resolves (tankcore -> nvs_flash).
set(COMMON ${CMAKE_CURRENT_LIST_DIR}/../../../common)
idf_component_register(
    SRCS "${COMMON}/tank.c" "${COMMON}/advisor.c" "${COMMON}/render.c" "${COMMON}/icons.c" "${COMMON}/progression.c" "${COMMON}/setup.c"
         "${COMMON}/audio.c" "${COMMON}/notice.c" "${COMMON}/sounds.c" "${COMMON}/battery.c" "${COMMON}/update.c"
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
if(CONFIG_POCKET_TANK_WATCH)   # the watch's 410 x 502 portrait tank
    target_compile_definitions(${COMPONENT_LIB} PUBLIC TANK_WATCH)
endif()
# a 320 x 240 board (this fork). common/ is shared with the simulator and does
# not include sdkconfig.h, so the Kconfig symbols are handed on under their own
# names: POCKET_TANK_320X240, the part every 320 x 240 board shares (the sim
# defines the same one, make 320X240=1), and the board's own, which only
# common/version.h asks - to name the board (PT_BOARD). The value is
# sdkconfig.h's own 1: a macro defined twice with the same body is no error.
if(CONFIG_POCKET_TANK_320X240)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CONFIG_POCKET_TANK_320X240=1)
endif()
if(CONFIG_POCKET_TANK_CYD_320X240)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CONFIG_POCKET_TANK_CYD_320X240=1)
endif()
if(CONFIG_POCKET_TANK_WST_320X240)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CONFIG_POCKET_TANK_WST_320X240=1)
endif()
