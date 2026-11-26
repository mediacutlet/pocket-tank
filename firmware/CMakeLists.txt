# pocket-tank firmware — ESP32-S3 (Waveshare ESP32-S3-Touch-AMOLED-1.8)
cmake_minimum_required(VERSION 3.16)
# THE APP'S ROOM (0.4.2): sdkconfig.defaults only fills keys an sdkconfig lacks, so a build folder (or the checkout's
# own sdkconfig, tools/flash.sh) made before 0.4.2 would keep the Wi-Fi/TLS parts the app left out (11-08 review).
# Their lines are taken out of an existing sdkconfig at every configure, and the defaults put them back as 0.4.2 has them.
if(DEFINED SDKCONFIG)
    set(PT_SDKCONFIG ${SDKCONFIG})
else()
    set(PT_SDKCONFIG ${CMAKE_CURRENT_LIST_DIR}/sdkconfig)
endif()
if(EXISTS ${PT_SDKCONFIG})
    file(READ ${PT_SDKCONFIG} PT_SDK_OLD)
    string(REGEX REPLACE "\n(# )?CONFIG_(ESP_WIFI_SOFTAP_SUPPORT|ESP_WIFI_ENTERPRISE_SUPPORT|LWIP_IPV6|MBEDTLS_TLS_SERVER_AND_CLIENT|MBEDTLS_TLS_SERVER_ONLY|MBEDTLS_TLS_CLIENT_ONLY|MBEDTLS_TLS_SERVER|MBEDTLS_TLS_CLIENT|ESP_HOSTED_HOST_LOG_LEVEL_[A-Z]+|ESP_HOSTED_HOST_FEAT_WIFI_EXT_ITWT|ESP_HOSTED_DFLT_TASK_FROM_SPIRAM|ESP_HOSTED_HOST_TRANSPORT_RESTART_ON_FAILURE)[= ][^\n]*" "" PT_SDK_NEW "${PT_SDK_OLD}")
    # (0.4.3, 11-24) the radio image's size cuts in sdkconfig.p4radio (silent asserts, no error names): an older RADIO build
    # folder's own values are taken out so the fragment's win. Only there - every other image's sdkconfig is left as it is.
    if("$ENV{PT_P4_RADIO}" STREQUAL "1")
        string(REGEX REPLACE "\n(# )?CONFIG_((COMPILER_)?OPTIMIZATION_ASSERTIONS_[A-Z]+|ESP_ERR_TO_NAME_LOOKUP)[= ][^\n]*" "" PT_SDK_NEW "${PT_SDK_NEW}")
    endif()
    if(NOT PT_SDK_NEW STREQUAL PT_SDK_OLD)
        file(WRITE ${PT_SDKCONFIG} "${PT_SDK_NEW}")
        message(STATUS "pocket-tank: the 0.4.2 Wi-Fi/TLS trims applied to ${PT_SDKCONFIG}")
    endif()
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# the build id: the commit's short hash (+ "-dirty"), never a tag - the
# release number lives in common/version.h and is shown beside it (ESP-IDF's
# own default is `git describe --tags`, which on the tagged release commit
# would read "v0.2.0" and say the number twice; 2026-09-29)
execute_process(COMMAND git describe --always --dirty --exclude=*
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE PT_BUILD_ID OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(PT_EARLY_ID)                     # tools/crew_release.py: "<commit>-early<n>" (the build's own commit, clean)
    set(PROJECT_VER ${PT_EARLY_ID})
elseif(PT_BUILD_ID)
    set(PROJECT_VER ${PT_BUILD_ID})
endif()
# execute_process runs at configure time only: without this, a build after a
# new commit keeps the previous id (2026-09-30: the tank reported bb7751a from
# a 092a913 tree). A change of HEAD or of the branch's ref reconfigures.
# In a worktree (~/.cache/pocket-tank/wt-*) .git is a FILE pointing into the
# main repo's .git/worktrees/<name>, so ../.git/HEAD does not exist and the id
# never refreshed (2026-10-04: a d326460 build reported 8c15fa2): ask git where
# this checkout's own HEAD and index live.
execute_process(COMMAND git rev-parse --absolute-git-dir
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE PT_GIT_DIR OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT PT_GIT_DIR)
    set(PT_GIT_DIR ${CMAKE_CURRENT_LIST_DIR}/../.git)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PT_GIT_DIR}/HEAD ${PT_GIT_DIR}/index)
# the home tank WITH its radio (tools/flash_p4.sh --radio): the hosted stack's components are
# declared by firmware/radio/pt_radio_deps and join the build only then - every other image,
# the plain P4's too, is built without them and is the image it was
if("$ENV{PT_P4_RADIO}" STREQUAL "1")
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/radio")
endif()
project(pocket_tank)
# THE RADIO'S ROOM (0.4.3, Strato 11-23: the big screens ship with the tank link): the hosted stack and the Wi-Fi it
# rides on are compiled for size in the radio image only - the tank's own code keeps -O2 (CONFIG_COMPILER_OPTIMIZATION_PERF),
# and every other image, the plain P4's too, is built as it was. Without this the radio image was 0x281000 B, 4 KB past
# the 2.5 MB app slot (f3edd9db); the partition table stays, so an update can still deliver it.
if("$ENV{PT_P4_RADIO}" STREQUAL "1")
    idf_build_get_property(pt_comps BUILD_COMPONENTS)
    foreach(pt_c espressif__esp_hosted espressif__esp_wifi_remote espressif__eppp_link espressif__wifi_remote_over_eppp
                 espressif__esp_serial_slave_link esp_wifi wpa_supplicant lwip esp_netif)
        if(pt_c IN_LIST pt_comps)
            idf_component_get_property(pt_lib ${pt_c} COMPONENT_LIB)
            get_target_property(pt_type ${pt_lib} TYPE)
            if(NOT pt_type STREQUAL "INTERFACE_LIBRARY")
                target_compile_options(${pt_lib} PRIVATE -Os)
                if(pt_c MATCHES "^espressif__")   # the hosted stack's own INFO / DEBUG lines are compiled out (their strings were 60 KB):
                    target_compile_definitions(${pt_lib} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_WARN)   # its warnings and errors still log
                endif()
            endif()
        endif()
    endforeach()
    # ... and the hosted stack's own libraries (eh_*: built in their own directories, not as IDF components)
    function(pt_radio_targets dir out)
        get_property(t DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
        get_property(subs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
        foreach(s ${subs})
            pt_radio_targets(${s} st)
            list(APPEND t ${st})
        endforeach()
        set(${out} ${t} PARENT_SCOPE)
    endfunction()
    pt_radio_targets(${CMAKE_CURRENT_LIST_DIR} pt_all)
    foreach(pt_t ${pt_all})
        get_target_property(pt_type ${pt_t} TYPE)
        # the hosted stack's libraries (eh_*, its message codec), and what only it and the updates use, none on the frame's
        # path: the SDIO host, the TLS library (the update's signature check), NVS, the event loop
        if(pt_t MATCHES "^(eh_|msg_codec$|esp_hosted_compat$|esp_wifi_remote_glue$|mbedcrypto$|mbedx509$|mbedtls$|__idf_(sdmmc|esp_driver_sdmmc|nvs_flash|esp_event)$)" AND (pt_type STREQUAL "STATIC_LIBRARY" OR pt_type STREQUAL "OBJECT_LIBRARY"))
            target_compile_options(${pt_t} PRIVATE -Os)
            if(pt_t MATCHES "^(eh_|msg_codec$|esp_hosted_compat$|esp_wifi_remote_glue$)")
                target_compile_definitions(${pt_t} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_WARN)
            endif()
            list(APPEND pt_done ${pt_t})
        endif()
    endforeach()
    message(STATUS "pocket-tank: the radio's libraries built for size: ${pt_done}")
    # (0.4.3, 11-24, Strato approved) ESP-IDF's own INFO / DEBUG lines are compiled out of the radio image too (~6 KB of
    # strings: the boot's chatter); their warnings and errors still log. The tank's own components keep every line - main,
    # tankcore and llm: our tags, `state`, `radio report`, all the help page's tank check reads - and log itself is left be.
    foreach(pt_c ${pt_comps})
        if(NOT pt_c MATCHES "^(main|tankcore|llm|log)$")
            idf_component_get_property(pt_lib ${pt_c} COMPONENT_LIB)
            get_target_property(pt_type ${pt_lib} TYPE)
            if(NOT pt_type STREQUAL "INTERFACE_LIBRARY")
                target_compile_definitions(${pt_lib} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_WARN)
            endif()
        endif()
    endforeach()
endif()
