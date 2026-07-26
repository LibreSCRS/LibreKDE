// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.librescrs.smartcard

// The "Save photo…" utility action, extracted from IdentityView so the shared
// CardActionBar can host it flat/icon-only in its TIER-2 strip. Owns its own
// save FileDialog; a failed write is reported via saveFailed() so the bar can
// surface it in its shared footer message area (write failures are rare, and a
// full-width banner belongs at footer level, not under a single icon).
PlasmaComponents.ToolButton {
    id: photoSave

    required property SmartCard smartCard

    // Emitted when savePhoto() returns false; carries the already path-stripped,
    // plainDisplay-neutralised name for the bar's InlineMessage.
    signal saveFailed(string fileName)
    // Emitted at the start of every accepted save so the bar can clear any prior
    // error banner (mirrors the pre-attempt reset the old IdentityView did).
    signal saveStarted()

    icon.name: "document-save"
    icon.width: Kirigami.Units.iconSizes.smallMedium
    icon.height: Kirigami.Units.iconSizes.smallMedium
    display: QQC2.AbstractButton.IconOnly
    text: i18nc("@action:button save the cardholder photo to a file", "Save photo…")
    onClicked: {
        // Preselect a name whose extension matches the RAW bytes' actual format
        // (savePhoto writes those bytes verbatim; an eMRTD photo may be
        // JPEG2000 — never suggest a lying ".png").
        photoDialog.selectedFile = photoDialog.currentFolder + "/"
                + photoSave.smartCard.photoSuggestedFileName
        photoDialog.open()
    }

    PlasmaComponents.ToolTip.text: text
    PlasmaComponents.ToolTip.visible: hovered
    PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay

    Accessible.role: Accessible.Button
    Accessible.name: text

    // The action persists outside the state Loader, so — unlike the pre-bar UI,
    // where a card swap destroyed the hosting state and closed the platform
    // dialog with it — an open save dialog would OUTLIVE its card and, on
    // accept, write the NEW card's live photo bytes to the file chosen for the
    // OLD card. Close it whenever the card context changes. state/readerName
    // never change on popup open/close (see the banner clearing in
    // CardActionBar), so a dialog for a stable card is never disturbed.
    Connections {
        target: photoSave.smartCard
        function onStateChanged() { photoDialog.close() }
        function onReaderNameChanged() { photoDialog.close() }
    }

    FileDialog {
        id: photoDialog
        fileMode: FileDialog.SaveFile
        title: i18nc("@title:window save cardholder photo file dialog", "Save cardholder photo")
        nameFilters: [ i18nc("@item:inlistbox image file filter", "Images") + " (*.png *.jpg *.jpeg *.jp2)" ]
        onAccepted: {
            // Clear any prior error banner before this fresh attempt.
            photoSave.saveStarted()
            if (!photoSave.smartCard.savePhoto(selectedFile)) {
                // plainDisplay: the chosen name is user/path-derived and
                // InlineMessage renders AutoText (no textFormat knob).
                var name = selectedFile.toString()
                name = name.substring(name.lastIndexOf("/") + 1)
                photoSave.saveFailed(photoSave.smartCard.plainDisplay(decodeURIComponent(name)))
            }
        }
    }
}
