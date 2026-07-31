# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0

set(CMAKE_AUTOMOC ON)

ecm_qt_declare_logging_category(PlasmoidLogging_SRCS
    HEADER plasmoid_log_categories.h
    IDENTIFIER LibreKDE::Plasmoid::Logging
    CATEGORY_NAME rs.librescrs.librekde.plasmoid
    EXPORT LibreKDE
    DESCRIPTION "LibreKDE Plasma 6 plasmoid"
)

# NO_GENERATE_PLUGIN_SOURCE + CLASS_NAME: we hand-write the engine-extension
# plugin (EngineInit) so initializeEngine() can register the card-photo image
# provider, rather than using the no-op plugin qt_add_qml_module would emit.
qt_add_qml_module(librekde-plasmoid
    URI org.librescrs.smartcard
    VERSION 1.0
    SOURCES
        src/SmartCardHandler.h src/SmartCardHandler.cpp
        src/CardStateModel.h src/CardStateModel.cpp
        src/CardPhotoStore.h src/CardPhotoStore.cpp
        src/CardPhotoProvider.h src/CardPhotoProvider.cpp
        ${PlasmoidLogging_SRCS}
    PLUGIN_TARGET librekde-plasmoidplugin
    CLASS_NAME LibreKDE__Plasmoid__EngineInit
    NO_GENERATE_PLUGIN_SOURCE
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/org/librescrs/smartcard
)

# The QML engine-extension plugin (separate, hidden-visibility `.so`) calls the
# backing library's installCardPhotoProvider(). Generate the proper export/import
# macro (LIBREKDE_PLASMOID_EXPORT) so the symbol is exported when building the lib
# and imported (not "defined-here"/protected) when consumed from the plugin —
# required under KDE's -fvisibility=hidden + -mno-direct-extern-access.
include(GenerateExportHeader)
generate_export_header(librekde-plasmoid
    BASE_NAME librekde_plasmoid
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/librekde_plasmoid_export.h
)

target_sources(librekde-plasmoidplugin PRIVATE
    src/EngineInit.h src/EngineInit.cpp
)
# The plugin's initializeEngine() delegates photo-provider registration to the
# backing library's installCardPhotoProvider() (which owns the CardPhotoProvider
# + the shared store lookup), so the plugin only links that library. The plugin
# itself carries NO user-visible strings and installs NO i18n context — the
# applet-package QML localizes via libplasma's per-applet KLocalizedQmlContext
# (domain plasma_applet_org.librescrs.smartcard; catalogs shipped in po/).
target_link_libraries(librekde-plasmoidplugin PRIVATE
    Qt6::Qml Qt6::Gui Qt6::Quick
    librekde-plasmoid
)
target_compile_features(librekde-plasmoidplugin PRIVATE cxx_std_23)
target_include_directories(librekde-plasmoidplugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR})

# qt_add_qml_module builds the QML plugin WITHOUT the "lib" prefix
# (librekde-plasmoidplugin.so), but at runtime the QML engine resolves the
# qmldir's `plugin librekde-plasmoidplugin` line the standard Unix way —
# lib<name>.so — so it searches for liblibrekde-plasmoidplugin.so and otherwise
# reports "plugin not found". Force the lib prefix so the built file matches.
set_target_properties(librekde-plasmoidplugin PROPERTIES PREFIX "lib")

target_include_directories(librekde-plasmoid PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Pin bare C++ i18nc() (SmartCardHandler) to the "librekde" catalog — without
# this the C++ side would resolve against plasmashell's domain. QML strings are
# NOT covered by this pin: libplasma installs a per-applet KLocalizedQmlContext
# whose domain is plasma_applet_org.librescrs.smartcard (the innermost context
# object wins unqualified lookup), so the applet-package QML localizes via the
# catalogs shipped under THAT domain in po/ — the stock Plasma applet layout.
target_compile_definitions(librekde-plasmoid PRIVATE TRANSLATION_DOMAIN="librekde")

# The plasmoid is a thin client of org.librescrs.Agent: it links the agent
# client library and NO LibreMiddleware target. Card I/O and secret entry live
# in the agent.
#
# LibreAgent::ClientQt is named here EXPLICITLY even though it would already
# arrive transitively — BOTH librekde-signing and librekde-agentclient carry it
# PUBLIC (for their own public headers' sake). This component includes the
# client's own headers directly (the card/reader/operation proxies, the
# capability tokens, the bounded-payload reader), so its dependency on them must
# not rest on either sibling's public interface, which a later change to that
# sibling could withdraw.
#
# librekde-agentclient supplies the two host-side pieces the client library has
# no counterpart for and never will: the localized error copy (ErrorText) and the
# identity-label table keyed on the frozen label keys the agent ships.
target_link_libraries(librekde-plasmoid
    PRIVATE
        Qt6::Core Qt6::Gui Qt6::DBus Qt6::Qml Qt6::Quick
        KF6::I18n KF6::CoreAddons KF6::Notifications KF6::KIOGui KF6::Service
        Plasma::Plasma
        LibreAgent::ClientQt
        librekde-agentclient
        librekde-signing
        LibreKDELogging
)
target_compile_features(librekde-plasmoid PRIVATE cxx_std_23)

plasma_install_package(${CMAKE_CURRENT_SOURCE_DIR}/package org.librescrs.smartcard)

# The backing library (SmartCardHandler + state classifier + the C++ QML types)
# is the QML plugin's link dependency, resolved at runtime via the plugin's
# RUNPATH (KDE_INSTALL_LIBDIR). This plasmoid is QML-only — metadata.json declares
# no C++ Applet — so plasmashell never dlopens it as an applet; it belongs in the
# libdir next to where the plugin looks for it, NOT under plasma/applets (where
# the plugin's RUNPATH would never find it → "plugin ... not found").
install(TARGETS librekde-plasmoid
    DESTINATION ${KDE_INSTALL_LIBDIR}
)

# qt_add_qml_module materialises two outputs: the main `librekde-plasmoid`
# library (already installed above) and the QML plugin shim
# `librekde-plasmoidplugin.so` + its `qmldir` / `*.qmltypes` metadata.
# The QML engine resolves `import org.librescrs.smartcard` by walking
# QML2_IMPORT_PATH; install the plugin shim + metadata under
# ${KDE_INSTALL_QMLDIR}/org/librescrs/smartcard/ matching that URI.
install(TARGETS librekde-plasmoidplugin
    DESTINATION ${KDE_INSTALL_QMLDIR}/org/librescrs/smartcard
)
install(FILES
    ${CMAKE_BINARY_DIR}/qml/org/librescrs/smartcard/qmldir
    ${CMAKE_BINARY_DIR}/qml/org/librescrs/smartcard/librekde-plasmoid.qmltypes
    DESTINATION ${KDE_INSTALL_QMLDIR}/org/librescrs/smartcard
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../packaging/appstream/org.librescrs.smartcard.metainfo.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR}
)
