# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# Every LibreKDE test runs headless via QT_QPA_PLATFORM=offscreen
# (set per-test via set_tests_properties ENVIRONMENT).

find_package(GTest REQUIRED)

# Source gate rather than a unit test: it fences off the two identity-render
# defects that are introduced by writing a line, not by a value flowing
# somewhere, and that therefore no assertion over the current code can catch —
# a progressive groupReady() subscription, and a detail view bound to the model
# the summary is a subset of. Run through cmake -P so it needs no interpreter
# beyond the one already building the project, and registered here so it runs
# in a plain local `ctest` as well as in CI's test step.
add_test(NAME IdentityRenderFence
         COMMAND ${CMAKE_COMMAND} -DLIBREKDE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
                 -P ${CMAKE_SOURCE_DIR}/tools/identity-render-fence.cmake)

# The catalog gate was runnable only by hand, and the one drift it exists to
# catch — a source string moving without its .po entries following — happened
# anyway. Registered beside the fence so it runs everywhere ctest does.
#
# Registered UNCONDITIONALLY, unlike the fence above which needs no interpreter.
# Wrapping the add_test() in if(Python3_Interpreter_FOUND) meant that on a host
# without an interpreter the test did not exist at all and ctest reported a
# clean run — the same silent pass this gate exists to end, and precisely the
# configuration a packager builds in (neither the CI package list nor the
# PKGBUILD declared python until this was found). A missing interpreter must
# fail the gate, not delete it. Python3 is now a declared test dependency in
# both, and when find_package cannot locate one the script is invoked through
# its own shebang so the failure is loud.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    set(_catalog_gate_command ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check-catalogs.py)
else()
    set(_catalog_gate_command ${CMAKE_SOURCE_DIR}/tools/check-catalogs.py)
endif()
add_test(NAME CatalogReconcile
         COMMAND ${_catalog_gate_command}
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_subdirectory(support)
add_subdirectory(agentclient)
if(LIBREKDE_BUILD_PLASMOID)
    add_subdirectory(plasmoid)
endif()
# tests/purpose exercises librekde-signing (SignJob, CardSelection, the
# MIME/format map), not the Purpose plugin target itself — the same library
# shared/CMakeLists.txt builds for EITHER surface that can sign. Gating these
# tests on LIBREKDE_BUILD_PURPOSE alone meant a plasmoid-only configuration
# built and shipped librekde-signing with its only test suite silently
# skipped. Match shared/CMakeLists.txt's own condition for the library.
if(LIBREKDE_BUILD_PLASMOID OR LIBREKDE_BUILD_PURPOSE)
    add_subdirectory(purpose)
endif()
if(LIBREKDE_BUILD_KIO)
    add_subdirectory(kio)
endif()
if(LIBREKDE_BUILD_CREDENTIALS)
    add_subdirectory(credentials)
endif()
