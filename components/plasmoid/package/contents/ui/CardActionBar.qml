// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.librescrs.smartcard

// The single, capability-driven footer shared by every present-card state
// (instantiated once in CardDetail below the state Loader; reused inside the
// MultiCardState detail pane). Two tiers: TIER 1 a raised primary (Sign) shown
// only for signing cards; TIER 2 a flat icon-only utility strip. Self-hides
// when no action applies, so PreAuth/card-less states are untouched. COMPOSES
// focused action components (SignFileAction, PhotoSaveAction) — it is a layout,
// not a reimplementation of their dialogs/results.
ColumnLayout {
    id: actionBar

    required property SmartCard smartCard

    // CardStateModel::State values, mirrored here as named constants to keep the
    // capability logic free of magic numbers (same int mapping documented in
    // main.qml / CardDetail). Deliberately QML-side — no C++/QML enum plumbing
    // for a presentation-only change.
    readonly property int stIdentityOnly: 2
    readonly property int stPkiOnly: 3
    readonly property int stHybrid: 4
    readonly property int stError: 5

    readonly property bool canSign:
        smartCard.state === stPkiOnly || smartCard.state === stHybrid
    readonly property bool hasIdentityActions:
        smartCard.state === stIdentityOnly || smartCard.state === stHybrid
    // Error keeps the card BOUND (a failed identity read, or a card whose only
    // advertised surface is PIN management classifies as Error), so "Manage
    // credentials…" must stay reachable there: changing/unblocking the PIN is
    // exactly what a failed-read card may need, and for a PinManagement-only
    // card this footer is its sole entry point. pinManagementAvailable is
    // false in every card-less state.
    readonly property bool offersManageInError:
        smartCard.state === stError && smartCard.pinManagementAvailable
    // Show the footer for present, CLASSIFIED cards (states 2/3/4) — both
    // canSign and hasIdentityActions imply such a card, and within 2/3/4 the bar
    // is never visible-but-empty (2/4 always show "Open in File Manager"; 3/4
    // always show Sign) — plus the Error manage-credentials case above.
    // Deliberately NOT included:
    //  • libreCelikAvailable — a construction-time CONSTANT (true whenever the
    //    `librecelik` binary is on PATH, independent of any card); it would leak
    //    the bar, with a non-functional "Open in LibreCelik", into every
    //    card-less/guided state (NoCard, PreAuth, …).
    //  • pinManagementAvailable in PreAuth — we intentionally do not surface
    //    "Manage credentials" before the card is read/unlocked (main showed it
    //    there; today's PreAuth PACE cards don't advertise it, so this is
    //    latent). Its own button binding still gates it within states 2/3/4.
    readonly property bool hasAnyAction: canSign || hasIdentityActions || offersManageInError
    // The LibreCelik utility button hides where the state body already offers
    // its own launch control (ErrorState's button; PkiState's placeholder
    // helpfulAction) — never two controls for the same action in one small
    // popup.
    readonly property bool showsLibreCelikButton:
        smartCard.libreCelikAvailable
        && smartCard.state !== stError && smartCard.state !== stPkiOnly
    // Whether the TIER-2 strip has any visible button (else it is hidden so a
    // bare signing token — no File Manager / LibreCelik / PIN mgmt — shows just
    // the primary, with no empty row or trailing dead space).
    readonly property bool hasUtilityAction:
        hasIdentityActions || showsLibreCelikButton || smartCard.pinManagementAvailable

    Layout.fillWidth: true
    spacing: Kirigami.Units.smallSpacing
    // Bound to capability: the layout collapses to zero height in PreAuth/
    // card-less states. (No opacity fade — the state body already fades via the
    // Loader's stateFade, and animating a Layout child's visibility is a no-op.)
    visible: hasAnyAction

    // Divides the footer from the scrolling content above.
    Kirigami.Separator { Layout.fillWidth: true }

    // Shared footer message slot: rare photo-save failure. (The sign flow
    // surfaces its own result inside SignFileAction.)
    Kirigami.InlineMessage {
        id: photoError
        Layout.fillWidth: true
        type: Kirigami.MessageType.Error
        showCloseButton: true
    }

    // The bar persists across card swaps (it lives outside the state Loader), so
    // clear a stale photo-save error when the card changes — otherwise a prior
    // card's failure would reappear on the next identity card. state/readerName
    // do not change on popup open/close, so a still-relevant banner survives.
    Connections {
        target: actionBar.smartCard
        function onStateChanged() { photoError.visible = false }
        function onReaderNameChanged() { photoError.visible = false }
    }

    // TIER 1 — the raised primary, self-contained (owns its dialog/result/busy).
    // Only signing cards (PkiOnly / Hybrid) show it.
    SignFileAction {
        id: primaryAction
        smartCard: actionBar.smartCard
        visible: actionBar.canSign
        Layout.fillWidth: true
    }

    // TIER 2 — flat icon-only utility strip, centered.
    RowLayout {
        visible: actionBar.hasUtilityAction
        Layout.alignment: Qt.AlignHCenter
        // A touch more air between the primary and the utility strip (tier gap
        // ≈ largeSpacing; the column's own spacing is smallSpacing). Only when
        // a primary is actually shown — in IdentityOnly the strip is the sole
        // tier, so it sits tight under the separator.
        Layout.topMargin: primaryAction.visible
                          ? Kirigami.Units.largeSpacing - Kirigami.Units.smallSpacing : 0
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents.ToolButton {
            visible: actionBar.hasIdentityActions
            icon.name: "system-file-manager"
            icon.width: Kirigami.Units.iconSizes.smallMedium
            icon.height: Kirigami.Units.iconSizes.smallMedium
            display: QQC2.AbstractButton.IconOnly
            text: i18nc("@action:button open this card in the file manager", "Open in File Manager")
            onClicked: actionBar.smartCard.openInFiles()
            PlasmaComponents.ToolTip.text: text
            PlasmaComponents.ToolTip.visible: hovered
            PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay
            Accessible.role: Accessible.Button
            Accessible.name: text
        }

        PhotoSaveAction {
            smartCard: actionBar.smartCard
            visible: actionBar.hasIdentityActions && actionBar.smartCard.hasCardPhoto
            onSaveStarted: photoError.visible = false
            onSaveFailed: (fileName) => {
                photoError.text = i18nc("@info:status saving the cardholder photo failed",
                                        "Could not save the photo to %1.", fileName)
                photoError.visible = true
            }
        }

        PlasmaComponents.ToolButton {
            visible: actionBar.showsLibreCelikButton
            icon.name: "document-open"
            icon.width: Kirigami.Units.iconSizes.smallMedium
            icon.height: Kirigami.Units.iconSizes.smallMedium
            display: QQC2.AbstractButton.IconOnly
            text: i18nc("@action:button open card in LibreCelik", "Open in LibreCelik")
            onClicked: actionBar.smartCard.openInLibreCelik()
            PlasmaComponents.ToolTip.text: text
            PlasmaComponents.ToolTip.visible: hovered
            PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay
            Accessible.role: Accessible.Button
            Accessible.name: text
            Accessible.description: i18nc("@info:whatsthis open in librecelik button",
                                          "Opens this card in LibreCelik for full details.")
        }

        PlasmaComponents.ToolButton {
            visible: actionBar.smartCard.pinManagementAvailable
            icon.name: "auth-sim"
            icon.width: Kirigami.Units.iconSizes.smallMedium
            icon.height: Kirigami.Units.iconSizes.smallMedium
            display: QQC2.AbstractButton.IconOnly
            text: i18nc("@action:button open the credential-management window", "Manage credentials…")
            onClicked: actionBar.smartCard.manageCredentials()
            PlasmaComponents.ToolTip.text: text
            PlasmaComponents.ToolTip.visible: hovered
            PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay
            Accessible.role: Accessible.Button
            Accessible.name: text
            Accessible.description: i18nc("@info:whatsthis manage credentials button",
                                          "Opens a window to change or unblock this card's PIN.")
        }
    }
}
