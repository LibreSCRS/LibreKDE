// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.librescrs.smartcard

// A small icon-only affordance shown in the card-less states (no card /
// detecting / error / service-unavailable) to force a re-check without waiting
// for the next agent event or re-seating the card. It re-runs client discovery
// (re-probe bus name + GetManagedObjects — the EXISTING wire, no new D-Bus
// method), which recovers from a card the agent exported but whose
// InterfacesAdded the client never saw, or an agent that reappeared without a
// watcher signal. It is deliberately absent from the ready states: those
// re-populate on their own from card events, so a refresh there is clutter.
PlasmaComponents.ToolButton {
    // This widget's own controller instance (per-widget state; see main.qml).
    required property SmartCard smartCard

    icon.name: "view-refresh"
    display: QQC2.AbstractButton.IconOnly
    text: i18nc("@action:button re-check the reader for a card", "Refresh")
    onClicked: smartCard.requestRefresh()

    PlasmaComponents.ToolTip.text: text
    PlasmaComponents.ToolTip.visible: hovered
    PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay

    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.description: i18nc("@info:whatsthis refresh button",
                                  "Re-checks the readers for smart cards.")
}
