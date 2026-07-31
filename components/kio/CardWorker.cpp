// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "CardWorker.h"

#include "AgentCardDataSource.h"

#include <LibreSCRS/AgentClient/AgentClient.h>

#include <KIO/WorkerFactory>
#include <KPluginFactory>

#include <QCoreApplication>

#include <cstdio>

using namespace KIO;

namespace LibreKDE {

CardWorkerOwnership::CardWorkerOwnership()
    : m_client(std::make_unique<LibreSCRS::AgentClient::AgentClient>()),
      m_source(std::make_unique<AgentCardDataSource>(*m_client))
{}

CardWorkerOwnership::~CardWorkerOwnership() = default;

CardDataSource& CardWorkerOwnership::source() const
{
    return *m_source;
}

CardWorker::CardWorker(const QByteArray& poolSocket, const QByteArray& appSocket)
    : WorkerBase("card", poolSocket, appSocket), CardWorkerOwnership(), CardWorkerLogic(source())
{}

CardWorker::~CardWorker() = default;

void CardWorker::emitListEntry(const UDSEntry& entry)
{
    listEntry(entry);
}
void CardWorker::emitStatEntry(const UDSEntry& entry)
{
    statEntry(entry);
}
void CardWorker::emitMimeType(const QString& mime)
{
    mimeType(mime);
}
void CardWorker::emitData(const QByteArray& bytes)
{
    data(bytes);
}

WorkerResult CardWorker::listDir(const QUrl& url)
{
    return doListDir(url);
}
WorkerResult CardWorker::stat(const QUrl& url)
{
    return doStat(url);
}
WorkerResult CardWorker::mimetype(const QUrl& url)
{
    return doMimetype(url);
}
WorkerResult CardWorker::get(const QUrl& url)
{
    return doGet(url);
}

} // namespace LibreKDE

// KIO 6 worker registration: a KIO::WorkerFactory plugin whose embedded JSON
// (card.json) carries the protocol descriptor under "KDE-KIO-Protocols" — KF6
// no longer reads a separate .protocol file. KIO dlopens the plugin into its
// own process and calls createWorker() to drive the `card` scheme.
class CardWorkerFactory : public KIO::WorkerFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kio.worker.card" FILE "card.json")

public:
    explicit CardWorkerFactory(QObject* parent = nullptr) : KIO::WorkerFactory(parent) {}

    std::unique_ptr<KIO::WorkerBase> createWorker(const QByteArray& pool, const QByteArray& app) override
    {
        return std::make_unique<LibreKDE::CardWorker>(pool, app);
    }
};

// Out-of-process entry point. KIO 6's kioworker helper dlopens the worker lib and
// resolves `kdemain` — every in-tree KF6 worker (e.g. kio_file) exports it
// alongside the WorkerFactory. Without it, kioworker rejects the plugin with
// "Could not find kdemain" and the `card` scheme falls back to a legacy exec that
// never answers.
extern "C" Q_DECL_EXPORT int kdemain(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kio_card"));

    if (argc != 4) {
        std::fprintf(stderr, "Usage: kio_card protocol pool app\n");
        return -1;
    }

    LibreKDE::CardWorker worker(argv[2], argv[3]);
    worker.dispatchLoop();
    return 0;
}

#include "CardWorker.moc"
