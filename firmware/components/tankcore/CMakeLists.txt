# Platform-agnostic reflex layer + renderer + progression, shared verbatim with
# sim/ via ../../../common, plus this platform's implementation of the
# progression ports (NVS + wall clock). The ports live here, not in main, so
# the static-archive link order resolves (tankcore -> nvs_flash).
set(COMMON ${CMAKE_CURRENT_LIST_DIR}/../../../common)
# The wall-clock port is the one place a board's *chip* differs here: the four
# Waveshare boards carry a PCF85063 (I2C 0x51), the Tab5 a Seiko RX8130CE (0x32,
# a different register map). Chosen by target, like the display/net ports in
# main/CMakeLists.txt.
if(CONFIG_IDF_TARGET_ESP32P4)
    set(RTC_SRC "rtc_port_rx8130.c")
else()
    set(RTC_SRC "rtc_port_pcf85063.c")
endif()
idf_component_register(
    SRCS "${COMMON}/tank.c" "${COMMON}/advisor.c" "${COMMON}/render.c" "${COMMON}/icons.c" "${COMMON}/progression.c" "${COMMON}/setup.c"
         "${COMMON}/audio.c" "${COMMON}/notice.c" "${COMMON}/sounds.c" "${COMMON}/battery.c" "${COMMON}/update.c"
         "persist_port_esp.c" ${RTC_SRC}
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
if(CONFIG_POCKET_TANK_SQUARE)  # the square's 480 x 480 tank
    target_compile_definitions(${COMPONENT_LIB} PUBLIC TANK_SQUARE)
endif()
if(CONFIG_POCKET_TANK_TAB5)    # the Tab5's 720 x 405 landscape tank (ESP32-P4, 720 x 1280 DSI glass)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC TANK_TAB5)
endif()
