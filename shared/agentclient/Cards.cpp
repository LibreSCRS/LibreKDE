// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#include "Cards.h"

#include <LibreSCRS/AgentClient/AgentCapabilities.h> // capabilityBits(), has()
#include <LibreSCRS/AgentClient/AgentReader.h>

namespace LibreKDE::Cards {

bool hasCapability(const LibreSCRS::AgentClient::AgentCard& card, std::uint32_t cap)
{
    return LibreSCRS::AgentClient::has(LibreSCRS::AgentClient::capabilityBits(card.capabilities()), cap);
}

LibreSCRS::AgentClient::AgentCard* firstWithCapability(LibreSCRS::AgentClient::AgentClient& client, std::uint32_t cap)
{
    for (LibreSCRS::AgentClient::AgentReader* reader : client.readers()) {
        if (reader == nullptr) {
            continue;
        }
        LibreSCRS::AgentClient::AgentCard* card = reader->card();
        if (card != nullptr && hasCapability(*card, cap)) {
            return card;
        }
    }
    return nullptr;
}

} // namespace LibreKDE::Cards
