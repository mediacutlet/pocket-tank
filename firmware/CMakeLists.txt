# pocket-tank firmware — ESP32-S3 (Waveshare ESP32-S3-Touch-AMOLED-1.8)
cmake_minimum_required(VERSION 3.16)
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
