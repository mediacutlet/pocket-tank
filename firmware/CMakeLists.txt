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
    string(REGEX REPLACE "\n(# )?CONFIG_(ESP_WIFI_SOFTAP_SUPPORT|ESP_WIFI_ENTERPRISE_SUPPORT|LWIP_IPV6|MBEDTLS_TLS_SERVER_AND_CLIENT|MBEDTLS_TLS_SERVER_ONLY|MBEDTLS_TLS_CLIENT_ONLY|MBEDTLS_TLS_SERVER|MBEDTLS_TLS_CLIENT)[= ][^\n]*" "" PT_SDK_NEW "${PT_SDK_OLD}")
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
project(pocket_tank)
