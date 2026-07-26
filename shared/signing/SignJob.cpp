// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "SignJob.h"

#include "AgentCapabilities.h"
#include "AgentCard.h"
#include "AgentOperation.h"
#include "ErrorText.h"
#include "signing_log_categories.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QPointer>
#include <QSaveFile>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace LibreKDE {

struct SignJob::Private
{
    // Held as a QPointer: makeCertChooser() spins a modal nested event loop, during
    // which AgentClient::onInterfacesRemoved may delete the AgentCard if the card is
    // pulled. A raw pointer would then dangle when the queued beginSign() runs;
    // QPointer auto-nulls so beginSign() can re-check and fail cleanly.
    QPointer<AgentCard> card;
    QString inputPath;
    QString mimeType;
    QString formatOverride;
    CertChooser chooser;
    OverwriteConfirmer overwriteConfirmer;

    SignChoice choice;
    QString outputPath;
    AgentOperation* certOp = nullptr;
    AgentOperation* signOp = nullptr;
    bool emitted = false;
};

SignJob::SignJob(AgentCard* card, QString inputPath, QString mimeType, QString formatOverride, CertChooser chooser,
                 OverwriteConfirmer overwriteConfirmer, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->card = card;
    d->inputPath = std::move(inputPath);
    d->mimeType = std::move(mimeType);
    d->formatOverride = std::move(formatOverride);
    d->chooser = std::move(chooser);
    d->overwriteConfirmer = std::move(overwriteConfirmer);
}

SignJob::~SignJob() = default;

QString SignJob::outputPath() const
{
    return d->outputPath;
}

void SignJob::fail(const QString& message)
{
    if (d->emitted) {
        return;
    }
    d->emitted = true;
    Q_EMIT failed(message);
}

void SignJob::start()
{
    if (d->card == nullptr) {
        fail(i18nc("@info:status sign failed", "No smart card is available for signing."));
        return;
    }

    // Capability gate: the card must advertise PKI before we even enumerate
    // certs (the agent would reject Sign on a non-PKI card with
    // CapabilityMissing anyway, but failing here is faster and clearer).
    if (!has(d->card->capabilities(), Cap::Pki)) {
        fail(i18nc("@info:status card has no PKI capability", "This card does not support signing."));
        return;
    }

    // Resolve the input MIME (sniff if not supplied) → concrete format/packaging.
    QString mime = d->mimeType;
    if (mime.isEmpty()) {
        QMimeDatabase db;
        mime = db.mimeTypeForFile(d->inputPath).name();
    }
    d->choice = MimeFormatMap::resolve(mime);
    if (!d->formatOverride.isEmpty()) {
        // Per-request override: the agent validates the vocabulary,
        // but we must re-derive `packaging` from the OVERRIDE's format family —
        // not leave the MIME-derived packaging. Otherwise an override (e.g. a PDF
        // MIME → pades/enveloped, overridden to cades) would forward a stale
        // `enveloped` with cades AND mis-name the output. Re-resolving from the
        // override's family keeps format + packaging + output name consistent.
        d->choice = MimeFormatMap::resolveFormat(d->formatOverride);
    }

    // Enumerate the card's signing certificates (capability PKI). The chosen
    // certId is the prerequisite handle for Sign (no auto-select).
    AgentOperation* op = d->card->readCertificates();
    if (op == nullptr) {
        fail(ErrorText::forCode(ErrorCode::CapabilityMissing, QString()));
        return;
    }
    d->certOp = op;
    connect(op, &AgentOperation::finished, this, &SignJob::onCertificatesFinished);
    connect(op, &AgentOperation::phaseChanged, this, &SignJob::phaseChanged);
}

void SignJob::onCertificatesFinished(OperationStatus status, ErrorCode errorCode, const QString& /*msgKey*/,
                                     const QString& msgFallback)
{
    AgentOperation* op = d->certOp;
    d->certOp = nullptr;
    if (op == nullptr) {
        return;
    }
    // Ordering contract: deleteLater() only schedules destruction for the next
    // event-loop turn; the synchronous result() reads below still run against
    // the live object before this slot returns. Safe by construction.
    op->deleteLater();

    if (status != OperationStatus::Ok) {
        // Forward the agent's specific message: ErrorText surfaces it
        // for the generic engine code instead of a hardcoded generic.
        fail(ErrorText::forCode(errorCode, msgFallback));
        return;
    }

    // Filter to signing-capable certs only.
    CertificateList signing;
    for (const CertificateInfo& c : op->certificatesResult()) {
        if (c.signingCapable) {
            signing.append(c);
        }
    }

    if (signing.isEmpty()) {
        // No signing-capable key on this card → the sign-specific CapabilityMissing
        // copy, localized via ki18n. (The generic
        // ErrorText::CapabilityMissing is "the requested operation"; here we know
        // the operation is signing, so we phrase it precisely.)
        fail(i18nc("@info:status no signing certificate on the card", "This card does not support signing."));
        return;
    }

    QString certId;
    if (signing.size() == 1) {
        certId = signing.first().certId; // auto-pick the lone signing cert.
    } else {
        std::optional<QString> chosen = d->chooser ? d->chooser(signing) : std::nullopt;
        if (!chosen.has_value()) {
            fail(i18nc("@info:status user cancelled the certificate chooser", "Signing was cancelled."));
            return;
        }
        certId = chosen.value();
    }

    // Defer the Sign onto a fresh event-loop turn. We are currently inside the
    // cert operation's `finished` slot, which Qt dispatched while the cert op's
    // blocking D-Bus call was unwinding; starting another operation (with its
    // own blocking method call + typed-Result match-rule registration) from
    // inside that nested context races the agent's Result signal against the
    // not-yet-installed match rule. A queued hop guarantees a clean turn.
    QMetaObject::invokeMethod(this, [this, certId]() { beginSign(certId); }, Qt::QueuedConnection);
}

void SignJob::beginSign(const QString& certId)
{
    // Re-check the card: makeCertChooser() may have spun a modal nested event loop
    // (see Private::card), during which the card could have been pulled and the
    // AgentCard deleted by AgentClient::onInterfacesRemoved. QPointer auto-nulls on
    // that delete, so fail cleanly (write nothing) rather than dereference a freed
    // pointer.
    if (d->card.isNull()) {
        fail(i18nc("@info:status sign failed, card removed during selection",
                   "The smart card was removed before signing could start."));
        return;
    }

    // Open the document read-only and hand the fd to the agent (sandbox-friendly).
    // The input is NEVER opened writable here.
    const int fd = ::open(QFile::encodeName(d->inputPath).constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fail(i18nc("@info:status sign failed to open input", "Could not open the file to sign."));
        return;
    }
    QDBusUnixFileDescriptor wrapped(fd);
    ::close(fd); // QDBusUnixFileDescriptor dup'd on construction.

    QVariantMap options;
    options.insert(QStringLiteral("format"), d->choice.format);
    options.insert(QStringLiteral("packaging"), d->choice.packaging);
    // level/TSA/trust are the agent's Config1 defaults — not forwarded here.

    AgentOperation* op = d->card->sign(certId, wrapped, options);
    if (op == nullptr) {
        // Method threw at entry (UnsupportedOnThisCard / UnsupportedSignatureParameter).
        fail(ErrorText::forCode(ErrorCode::CapabilityMissing, QString()));
        return;
    }
    d->signOp = op;
    connect(op, &AgentOperation::finished, this, &SignJob::onSignFinished);
    connect(op, &AgentOperation::phaseChanged, this, &SignJob::phaseChanged);
}

void SignJob::onSignFinished(OperationStatus status, ErrorCode errorCode, const QString& /*msgKey*/,
                             const QString& msgFallback)
{
    AgentOperation* op = d->signOp;
    d->signOp = nullptr;
    if (op == nullptr) {
        return;
    }
    // Ordering contract: deleteLater() only schedules destruction for the next
    // event-loop turn; the synchronous result() reads below still run against
    // the live object before this slot returns. Safe by construction.
    op->deleteLater();

    if (status != OperationStatus::Ok) {
        // Forward the agent's specific message.
        fail(ErrorText::forCode(errorCode, msgFallback));
        return;
    }

    const QDBusUnixFileDescriptor& artifact = op->signResult().artifact;
    if (!artifact.isValid()) {
        fail(ErrorText::forCode(ErrorCode::CommunicationError, QString()));
        return;
    }

    // Derive the output path next to the input. The input is never
    // modified in place — even enveloped output lands under a derived name.
    // TODO: honour the agent's default output folder (the DefaultLocation
    // property on the org.librescrs.Agent.Config1 D-Bus interface) once the
    // client consumes it; until then output is written alongside the input.
    const QFileInfo inInfo(d->inputPath);
    const QString outName = MimeFormatMap::outputName(inInfo.fileName(), d->choice);
    const QString outPath = inInfo.absoluteDir().filePath(outName);

    // Copy the agent's artifact fd into the output file. Read from the artifact
    // fd (the agent retains no copy) and write the destination. Rewind first:
    // the sealed-memfd contract is seekable, but if the seek ever fails we'd be
    // signing from mid-stream — fail loudly rather than write a truncated file.
    const int srcFd = artifact.fileDescriptor();
    if (::lseek(srcFd, 0, SEEK_SET) == static_cast<off_t>(-1)) {
        fail(ErrorText::forCode(ErrorCode::CommunicationError, QString()));
        return;
    }

    // Write via QSaveFile: it stages bytes in a temp sibling and only swaps it in
    // on commit() (atomic rename). A mid-write crash therefore leaves any existing
    // good output untouched, instead of a half-written truncated file — fixing the
    // exists()→confirm→WriteOnly|Truncate TOCTOU/destruction window.
    QSaveFile out(outPath);
    if (!out.open(QIODevice::WriteOnly)) {
        fail(i18nc("@info:status sign failed to write output", "Could not write the signed file %1.", outName));
        return;
    }
    char buf[64 * 1024];
    bool writeOk = true;
    for (;;) {
        ssize_t n = 0;
        do {
            n = ::read(srcFd, buf, sizeof(buf));
        } while (n < 0 && errno == EINTR); // EINTR is a retry, not a failure.
        if (n < 0) {
            writeOk = false; // a real read error
            break;
        }
        if (n == 0) {
            break; // EOF
        }
        // QSaveFile::write loops over its QFileDevice buffer internally; a short
        // return is a genuine write error (out of space / closed), not EINTR.
        if (out.write(buf, n) != n) {
            writeOk = false;
            break;
        }
    }
    if (!writeOk) {
        out.cancelWriting(); // discard the staged temp; the existing file is untouched.
        fail(i18nc("@info:status sign failed to write output", "Could not write the signed file %1.", outName));
        return;
    }

    // The overwrite confirmation lives here, immediately before commit():
    // QSaveFile::commit() overwrites unconditionally, so the existence check + the
    // injected OverwriteConfirmer must gate it. (The staged temp is discarded if
    // the user declines, leaving any existing output intact.)
    if (QFile::exists(outPath)) {
        const bool ok = d->overwriteConfirmer && d->overwriteConfirmer(outPath);
        if (!ok) {
            out.cancelWriting();
            fail(i18nc("@info:status user declined overwriting the output",
                       "Signing was cancelled to avoid overwriting %1.", outName));
            return;
        }
    }

    if (!out.commit()) {
        fail(i18nc("@info:status sign failed to write output", "Could not write the signed file %1.", outName));
        return;
    }

    d->outputPath = outPath;
    if (d->emitted) {
        return;
    }
    d->emitted = true;
    qCInfo(LibreKDE::Signing::Logging) << "signed artifact written to" << outPath;
    Q_EMIT succeeded(outPath);
}

} // namespace LibreKDE
