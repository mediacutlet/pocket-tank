# Aqua Pets firmware — ESP32-S3 (Waveshare ESP32-S3-Touch-AMOLED-1.8)
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# the build id: the commit's short hash (+ "-dirty"), never a tag - the
# release number lives in common/version.h and is shown beside it (ESP-IDF's
# own default is `git describe --tags`, which on the tagged release commit
# would read "v0.2.0" and say the number twice; 2026-09-29)
execute_process(COMMAND git describe --always --dirty --exclude=*
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE PT_BUILD_ID OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(PT_BUILD_ID)
    set(PROJECT_VER ${PT_BUILD_ID})
endif()
# Optional local build label keeps successive uncommitted browser builds distinct.
if(DEFINED PT_BUILD_LABEL AND NOT PT_BUILD_LABEL STREQUAL "")
    string(LENGTH "${PT_BUILD_LABEL}" PT_BUILD_LABEL_LENGTH)
    if(PT_BUILD_LABEL_LENGTH GREATER 31)
        message(FATAL_ERROR "PT_BUILD_LABEL must fit the 31-byte app version")
    endif()
    set(PROJECT_VER "${PT_BUILD_LABEL}")
endif()
# execute_process runs at configure time only: without this, a build after a
# new commit keeps the previous id (2026-09-30: the tank reported bb7751a from
# a 092a913 tree). A change of HEAD or of the branch's ref reconfigures.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_CURRENT_LIST_DIR}/../.git/HEAD ${CMAKE_CURRENT_LIST_DIR}/../.git/index)
project(aqua_pets)
# flash is nearly full (2026-10-10 night: the swordfish took the 1.8's image 4 KB past its 2.5 MB
# slot). The network stack, the crypto and the HTTP pieces only run for an update and a time sync,
# so they are built for size (-Os) while the tank keeps the performance build.
foreach(comp lwip wpa_supplicant esp_netif esp-tls esp_http_client esp_https_ota esp_http_server
             nvs_flash http_parser esp_event json protocomm wifi_provisioning mqtt esp_local_ctrl console
             esp_wifi esp_coex esp_phy app_update bootloader_support esp_partition spiffs vfs)
    idf_component_get_property(lib ${comp} COMPONENT_LIB)
    if(lib AND TARGET ${lib})
        get_target_property(kind ${lib} TYPE)
        if(NOT kind STREQUAL "INTERFACE_LIBRARY")
            target_compile_options(${lib} PRIVATE -Os)
        endif()
    endif()
endforeach()
foreach(t mbedtls mbedcrypto mbedx509 everest p256m)
    if(TARGET ${t})
        target_compile_options(${t} PRIVATE -Os)
    endif()
endforeach()
