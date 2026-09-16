# Platform-agnostic reflex layer + renderer + progression, shared verbatim with
# sim/ via ../../../common, plus this platform's implementation of the
# progression ports (NVS + wall clock). The ports live here, not in main, so
# the static-archive link order resolves (tankcore -> nvs_flash).
set(COMMON ${CMAKE_CURRENT_LIST_DIR}/../../../common)
set(TC_SRCS "${COMMON}/tank.c" "${COMMON}/advisor.c" "${COMMON}/render.c" "${COMMON}/icons.c" "${COMMON}/progression.c" "${COMMON}/setup.c"
         "${COMMON}/audio.c" "${COMMON}/notice.c" "${COMMON}/sounds.c"
         "persist_port_esp.c")
# The 4B has no external RTC: main/p4/rtc_port_chip.c defines rtc_port_init()
# and clock_port_now_unix() (chip time) there, so the PCF85063 port is left
# out of the 4B build to keep the symbols from duplicating at link.
if(CONFIG_POCKET_TANK_BOARD_4B)
    message(STATUS "tankcore: 4B board - excluding rtc_port_pcf85063.c (chip clock in main)")
else()
    list(APPEND TC_SRCS "rtc_port_pcf85063.c")
endif()
idf_component_register(
    SRCS ${TC_SRCS}
    INCLUDE_DIRS "${COMMON}" "."
    PRIV_REQUIRES nvs_flash esp_driver_i2c
)
# POP_CAP: the device ships a 5-fish ceiling until advisor latency is measured on
# hardware (docs/progression-next.md); the sim shows all 6. N_FISH_MAX stays 6.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffast-math -DPOP_CAP=5)
target_compile_definitions(${COMPONENT_LIB} PUBLIC POP_CAP=5)
