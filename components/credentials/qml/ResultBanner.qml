// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.librescrs.credentials

// The outcome banner for the last finished verb. An error banner (red) for a
// genuine failure — a wrong PIN, a blocked credential, a failed key activation —
// and an informational one for a success or a neutral notice (a stale-id refresh,
// a "please wait"). A user cancel produces an EMPTY message, so nothing shows.
// Visible only in the Result state; the controller holds that state across the
// mandatory re-list so the outcome stays readable while the fresh list loads.
Kirigami.InlineMessage {
    id: banner

    required property var controller

    Layout.fillWidth: true

    type: banner.controller.resultIsError ? Kirigami.MessageType.Error : Kirigami.MessageType.Information
    // plainDisplay: InlineMessage renders AutoText (no textFormat knob) —
    // neutralize markup-looking content so the outcome copy always renders as
    // literal characters (defense in depth; same treatment as the plasmoid's
    // error banner).
    text: banner.controller.plainDisplay(banner.controller.resultMessage)
    visible: banner.controller.state === CredentialController.Result && banner.controller.resultMessage.length > 0
}
