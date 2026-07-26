// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import org.kde.plasma.configuration

// Translation-domain note: libplasma's ConfigView installs a per-applet
// KLocalizedQmlContext (domain plasma_applet_org.librescrs.smartcard) on the
// config dialog's engine, exactly like the applet itself, so the i18nc below
// resolves against the applet catalog shipped in po/ (entry "General" is
// cataloged there).
ConfigModel {
    ConfigCategory {
        name: i18nc("@title:tab plasmoid config page", "General")
        // Breeze ships no smartcard-named icon; secure-card is the stock card
        // glyph (verified present).
        icon: "secure-card"
        source: "ConfigGeneral.qml"
    }
}
