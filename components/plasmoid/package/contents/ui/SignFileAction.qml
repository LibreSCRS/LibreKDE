// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.librescrs.smartcard

// "Sign a file…" — the card-first signing entry, shown only in the
// PKI-capable states. Picks an input via the native FileDialog, then hands it to
// smartCard.signFile(), which drives the shared SignJob core (the same one the
// Purpose Share action uses). The agent raises its own PIN prompter. The
// action is never-blank: a busy-disabled button while a sign is in flight, and an
// inline result affordance (success confirmation / localized error) after.
//
// Every surface here is READER-SCOPED: signingBusy, signPhase and signResult all
// answer for the card currently displayed, so a sign running on another reader
// neither spins this card's spinner nor disables its button, and its outcome
// never lands on this card's banner.
ColumnLayout {
    id: signAction

    // This widget's own controller instance (per-widget state; see main.qml).
    required property SmartCard smartCard

    // SmartCardHandler::SignOutcome, mirrored here as named constants the same
    // way CardActionBar mirrors the card states — no C++/QML enum plumbing for
    // a presentation-only value.
    readonly property int outcomeNone: 0
    readonly property int outcomeSucceeded: 1
    readonly property int outcomeFailed: 2

    readonly property var result: smartCard.signResult

    Layout.alignment: Qt.AlignHCenter
    Layout.fillWidth: true
    spacing: Kirigami.Units.smallSpacing

    // The banner's text, composed from the displayed reader's outcome.
    // plainDisplay on both payloads: the filename is user/download-controlled
    // and the error text is agent-derived, while InlineMessage's label renders
    // AutoText (no textFormat knob) — a name like "<a href=…>doc</a>.pdf" must
    // never become a styled link in the result banner.
    readonly property string resultText: {
        if (result.outcome === outcomeFailed)
            return smartCard.plainDisplay(result.message)
        if (result.outcome !== outcomeSucceeded)
            return ""

        const outputPath = result.outputPath
        const shown = smartCard.plainDisplay(outputPath.substring(outputPath.lastIndexOf("/") + 1))
        // certLabel is non-empty only when the card carried SEVERAL signing
        // certs and the deterministic first was picked implicitly — name it
        // so the choice is never silent (a real chooser comes later).
        //
        // The level is what the agent REPORTS having produced, not what was
        // asked for — nothing here asks. It is shown uppercased, the way the
        // AdES levels are written (B-T), and omitted entirely when the agent
        // reported none rather than shown as a blank.
        const certLabel = result.certLabel
        const level = result.level
        if (level.length > 0) {
            return certLabel.length > 0
                ? i18nc("@info:status a file was signed with an implicitly picked certificate; %1 file name, %2 certificate name, %3 AdES conformance level such as B-T",
                        "Signed %1 with certificate %2 at %3",
                        shown,
                        smartCard.plainDisplay(certLabel),
                        level.toUpperCase())
                : i18nc("@info:status a file was signed; %1 file name, %2 AdES conformance level such as B-T",
                        "Signed %1 at %2", shown, level.toUpperCase())
        }
        return certLabel.length > 0
            ? i18nc("@info:status a file was signed with an implicitly picked certificate; %1 file name, %2 certificate name",
                    "Signed %1 with certificate %2",
                    shown,
                    smartCard.plainDisplay(certLabel))
            : i18nc("@info:status a file was signed", "Signed %1", shown)
    }

    // Inline result affordance (never-blank / "wow"): a success confirmation
    // naming the written artifact, or the agent's own localized error. It
    // PERSISTS until the user dismisses it or a new sign on THIS card
    // supersedes it — including across a chip switch and a popup close, since
    // the handler holds it per reader and this is a pure binding onto that.
    //
    // Dismissal goes through an action rather than `showCloseButton`: Kirigami's
    // built-in close button writes `visible = false` on the message itself,
    // which would destroy the binding below for good — the next result would
    // then never appear.
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: signAction.result.outcome !== signAction.outcomeNone
        text: signAction.resultText
        type: signAction.result.outcome === signAction.outcomeFailed
              ? Kirigami.MessageType.Error : Kirigami.MessageType.Positive
        actions: [
            Kirigami.Action {
                icon.name: "dialog-close"
                text: i18nc("@action:button dismiss the signing result message", "Dismiss")
                displayHint: Kirigami.DisplayHint.IconOnly
                onTriggered: signAction.smartCard.dismissSignResult()
            }
        ]
    }

    // In-flight progress: a running spinner + phase-aware status line
    // so the popup is never a frozen disabled button. Covers the pre-PIN card
    // read AND the post-PIN AdES work. signPhase, not operationPhase: the two
    // can genuinely run at once now (a read on this card while another reader
    // signs), so the sign spinner reads the sign's own phase.
    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: Kirigami.Units.smallSpacing
        visible: signAction.smartCard.signingBusy
        PlasmaComponents.BusyIndicator {
            running: parent.visible
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
        }
        PlasmaComponents.Label {
            text: signAction.smartCard.operationPhaseLabel(signAction.smartCard.signPhase)
            opacity: 0.8
        }
    }

    PlasmaComponents.Button {
        Layout.alignment: Qt.AlignHCenter
        text: i18nc("@action:button sign a document with the smart card", "Sign a file…")
        // "document-sign" is a stock Breeze action icon (breeze-icons actions/*),
        // verified present, so the button always renders with an icon.
        icon.name: "document-sign"
        // Never a dead affordance while THIS card's sign is in flight
        // (never-blank). Another reader signing leaves this button live — that
        // is the whole point of the reader-scoped flag.
        enabled: !signAction.smartCard.signingBusy
        onClicked: signFileDialog.open()

        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.description: i18nc("@info:whatsthis sign a file button",
                                      "Choose a file and sign it with this card.")
    }

    FileDialog {
        id: signFileDialog
        title: i18nc("@title:window choose a file to sign", "Choose a file to sign")
        fileMode: FileDialog.OpenFile
        onAccepted: signAction.smartCard.signFile(selectedFile)
    }
}
