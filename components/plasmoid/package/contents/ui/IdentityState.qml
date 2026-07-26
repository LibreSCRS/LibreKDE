// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.librescrs.smartcard

ColumnLayout {
    id: identityRoot

    // This widget's own controller instance (per-widget state; see main.qml).
    required property SmartCard smartCard

    spacing: Kirigami.Units.largeSpacing

    Kirigami.Heading {
        level: 3
        text: identityRoot.smartCard.cardLabel.length > 0
              ? identityRoot.smartCard.cardLabel
              : i18nc("@title plasmoid identity-card section header generic", "Identity card")
        // cardLabel is card/agent-derived: never AutoText (hostile-card hardening).
        textFormat: Text.PlainText
        Layout.alignment: Qt.AlignHCenter
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    IdentityView {
        smartCard: identityRoot.smartCard
        Layout.fillWidth: true
        // Take the remaining popup height: the identity body scrolls inside
        // (IdentityView's ScrollView), the popup never overflows.
        Layout.fillHeight: true
    }
}
