// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CertSelector.h" // LibreKDE::CardChooser

#include <LibreSCRS/AgentClient/AgentCard.h>
#include <LibreSCRS/AgentClient/AgentClient.h>

/// @file
/// @brief Which card a sign flow uses, when the desk holds more than one that
///        could serve. The policy, separated from the dialog that implements
///        the asking (SignSeams) so it can be driven headlessly.

namespace LibreKDE::Signing {

/// @brief The outcome of resolving which card signs.
struct SigningCardSelection
{
    /// The card to sign with; null when none was resolved.
    LibreSCRS::AgentClient::AgentCard* card = nullptr;
    /// True only when the user was ASKED and declined. "You have no card that
    /// can sign" and "you changed your mind" are different things to tell
    /// someone, and a bare null pointer cannot tell them apart.
    bool cancelled = false;
};

/// @brief The card to sign with, asking @p chooser only when the answer is
///        genuinely ambiguous.
///
/// The rule, in full:
///   - no signing-capable card present → null card, @p chooser NOT called;
///   - exactly one → that card, @p chooser NOT called;
///   - more than one → @p chooser is called with all of them, in the client's
///     deterministic reader order, and the card whose id it returns is the
///     result. A declined choice reports `cancelled`; an id naming no card
///     yields a null card rather than a silent substitution.
///
/// @p chooser is assumed to be MODAL, and therefore to run a nested event loop
/// in which the card registry can change under it. Only values are handed to it,
/// and its answer is resolved against the registry as it stands when it returns
/// — so a card pulled out of its reader while the dialog was open comes back as
/// a null card (NOT a cancellation: the user chose, the card left) rather than
/// as a pointer to a card the client has already deleted.
///
/// The two silent cases are the point, not an optimisation: a desk with a
/// single card must never gain a dialog whose only answer is the card already
/// in the reader. The same rule already governs the certificate chooser one
/// step later in the flow.
///
/// An empty @p chooser (a caller with no way to ask) counts as a declined
/// choice in the ambiguous case rather than silently picking one: guessing
/// between two cards is exactly what this function exists to stop.
[[nodiscard]] SigningCardSelection chooseSigningCard(LibreSCRS::AgentClient::AgentClient& client,
                                                     const LibreKDE::CardChooser& chooser);

} // namespace LibreKDE::Signing
