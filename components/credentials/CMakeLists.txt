# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0

set(CMAKE_AUTOMOC ON)

# Backing static library (linked by the exe AND the tests, so the controller +
# model stay unit-testable without the app), mirroring the plasmoid's backing-lib
# pattern.
qt_add_library(credentials-core STATIC
    src/CredentialController.h src/CredentialController.cpp
    src/CredentialModel.h src/CredentialModel.cpp
)
# `-Wswitch` is already on (it comes with -Wall), but this build sets no blanket
# -Werror, so on its own an unhandled enumerator is a warning a busy build log
# swallows. Promoting it to an error here means a switch over a CLOSED enum in
# these two files cannot silently acquire a gap.
#
# Nothing in the two files relies on it today: they now consume the agent client
# library's credential vocabulary directly, with no enumerator-to-enumerator
# mapping left to keep in step, and the one remaining enum switch
# (`handleRefusal` over the wire's append-only refusal names) deliberately keeps a
# `default:` because that vocabulary grows. The flag stands so that the NEXT
# closed-enum switch added here fails the build instead of warning.
#
# Scoped to these two hand-written translation units rather than the whole target
# on purpose: the target also compiles moc, QML type-registration and QML-cache
# sources, none of which are ours to keep warning-clean across Qt versions.
set_source_files_properties(src/CredentialController.cpp src/CredentialModel.cpp
    PROPERTIES COMPILE_OPTIONS "-Werror=switch")
qt_add_qml_module(credentials-core
    URI org.librescrs.credentials
    VERSION 1.0
    # Embed the module under the modern default QML resource root so the engine's
    # built-in `qrc:/qt/qml` import path resolves `import org.librescrs.credentials`
    # (and its `Main` type) from the statically-linked resource. Without this the
    # module lands at `qrc:/org/...`, which is NOT on the import path, and
    # loadFromModule reports "no type named Main".
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        qml/Main.qml
        qml/Dashboard.qml
        qml/CredentialDelegate.qml
        qml/StateMessage.qml
        qml/ResultBanner.qml
        qml/UnblockConfirmSheet.qml
    OUTPUT_TARGETS credentials_core_generated
)
# A static QML module compiles its resources and its plugin initializer as
# separate object libraries, and Qt gives each one the include directories of
# the target it was made for -- including that target's AUTOMOC output
# directory, which only exists once that target's moc step has run. Nothing
# orders the two, so on a fresh tree these objects can compile first and
# GCC's -Wmissing-include-dirs (on through KDECompilerSettings) reports a
# directory that is not there yet. Order each one after the moc step whose
# directory it was handed.
foreach(generated IN LISTS credentials_core_generated)
    string(REGEX REPLACE "(_resources_[0-9]+|_init)$" "" owner "${generated}")
    add_dependencies(${generated} ${owner}_autogen)
endforeach()
target_compile_definitions(credentials-core PUBLIC
    TRANSLATION_DOMAIN="librekde" LIBREKDE_VERSION_STRING="${PROJECT_VERSION}")
# LibreAgent::ClientQt is named here EXPLICITLY even though librekde-agentclient
# already carries it PUBLIC: this component includes the client's own headers
# directly (the card/reader/operation proxies, the credential value types, the
# capability tokens, the mutation verb enum), so its dependency on them must not
# rest on a sibling target's public interface, which a later change to that
# sibling could withdraw. It is PUBLIC here in turn because the component's own
# public headers name those types (the controller's client/card members, the
# model's record type).
#
# librekde-agentclient supplies the two host-side pieces the client library has
# no counterpart for and never will: the localized credential copy
# (CredentialText) and the localized transport-failure copy (ErrorText). Both are
# keyed on the client library's own enums, so nothing is translated twice.
target_link_libraries(credentials-core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::DBus
    KF6::Kirigami KF6::I18n KF6::CoreAddons KF6::DBusAddons
    LibreAgent::ClientQt
    librekde-agentclient)
target_compile_features(credentials-core PUBLIC cxx_std_23)
target_include_directories(credentials-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The window process. QApplication (not QGuiApplication) is required: the
# org.kde.desktop QtQuick Controls style is QtWidgets-backed, so the exe links
# Qt6::Widgets.
qt_add_executable(librescrs-credentials-kde src/main.cpp)
target_link_libraries(librescrs-credentials-kde PRIVATE
    credentials-core credentials-coreplugin Qt6::Widgets)
target_compile_features(librescrs-credentials-kde PRIVATE cxx_std_23)
install(TARGETS librescrs-credentials-kde ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES org.librescrs.credentials.desktop DESTINATION ${KDE_INSTALL_APPDIR})
# The window is an executable on PATH, so it carries a manual page.
install(FILES doc/librescrs-credentials-kde.1 DESTINATION ${KDE_INSTALL_MANDIR}/man1)
