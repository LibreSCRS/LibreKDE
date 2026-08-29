// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// The unblock pre-flight sheet: it surfaces the PUK's remaining budget (so the
// holder sees how many unblocks are left BEFORE spending one) and, on Continue,
// asks the controller to start the unblock. No secret is entered here — the PUK
// is collected by the agent's secure prompter that the unblock verb raises.
Kirigami.PromptDialog {
    id: sheet

    required property var controller
    // The credential being unblocked; forwarded to confirmUnblock().
    property string pinId: ""
    // The PUK's remaining-budget line (from the controller's pre-flight), or empty.
    property string budgetText: ""

    title: i18nc("@title:window", "Unblock PIN")
    // plainDisplay: the PromptDialog subtitle renders AutoText (no textFormat
    // knob) — neutralize the inserted budget line so it always renders as
    // literal characters (defense in depth).
    subtitle: sheet.budgetText.length > 0
        ? i18nc("@info", "%1.\n\nContinue to enter the PUK in the secure prompt.",
                sheet.controller.plainDisplay(sheet.budgetText))
        : i18nc("@info", "You will be asked for the PUK in a secure prompt.")

    // Only the two custom actions; no default OK/Cancel pair.
    standardButtons: QQC2.Dialog.NoButton
    customFooterActions: [
        Kirigami.Action {
            text: i18nc("@action:button proceed to unblock a PIN with its PUK", "Continue")
            // Same verified-Breeze glyph as the dashboard's Unblock… button.
            icon.name: "emblem-unlocked"
            onTriggered: {
                sheet.controller.confirmUnblock(sheet.pinId)
                sheet.close()
            }
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Cancel")
            icon.name: "dialog-cancel"
            onTriggered: sheet.close()
        }
    ]

    // Populate + raise the sheet for a specific credential and budget line.
    function openFor(id, budget) {
        sheet.pinId = id
        sheet.budgetText = budget
        sheet.open()
    }
}
