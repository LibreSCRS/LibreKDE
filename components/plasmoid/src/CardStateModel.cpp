// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "CardStateModel.h"

namespace LibreKDE::Plasmoid {

namespace Client = LibreSCRS::AgentClient;

CardStateModel::State CardStateModel::fromUiState(Client::UiState ui)
{
    switch (ui) {
    case Client::UiState::NoCard:
        return State::NoCard;
    case Client::UiState::PreAuthRequired:
        return State::PreAuthRequired;
    case Client::UiState::IdentityOnly:
        return State::IdentityOnly;
    case Client::UiState::PkiOnly:
        return State::PkiOnly;
    case Client::UiState::Hybrid:
        return State::Hybrid;
    case Client::UiState::UnknownCard:
        return State::UnknownCard;
    case Client::UiState::None:
    case Client::UiState::Error:
        return State::Error;
    }
    return State::Error;
}

CardStateModel::State CardStateModel::classify(std::uint32_t capabilities)
{
    // Delegate to the library's single source of truth rather than re-deriving
    // the grouping + promotion here: a present card with no pre-read unlock and
    // identity not yet read resolves exactly to the coarse capability surface
    // (UnknownCard for an empty set, Error for an ancillary-only one).
    return fromUiState(
        Client::resolveCardState(capabilities, Client::PreReadAuth::None, /*present=*/true, /*identityRead=*/false));
}

} // namespace LibreKDE::Plasmoid
