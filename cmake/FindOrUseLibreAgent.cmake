# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# CONFIG is the default: the path packaging takes, so the default build, the CI
# build and the shipped build are one path. FetchContent is an opt-in
# development escape hatch for iterating on both repos at once.
option(LIBREKDE_FETCH_AGENT "Build LibreAgent from source instead of find_package" OFF)

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
    FetchContent_Declare(LibreAgent
        GIT_REPOSITORY https://github.com/LibreSCRS/LibreAgent.git
        GIT_TAG main)
    FetchContent_MakeAvailable(LibreAgent)
endif()
