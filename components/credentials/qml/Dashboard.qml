// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.librescrs.credentials

// The populated view: the result banner, one CredentialDelegate card per listed
// credential, a Working overlay while a verb runs, and the unblock confirm sheet.
// A thin renderer over the controller — no card I/O, no secrets. Reached only in
// the Ready/Working/Result states (Main.qml gates that); Empty/Loading/no-card
// render StateMessage instead. Plain content (NOT a Kirigami.Page): the ONE page
// in Main.qml owns the title/header chrome — a nested page's title is dead and
// stacks a second Page background for nothing.
Item {
    id: dashboard

    required property var controller

    // True while a verb is in flight — the scrim below is a MODAL surface then.
    readonly property bool working: dashboard.controller.state === CredentialController.Working

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ResultBanner {
            controller: dashboard.controller
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
        }

        ListView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            // The scrim blocks pointer clicks, but a merely-obscured list would
            // still wheel-scroll under it and still take Tab focus into its
            // action buttons. Disabling it while Working closes ALL input
            // routes into the stale rows at once.
            enabled: !dashboard.working
            model: dashboard.controller.credentials
            spacing: Kirigami.Units.largeSpacing
            reuseItems: true

            delegate: CredentialDelegate {
                width: ListView.view.width
                controller: dashboard.controller
            }
        }
    }

    // In-flight verb: a scrim over the (now-stale) list with a spinner + Cancel,
    // so no action can be started until the mutation finishes and the mandatory
    // re-list has refreshed the rows. A MODAL overlay: the MouseArea swallows
    // clicks and wheel, the list beneath is disabled, keyboard focus is trapped
    // on the scrim's Cancel, and assistive tech is told a dialog took over.
    Rectangle {
        id: workingScrim
        anchors.fill: parent
        visible: dashboard.working
        color: Qt.rgba(0, 0, 0, 0.5)

        Accessible.role: Accessible.Dialog
        Accessible.name: i18nc("@info:status accessible name of the modal overlay while a credential operation runs",
                               "Operation in progress")

        // Keyboard trap: while modal, the ONLY reachable control is Cancel —
        // focus moves onto it when the scrim raises, and Tab/Backtab self-loop
        // there so focus can never wander into the obscured window chrome.
        // When the scrim lowers the trap must release symmetrically: focus
        // returns to the (re-enabled) list, so keyboard/AT users regain their
        // position after every verb instead of stranding on a hidden button.
        onVisibleChanged: {
            if (visible) {
                scrimCancel.forceActiveFocus()
            } else {
                view.forceActiveFocus()
            }
        }

        MouseArea {
            anchors.fill: parent
            // Swallow wheel events too — with no handler they would fall
            // through the scrim to whatever scrollable sits beneath.
            onWheel: wheel => {
                wheel.accepted = true
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: Kirigami.Units.largeSpacing

            QQC2.BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: dashboard.working
            }
            QQC2.Button {
                id: scrimCancel
                Layout.alignment: Qt.AlignHCenter
                text: i18nc("@action:button", "Cancel")
                icon.name: "dialog-cancel"
                KeyNavigation.tab: scrimCancel
                KeyNavigation.backtab: scrimCancel
                onClicked: dashboard.controller.cancel()

                Accessible.role: Accessible.Button
                Accessible.name: text
                Accessible.description: i18nc("@info:whatsthis cancel the running credential operation",
                                              "Cancels the operation in progress.")
            }
        }
    }

    // The unblock pre-flight: the controller emits the budget, we raise the sheet.
    UnblockConfirmSheet {
        id: unblockSheet
        controller: dashboard.controller
    }

    Connections {
        target: dashboard.controller
        function onUnblockConfirmRequested(id, budgetText) {
            unblockSheet.openFor(id, budgetText)
        }
    }
}
