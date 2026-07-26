// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.librescrs.smartcard

ColumnLayout {
    id: root
    // This widget's own controller instance (per-widget state; see main.qml).
    required property SmartCard smartCard

    spacing: Kirigami.Units.largeSpacing
    Kirigami.PlaceholderMessage {
        Layout.alignment: Qt.AlignCenter
        Layout.fillWidth: true
        // Four cases: a card is physically seated but the agent has not yet
        // published a resolvable card for it (the deferred-publish window — a
        // card IS present, so never say "insert one"); a widget bound to a
        // reader that is present but card-less (insert one); a widget bound to a
        // reader that is NOT on the bus (unplugged or its arrival was missed — a
        // refresh cannot conjure it back, so say so honestly rather than
        // "waiting"); or an Auto widget prompting for any supported card.
        // Breeze ships no smartcard-named icon; auth-sim-missing (status) and
        // auth-sim (devices) are the stock chip-card glyphs (verified present).
        icon.name: smartCard.cardDetected
                   ? "auth-sim"
                   : (smartCard.boundReaderName.length > 0 && !smartCard.boundReaderPresent
                      ? "auth-sim-missing"
                      : "auth-sim")
        // plainDisplay: the reader name is hardware/agent-derived and
        // PlaceholderMessage's internals render AutoText (no textFormat knob) —
        // neutralize markup-looking names (hostile-device hardening).
        text: smartCard.cardDetected
              ? i18nc("@info plasmoid a card is seated but not yet readable",
                      "Detecting a card…")
              : (smartCard.boundReaderName.length > 0
                 ? (smartCard.boundReaderPresent
                    ? i18nc("@info plasmoid the bound reader is present but holds no card",
                            "Insert a card into %1", smartCard.plainDisplay(smartCard.boundReaderName))
                    : i18nc("@info plasmoid the bound reader is not on the bus",
                            "Reader %1 not connected", smartCard.plainDisplay(smartCard.boundReaderName)))
                 : i18nc("@info plasmoid no card", "Insert a supported card"))

        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    RefreshButton {
        Layout.alignment: Qt.AlignCenter
        smartCard: root.smartCard
    }
}
