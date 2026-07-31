# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# CredentialController driven by the FakeAgent over a private session bus (reuses
# the shared D-Bus test harness + its QCoreApplication TestMain). Runs under
# dbus-run-session, headless (QT_QPA_PLATFORM=offscreen). The test links the
# credentials-core backing library directly (no app / QML engine needed).

set(CMAKE_AUTOMOC ON)

# CredentialModel role coverage — pure (no D-Bus, no QML engine). Reuses the
# shared TestMain (a QCoreApplication + gtest entry) so ki18n has an app to
# resolve the locale; runs headless (no dbus-run-session needed).
add_executable(CredentialModelTest
    CredentialModelTest.cpp
)
target_link_libraries(CredentialModelTest PRIVATE
    GTest::gtest credentials-core librekde-agentclient librekde-test-main
    Qt6::Core Qt6::Gui Qt6::Test KF6::I18n
)
target_compile_features(CredentialModelTest PRIVATE cxx_std_23)
add_test(NAME CredentialModelTest COMMAND CredentialModelTest)
set_tests_properties(CredentialModelTest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en"
)

add_executable(CredentialControllerTest
    CredentialControllerTest.cpp
)
target_link_libraries(CredentialControllerTest PRIVATE
    GTest::gtest credentials-core librekde-agentclient librekde-test-support librekde-test-main
    Qt6::Core Qt6::DBus Qt6::Test
)
target_compile_features(CredentialControllerTest PRIVATE cxx_std_23)
add_test(NAME CredentialControllerTest COMMAND dbus-run-session -- $<TARGET_FILE:CredentialControllerTest>)
set_tests_properties(CredentialControllerTest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en"
)

# Capstone E2E — the full credential-lifecycle flow (list → manage → result →
# mandatory re-list) over the real private session bus, asserting the agent-side
# op SEQUENCING, not just the controller's final state. Same harness shape as
# CredentialControllerTest: FakeAgent + TestMain, under dbus-run-session, headless.
add_executable(CredentialsE2ETest
    CredentialsE2ETest.cpp
)
target_link_libraries(CredentialsE2ETest PRIVATE
    GTest::gtest credentials-core librekde-agentclient librekde-test-support librekde-test-main
    Qt6::Core Qt6::DBus Qt6::Test
)
target_compile_features(CredentialsE2ETest PRIVATE cxx_std_23)
add_test(NAME CredentialsE2E COMMAND dbus-run-session -- $<TARGET_FILE:CredentialsE2ETest>)
set_tests_properties(CredentialsE2E PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en"
)

# Credential guidance/state resolver — proves the two frozen guidance keys the
# agent emits (librescrs.pin.blocked.issuer / .keyActivation.issuer) and a state
# label resolve to the real Serbian strings end-to-end, and that an unknown key
# degrades to the agent's English fallback. Mirrors the agentclient
# IdentityLabelTest: the sr catalog is compiled into a PRIVATE XDG data tree
# (never a system install) and exposed via XDG_DATA_DIRS, so KLocalizedString
# resolves it through the standard GenericDataLocation lookup — stable across
# ki18n versions, no version-fragile addDomainLocaleDir() registration.
find_program(MSGFMT_EXECUTABLE NAMES msgfmt)
if(MSGFMT_EXECUTABLE)
    set(_cred_locale_root "${CMAKE_CURRENT_BINARY_DIR}/test-locale")
    set(_cred_sr_mo "${_cred_locale_root}/locale/sr/LC_MESSAGES/librekde.mo")
    add_custom_command(
        OUTPUT "${_cred_sr_mo}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_cred_locale_root}/locale/sr/LC_MESSAGES"
        COMMAND ${MSGFMT_EXECUTABLE} "${CMAKE_SOURCE_DIR}/po/sr/librekde.po" -o "${_cred_sr_mo}"
        DEPENDS "${CMAKE_SOURCE_DIR}/po/sr/librekde.po"
        COMMENT "Compiling sr librekde catalog for CredentialGuidanceLocaleTest"
        VERBATIM)
    add_custom_target(CredentialGuidanceLocaleTest_catalog DEPENDS "${_cred_sr_mo}")

    add_executable(CredentialGuidanceLocaleTest CredentialGuidanceLocaleTest.cpp)
    add_dependencies(CredentialGuidanceLocaleTest CredentialGuidanceLocaleTest_catalog)
    target_link_libraries(CredentialGuidanceLocaleTest PRIVATE
        GTest::gtest librekde-agentclient KF6::I18n Qt6::Core
    )
    target_compile_features(CredentialGuidanceLocaleTest PRIVATE cxx_std_23)
    add_test(NAME CredentialGuidanceLocaleTest COMMAND CredentialGuidanceLocaleTest)
    # LANG must name a real UTF-8 locale: ki18n reads .mo catalogs through the C
    # library's gettext, which suppresses translation under C / POSIX / C.UTF-8.
    # LANGUAGE=sr then selects Serbian. CI generates en_US.UTF-8 (see workflow).
    #
    # XDG_DATA_HOME is pinned to the private catalog root so the test is
    # hermetic: QStandardPaths::GenericDataLocation searches XDG_DATA_HOME BEFORE
    # XDG_DATA_DIRS, so a librekde catalog dogfood-installed under the developer's
    # ~/.local/share (an older revision, missing these msgids) would otherwise
    # shadow the freshly compiled one and mask a real regression. Redirecting
    # XDG_DATA_HOME here makes the newly built catalog the highest-priority hit;
    # KF6 / system catalogs still resolve through the untouched system
    # XDG_DATA_DIRS.
    set_tests_properties(CredentialGuidanceLocaleTest PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=sr;LANG=en_US.UTF-8"
        ENVIRONMENT_MODIFICATION "XDG_DATA_DIRS=path_list_prepend:${_cred_locale_root};XDG_DATA_HOME=set:${_cred_locale_root}")
else()
    message(WARNING "msgfmt not found — CredentialGuidanceLocaleTest (Serbian catalog resolution) is skipped")
endif()
