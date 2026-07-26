// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.librescrs.credentials

Kirigami.ApplicationWindow {
    id: root

    title: i18nc("@title:window", "Card Credentials")
    width: Kirigami.Units.gridUnit * 26
    height: Kirigami.Units.gridUnit * 32

    // ONE controller for the window; the dashboard is its `credentials` model.
    CredentialController {
        id: controller
    }

    // The dashboard is shown once the credential list is populated (Ready) and
    // stays up while a verb runs / its result is shown (Working/Result); every
    // other state — agent down, no card, not manageable, still loading, or an
    // empty list — renders the full-page StateMessage instead.
    readonly property bool showDashboard: controller.state === CredentialController.Ready
        || controller.state === CredentialController.Working
        || controller.state === CredentialController.Result

    // ONE page owns the whole window: it (not a nested page) carries the title
    // the pageStack surfaces in the window's header chrome, and it names the
    // bound reader — the Unique window manages one reader at a time, so on a
    // multi-reader machine the user must be able to see which card this is.
    pageStack.initialPage: Kirigami.Page {
        title: i18nc("@title", "Card Credentials")
        padding: 0

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            QQC2.Label {
                visible: controller.readerName.length > 0
                text: visible
                    ? i18nc("@info:status the reader whose card this window manages",
                            "Reader: %1", controller.readerName)
                    : ""
                // The reader name is hardware/agent-derived; a plain Label's
                // default AutoText would promote a tag-looking name to
                // StyledText (markup injection). This sink has the knob, so pin
                // it (PlaceholderMessage/InlineMessage route through
                // controller.plainDisplay instead).
                textFormat: Text.PlainText
                elide: Text.ElideRight
                opacity: 0.75
                padding: Kirigami.Units.largeSpacing
                Layout.fillWidth: true

                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: root.showDashboard ? dashboardComponent : messageComponent
            }
        }

        Component {
            id: messageComponent
            StateMessage {
                controller: controller
            }
        }

        Component {
            id: dashboardComponent
            Dashboard {
                controller: controller
            }
        }
    }
}
