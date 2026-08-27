# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# Purpose plugin tests: the pure MIME→format map + the agent-driven SignJob
# (against the FakeAgent on a private dbus-run-session bus). All run headless
# (QT_QPA_PLATFORM=offscreen).

# Pure (no D-Bus) MIME→format + output-naming unit tests.
add_executable(MimeFormatMapTest MimeFormatMapTest.cpp)
target_link_libraries(MimeFormatMapTest PRIVATE GTest::gtest_main librekde-signing)
target_compile_features(MimeFormatMapTest PRIVATE cxx_std_23)
add_test(NAME MimeFormatMapTest COMMAND MimeFormatMapTest)
set_tests_properties(MimeFormatMapTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

# SignJob end-to-end against the FakeAgent. Reuses the shared FakeAgent +
# its custom QCoreApplication-owning TestMain, run under dbus-run-session.
#
# LibreAgent::ClientQt is named EXPLICITLY even though librekde-signing carries
# it PUBLIC: this suite includes that library's own headers, and a test's
# dependency on headers it includes itself must not rest on another target's
# public interface, which a later change there could withdraw.
add_executable(SignJobTest
    SignJobTest.cpp
)
set_target_properties(SignJobTest PROPERTIES AUTOMOC ON)
target_link_libraries(SignJobTest PRIVATE
    GTest::gtest
    librekde-agentclient
    LibreAgent::ClientQt
    librekde-test-support
    librekde-test-main
    librekde-signing
    Qt6::Core Qt6::DBus Qt6::Test
)
target_compile_features(SignJobTest PRIVATE cxx_std_23)
add_test(NAME SignJobTest COMMAND dbus-run-session -- $<TARGET_FILE:SignJobTest>)
# A wedged test under dbus-run-session must fail in minutes, not consume the
# runner's whole wall: every bus-backed test here carries an explicit timeout.
set_tests_properties(SignJobTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en" TIMEOUT 120)

# Which card signs, against the FakeAgent with one and with two signing-capable
# cards. Drives the injected chooser seam directly (a recording fake), so the
# "no prompt with fewer than two candidates" half of the rule is asserted and
# not merely implied by the returned card.
add_executable(CardSelectionTest
    CardSelectionTest.cpp
)
set_target_properties(CardSelectionTest PROPERTIES AUTOMOC ON)
target_link_libraries(CardSelectionTest PRIVATE
    GTest::gtest
    LibreAgent::ClientQt
    librekde-test-support
    librekde-test-main
    librekde-signing
    Qt6::Core Qt6::DBus Qt6::Test
)
target_compile_features(CardSelectionTest PRIVATE cxx_std_23)
add_test(NAME CardSelectionTest COMMAND dbus-run-session -- $<TARGET_FILE:CardSelectionTest>)
set_tests_properties(CardSelectionTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en" TIMEOUT 120)
