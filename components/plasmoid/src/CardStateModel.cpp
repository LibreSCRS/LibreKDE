// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "CardStateModel.h"

namespace LibreKDE::Plasmoid {

CardStateModel::State CardStateModel::fromUiState(UiState ui)
{
    switch (ui) {
    case UiState::NoCard:
        return State::NoCard;
    case UiState::PreAuthRequired:
        return State::PreAuthRequired;
    case UiState::IdentityOnly:
        return State::IdentityOnly;
    case UiState::PkiOnly:
        return State::PkiOnly;
    case UiState::Hybrid:
        return State::Hybrid;
    case UiState::UnknownCard:
        return State::UnknownCard;
    case UiState::None:
    case UiState::Error:
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
    return fromUiState(resolveCardState(capabilities, PreReadAuth::None, /*present=*/true, /*identityRead=*/false));
}

} // namespace LibreKDE::Plasmoid
