# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# card:/ KIO worker tests. CardTree + CardRenderers are pure (no KIO, no D-Bus)
# and run against an in-memory FakeCardDataSource. CardWorkerTest drives the
# WorkerBase logic against the same fake (the worker's emit* hooks are overridden
# to capture) — the highest-value test asserts listDir/stat/mimeType issue ZERO
# card-read ops, only get() does (lazy PACE). All run headless.

# Pure core tests.
add_executable(CardTreeTest CardTreeTest.cpp)
target_link_libraries(CardTreeTest PRIVATE GTest::gtest_main librekde-kio-core librekde-agentclient)
target_include_directories(CardTreeTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(CardTreeTest PRIVATE cxx_std_23)
add_test(NAME CardTreeTest COMMAND CardTreeTest)
set_tests_properties(CardTreeTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

add_executable(CardRenderersTest CardRenderersTest.cpp)
target_link_libraries(CardRenderersTest PRIVATE GTest::gtest_main librekde-kio-core librekde-agentclient)
target_include_directories(CardRenderersTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(CardRenderersTest PRIVATE cxx_std_23)
add_test(NAME CardRenderersTest COMMAND CardRenderersTest)
set_tests_properties(CardRenderersTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

# P0 regression: AgentCardDataSource over a live FakeAgent. Drives a read
# and pulls the card mid-loop to exercise the card-removed-mid-read UAF window.
# Reuses the shared FakeAgent peer + TestMain (a QCoreApplication-owning
# main), and runs under dbus-run-session on a private bus.
#
# LibreAgent::ClientQt is named EXPLICITLY even though librekde-kio-core carries
# it PUBLIC: this suite includes that library's own headers, and a test's
# dependency on headers it includes itself must not rest on another target's
# public interface, which a later change there could withdraw.
add_executable(AgentCardDataSourceTest
    AgentCardDataSourceTest.cpp
)
set_target_properties(AgentCardDataSourceTest PROPERTIES AUTOMOC ON)
target_include_directories(AgentCardDataSourceTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(AgentCardDataSourceTest PRIVATE
    GTest::gtest
    librekde-kio-core librekde-agentclient LibreAgent::ClientQt
    librekde-test-support librekde-test-main
    Qt6::Core Qt6::DBus Qt6::Gui Qt6::Test
)
target_compile_features(AgentCardDataSourceTest PRIVATE cxx_std_23)
add_test(NAME AgentCardDataSourceTest COMMAND dbus-run-session -- $<TARGET_FILE:AgentCardDataSourceTest>)
# A wedged test under dbus-run-session must fail in minutes, not consume the
# runner's whole wall.
set_tests_properties(AgentCardDataSourceTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en" TIMEOUT 120)

# Worker-logic test: subclasses CardWorkerLogic (decoupled from the
# not-standalone-constructible KIO::WorkerBase) and runs it against the in-memory
# fake. No D-Bus, no live KIO process.
add_executable(CardWorkerTest CardWorkerTest.cpp)
target_include_directories(CardWorkerTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(CardWorkerTest PRIVATE
    GTest::gtest_main
    librekde-kio-core librekde-agentclient
    KF6::KIOCore
)
target_compile_features(CardWorkerTest PRIVATE cxx_std_23)
add_test(NAME CardWorkerTest COMMAND CardWorkerTest)
set_tests_properties(CardWorkerTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")
