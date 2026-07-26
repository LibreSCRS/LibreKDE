// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.librescrs.smartcard

// Auto-mode master-detail: one chip per reader currently holding a
// card selects which card the detail below reflects. Shown only for >=2 cards;
// a single card collapses to CardDetail directly (decided in main.qml).
ColumnLayout {
    id: multiRoot

    // This widget's own controller instance (per-widget state; see main.qml).
    required property SmartCard smartCard

    spacing: Kirigami.Units.largeSpacing

    Kirigami.Heading {
        level: 4
        text: i18nc("@title plasmoid multi-card reader picker", "Select a reader")
        Layout.fillWidth: true

        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    // Chips spread evenly across the full width (each fillWidth), so two readers
    // split the row in half rather than packing to the left.
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            model: multiRoot.smartCard.readersWithCards
            delegate: PlasmaComponents.Button {
                id: readerChip
                required property string modelData
                Layout.fillWidth: true
                // Friendly, distinguishable label (short model + contact/
                // contactless). plainDisplay: the label is still hardware/agent-
                // derived and the PC3 button renders AutoText (verified) —
                // neutralize markup-looking names. Selection uses the EXACT raw
                // name (modelData), never the display label.
                text: multiRoot.smartCard.plainDisplay(multiRoot.smartCard.readerDisplayName(modelData))
                // Breeze ships no smartcard-named icon; auth-sim is the stock
                // chip-card device glyph (verified present).
                icon.name: "auth-sim"
                // The full raw reader name on hover, so the exact device is
                // never hidden by the friendly label.
                PlasmaComponents.ToolTip.text: multiRoot.smartCard.plainDisplay(modelData)
                PlasmaComponents.ToolTip.visible: readerChip.hovered
                PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay
                // Highlight the reader the detail currently reflects, driven
                // PURELY from C++ state. Deliberately NOT checkable: a
                // checkable button's click writes `checked` imperatively
                // (and autoExclusive unchecks siblings), destroying this
                // binding — after the first manual pick an externally-driven
                // readerName change (bound card removed -> deterministic
                // fallback) would no longer re-highlight the right chip. A
                // non-checkable button still renders the checked visual; the
                // click only REQUESTS the selection and the highlight follows
                // readerName round-trip.
                checked: modelData === multiRoot.smartCard.readerName
                onClicked: multiRoot.smartCard.selectReader(modelData)

                Accessible.role: Accessible.RadioButton
                Accessible.name: text
                Accessible.checked: checked
            }
        }
    }

    Kirigami.Separator { Layout.fillWidth: true }

    CardDetail {
        smartCard: multiRoot.smartCard
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
}
