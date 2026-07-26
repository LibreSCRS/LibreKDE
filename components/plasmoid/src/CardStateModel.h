// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "AgentCapabilities.h"

#include <QObject>

#include <cstdint>

namespace LibreKDE::Plasmoid {

/// Capability → UI-state classifier. Pure functions isolated from
/// `SmartCardHandler` so they're unit-testable without standing up an
/// `AgentClient` / D-Bus connection.
///
/// The state values are wire-stable: `main.qml` switches on the integer
/// `SmartCard.state` (0 NoCard … 7 AgentUnavailable), so the enumerator ORDER
/// must not change without updating the QML `Loader`.
class CardStateModel
{
    Q_GADGET
public:
    /// UI states; mapped 1:1 onto the QML state components under
    /// `contents/ui/<State>.qml` (and onto `LibreKDE::UiState`).
    enum class State {
        NoCard,           ///< No card present in any monitored reader.        (0)
        PreAuthRequired,  ///< Card present, preReadAuthMethod() != None.      (1)
        IdentityOnly,     ///< capabilities ⊇ IdentityData, NOT Pki.           (2)
        PkiOnly,          ///< capabilities ⊇ Pki, NOT IdentityData.           (3)
        Hybrid,           ///< capabilities ⊇ {IdentityData, Pki}.             (4)
        Error,            ///< Card present, classification/read failed.       (5)
        UnknownCard,      ///< Card present but no plugin matched (caps == 0).  (6)
        AgentUnavailable, ///< The agent is not reachable (client-availability).(7)
    };
    Q_ENUM(State)

    /// Classify a `Card1.Capabilities` bitfield (as carried on the agent's
    /// D-Bus surface, mirrored by `LibreKDE::Cap`). An empty capability set
    /// (`caps == 0`, no plugin matched) maps to `UnknownCard`; an
    /// ancillary-only set (EmrtdCrypto/PinManagement without IdentityData or
    /// Pki) has no plasmoid-visible surface and maps to `Error`.
    [[nodiscard]] static State classify(std::uint32_t capabilities);

    /// Map the agent client's coarse `UiState` onto the plasmoid's `State`.
    /// `UnknownCard` (an empty capability set, produced by `resolveCardState`)
    /// passes through; `None` (an ancillary-only set) collapses to `Error`;
    /// `NoCard`/`PreAuthRequired`/`Error` pass through unchanged.
    [[nodiscard]] static State fromUiState(UiState ui);
};

} // namespace LibreKDE::Plasmoid
