// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#include "Readers.h"

#include <KLocalizedString>

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

QString readerLabel(const QString& model, LibreSCRS::AgentClient::ReaderInterface iface)
{
    using LibreSCRS::AgentClient::ReaderInterface;
    switch (iface) {
    case ReaderInterface::Contact:
        return ki18ndc("librekde", "@item reader label: <model> on the contact interface", "%1 — contact")
            .subs(model)
            .toString();
    case ReaderInterface::Contactless:
        return ki18ndc("librekde", "@item reader label: <model> on the contactless interface", "%1 — contactless")
            .subs(model)
            .toString();
    case ReaderInterface::Unknown:
        break;
    }
    return model;
}

QString readerLabel(const LibreSCRS::AgentClient::AgentReader& reader)
{
    return readerLabel(reader.model(), reader.interface());
}

} // namespace LibreKDE::Readers
