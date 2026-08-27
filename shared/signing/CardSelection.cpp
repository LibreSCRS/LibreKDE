// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "CardSelection.h"

#include "Cards.h" // LibreKDE::Cards::cardsWithCapability

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include <LibreSCRS/AgentClient/AgentReader.h>

#include <algorithm>

namespace Client = LibreSCRS::AgentClient;

namespace LibreKDE::Signing {

SigningCardSelection chooseSigningCard(Client::AgentClient& client, const LibreKDE::CardChooser& chooser)
{
    // NO AgentCard* may outlive this block, which is why the candidate list is
    // scoped to it rather than kept alongside `choices`. The chooser is a MODAL
    // dialog in production, and a modal dialog runs a NESTED event loop: every
    // signal the agent sends while it is up is delivered inside that loop,
    // including a card removal, which the client services by DELETING the
    // AgentCard on the spot. A pointer captured out here would be freed memory
    // by the time the dialog closes, and no guard downstream can recover — a
    // QPointer built from an already-dangling pointer never becomes null, so
    // SignJob's own removal check would wave it straight through. Values cross
    // the dialog; the answer is re-resolved against the live registry below.
    QList<LibreKDE::CardChoice> choices;
    {
        const QList<Client::AgentCard*> candidates = Cards::cardsWithCapability(client, Client::Cap::Pki);
        if (candidates.size() <= 1) {
            // Zero or one: nothing to disambiguate, so nothing is asked. No
            // dialog runs, so nothing can delete this card behind our back and
            // returning it directly is safe. value(0) covers both — an empty
            // list yields nullptr.
            return {candidates.value(0), false};
        }
        if (!chooser) {
            return {nullptr, true};
        }

        choices.reserve(candidates.size());
        for (Client::AgentCard* card : candidates) {
            // The reader NAME is the discriminator a person can act on ("the one
            // on the left"); the card id is an opaque token and the card type is
            // empty until a read resolves it. Resolved here rather than in the
            // dialog so the chooser is handed values and never a live card proxy.
            Client::AgentReader* reader = client.reader(card->readerId());
            choices << LibreKDE::CardChoice{card->id(), reader != nullptr ? reader->name() : QString(),
                                            card->cardType()};
        }
    }

    const std::optional<QString> chosen = chooser(choices);
    if (!chosen.has_value()) {
        return {nullptr, true};
    }
    // Re-resolve through the registry as it stands NOW, not as it was before the
    // dialog opened: the chosen card may have been pulled while the user was
    // looking at it, in which case the client has already dropped it and this
    // yields nullptr. The capability is re-checked for the same reason — a card
    // that lost Pki mid-dialog is no longer an answer to the question asked.
    Client::AgentCard* card = client.card(*chosen);
    if (card == nullptr || !Cards::hasCapability(*card, Client::Cap::Pki)) {
        // Also the "id naming no candidate" case — a chooser bug. Both report a
        // null card WITHOUT `cancelled`: the user did not decline, so this is
        // not a cancellation, and signing some other card instead would sign
        // with one they did not pick.
        return {nullptr, false};
    }
    // The id alone is not enough: it is an object path minted from a
    // PER-PROCESS counter, so an agent restart while the dialog was up
    // re-mints ids from zero and the chosen id can resolve a LIVE card in a
    // different reader. The reader name in `choices` is what the person
    // actually picked by; a mismatch means the id no longer names their
    // choice — refuse, same as above, rather than sign with an identity they
    // did not pick.
    const auto choiceIt = std::find_if(choices.cbegin(), choices.cend(),
                                       [&](const LibreKDE::CardChoice& c) { return c.cardId == *chosen; });
    Client::AgentReader* liveReader = client.reader(card->readerId());
    const QString liveReaderName = liveReader != nullptr ? liveReader->name() : QString();
    if (choiceIt == choices.cend() || liveReaderName != choiceIt->readerName) {
        return {nullptr, false};
    }
    return {card, false};
}

} // namespace LibreKDE::Signing
