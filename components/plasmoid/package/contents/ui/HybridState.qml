// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.librescrs.smartcard

ColumnLayout {
    id: hybridRoot
    required property SmartCard smartCard
    spacing: Kirigami.Units.largeSpacing

    Kirigami.Heading {
        level: 3
        text: hybridRoot.smartCard.cardLabel.length > 0
              ? hybridRoot.smartCard.cardLabel
              : i18nc("@title plasmoid hybrid-card header generic", "Identity + PKI card")
        textFormat: Text.PlainText
        Layout.alignment: Qt.AlignHCenter
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    IdentityView {
        smartCard: hybridRoot.smartCard
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
}
