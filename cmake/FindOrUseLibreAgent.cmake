# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# CONFIG is the default: the path packaging takes, so the default build, the CI
# build and the shipped build are one path. FetchContent is an opt-in
# development escape hatch for iterating on both repos at once.
option(LIBREKDE_FETCH_AGENT "Build LibreAgent from source instead of find_package" OFF)

# The fetch branch below takes a fixed 40-hex revision from cmake/libreagent.pin,
# never a branch name. This path is opt-in (CONFIG is the default), so the blast
# radius is smaller than a repo that fetches by default -- but a moving branch
# still means two developers running the same escape hatch on different days
# build against different agents, which is exactly the confusion the hatch
# exists to avoid.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libreagent.pin" LIBREAGENT_PIN)
string(STRIP "${LIBREAGENT_PIN}" LIBREAGENT_PIN)
# Exactly 40 lowercase hex characters. Spelled as a length test plus a
# character-class test because CMake's regex engine has no {n} repetition
# operator -- "^[0-9a-f]{40}$" would silently never match.
string(LENGTH "${LIBREAGENT_PIN}" LIBREAGENT_PIN_LENGTH)
if(NOT LIBREAGENT_PIN_LENGTH EQUAL 40 OR NOT LIBREAGENT_PIN MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "cmake/libreagent.pin must hold one 40-hex commit SHA")
endif()

if(NOT LIBREKDE_FETCH_AGENT)
    find_package(LibreAgent 4.2 REQUIRED CONFIG COMPONENTS ClientQt)
else()
    include(FetchContent)
    # All three are mandatory: CLIENT_QT defaults OFF, and CORE defaults ON and
    # unconditionally requires LibreMiddleware, which this repo must never pull in.
    # LibreAgent's own cmake_minimum_required() is 3.28; this repo's floor stays
    # 3.24 (unaffected on the default CONFIG path above), so this branch will
    # fail to configure under CMake 3.24-3.27 -- a limitation of the fetched
    # project, not of this file.
    set(LIBREAGENT_BUILD_CLIENT_QT ON  CACHE BOOL "" FORCE)
    set(LIBREAGENT_BUILD_CORE      OFF CACHE BOOL "" FORCE)
    set(LIBREAGENT_BUILD_WIRE      ON  CACHE BOOL "" FORCE)
    message(STATUS "LibreAgent: building from source (FetchContent, pin ${LIBREAGENT_PIN})")
    FetchContent_Declare(LibreAgent
        GIT_REPOSITORY https://github.com/LibreSCRS/LibreAgent.git
        GIT_TAG ${LIBREAGENT_PIN})
    FetchContent_MakeAvailable(LibreAgent)
endif()
