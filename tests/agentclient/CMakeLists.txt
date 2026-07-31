# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# Agent-client unit tests. The D-Bus tests run against an in-process FakeAgent
# on a PRIVATE session bus — the test executable is launched under
# dbus-run-session (see DBUS_TESTS below), mirroring the agent's own
# DBusServiceTest harness. All run headless (QT_QPA_PLATFORM=offscreen).

# Pure (no D-Bus) tests — capability grouping + error text.
add_executable(AgentCapabilitiesTest AgentCapabilitiesTest.cpp)
target_link_libraries(AgentCapabilitiesTest PRIVATE GTest::gtest_main librekde-agentclient)
target_compile_features(AgentCapabilitiesTest PRIVATE cxx_std_23)
add_test(NAME AgentCapabilitiesTest COMMAND AgentCapabilitiesTest)
set_tests_properties(AgentCapabilitiesTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

add_executable(ErrorTextTest ErrorTextTest.cpp)
target_link_libraries(ErrorTextTest PRIVATE GTest::gtest_main librekde-agentclient KF6::I18n)
target_compile_features(ErrorTextTest PRIVATE cxx_std_23)
add_test(NAME ErrorTextTest COMMAND ErrorTextTest)
set_tests_properties(ErrorTextTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

add_executable(CredentialTypesTest CredentialTypesTest.cpp)
target_link_libraries(CredentialTypesTest PRIVATE GTest::gtest_main librekde-agentclient Qt6::DBus)
target_compile_features(CredentialTypesTest PRIVATE cxx_std_23)
add_test(NAME CredentialTypesTest COMMAND CredentialTypesTest)
set_tests_properties(CredentialTypesTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

# Total-coverage guard: walks every CredentialOutcome value and demands a
# deliberate copy decision per outcome (localized message, or the neutral
# empty return for Ok/UserCancelled). Compiled with -Werror=switch so
# extending CredentialOutcome without a decision here is a compile error.
add_executable(CredentialTextCoverageTest CredentialTextCoverageTest.cpp)
target_link_libraries(CredentialTextCoverageTest PRIVATE GTest::gtest_main librekde-agentclient KF6::I18n)
target_compile_features(CredentialTextCoverageTest PRIVATE cxx_std_23)
target_compile_options(CredentialTextCoverageTest PRIVATE -Werror=switch)
add_test(NAME CredentialTextCoverageTest COMMAND CredentialTextCoverageTest)
set_tests_properties(CredentialTextCoverageTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

# Total-coverage guard for the four closed Credentials1 record-token enums
# (Kind/State/UnblockStyle/Recovery): -Werror=switch classifiers force a
# deliberate wire-token decision per enumerator, the walks round-trip every
# token through the production demarshaller and demand non-empty
# kindName()/stateName() display copy. Sibling of CredentialTextCoverageTest
# (which owns CredentialOutcome).
add_executable(CredentialVocabularyCoverageTest CredentialVocabularyCoverageTest.cpp)
target_link_libraries(CredentialVocabularyCoverageTest PRIVATE GTest::gtest_main librekde-agentclient KF6::I18n)
target_compile_features(CredentialVocabularyCoverageTest PRIVATE cxx_std_23)
target_compile_options(CredentialVocabularyCoverageTest PRIVATE -Werror=switch)
add_test(NAME CredentialVocabularyCoverageTest COMMAND CredentialVocabularyCoverageTest)
set_tests_properties(CredentialVocabularyCoverageTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

# Identity-field label resolver — proves a frozen labelKey ("field.surname")
# resolves to the real Serbian string end-to-end. The sr catalog is compiled
# into a PRIVATE locale tree and registered via addDomainLocaleDir() (no
# system-wide install), so the runtime translation is genuinely exercised.
find_program(MSGFMT_EXECUTABLE NAMES msgfmt)
if(MSGFMT_EXECUTABLE)
    # Compile the sr catalog into a PRIVATE XDG data tree laid out exactly like a
    # system install (<root>/locale/<lang>/LC_MESSAGES/<domain>.mo). The test
    # prepends <root> to XDG_DATA_DIRS, so KLocalizedString finds the catalog
    # through the standard QStandardPaths::GenericDataLocation lookup — the same
    # path that resolves system-installed catalogs, stable across ki18n versions
    # — instead of the version-fragile addDomainLocaleDir() runtime registration.
    set(_id_locale_root "${CMAKE_CURRENT_BINARY_DIR}/test-locale")
    set(_id_sr_mo "${_id_locale_root}/locale/sr/LC_MESSAGES/librekde.mo")
    add_custom_command(
        OUTPUT "${_id_sr_mo}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_id_locale_root}/locale/sr/LC_MESSAGES"
        COMMAND ${MSGFMT_EXECUTABLE} "${CMAKE_SOURCE_DIR}/po/sr/librekde.po" -o "${_id_sr_mo}"
        DEPENDS "${CMAKE_SOURCE_DIR}/po/sr/librekde.po"
        COMMENT "Compiling sr librekde catalog for IdentityLabelTest"
        VERBATIM)
    add_custom_target(IdentityLabelTest_catalog DEPENDS "${_id_sr_mo}")

    add_executable(IdentityLabelTest IdentityLabelTest.cpp)
    add_dependencies(IdentityLabelTest IdentityLabelTest_catalog)
    target_link_libraries(IdentityLabelTest PRIVATE GTest::gtest librekde-agentclient KF6::I18n)
    target_compile_features(IdentityLabelTest PRIVATE cxx_std_23)
    add_test(NAME IdentityLabelTest COMMAND IdentityLabelTest)
    # LANG must name a real UTF-8 locale: ki18n resolves catalogs through the C
    # library's gettext, which suppresses translation entirely under the C /
    # POSIX / C.UTF-8 locale. LANGUAGE=sr then selects Serbian as the message
    # language. CI must have en_US.UTF-8 generated (see the workflow's locale
    # step); a developer desktop already carries a UTF-8 locale.
    #
    # XDG_DATA_HOME is pinned to the private catalog root for the same reason
    # CredentialGuidanceLocaleTest pins it: QStandardPaths::GenericDataLocation
    # searches XDG_DATA_HOME BEFORE XDG_DATA_DIRS, so a librekde catalog
    # dogfood-installed under the developer's ~/.local/share shadows the freshly
    # compiled one — the test then asserts against whatever that older install
    # happens to say instead of against this checkout.
    set_tests_properties(IdentityLabelTest PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=sr;LANG=en_US.UTF-8"
        ENVIRONMENT_MODIFICATION "XDG_DATA_DIRS=path_list_prepend:${_id_locale_root};XDG_DATA_HOME=set:${_id_locale_root}")
else()
    message(WARNING "msgfmt not found — IdentityLabelTest (Serbian catalog resolution) is skipped")
endif()

# Total-coverage guard: walks every mirrored ErrorCode value and demands a
# deliberate ErrorText outcome per code (localized copy vs agent fallback).
# Compiled with -Werror=switch so extending the mirror enum without a decision
# there is a compile error — the cross-repo growth gate itself lives
# agent-side, pinned to the published Operation1.xml wire contract.
add_executable(ErrorTextCoverageTest ErrorTextCoverageTest.cpp)
target_link_libraries(ErrorTextCoverageTest PRIVATE GTest::gtest_main librekde-agentclient KF6::I18n)
target_compile_features(ErrorTextCoverageTest PRIVATE cxx_std_23)
target_compile_options(ErrorTextCoverageTest PRIVATE -Werror=switch)
add_test(NAME ErrorTextCoverageTest COMMAND ErrorTextCoverageTest)
set_tests_properties(ErrorTextCoverageTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

# Cross-stack wire-contract drift guard, LM-free half: pins the KDE Cap::* +
# ErrorCode mirrors to the canonical wire literals that the LM-anchored agent
# guard (LibreLinux agent/tests/WireContractGuardTest.cpp) ties to LM.
add_executable(WireContractGuardTest WireContractGuardTest.cpp)
target_link_libraries(WireContractGuardTest PRIVATE GTest::gtest_main librekde-agentclient KF6::I18n)
target_compile_features(WireContractGuardTest PRIVATE cxx_std_23)
add_test(NAME WireContractGuardTest COMMAND WireContractGuardTest)
set_tests_properties(WireContractGuardTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

# Pins the typed Result D-Bus signatures to the frozen agent XML (no bus needed).
add_executable(AgentResultSignatureTest AgentResultSignatureTest.cpp)
target_link_libraries(AgentResultSignatureTest PRIVATE GTest::gtest_main librekde-agentclient Qt6::DBus)
target_compile_features(AgentResultSignatureTest PRIVATE cxx_std_23)
add_test(NAME AgentResultSignatureTest COMMAND AgentResultSignatureTest)
set_tests_properties(AgentResultSignatureTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

# D-Bus FakeAgent tests — each runs under dbus-run-session, with a custom main
# that owns a QCoreApplication (TestMain.cpp) instead of gtest_main. The
# librekde_add_dbus_test helper, and the harness it links, live in
# tests/support; this file is its only caller.
librekde_add_dbus_test(FakeAgentWireShapeTest)
librekde_add_dbus_test(AgentOperationTest)
librekde_add_dbus_test(AgentCardTest)
librekde_add_dbus_test(AgentDiscoveryTest)
librekde_add_dbus_test(SharedAgentClientTest)
