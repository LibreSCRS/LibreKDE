// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CardDataSource.h"
#include "CardWorkerLogic.h"

#include <KIO/WorkerBase>

#include <QByteArray>
#include <QString>
#include <memory>

/// @file
/// @brief The read-only `card:/` KIO worker: a thin `KIO::WorkerBase` glue over
///        the pure `CardWorkerLogic` (which holds the `CardTree` + renderers and
///        the `CardDataSource`). `listDir`/`stat`/`mimetype` do zero card I/O;
///        only `get()` of a leaf (and `listDir` of `PKI/`) reads the card —
///        lazy PACE.

namespace LibreKDE {

class AgentClient;

/// @brief Owns the worker's `AgentClient` + agent-backed `CardDataSource`. A
///        base class (listed BEFORE `CardWorkerLogic`) so the source is alive
///        when `CardWorkerLogic`'s ctor binds its reference — base subobjects
///        are constructed in declaration order.
class CardWorkerOwnership
{
public:
    CardWorkerOwnership();
    ~CardWorkerOwnership();

protected:
    [[nodiscard]] CardDataSource& source() const;

private:
    std::unique_ptr<AgentClient> m_client;
    std::unique_ptr<CardDataSource> m_source;
};

/// @brief The KIO worker plugin entry: glues `CardWorkerLogic` to a live KIO
///        process, owning its own `AgentClient` + `AgentCardDataSource`.
class CardWorker : public KIO::WorkerBase, private CardWorkerOwnership, public CardWorkerLogic
{
public:
    CardWorker(const QByteArray& poolSocket, const QByteArray& appSocket);
    ~CardWorker() override;

    KIO::WorkerResult listDir(const QUrl& url) override;
    KIO::WorkerResult stat(const QUrl& url) override;
    KIO::WorkerResult mimetype(const QUrl& url) override;
    KIO::WorkerResult get(const QUrl& url) override;

protected:
    void emitListEntry(const KIO::UDSEntry& entry) override;
    void emitStatEntry(const KIO::UDSEntry& entry) override;
    void emitMimeType(const QString& mime) override;
    void emitData(const QByteArray& bytes) override;
};

} // namespace LibreKDE
