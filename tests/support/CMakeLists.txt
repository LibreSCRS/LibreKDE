# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# Shared D-Bus test harness. Exported as TWO separate INTERFACE libraries,
# not one, because the two things it provides are needed independently:
#
#   librekde-test-support — the in-process org.librescrs.Agent double and the
#     harness that stands it up on a private bus. No main() here — safe to
#     link into a test that supplies its own entry point, as tests/plasmoid's
#     SmartCardHandlerTest does (PlasmoidTestMain.cpp).
#
#   librekde-test-main — TestMain.cpp alone, the QCoreApplication-owning
#     gtest entry point, for a test that does NOT supply its own main().
#
# A single bundled library cannot serve both shapes: pulling TestMain.cpp's
# main() into a target that already has its own would be a duplicate
# symbol at link time. Link librekde-test-support alone for a FakeAgent
# peer under a custom main; link both for a FakeAgent peer under gtest's
# usual entry point.
#
# The double itself is no longer written here. It used to be: a 2771-line copy
# of the agent client's own FakeAgent/TestBus, which that repository had in
# turn lifted from this one — two spellings of one bus surface, drifting apart
# with a green suite on each side, and 616 of those lines existed on this side
# alone. What survives of them is now in the shared double: the well-known-name
# claim, the swapped-card-path re-mint, and the Sign() call counter. This
# target is the seam that says which double a LibreKDE suite gets, so a suite
# links this name and not the agent's.
add_library(librekde-test-support INTERFACE)
target_link_libraries(librekde-test-support INTERFACE LibreAgent::ClientQtTestSupport)

add_library(librekde-test-main INTERFACE)
target_sources(librekde-test-main INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/TestMain.cpp
)
# TestMain.cpp owns a QCoreApplication and calls into gtest directly (it IS
# the entry point, so it links GTest::gtest, never GTest::gtest_main, which
# would supply a competing main() of its own). LibreAgent::ClientQt is for the
# operation-phase enum it registers there — the header is part of that library's
# installed interface and reaches this target no other way.
target_link_libraries(librekde-test-main INTERFACE Qt6::Core GTest::gtest LibreAgent::ClientQt)

# D-Bus FakeAgent test helper — used today only by tests/agentclient, but
# nothing here restricts it to that directory. Defined alongside the harness
# it wraps so the sources and include directory reach the caller by linking
# librekde-test-support / librekde-test-main rather than a bare variable,
# which would not carry from this directory into a sibling caller's scope.
# CMAKE_CURRENT_SOURCE_DIR below is resolved when the caller invokes the
# function, so it correctly names the CALLER's own directory, not this one.
function(librekde_add_dbus_test name)
    add_executable(${name} ${name}.cpp)
    set_target_properties(${name} PROPERTIES AUTOMOC ON)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE GTest::gtest librekde-agentclient librekde-test-support librekde-test-main Qt6::Core Qt6::DBus Qt6::Test)
    target_compile_features(${name} PRIVATE cxx_std_23)
    add_test(NAME ${name} COMMAND dbus-run-session -- $<TARGET_FILE:${name}>)
    # A wedged test under dbus-run-session must fail in minutes, not consume
    # the runner's whole wall.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en" TIMEOUT 120)
endfunction()
