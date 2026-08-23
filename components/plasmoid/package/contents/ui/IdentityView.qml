// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.librescrs.smartcard

// Shared identity body reused by IdentityState and HybridState: photo + loading
// placeholder + summary + expander, all inside a ScrollView bounded to the
// popup height (a real card emits ~25-30 rows; the content scrolls). Card
// actions (save photo, open, sign, manage) live in the shared CardActionBar
// footer below the state Loader — not here. Reads this widget's own SmartCard
// instance (handed down from the host state).
ColumnLayout {
    id: identityView

    // This widget's own controller instance (per-widget state; see main.qml).
    required property SmartCard smartCard

    spacing: Kirigami.Units.largeSpacing

    PlasmaComponents.ScrollView {
        id: scrollArea
        Layout.fillWidth: true
        Layout.fillHeight: true
        // Never scroll horizontally: the column tracks the viewport width.
        contentWidth: availableWidth

        ColumnLayout {
            width: scrollArea.availableWidth
            spacing: Kirigami.Units.largeSpacing

            // Cardholder photo — shown only when present; the summary/placeholder
            // covers cards with no photo (or an undecodable one).
            Item {
                visible: identityView.smartCard.hasCardPhoto
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                Layout.preferredHeight: Kirigami.Units.gridUnit * 7.5

                Image {
                    anchors.fill: parent
                    source: identityView.smartCard.cardPhotoUrl
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    asynchronous: true
                    smooth: true

                    Accessible.role: Accessible.Graphic
                    Accessible.name: i18nc("@info:label accessible name for the ID photo", "Cardholder photo")
                    Accessible.description: i18nc("@info:whatsthis ID photo description",
                                                  "The identity photo read from the smart card.")
                }
            }

            // Never blank: a loading line while a read is in flight and nothing
            // shown yet.
            PlasmaComponents.Label {
                visible: identityView.smartCard.busy && !identityView.smartCard.hasIdentity
                Layout.alignment: Qt.AlignHCenter
                opacity: 0.7
                // Phase-aware: reflects the real read phase
                // ("Reading card…", "Waiting for input…") rather than a static line.
                text: identityView.smartCard.operationPhaseLabel(identityView.smartCard.operationPhase)
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            // Summary: the identifying fields (name / document no. / type / expiry).
            ColumnLayout {
                visible: identityView.smartCard.hasIdentity
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Repeater {
                    model: identityView.smartCard.identitySummary
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        PlasmaComponents.Label {
                            // Raw card/agent bytes: never let AutoText promote a
                            // tag-looking value to styled text (hostile-card hardening).
                            text: modelData.label
                            textFormat: Text.PlainText
                            opacity: 0.7
                        }
                        PlasmaComponents.Label {
                            text: modelData.value
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                            font.bold: true
                        }
                        PlasmaComponents.ToolButton {
                            icon.name: "edit-copy"
                            onClicked: identityView.smartCard.copyField(modelData.value)
                            // Attached ToolTips never show by themselves: visible
                            // (+ the standard delay) must be driven explicitly.
                            PlasmaComponents.ToolTip.text: i18nc("@info:tooltip copy this field to the clipboard", "Copy to clipboard")
                            PlasmaComponents.ToolTip.visible: hovered
                            PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay
                            Accessible.role: Accessible.Button
                            Accessible.name: i18nc("@info:tooltip copy this field to the clipboard", "Copy to clipboard")
                        }
                    }
                }
            }

            // Progressive disclosure: reveal the REMAINING field groups in
            // place. `identityDetails` is the model minus the rows the summary
            // above already shows, so expanding adds rows rather than repeating
            // the ones on screen; the button is offered only when there are
            // such rows.
            PlasmaComponents.Button {
                id: expander
                visible: identityView.smartCard.hasIdentity
                         && identityView.smartCard.identityDetails.length > 0
                Layout.alignment: Qt.AlignHCenter
                checkable: true
                icon.name: checked ? "arrow-up" : "arrow-down"
                text: checked ? i18nc("@action:button hide all identity fields", "Hide details")
                              : i18nc("@action:button reveal all identity fields", "Show all details")
                Accessible.name: text
            }

            ColumnLayout {
                visible: expander.checked
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Repeater {
                    // NOT identityFields: that list still contains the rows the
                    // summary above is already showing, so binding it here
                    // renders each of them a second time.
                    model: identityView.smartCard.identityDetails
                    delegate: ColumnLayout {
                        id: detailRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        // The heading of the group this row opens. The model
                        // stamps it on the FIRST row of each group and leaves it
                        // empty on the rest, so this delegate never has to look
                        // at its neighbours — which it could not do reliably
                        // anyway, and which is where "print the heading twice"
                        // comes from.
                        //
                        // It is what ties a verdict to the data it covers: this
                        // card reports the travel document's passive
                        // authentication and an annex's integrity-only result in
                        // one read, and unheaded they read as one guarantee.
                        // Kirigami.Heading, as every other section header in
                        // this package: it takes its size and weight from the
                        // theme's heading scale. Hand-setting font.pointSize
                        // from Kirigami.Theme.smallFont would read back -1 on a
                        // theme defined in pixelSize, warn, and be ignored —
                        // leaving the heading at body size.
                        Kirigami.Heading {
                            // Full opacity and a large top margin, unlike the
                            // 0.7 field labels below: dimmed at label opacity
                            // the heading read as just another row.
                            level: 4
                            visible: detailRow.modelData.groupHeading !== undefined
                                     && detailRow.modelData.groupHeading !== ""
                            Layout.fillWidth: true
                            Layout.topMargin: Kirigami.Units.largeSpacing
                            text: detailRow.modelData.groupHeading ?? ""
                            // Group headings come from this host's own catalog,
                            // never the card — but PlainText throughout the
                            // package is the uniform hardening rule.
                            textFormat: Text.PlainText
                            // Wrap instead of forcing the popup wider on a
                            // long heading at the narrow plasmoid width.
                            wrapMode: Text.WordWrap
                            Accessible.role: Accessible.Heading
                            Accessible.name: text
                        }

                        RowLayout {
                        Layout.fillWidth: true
                        PlasmaComponents.Label {
                            // Raw card/agent bytes — PlainText, as in the summary rows.
                            text: detailRow.modelData.label
                            textFormat: Text.PlainText
                            opacity: 0.7
                        }
                        PlasmaComponents.Label {
                            text: detailRow.modelData.value
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                            wrapMode: Text.WordWrap
                        }
                        PlasmaComponents.ToolButton {
                            icon.name: "edit-copy"
                            onClicked: identityView.smartCard.copyField(detailRow.modelData.value)
                            // Attached ToolTips never show by themselves: visible
                            // (+ the standard delay) must be driven explicitly.
                            PlasmaComponents.ToolTip.text: i18nc("@info:tooltip copy this field to the clipboard", "Copy to clipboard")
                            PlasmaComponents.ToolTip.visible: hovered
                            PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay
                            Accessible.role: Accessible.Button
                            Accessible.name: i18nc("@info:tooltip copy this field to the clipboard", "Copy to clipboard")
                        }
                        }
                    }
                }
            }
        }
    }
}
