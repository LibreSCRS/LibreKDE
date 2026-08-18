// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "CardSelection.h"

#include "Cards.h" // LibreKDE::Cards::cardsWithCapability

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include <LibreSCRS/AgentClient/AgentReader.h>

namespace Client = LibreSCRS::AgentClient;

namespace LibreKDE::Signing {

SigningCardSelection chooseSigningCard(Client::AgentClient& client, const LibreKDE::CardChooser& chooser)
{
    const QList<Client::AgentCard*> candidates = Cards::cardsWithCapability(client, Client::Cap::Pki);
    if (candidates.size() <= 1) {
        // Zero or one: nothing to disambiguate, so nothing is asked. value(0)
        // covers both — an empty list yields nullptr.
        return {candidates.value(0), false};
    }
    if (!chooser) {
        return {nullptr, true};
    }

    QList<LibreKDE::CardChoice> choices;
    choices.reserve(candidates.size());
    for (Client::AgentCard* card : candidates) {
        // The reader NAME is the discriminator a person can act on ("the one on
        // the left"); the card id is an opaque token and the card type is empty
        // until a read resolves it. Resolved here rather than in the dialog so
        // the chooser is handed values and never a live card proxy.
        Client::AgentReader* reader = client.reader(card->readerId());
        choices << LibreKDE::CardChoice{card->id(), reader != nullptr ? reader->name() : QString(), card->cardType()};
    }

    const std::optional<QString> chosen = chooser(choices);
    if (!chosen.has_value()) {
        return {nullptr, true};
    }
    for (Client::AgentCard* card : candidates) {
        if (card->id() == *chosen) {
            return {card, false};
        }
    }
    // An id naming no candidate is a chooser bug, not a selection: signing the
    // "first" card instead would sign with a card the user did not pick.
    return {nullptr, false};
}

} // namespace LibreKDE::Signing
