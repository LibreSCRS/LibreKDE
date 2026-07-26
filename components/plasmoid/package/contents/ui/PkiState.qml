// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.librescrs.smartcard

// PKI-only token (PIV, PKCS#15 CardEdge without an eID applet): no identity
// fields, so the body would be empty. It is filled by a descriptive
// PlaceholderMessage (what the token IS) that carries the "Open in LibreCelik"
// launch button as its helpfulAction — the footer's own LibreCelik icon hides
// in this state, so each action has exactly ONE control. Sign and Manage
// credentials live in the shared CardActionBar footer below the state Loader.
// The placeholder text is deliberately non-imperative so it does not duplicate
// the footer's Sign button.
ColumnLayout {
    id: pkiRoot

    required property SmartCard smartCard

    spacing: Kirigami.Units.largeSpacing

    Kirigami.Heading {
        level: 3
        text: pkiRoot.smartCard.cardLabel.length > 0
              ? pkiRoot.smartCard.cardLabel
              : i18nc("@title plasmoid pki-card section header generic", "PKI token")
        // cardLabel is card/agent-derived: never AutoText (hostile-card hardening).
        textFormat: Text.PlainText
        Layout.alignment: Qt.AlignHCenter
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    // Spacers above/below vertically center the placeholder in the popup body
    // (PlaceholderMessage is itself a top-aligned ColumnLayout, so `fillHeight`
    // alone would not center it).
    Item { Layout.fillWidth: true; Layout.fillHeight: true }

    Kirigami.PlaceholderMessage {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
        // Certificate glyph — verified present in Breeze.
        icon.name: "application-certificate"
        // Generic (not card-derived) title, so no hostile-text concern here.
        text: i18nc("@info plasmoid pki-card placeholder title", "Signing token")
        // No imperative LibreCelik mention here: when LibreCelik is installed the
        // helpfulAction below is a REAL launch button (dead text helps no one);
        // when it is not installed there is nothing to open, so we do not dangle
        // its name.
        explanation: i18nc("@info plasmoid pki-card placeholder explanation",
                           "This card has no identity data.")
        // A proper launch button for the sparse PKI view — shown only when
        // LibreCelik is actually resolvable (see SmartCardHandler::locateLibreCelik).
        helpfulAction: pkiRoot.smartCard.libreCelikAvailable ? openInLibreCelikAction : null
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.description: explanation
    }

    // Non-visual: the placeholder's launch action, wired to the same invokable
    // the footer uses. Declared once here so helpfulAction can reference it.
    Kirigami.Action {
        id: openInLibreCelikAction
        icon.name: "document-open"
        text: i18nc("@action:button open card in LibreCelik", "Open in LibreCelik")
        onTriggered: pkiRoot.smartCard.openInLibreCelik()
    }

    Item { Layout.fillWidth: true; Layout.fillHeight: true }
}
