// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.librescrs.smartcard

ColumnLayout {
    id: root
    // This widget's own controller instance (per-widget state; see main.qml).
    required property SmartCard smartCard

    spacing: Kirigami.Units.largeSpacing

    Kirigami.PlaceholderMessage {
        Layout.alignment: Qt.AlignCenter
        Layout.fillWidth: true
        icon.name: "dialog-warning"
        text: i18nc("@info plasmoid agent unavailable", "Smart card service unavailable")
        explanation: smartCard.agentInstalled
                     ? i18nc("@info plasmoid agent stopped",
                             "The LibreSCRS smart card service is installed but not running.")
                     : i18nc("@info plasmoid agent not installed",
                             "The LibreSCRS smart card service is not installed.")
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.description: explanation
    }

    // Installed but stopped: offer the exact next step.
    ColumnLayout {
        visible: smartCard.agentInstalled
        Layout.alignment: Qt.AlignCenter
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents.Button {
            Layout.alignment: Qt.AlignCenter
            text: i18nc("@action:button start smart card service", "Start service")
            icon.name: "system-run"
            onClicked: smartCard.startAgent()
            Accessible.name: text
        }
        PlasmaComponents.Label {
            Layout.alignment: Qt.AlignCenter
            opacity: 0.7
            text: i18nc("@info plasmoid agent start instruction", "Start it with this command:")
        }
        RowLayout {
            Layout.alignment: Qt.AlignCenter
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents.Label {
                id: commandLabel
                // The literal command is NOT translated. font.family "monospace"
                // resolves via the fontconfig alias (Kirigami.Theme exposes no
                // fixed-width font) — deliberate.
                text: "systemctl --user start librescrs-agent"
                textFormat: Text.PlainText
                font.family: "monospace"
            }
            // Actionable guidance: the command is copyable, not just
            // readable — same affordance as the identity-field copy buttons.
            PlasmaComponents.ToolButton {
                icon.name: "edit-copy"
                onClicked: smartCard.copyField(commandLabel.text)
                PlasmaComponents.ToolTip.text: i18nc("@info:tooltip copy this field to the clipboard", "Copy to clipboard")
                PlasmaComponents.ToolTip.visible: hovered
                PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay
                Accessible.role: Accessible.Button
                Accessible.name: i18nc("@info:tooltip copy this field to the clipboard", "Copy to clipboard")
            }
        }
    }

    // Not installed: link to the installation documentation (a docs link, not a
    // card/app action — the never-a-browser rule targets card actions, not help).
    PlasmaComponents.Button {
        visible: !root.smartCard.agentInstalled
        Layout.alignment: Qt.AlignCenter
        text: i18nc("@action:button open online installation help", "Installation help")
        icon.name: "help-contents"
        onClicked: Qt.openUrlExternally("https://librescrs.github.io/")
        Accessible.name: text
    }

    // Re-check the bus: if the agent has since started (or a NameOwnerChanged was
    // missed), this leaves the unavailable state without waiting for the watcher.
    RefreshButton {
        Layout.alignment: Qt.AlignCenter
        smartCard: root.smartCard
    }
}
