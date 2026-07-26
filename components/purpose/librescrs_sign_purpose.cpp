// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "librescrs_sign_purpose.h"

#include "AgentCapabilities.h"
#include "AgentCard.h"
#include "AgentClient.h"
#include "SignJob.h"
#include "SignSeams.h"
#include "SharedAgentClient.h"

#include <KLocalizedString>
#include <KPluginFactory>

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

namespace LibreKDE::Purpose {

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

    const QUrl url(urls.first().toString());
    if (!url.isLocalFile()) {
        finishWithError(KJob::UserDefinedError, i18nc("@info:status", "Only local files can be signed."));
        return;
    }
    const QString inputPath = url.toLocalFile();
    const QString mimeType = in.value(QStringLiteral("mimeType")).toString();

    m_client = LibreKDE::sharedAgentClient();
    if (!m_client->isAvailable()) {
        finishWithError(KJob::UserDefinedError,
                        i18nc("@info:status", "The LibreSCRS smart-card service is not running."));
        return;
    }

    // The first present PKI-capable card (deterministic, sorted-path) — the
    // centralized selection in AgentClient. Multi-card fan-out beyond "first
    // signing-capable card" is a LibreCelik concern; the Share action signs with
    // the obvious card and defers to the cert chooser for key selection.
    LibreKDE::AgentCard* card = m_client->cardWithCapability(LibreKDE::Cap::Pki);
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
