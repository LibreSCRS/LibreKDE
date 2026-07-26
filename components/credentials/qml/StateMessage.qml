// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.librescrs.credentials

// The full-page placeholder shown for every non-dashboard state: the agent is
// down, no card is present, the card can't be managed here, the list is being
// fetched, or the card exposes no manageable credentials. `Empty` is a distinct,
// gentler message than `NotManageable` — a manageable card that simply has
// nothing to act on, versus a card outside this window's scope.
//
// `Loading` is the one state with live affordances: a spinner
// plus a Cancel action — the list op is cancellable, and the agent may raise a
// CAN prompt during the read, so the window must offer its own way out.
Kirigami.PlaceholderMessage {
    id: root

    required property var controller

    // Item.state is a reserved string property, so the controller state is
    // carried on a purpose-named int.
    readonly property int uiState: root.controller.state

    anchors.centerIn: parent
    width: parent.width - Kirigami.Units.gridUnit * 4
    visible: true
    text: switch (root.uiState) {
    case CredentialController.AgentUnavailable:
        return i18n("The smart-card service isn't running.")
    case CredentialController.NoCard:
        // Name the reader to insert into when one is bound —
        // after a card removal the binding survives, so the name is available.
        // plainDisplay: the reader name is hardware/agent-derived and
        // PlaceholderMessage's internals render AutoText (no textFormat knob) —
        // neutralize markup-looking names (hostile-device hardening).
        return root.controller.readerName.length > 0
            ? i18nc("@info %1 is the reader name", "Insert the card in reader %1.",
                    root.controller.plainDisplay(root.controller.readerName))
            : i18n("Insert the card in the reader.")
    case CredentialController.NotManageable:
        return i18n("This card doesn't support credential management here.")
    case CredentialController.Loading:
        return i18n("Reading credentials…")
    case CredentialController.Empty:
        return i18n("No manageable credentials on this card.")
    case CredentialController.ReadFailed:
        return i18n("The card's credentials couldn't be read. Try removing and reinserting the card.")
    default:
        return ""
    }

    // A mutation terminated by a card pull surfaces its truthful outcome here:
    // the removal lands NoCard (where the Result banner never renders), so the
    // card-removed copy rides the placeholder's explanation until the next card
    // arrives (the controller clears the notice on the next card bind).
    explanation: root.uiState === CredentialController.NoCard ? root.controller.removalNotice : ""

    // The window-side escape hatch for a stalled / CAN-prompting read.
    helpfulAction: root.uiState === CredentialController.Loading ? cancelReadAction : null
    Kirigami.Action {
        id: cancelReadAction
        text: i18nc("@action:button", "Cancel")
        icon.name: "dialog-cancel"
        onTriggered: root.controller.cancel()
    }

    QQC2.BusyIndicator {
        Layout.alignment: Qt.AlignHCenter
        visible: root.uiState === CredentialController.Loading
        running: visible
    }
}
