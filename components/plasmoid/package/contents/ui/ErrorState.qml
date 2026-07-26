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
        icon.name: "dialog-error"
        text: i18nc("@info plasmoid error placeholder title",
                    "Could not read this card")
        // plainDisplay: the agent's error text can embed card/reader-derived
        // fragments and PlaceholderMessage's explanation renders AutoText (no
        // textFormat knob) — neutralize markup-looking content.
        // The fallback names LibreCelik, so it is gated on the app actually
        // being present — same gate as the button below. With no error text
        // and no app there is nothing actionable to point at; an empty
        // explanation collapses PlaceholderMessage's explanation label.
        explanation: smartCard.errorMessage.length > 0
                     ? smartCard.plainDisplay(smartCard.errorMessage)
                     : (smartCard.libreCelikAvailable
                        ? i18nc("@info plasmoid error placeholder fallback explanation",
                                "Open LibreCelik for more details.")
                        : "")

        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.description: explanation
    }

    RowLayout {
        Layout.alignment: Qt.AlignCenter
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents.Button {
            visible: root.smartCard.libreCelikAvailable
            text: i18nc("@action:button open card in LibreCelik", "Open in LibreCelik")
            icon.name: "document-open"
            onClicked: root.smartCard.openInLibreCelik()

            Accessible.role: Accessible.Button
            Accessible.name: text
            Accessible.description: i18nc("@info:whatsthis open in librecelik button",
                                          "Opens this card in LibreCelik for full details.")
        }
        // A read that failed on a card still in the reader: let the user retry
        // discovery without re-seating it.
        RefreshButton {
            smartCard: root.smartCard
        }
    }
}
