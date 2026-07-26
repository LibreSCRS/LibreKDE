// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.librescrs.smartcard

ColumnLayout {
    // This widget's own controller instance (per-widget state; see main.qml).
    required property SmartCard smartCard

    spacing: Kirigami.Units.largeSpacing

    Kirigami.PlaceholderMessage {
        Layout.alignment: Qt.AlignCenter
        Layout.fillWidth: true
        // Breeze ships no smartcard-named icon; auth-sim-locked is the stock
        // locked chip-card status glyph (verified present).
        icon.name: "auth-sim-locked"
        text: i18nc("@info plasmoid pre-read auth required",
                    "This card must be unlocked before it can be read")
        // No CAN/MRZ entry in the plasmoid: secrets are collected by the
        // agent's secure prompter, never by this client. Reading the card
        // triggers the agent to raise that prompt.
        explanation: i18nc("@info plasmoid pre-read auth explanation",
                           "When you read the card, the LibreSCRS service opens a secure prompt to enter the card's CAN or scan its MRZ.")

        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.description: explanation
    }

    PlasmaComponents.Button {
        enabled: !smartCard.busy
        Layout.alignment: Qt.AlignCenter
        text: i18nc("@action:button start reading a pre-auth card",
                    "Read Card…")
        // secure-card is the stock Breeze ACTION icon for card operations
        // (verified present incl. -symbolic).
        icon.name: "secure-card"
        onClicked: smartCard.readIdentity()

        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.description: i18nc("@info:whatsthis read pre-auth card button",
                                      "Reads this card. A secure prompt from the LibreSCRS service will ask for the card's CAN or MRZ.")
    }

    // In-flight progress: spinner + phase-aware line (e.g. "Waiting
    // for input…" while the agent's CAN/MRZ prompt is up, "Reading card…"
    // during the ~10 s PACE read) so the popup is never silent.
    RowLayout {
        Layout.alignment: Qt.AlignCenter
        spacing: Kirigami.Units.smallSpacing
        visible: smartCard.busy
        PlasmaComponents.BusyIndicator {
            running: parent.visible
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
        }
        PlasmaComponents.Label {
            text: smartCard.operationPhaseLabel(smartCard.operationPhase)
            opacity: 0.8
        }
    }
}
