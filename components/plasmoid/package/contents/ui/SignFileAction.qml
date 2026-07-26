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
ColumnLayout {
    id: signAction

    // This widget's own controller instance (per-widget state; see main.qml).
    required property SmartCard smartCard

    Layout.alignment: Qt.AlignHCenter
    Layout.fillWidth: true
    spacing: Kirigami.Units.smallSpacing

    // Inline result affordance (never-blank / "wow"): a success
    // confirmation naming the written artifact, or the agent's own localized
    // error. It PERSISTS until the user dismisses it (close button) or a new
    // sign clears it (Connections below) — the earlier 6 s auto-hide lost the
    // confirmation too fast.
    Kirigami.InlineMessage {
        id: signResult
        Layout.fillWidth: true
        showCloseButton: true
    }

    // A new sign clears the previous banner so success/error never stacks stale.
    // This action now lives in the persistent CardActionBar (outside the state
    // Loader), so — unlike on main where the host state was destroyed on swap —
    // it also must clear the banner when the CARD changes, or a prior card's
    // "Signed X" / error would reappear on the next card. state/readerName cover
    // every swap (physical removal routes through NoCard → stateChanged; a
    // multi-card reader-selection switch → readerNameChanged) and neither fires
    // on popup open/close, so the "persist until dismissed" contract for the
    // SAME card is kept.
    Connections {
        target: signAction.smartCard
        function onSigningBusyChanged() {
            if (signAction.smartCard.signingBusy)
                signResult.visible = false
        }
        function onStateChanged() { signResult.visible = false }
        function onReaderNameChanged() { signResult.visible = false }
    }

    // In-flight progress: a running spinner + phase-aware status line
    // so the popup is never a frozen disabled button. Covers the pre-PIN card
    // read AND the post-PIN AdES work.
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
            text: signAction.smartCard.operationPhaseLabel(signAction.smartCard.operationPhase)
            opacity: 0.8
        }
    }

    PlasmaComponents.Button {
        Layout.alignment: Qt.AlignHCenter
        text: i18nc("@action:button sign a document with the smart card", "Sign a file…")
        // "document-sign" is a stock Breeze action icon (breeze-icons actions/*),
        // verified present, so the button always renders with an icon.
        icon.name: "document-sign"
        // Never a dead affordance while a sign is in flight (never-blank).
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

    // Surface the SmartCardHandler outcome in place — no silent success/failure.
    // plainDisplay on both payloads: the filename is user/download-controlled and
    // the error text is agent-derived, while InlineMessage's label renders
    // AutoText (no textFormat knob) — a name like "<a href=…>doc</a>.pdf" must
    // never become a styled link in the result banner.
    Connections {
        target: signAction.smartCard
        function onSignSucceeded(outputPath, certLabel) {
            var name = outputPath.substring(outputPath.lastIndexOf("/") + 1)
            // certLabel is non-empty only when the card carried SEVERAL signing
            // certs and the deterministic first was picked implicitly — name it
            // so the choice is never silent (a real chooser comes later).
            signResult.text = certLabel.length > 0
                ? i18nc("@info:status a file was signed with an implicitly picked certificate; %1 file name, %2 certificate name",
                        "Signed %1 with certificate %2",
                        signAction.smartCard.plainDisplay(name),
                        signAction.smartCard.plainDisplay(certLabel))
                : i18nc("@info:status a file was signed", "Signed %1",
                        signAction.smartCard.plainDisplay(name))
            signResult.type = Kirigami.MessageType.Positive
            signResult.visible = true
        }
        function onSignFailed(message) {
            signResult.text = signAction.smartCard.plainDisplay(message)
            signResult.type = Kirigami.MessageType.Error
            signResult.visible = true
        }
    }
}
