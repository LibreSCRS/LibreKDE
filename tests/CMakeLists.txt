# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# Every LibreKDE test runs headless via QT_QPA_PLATFORM=offscreen
# (set per-suite through the discovery call's PROPERTIES below).

find_package(GTest REQUIRED)
include(GoogleTest)

# One ctest entry per GoogleTest case rather than one per binary. With one entry
# per binary, ctest counts binaries: `ctest -R` cannot select a case, the
# manifest check compares file names so a case deleted inside a surviving binary
# passes, and the skip ledger reads the ctest log and therefore records nothing
# for a binary whose cases all skipped.
#
# PRE_TEST, not the default POST_BUILD: POST_BUILD runs each freshly linked
# binary in the middle of a parallel build, and on a loaded runner that yields an
# empty test-list and kills the build, naming a different target each time. The
# failure reads as a regression and is not one.
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

# The D-Bus suites each get their own session bus, through the target property
# GoogleTest's discovery module reads. It reads that property only while CMP0158
# is unset or OLD; under NEW (which arrives with the first bump of
# cmake_minimum_required to 3.29 or later) the emulator stops being consulted
# without cross-compiling, the wrapper silently falls off, and the suites go to
# whatever session bus the runner is on. The guard is required because
# cmake_policy(SET CMP0158 ...) is an error on CMake 3.24 through 3.28, which
# this project still declares as its minimum. The behaviour is asserted too, by
# PrivateSessionBusTest.cpp below, because this line cannot fail.
if(POLICY CMP0158)
    cmake_policy(SET CMP0158 OLD)
endif()

find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)

# An address no bus can be listening on. ctest hands it to every bus-backed test
# in both variables below; dbus-run-session overwrites DBUS_SESSION_BUS_ADDRESS
# with the bus it starts, so the two are equal exactly when the wrapper did not
# run -- which is what PrivateSessionBusTest.cpp asserts against.
set(LIBREKDE_PRIVATE_BUS_SENTINEL "unix:path=/nonexistent/librescrs-no-private-bus")
set(LIBREKDE_PRIVATE_BUS_ENV
    "LIBRESCRS_OUTER_BUS=${LIBREKDE_PRIVATE_BUS_SENTINEL};DBUS_SESSION_BUS_ADDRESS=${LIBREKDE_PRIVATE_BUS_SENTINEL}")
set(LIBREKDE_PRIVATE_BUS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/support/PrivateSessionBusTest.cpp")

# Wraps <tgt> in its own session bus and compiles in the case that says so. The
# suite name carries the target's name because every bus-backed binary gets this
# case, and ctest entries are named after the case alone.
function(librekde_private_session_bus tgt)
    set_target_properties(${tgt} PROPERTIES
        CROSSCOMPILING_EMULATOR "${DBUS_RUN_SESSION};--")
    target_sources(${tgt} PRIVATE "${LIBREKDE_PRIVATE_BUS_SOURCE}")
    target_compile_definitions(${tgt} PRIVATE
        LIBRESCRS_PRIVATE_BUS_SUITE=${tgt}PrivateSessionBus)
endfunction()

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
