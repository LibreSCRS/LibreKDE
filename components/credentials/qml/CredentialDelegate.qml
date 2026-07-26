// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One card per credential row: the kind name + a display-only state chip in the
// header row, the counters + guidance lines (each shown only when non-empty),
// and the capability-gated action buttons — all in ONE column so the vertical
// rhythm is a single, deterministic spacing between every row regardless of
// which pieces a given record carries. Every button's visibility is bound
// STRICTLY to its per-record capability role — a card never offers a verb the
// record doesn't advertise. No secret is ever collected here — the agent's
// secure prompter does that.
//
// The whole body lives in `contentItem` (AbstractCard's header/footer slots are
// intentionally unused): AbstractCard is a Control that stretches its
// contentItem to the available content height, and a bare Layout stretched that
// way mis-positions its children (guidance overflowing the card in a ListView);
// an always-assigned footer RowLayout additionally collapses to zero height for
// an actionless record (a PUK), leaving phantom spacing and an uneven card. A
// plain Item wrapper whose implicit size tracks the body, with the body
// TOP-anchored (not filled), keeps every row at its implicit position no matter
// how tall the Control makes the wrapper, and the action row simply drops out of
// the layout when the record advertises no verb.
//
// Icon names are verified present in shipping Breeze (which ships NO
// smartcard/certificate-named icons — see the plasmoid's MultiCardState note);
// re-verify on a clean Breeze before swapping any.
Kirigami.AbstractCard {
    id: delegate

    // The row's model object (roles: kindName / stateName / state / counters /
    // guidance / canChange / unblockable / activatable / keyActivatable /
    // keyActivationPending / id) is injected by the ListView; the controller is
    // bound per-instance by the Dashboard.
    required property var model
    required property var controller

    // Any capability-gated action available for this record? Mirrors the OR of
    // every button's visibility below, so the action row is present exactly when
    // at least one button is. When false the row leaves the layout entirely, so
    // an actionless record (a PUK) keeps the SAME title<->counter rhythm as one
    // with buttons instead of collapsing a footer to zero height.
    readonly property bool hasAction: delegate.model.canChange || delegate.model.unblockable
                                      || delegate.model.activatable
                                      || (delegate.model.keyActivatable && delegate.model.keyActivationPending)

    contentItem: Item {
        implicitWidth: body.implicitWidth
        implicitHeight: body.implicitHeight

        ColumnLayout {
            id: body
            // Top-anchored (never filled): the Control may stretch this wrapper
            // Item taller than the body needs, but the column keeps its implicit
            // height and its children stay put.
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: Kirigami.Units.smallSpacing

            // Header row: the kind name + a colour-coded, display-only state pill.
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Heading {
                    level: 3
                    text: delegate.model.kindName
                    // Model-derived text renders PlainText throughout the card —
                    // uniform hardening against rich-text promotion (the guidance
                    // line below can carry an agent-supplied fallback string).
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    Layout.fillWidth: true

                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                // Colour-coded, display-only state pill (blocked = negative,
                // transport/needsChange = caution). A plain Rectangle+Label rather
                // than Kirigami.Chip: a Chip is an AbstractButton, which a screen
                // reader announces as actionable — this is pure status.
                Rectangle {
                    id: stateChip

                    // CredentialState ints (CredentialModel StateRole):
                    //   0 Unknown, 1 Transport, 2 Operational, 3 NeedsChange, 4 Blocked
                    readonly property color stateColor: {
                        switch (delegate.model.state) {
                        case 4: return Kirigami.Theme.negativeTextColor;
                        case 1:
                        case 3: return Kirigami.Theme.neutralTextColor;
                        case 2: return Kirigami.Theme.positiveTextColor;
                        default: return Kirigami.Theme.textColor;
                        }
                    }

                    radius: height / 2
                    color: Qt.alpha(stateColor, 0.12)
                    border.color: Qt.alpha(stateColor, 0.5)
                    border.width: 1
                    implicitWidth: stateLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
                    implicitHeight: stateLabel.implicitHeight + Kirigami.Units.smallSpacing * 2

                    QQC2.Label {
                        id: stateLabel
                        anchors.centerIn: parent
                        text: delegate.model.stateName
                        textFormat: Text.PlainText
                        color: stateChip.stateColor
                    }

                    Accessible.role: Accessible.StaticText
                    Accessible.name: stateLabel.text
                }
            }

            QQC2.Label {
                text: delegate.model.counters
                visible: text.length > 0
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            QQC2.Label {
                // The guidance line can carry the AGENT-SUPPLIED English fallback
                // (an untranslated frozen key degrades to the agent's own string) —
                // never let a markup-looking value promote to StyledText.
                text: delegate.model.guidance
                visible: text.length > 0
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                opacity: 0.75
                Layout.fillWidth: true
            }

            // Action row: present only when the record advertises a verb, so an
            // actionless PUK adds no phantom space. Each button is still gated
            // STRICTLY on its own per-record capability role.
            RowLayout {
                Layout.fillWidth: true
                visible: delegate.hasAction
                spacing: Kirigami.Units.smallSpacing
                Item {
                    Layout.fillWidth: true
                } // push the actions to the trailing edge
                QQC2.Button {
                    text: i18nc("@action:button change a PIN", "Change…")
                    icon.name: "document-edit"
                    visible: delegate.model.canChange
                    onClicked: delegate.controller.changePin(delegate.model.id)

                    Accessible.role: Accessible.Button
                    Accessible.name: text
                    Accessible.description: i18nc("@info:whatsthis change PIN button",
                                                  "Opens the secure prompt to change this PIN.")
                }
                QQC2.Button {
                    text: i18nc("@action:button unblock a PIN with its PUK", "Unblock…")
                    icon.name: "emblem-unlocked"
                    visible: delegate.model.unblockable
                    onClicked: delegate.controller.requestUnblock(delegate.model.id)

                    Accessible.role: Accessible.Button
                    Accessible.name: text
                    Accessible.description: i18nc("@info:whatsthis unblock PIN button",
                                                  "Unblocks this PIN with its PUK, entered in the secure prompt.")
                }
                QQC2.Button {
                    text: i18nc("@action:button activate a transport PIN", "Activate…")
                    icon.name: "dialog-ok-apply"
                    visible: delegate.model.activatable
                    onClicked: delegate.controller.activate(delegate.model.id)

                    Accessible.role: Accessible.Button
                    Accessible.name: text
                    Accessible.description: i18nc("@info:whatsthis activate transport PIN button",
                                                  "Activates this transport PIN via the secure prompt.")
                }
                QQC2.Button {
                    text: i18nc("@action:button activate the on-card signing key", "Activate signing key")
                    icon.name: "application-certificate"
                    // The standalone recovery button needs the key to still be
                    // pending — key_activatable alone would offer a no-op re-activation.
                    visible: delegate.model.keyActivatable && delegate.model.keyActivationPending
                    onClicked: delegate.controller.activateSigningKey(delegate.model.id)

                    Accessible.role: Accessible.Button
                    Accessible.name: text
                    Accessible.description: i18nc("@info:whatsthis activate signing key button",
                                                  "Activates the card's signing key.")
                }
            }
        }
    }
}
