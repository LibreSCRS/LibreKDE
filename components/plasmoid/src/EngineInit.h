// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <QQmlExtensionPlugin>

/// @file
/// @brief The QML engine-extension plugin for the `org.librescrs.smartcard`
///        module. It replaces the source `qt_add_qml_module` would otherwise
///        auto-generate (selected via `CLASS_NAME` + `NO_GENERATE_PLUGIN_SOURCE`)
///        so we can override `initializeEngine` and register the module's
///        card-photo image provider.

namespace LibreKDE::Plasmoid {

/// @brief Engine-extension plugin: registers the module's QML types (the symbol
///        `qt_add_qml_module` emits) AND installs the `CardPhotoProvider` on the
///        importing engine.
///
/// Deliberately does NOT touch the engine's i18n contexts: the plasmoid runs in
/// plasmashell's shared global engine, which this module does not own, and
/// libplasma already installs a per-applet `KLocalizedQmlContext` whose domain
/// (`plasma_applet_org.librescrs.smartcard`) shadows any root-context object
/// inside the applet. The applet-package QML therefore translates via catalogs
/// shipped under that stock applet domain (see `po/`), the KDE-native shape —
/// never via a root-context mutation.
class EngineInit : public QQmlEngineExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlEngineExtensionInterface_iid)
public:
    explicit EngineInit(QObject* parent = nullptr);
    void initializeEngine(QQmlEngine* engine, const char* uri) override;
};

} // namespace LibreKDE::Plasmoid
