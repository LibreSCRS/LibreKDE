// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CardDataSource.h"

#include <QString>

/// @file
/// @brief The single I/O-bearing `CardDataSource`, over the shared agent client
///        library. KIO-free: it drives an agent operation to its terminal state
///        with a scoped `QEventLoop` (the KIO worker process runs a
///        `QCoreApplication`, so there is an outer loop to nest inside), then
///        flattens the typed result into the plain view structs. On a PACE card
///        the read triggers the agent's own CAN prompter — this client never
///        collects secrets.

namespace LibreSCRS::AgentClient {
class AgentClient;
}

namespace LibreKDE {

class AgentCardDataSource : public CardDataSource
{
public:
    /// @brief Borrow a live client (owned by the worker). Does no I/O at
    ///        construction. Uses the production op-stall backstop, seeded from
    ///        the client library's long-operation budget.
    explicit AgentCardDataSource(LibreSCRS::AgentClient::AgentClient& client);
    /// @brief Test seam: inject a short op-stall backstop so a machine-phase stall
    ///        surfaces fast (a real read never trips the production 35 s backstop).
    AgentCardDataSource(LibreSCRS::AgentClient::AgentClient& client, int opStallTimeoutMs);
    ~AgentCardDataSource() override;

    [[nodiscard]] QList<CardPresence> listReadersWithCards() override;
    [[nodiscard]] IdentityResult readIdentity(const QString& cardId) override;
    [[nodiscard]] CertListResult readCertificates(const QString& cardId) override;
    [[nodiscard]] PhotoResult getPhoto(const QString& cardId) override;
    [[nodiscard]] CertDerResult getCertificateDer(const QString& cardId, const QString& certId) override;

private:
    LibreSCRS::AgentClient::AgentClient& m_client;
    int m_opStallTimeoutMs;
};

} // namespace LibreKDE
