// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#include "Readers.h"

namespace LibreKDE::Readers {

LibreSCRS::AgentClient::AgentReader* firstReaderWithCard(LibreSCRS::AgentClient::AgentClient& client)
{
    for (LibreSCRS::AgentClient::AgentReader* reader : client.readers()) {
        if (reader != nullptr && reader->card() != nullptr) {
            return reader;
        }
    }
    return nullptr;
}

} // namespace LibreKDE::Readers
