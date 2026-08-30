# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# Agent-client unit tests. The D-Bus tests run against an in-process FakeAgent
# on a PRIVATE session bus — the test executable is launched under
# dbus-run-session (see DBUS_TESTS below), mirroring the agent's own
# DBusServiceTest harness. All run headless (QT_QPA_PLATFORM=offscreen).

# Pure (no D-Bus) test — the error copy this repo owns.
add_executable(ErrorTextTest ErrorTextTest.cpp)
target_link_libraries(ErrorTextTest PRIVATE GTest::gtest_main librekde-agentclient KF6::I18n)
target_compile_features(ErrorTextTest PRIVATE cxx_std_23)
add_test(NAME ErrorTextTest COMMAND ErrorTextTest)
set_tests_properties(ErrorTextTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

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
    # The pin below covers LANG only, and LC_ALL outranks LANG in the C
    # library's category resolution — so an LC_ALL=C inherited from the
    # invoking shell still reaches this test and still suppresses the
    # translation, and the test fails with the English label. That failure is
    # the environment, not the catalog. (It is also the proof this test cannot
    # pass vacuously: strip the locale and it goes red.)
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

    # Serbian LATIN catalog — the runtime counterpart to CatalogReconcile
    # (tools/check-catalogs.py), which only proves po/sr@latin carries the
    # SAME msgids as po/sr, never what they say. Same private-XDG-tree
    # mechanism as IdentityLabelTest above, reusing its locale root so both
    # languages' compiled catalogs coexist under one XDG_DATA_DIRS entry;
    # sr@latin is a real, already-installed locale (see ki18n_install(po) at
    # the top level), so this exercises exactly what a user's desktop does.
    set(_id_latin_mo "${_id_locale_root}/locale/sr@latin/LC_MESSAGES/librekde.mo")
    add_custom_command(
        OUTPUT "${_id_latin_mo}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_id_locale_root}/locale/sr@latin/LC_MESSAGES"
        COMMAND ${MSGFMT_EXECUTABLE} "${CMAKE_SOURCE_DIR}/po/sr@latin/librekde.po" -o "${_id_latin_mo}"
        DEPENDS "${CMAKE_SOURCE_DIR}/po/sr@latin/librekde.po"
        COMMENT "Compiling sr@latin librekde catalog for LatinCatalogTest"
        VERBATIM)
    add_custom_target(LatinCatalogTest_catalog DEPENDS "${_id_latin_mo}")

    add_executable(LatinCatalogTest LatinCatalogTest.cpp)
    add_dependencies(LatinCatalogTest LatinCatalogTest_catalog)
    target_link_libraries(LatinCatalogTest PRIVATE GTest::gtest librekde-agentclient KF6::I18n)
    target_compile_features(LatinCatalogTest PRIVATE cxx_std_23)
    # The whole-catalog content scan opens the shipped .po files directly
    # (not through any build-tree copy), so it needs the checkout root.
    target_compile_definitions(LatinCatalogTest PRIVATE LIBREKDE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME LatinCatalogTest COMMAND LatinCatalogTest)
    # Same locale-resolution rationale as IdentityLabelTest: LANGUAGE selects
    # the catalog, LANG must still name a real UTF-8 locale, and XDG_DATA_HOME
    # is pinned ahead of XDG_DATA_DIRS so a dogfood-installed librekde catalog
    # cannot shadow the freshly built one.
    set_tests_properties(LatinCatalogTest PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=sr@latin;LANG=en_US.UTF-8"
        ENVIRONMENT_MODIFICATION "XDG_DATA_DIRS=path_list_prepend:${_id_locale_root};XDG_DATA_HOME=set:${_id_locale_root}")
else()
    message(WARNING "msgfmt not found — IdentityLabelTest and LatinCatalogTest (Serbian catalog resolution) are skipped")
endif()

# Pins foldSecurityCheckFields against both security-check shapes a plugin may
# ship: the joined shape (one field per check) and the structured shape
# (several check_<N>_<suffix> fields), which must fold into one readable row
# per check rather than one row per wire field.
add_executable(IdentityRowFoldingTest IdentityRowFoldingTest.cpp)
target_link_libraries(IdentityRowFoldingTest PRIVATE GTest::gtest_main librekde-agentclient KF6::I18n)
target_compile_features(IdentityRowFoldingTest PRIVATE cxx_std_23)
add_test(NAME IdentityRowFoldingTest COMMAND IdentityRowFoldingTest)
set_tests_properties(IdentityRowFoldingTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

# Total-coverage guard: walks every value of the client library's ErrorCode and
# demands a deliberate ErrorText outcome per code (localized copy vs agent
# fallback). Compiled with -Werror=switch, so rebuilding against a client
# library that appended a code fails HERE until that code gets a decision —
# the only place in this repo where an append fails the build. The cross-repo
# growth gate itself lives agent-side, pinned to the published wire contract.
add_executable(ErrorTextCoverageTest ErrorTextCoverageTest.cpp)
target_link_libraries(ErrorTextCoverageTest PRIVATE GTest::gtest_main librekde-agentclient KF6::I18n)
target_compile_features(ErrorTextCoverageTest PRIVATE cxx_std_23)
target_compile_options(ErrorTextCoverageTest PRIVATE -Werror=switch)
add_test(NAME ErrorTextCoverageTest COMMAND ErrorTextCoverageTest)
set_tests_properties(ErrorTextCoverageTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")

# D-Bus FakeAgent tests — each runs under dbus-run-session, with a custom main
# that owns a QCoreApplication (TestMain.cpp) instead of gtest_main. The
# librekde_add_dbus_test helper, and the harness it links, live in
# tests/support; this file is its only caller.
librekde_add_dbus_test(FakeAgentWireShapeTest)

# The shared card capability predicates, over a real AgentClient against the
# fake. Exercises the one definition every component links, so a defect in it
# fails HERE rather than in whichever surface happens to notice.
librekde_add_dbus_test(CardsTest)
# Named explicitly, though librekde-agentclient carries it PUBLIC: this suite
# includes the client library's own headers, and a test's dependency on headers
# it includes itself must not rest on another target's public interface.
target_link_libraries(CardsTest PRIVATE LibreAgent::ClientQt)
