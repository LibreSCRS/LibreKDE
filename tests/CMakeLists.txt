# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# Every LibreKDE test runs headless via QT_QPA_PLATFORM=offscreen
# (set per-test via set_tests_properties ENVIRONMENT).

find_package(GTest REQUIRED)

add_subdirectory(support)
add_subdirectory(agentclient)
if(LIBREKDE_BUILD_PLASMOID)
    add_subdirectory(plasmoid)
endif()
if(LIBREKDE_BUILD_PURPOSE)
    add_subdirectory(purpose)
endif()
if(LIBREKDE_BUILD_KIO)
    add_subdirectory(kio)
endif()
if(LIBREKDE_BUILD_CREDENTIALS)
    add_subdirectory(credentials)
endif()
