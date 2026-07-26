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

# SignJob end-to-end against the FakeAgent. Reuses the agentclient FakeAgent +
# its custom QCoreApplication-owning TestMain, run under dbus-run-session.
set(_fake_agent_dir ${CMAKE_CURRENT_SOURCE_DIR}/../agentclient)
add_executable(SignJobTest
    SignJobTest.cpp
    ${_fake_agent_dir}/FakeAgent.cpp
    ${_fake_agent_dir}/FakeAgent.h
    ${_fake_agent_dir}/TestMain.cpp
)
set_target_properties(SignJobTest PROPERTIES AUTOMOC ON)
target_include_directories(SignJobTest PRIVATE ${_fake_agent_dir})
target_link_libraries(SignJobTest PRIVATE
    GTest::gtest
    librekde-agentclient
    librekde-signing
    Qt6::Core Qt6::DBus Qt6::Test
)
target_compile_features(SignJobTest PRIVATE cxx_std_23)
add_test(NAME SignJobTest COMMAND dbus-run-session -- $<TARGET_FILE:SignJobTest>)
set_tests_properties(SignJobTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")
