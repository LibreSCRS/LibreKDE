// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CardDataSource.h"

#include <QString>

/// @file
/// @brief The single I/O-bearing `CardDataSource`, over `AgentClient`. KIO-free:
///        it drives an `AgentCard` operation to its terminal state with a scoped
///        `QEventLoop` (the KIO worker has its own per-call event loop), then
///        flattens the typed result into the plain view structs. On a PACE card
///        the read triggers the agent's own CAN prompter — this client never
///        collects secrets.

namespace LibreKDE {

class AgentClient;

class AgentCardDataSource : public CardDataSource
{
public:
    /// @brief Borrow a live `AgentClient` (owned by the worker). Does no I/O at
    ///        construction. Uses the production op-stall backstop (kOpStallTimeoutMs).
    explicit AgentCardDataSource(AgentClient& client);
    /// @brief Test seam: inject a short op-stall backstop so a machine-phase stall
    ///        surfaces fast (a real read never trips the production 35 s backstop).
    AgentCardDataSource(AgentClient& client, int opStallTimeoutMs);
    ~AgentCardDataSource() override;

    [[nodiscard]] QList<CardPresence> listReadersWithCards() override;
    [[nodiscard]] IdentityResult readIdentity(const QString& cardPath) override;
    [[nodiscard]] CertListResult readCertificates(const QString& cardPath) override;
    [[nodiscard]] PhotoResult getPhoto(const QString& cardPath) override;
    [[nodiscard]] CertDerResult getCertificateDer(const QString& cardPath, const QString& certId) override;

private:
    AgentClient& m_client;
    int m_opStallTimeoutMs;
};

} // namespace LibreKDE
