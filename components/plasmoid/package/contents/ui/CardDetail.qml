// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.librescrs.smartcard

// The single-card detail view: the capability-driven state switch (extracted
// from main.qml so BOTH the collapsed single-card case and the master-detail's
// detail pane reuse the SAME state components), plus the shared, capability-gated
// "Manage credentials…" affordance below it. PIN management is ancillary — it is
// never its OWN card state, so it rides on whichever present-card surface loaded
// (PkiOnly / IdentityOnly / Hybrid, plus Error while the card stays bound) and
// lives here in the shared container rather than being duplicated across each
// state component.
ColumnLayout {
    id: detailRoot

    // This widget's own controller instance, handed down from the PlasmoidItem
    // root (per-widget state; see main.qml).
    required property SmartCard smartCard

    spacing: Kirigami.Units.largeSpacing

    Loader {
        id: stateLoader

        Layout.fillWidth: true
        Layout.fillHeight: true

        // smartCard.state is an int Q_PROPERTY mapped from CardStateModel::State:
        //   0 NoCard, 1 PreAuthRequired, 2 IdentityOnly, 3 PkiOnly, 4 Hybrid,
        //   5 Error, 6 UnknownCard, 7 AgentUnavailable
        sourceComponent: {
            switch (detailRoot.smartCard.state) {
            case 0: return noCardComp;
            case 1: return preAuthComp;
            case 2: return identityComp;
            case 3: return pkiComp;
            case 4: return hybridComp;
            case 6: return unknownCardComp;
            case 7: return agentUnavailableComp;
            default: return errorComp;
            }
        }

        // Smooth, Breeze-bounded fade-in on each state swap.
        onLoaded: stateFade.reveal(item)
        FadeIn { id: stateFade }

        Component { id: noCardComp;           NoCardState           { smartCard: detailRoot.smartCard } }
        Component { id: preAuthComp;          PreAuthState          { smartCard: detailRoot.smartCard } }
        Component { id: identityComp;         IdentityState         { smartCard: detailRoot.smartCard } }
        Component { id: pkiComp;              PkiState              { smartCard: detailRoot.smartCard } }
        Component { id: hybridComp;           HybridState           { smartCard: detailRoot.smartCard } }
        Component { id: errorComp;            ErrorState            { smartCard: detailRoot.smartCard } }
        Component { id: unknownCardComp;      UnknownCardState      { smartCard: detailRoot.smartCard } }
        Component { id: agentUnavailableComp; AgentUnavailableState { smartCard: detailRoot.smartCard } }
    }

    // The shared, capability-gated action footer for every present-card state.
    // Self-hides when no action applies (PreAuth / card-less), so it is safe to
    // place unconditionally here below the state Loader.
    CardActionBar {
        smartCard: detailRoot.smartCard
        Layout.fillWidth: true
    }
}
