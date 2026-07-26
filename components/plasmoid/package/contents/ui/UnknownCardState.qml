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
        icon.name: "dialog-question"
        text: i18nc("@info plasmoid unknown card", "Unrecognized card")
        explanation: i18nc("@info plasmoid unknown card explanation",
                           "This card is not recognized by any installed LibreSCRS plugin.")
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.description: explanation
    }

    PlasmaComponents.Button {
        visible: smartCard.libreCelikAvailable
        Layout.alignment: Qt.AlignCenter
        text: i18nc("@action:button open card in LibreCelik", "Open in LibreCelik")
        icon.name: "document-open"
        onClicked: smartCard.openInLibreCelik()
        Accessible.name: text
    }
}
