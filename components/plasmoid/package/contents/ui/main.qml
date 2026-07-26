// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami
import org.librescrs.smartcard

PlasmoidItem {
    id: root

    // The per-WIDGET controller. Plasma 6 hosts every applet instance in ONE
    // shared QQmlEngine, so this must be a plain instantiated type — an
    // engine-scoped QML singleton would alias all widget instances onto one
    // handler and break the per-widget reader binding (two widgets
    // bound to different readers must render different cards concurrently).
    // Process-wide state stays shared INSIDE the handler (sharedAgentClient /
    // sharedCardPhotoStore).
    //
    // boundReaderName is driven one-way from the persisted per-widget config:
    // "" = Auto (follow the active card). The config dialog writes
    // Plasmoid.configuration; the handler only reads the value here.
    readonly property SmartCard smartCard: SmartCard {
        boundReaderName: Plasmoid.configuration.boundReaderName

        // The "free read" ON FIRST VIEW: a no-secret (PreReadAuth None)
        // identity card auto-populates when the popup opens — or when such a
        // card lands / gets chip-selected / is swapped in while it is open —
        // never merely at insertion, so identity+photo PII is not read for
        // widgets nobody opens. The handler drives the read internally off
        // this flag + every active-card (re)classification, which also covers
        // switches between two SAME-classification cards (no stateChanged to
        // hook). Can/Mrz cards are never touched by this (lazy
        // card-I/O invariant).
        viewActive: root.expanded
    }

    // Localized, state-aware status line: the panel hover tooltip's subtext
    // (stock Plasma applets surface their state here; the main text
    // stays the default applet title) and the compact icon's accessible
    // description.
    readonly property string statusText: {
        switch (root.smartCard.state) {
        case 0: return root.smartCard.cardDetected
                       ? i18nc("@info:status tray icon", "Detecting a smart card…")
                       : i18nc("@info:status tray icon", "No smart card inserted");
        case 1: return i18nc("@info:status tray icon", "Smart card must be unlocked");
        case 2: return i18nc("@info:status tray icon", "Identity card ready");
        case 3: return i18nc("@info:status tray icon", "PKI token ready");
        case 4: return i18nc("@info:status tray icon", "Identity and PKI card ready");
        case 6: return i18nc("@info:status tray icon", "Unrecognized smart card");
        case 7: return i18nc("@info:status tray icon", "Smart card service unavailable");
        default: return i18nc("@info:status tray icon", "Smart card could not be read");
        }
    }
    toolTipSubText: statusText

    compactRepresentation: CompactRepresentation {
        smartCard: root.smartCard
        statusText: root.statusText
    }

    fullRepresentation: Loader {
        Layout.minimumWidth: Kirigami.Units.gridUnit * 22
        Layout.minimumHeight: Kirigami.Units.gridUnit * 14
        // A bounded popup size (plasmashell sizes the dialog from the
        // preferred hints): the expanded identity view SCROLLS inside this
        // height (IdentityView's ScrollView) instead of growing the popup
        // past the panel edge on a real card's ~25-30 rows.
        Layout.preferredWidth: Kirigami.Units.gridUnit * 24
        Layout.preferredHeight: Kirigami.Units.gridUnit * 26

        // Auto mode holding two or more cards -> master-detail; a single card
        // (or a widget bound to one reader) collapses straight to the detail
        // The capability-state switch itself (incl. UnknownCard /
        // AgentUnavailable + the state fade) lives in CardDetail.qml.
        sourceComponent: (Plasmoid.configuration.boundReaderName === ""
                          && root.smartCard.readersWithCards.length >= 2)
                         ? multiCardComp : detailComp

        // The same fade-in on the master-detail <-> single-detail swap.
        onLoaded: viewFade.reveal(item)
        FadeIn { id: viewFade }

        Component { id: detailComp;    CardDetail     { smartCard: root.smartCard } }
        Component { id: multiCardComp; MultiCardState { smartCard: root.smartCard } }
    }
}
