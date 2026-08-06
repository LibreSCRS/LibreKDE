// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "librescrs_sign_purpose.h"

#include "SignJob.h"
#include "SignSeams.h"

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include <LibreSCRS/AgentClient/AgentCard.h>
#include <LibreSCRS/AgentClient/AgentClient.h>
#include <LibreSCRS/AgentClient/AgentReader.h>
#include <LibreSCRS/AgentClient/SharedAgentClient.h>

#include <KLocalizedString>
#include <KPluginFactory>

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

#include <cstdint>

// Short local spelling for the agent client library, matching the other
// LibreKDE surfaces that consume it.
namespace Client = LibreSCRS::AgentClient;

namespace LibreKDE::Purpose {

namespace {

/// @brief The first present card advertising @p cap, over the client's own
///        deterministic `readers()` view.
///
/// A pure function of that list, so the client library does not carry it; this
/// is the one finder this plugin needs, re-added locally exactly as the other
/// consuming surfaces do. The capability set arrives as stable TOKENS, so it is
/// round-tripped through `capabilityBits()` — the exact inverse of the
/// tokenizer, lossless even for a bit this build has no name for.
[[nodiscard]] Client::AgentCard* cardWithCapability(Client::AgentClient& client, std::uint32_t cap)
{
    for (Client::AgentReader* reader : client.readers()) {
        if (reader == nullptr) {
            continue;
        }
        Client::AgentCard* card = reader->card();
        if (card != nullptr && Client::has(Client::capabilityBits(card->capabilities()), cap)) {
            return card;
        }
    }
    return nullptr;
}

} // namespace

SignPurposeJob::SignPurposeJob(QObject* parent) : ::Purpose::Job(parent) {}

SignPurposeJob::~SignPurposeJob() = default;

void SignPurposeJob::finishWithError(int code, const QString& message)
{
    setError(code);
    setErrorText(message);
    emitResult();
}

void SignPurposeJob::start()
{
    const QJsonObject in = data();
    const QJsonArray urls = in.value(QStringLiteral("urls")).toArray();
    if (urls.isEmpty()) {
        finishWithError(KJob::UserDefinedError, i18nc("@info:status", "No file was selected to sign."));
        return;
    }
    // One signature, one artifact, one cert prompt: this action signs a single
    // file. A multi-file share must fail loudly here — silently signing only
    // the first selection would misreport what happened to the rest.
    if (urls.size() > 1) {
        finishWithError(KJob::UserDefinedError,
                        i18ncp("@info:status", "Select a single file to sign — %1 file was shared.",
                               "Select a single file to sign — %1 files were shared.", urls.size()));
        return;
    }

    const QUrl url(urls.first().toString());
    if (!url.isLocalFile()) {
        finishWithError(KJob::UserDefinedError, i18nc("@info:status", "Only local files can be signed."));
        return;
    }
    const QString inputPath = url.toLocalFile();
    const QString mimeType = in.value(QStringLiteral("mimeType")).toString();

    m_client = Client::sharedAgentClient();
    if (!m_client->isAvailable()) {
        finishWithError(KJob::UserDefinedError,
                        i18nc("@info:status", "The LibreSCRS smart-card service is not running."));
        return;
    }

    // The first present PKI-capable card, over the client's deterministic reader
    // view. Multi-card fan-out beyond "first signing-capable card" is a
    // LibreCelik concern; the Share action signs with the obvious card and
    // defers to the cert chooser for key selection.
    Client::AgentCard* card = cardWithCapability(*m_client, Client::Cap::Pki);
    if (card == nullptr) {
        finishWithError(KJob::UserDefinedError, i18nc("@info:status", "Insert a smart card that supports signing."));
        return;
    }

    m_signJob = new LibreKDE::SignJob(card, inputPath, mimeType, QString(), LibreKDE::Signing::widgetCertChooser(),
                                      LibreKDE::Signing::widgetOverwriteConfirmer(), this);
    connect(m_signJob, &LibreKDE::SignJob::succeeded, this, [this](const QString& outputPath) {
        QJsonObject out;
        out.insert(QStringLiteral("url"), QUrl::fromLocalFile(outputPath).toString());
        setOutput(out);
        emitResult();
    });
    connect(m_signJob, &LibreKDE::SignJob::failed, this,
            [this](const QString& message) { finishWithError(KJob::UserDefinedError, message); });
    m_signJob->start();
}

::Purpose::Job* SignPurposePlugin::createJob() const
{
    return new SignPurposeJob();
}

} // namespace LibreKDE::Purpose

K_PLUGIN_CLASS_WITH_JSON(LibreKDE::Purpose::SignPurposePlugin, "librescrs_sign_purpose.json")

#include "librescrs_sign_purpose.moc"
