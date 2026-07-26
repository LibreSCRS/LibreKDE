// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.plasma.plasmoid
import org.librescrs.smartcard

Kirigami.Icon {
    id: trayIcon

    // This widget's own controller instance (per-widget state; see main.qml).
    required property SmartCard smartCard

    // System-tray icon. Different glyph variant per state for at-a-glance
    // status. Breeze ships NO smartcard-named icon, so the state set uses the
    // auth-sim status family (a chip card, with real locked/missing variants —
    // exactly the tray semantics needed); every name below is verified present
    // in breeze-icons (devices/status/actions, incl. -symbolic variants).
    source: {
        switch (smartCard.state) {
        case 0: return "auth-sim-missing";           // NoCard
        case 1: return "auth-sim-locked";            // PreAuthRequired
        case 2: case 3: case 4: return "auth-sim";   // identity / pki / hybrid
        case 6: return "dialog-question";            // UnknownCard
        case 7: return "dialog-warning";             // AgentUnavailable
        default: return "data-error";                // Error (5)
        }
    }

    // Localized, state-aware label for screen readers — the SAME line the
    // panel hover tooltip shows (owned by main.qml, which also feeds it to
    // PlasmoidItem.toolTipSubText).
    required property string statusText

    Accessible.role: Accessible.Button
    Accessible.name: i18nc("@title tray icon accessible name", "LibreSCRS smart card")
    Accessible.description: statusText
    Accessible.onPressAction: Plasmoid.expanded = !Plasmoid.expanded

    MouseArea {
        anchors.fill: parent
        onClicked: Plasmoid.expanded = !Plasmoid.expanded
    }
}
