// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "SignJob.h"

#include "Cards.h" // LibreKDE::Cards::hasCapability
#include "ErrorText.h"
#include "signing_log_categories.h"

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include <LibreSCRS/AgentClient/AgentCard.h>
#include <LibreSCRS/AgentClient/AgentOperation.h>
#include <LibreSCRS/AgentClient/FdHandle.h>
#include <LibreSCRS/AgentClient/SignOptions.h>
#include <LibreSCRS/AgentClient/Types.h>

#include <KLocalizedString>

#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMimeDatabase>
#include <QPointer>

#include <fcntl.h>
#include <optional>
#include <utility>

// Short local spelling for the agent client library, whose value types this
// signing core consumes directly — the error taxonomy among them. ErrorText, the
// host's localized copy for that taxonomy, is keyed on the very same enum.
namespace Client = LibreSCRS::AgentClient;

namespace LibreKDE {

namespace {

/// @brief The typed sign options for @p choice, or `std::nullopt` when its
///        format is outside the closed vocabulary the options can express.
///
/// The wire tokens `MimeFormatMap` speaks ARE that vocabulary
/// (pades|cades|xades|jades|asice); packaging is the two-valued split
/// `SignChoice::enveloped()` already makes. No timestamp authority, no
/// visible-signature placement and no level are requested, so all three stay
/// the agent's own configuration — including its configured default level and
/// that value's upgrade when a timestamp authority is set.
[[nodiscard]] std::optional<Client::SignOptions> toSignOptions(const SignChoice& choice)
{
    Client::SignOptions options;
    if (choice.format == QLatin1String("pades")) {
        options.format = Client::SignatureFormat::PAdES;
    } else if (choice.format == QLatin1String("cades")) {
        options.format = Client::SignatureFormat::CAdES;
    } else if (choice.format == QLatin1String("xades")) {
        options.format = Client::SignatureFormat::XAdES;
    } else if (choice.format == QLatin1String("jades")) {
        options.format = Client::SignatureFormat::JAdES;
    } else if (choice.format == QLatin1String("asice")) {
        options.format = Client::SignatureFormat::ASiCe;
    } else {
        return std::nullopt;
    }
    options.packaging = choice.enveloped() ? Client::Packaging::Enveloped : Client::Packaging::Detached;
    // The agent's configuration is the signing policy, not this client's.
    // Stated explicitly even though it is the default, because a level that
    // silently overrode that policy is exactly the defect this replaces.
    options.level = Client::SignatureLevel::Auto;
    return options;
}

} // namespace

struct SignJob::Private
{
    // Held as a QPointer. Two windows can destroy the card between choosing a
    // certificate and signing with it: an injected CertChooser MAY spin a nested
    // event loop (the QtWidgets seam raises a modal dialog; the plasmoid
    // deliberately injects a non-modal seam instead), and beginSign() runs a full
    // event-loop turn later either way (see the queued hop in
    // onCertificatesFinished). Either is enough for the agent client to delete
    // the AgentCard when the card is pulled — its card-removal path terminalizes
    // every live operation and then destroys the card synchronously. A raw
    // pointer would dangle by the time beginSign() runs; QPointer auto-nulls so
    // beginSign() can re-check and fail cleanly.
    QPointer<Client::AgentCard> card;
    QString inputPath;
    QString mimeType;
    QString formatOverride;
    CertChooser chooser;

    // Settled together, once, in start(): `signOptions` is derived from
    // `choice`, so anything that reassigns one must reassign the other or the
    // request and the output name stop describing the same signature.
    SignChoice choice;
    Client::SignOptions signOptions;
    QString outputPath;
    QVariantMap signMeta;
    Client::AgentOperation* certOp = nullptr;
    Client::AgentOperation* signOp = nullptr;
    bool emitted = false;
};

SignJob::SignJob(Client::AgentCard* card, QString inputPath, QString mimeType, QString formatOverride,
                 CertChooser chooser, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->card = card;
    d->inputPath = std::move(inputPath);
    d->mimeType = std::move(mimeType);
    d->formatOverride = std::move(formatOverride);
    d->chooser = std::move(chooser);
}

SignJob::~SignJob() = default;

QString SignJob::outputPath() const
{
    return d->outputPath;
}

QVariantMap SignJob::signMeta() const
{
    return d->signMeta;
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
    if (!LibreKDE::Cards::hasCapability(*d->card, Client::Cap::Pki)) {
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
        // Per-request override: we must re-derive `packaging` from the
        // OVERRIDE's format family — not leave the MIME-derived packaging.
        // Otherwise an override (e.g. a PDF MIME → pades/enveloped, overridden
        // to cades) would forward a stale `enveloped` with cades AND mis-name
        // the output. Re-resolving from the override's family keeps format +
        // packaging + output name consistent.
        d->choice = MimeFormatMap::resolveFormat(d->formatOverride);
    }

    // The typed sign options carry a CLOSED format vocabulary, so a format
    // string this client cannot map has no way to reach the agent and be
    // refused there. Refuse it here instead — substituting some other format
    // would sign the document differently from what was asked for.
    // MimeFormatMap::resolve() only ever yields vocabulary formats, so only a
    // caller-supplied formatOverride can land here; nothing has been opened or
    // started yet at this point.
    std::optional<Client::SignOptions> options = toSignOptions(d->choice);
    if (!options.has_value()) {
        fail(ErrorText::forCode(Client::ErrorCode::CapabilityMissing, QString()));
        return;
    }
    d->signOptions = std::move(options).value();

    // Enumerate the card's signing certificates (capability PKI). The chosen
    // certificate id is the prerequisite handle for Sign (no auto-select).
    // The client never hands back a null operation: a call it refuses at entry
    // comes back already finished with the mapped error, and the terminal is
    // queued so connecting right here still observes it.
    Client::AgentOperation* op = d->card->readCertificates();
    d->certOp = op;
    connect(op, &Client::AgentOperation::finished, this, &SignJob::onCertificatesFinished);
    connect(op, &Client::AgentOperation::phaseChanged, this, &SignJob::phaseChanged);
}

void SignJob::onCertificatesFinished()
{
    Client::AgentOperation* op = d->certOp;
    d->certOp = nullptr;
    if (op == nullptr) {
        return;
    }
    // Ordering contract: deleteLater() only schedules destruction for the next
    // event-loop turn; the synchronous outcome and result reads below still run
    // against the live object before this slot returns. Safe by construction.
    op->deleteLater();

    if (op->status() != Client::OperationStatus::Ok) {
        // Both failure axes, not just the taxonomy one. A cert read that never
        // reached the agent (not running, refused, timed out, connection lost)
        // carries ErrorCode::None and reports its reason on callError()
        // instead; forOutcome() renders that as localized copy, and guarantees
        // a banner a user can read even when the terminal carried no message.
        fail(ErrorText::forOutcome(op->errorCode(), op->callError(), op->messageKey(), op->messageFallback()));
        return;
    }

    // Filter to signing-capable certs only.
    const QList<Client::CertificateInfo> certificates = op->certificatesResult();
    QList<Client::CertificateInfo> signing;
    for (const Client::CertificateInfo& c : certificates) {
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
        certId = signing.first().id; // auto-pick the lone signing cert.
    } else {
        std::optional<QString> chosen = d->chooser ? d->chooser(signing) : std::nullopt;
        if (!chosen.has_value()) {
            fail(i18nc("@info:status user cancelled the certificate chooser", "Signing was cancelled."));
            return;
        }
        certId = chosen.value();
    }

    // Defer the Sign onto a fresh event-loop turn. We are currently inside the
    // cert operation's `finished` slot — and, above, may have just unwound a
    // chooser's nested loop from inside it.
    //
    // The hop must NOT be removed. It is the only route into beginSign(), and
    // the turn it inserts is one more point at which a pending card-removal is
    // delivered before beginSign() runs. That is why beginSign() re-checks the
    // card even when the injected chooser spun no loop of its own — deleting the
    // hop would move the sign call back inside the previous operation's terminal
    // delivery and quietly narrow the window that re-check exists to cover.
    QMetaObject::invokeMethod(this, [this, certId]() { beginSign(certId); }, Qt::QueuedConnection);
}

void SignJob::beginSign(const QString& certId)
{
    // Re-check the card: the injected CertChooser may have spun a modal nested
    // event loop (see Private::card), during which the card could have been
    // pulled and the AgentCard deleted by the agent client. QPointer auto-nulls
    // on that delete, so fail cleanly (write nothing) rather than dereference a
    // freed pointer.
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

    // FdHandle TAKES ownership of the descriptor rather than duplicating it, so
    // the fd must NOT be closed here. It changes hands exactly once, at this one
    // call: the handle is constructed straight into sign()'s BY-VALUE parameter,
    // so ownership leaves this function unconditionally — including when the
    // agent refuses the call at entry, which still returns an operation (already
    // finished, carrying the mapped error) rather than dropping the request.
    Client::AgentOperation* op = d->card->sign(certId, Client::FdHandle(fd), d->signOptions);
    d->signOp = op;
    connect(op, &Client::AgentOperation::finished, this, &SignJob::onSignFinished);
    connect(op, &Client::AgentOperation::phaseChanged, this, &SignJob::phaseChanged);
}

void SignJob::onSignFinished()
{
    Client::AgentOperation* op = d->signOp;
    d->signOp = nullptr;
    if (op == nullptr) {
        return;
    }
    // Ordering contract: deleteLater() only schedules destruction for the next
    // event-loop turn; the synchronous outcome and artifact reads below still run
    // against the live object before this slot returns. Safe by construction.
    op->deleteLater();

    if (op->status() != Client::OperationStatus::Ok) {
        // Both failure axes — same reasoning as onCertificatesFinished above.
        fail(ErrorText::forOutcome(op->errorCode(), op->callError(), op->messageKey(), op->messageFallback()));
        return;
    }

    // What the agent says it actually produced — read off the live operation in
    // this slot, like the artifact below, since the deleteLater above only takes
    // effect on the next event-loop turn. Kept because the request may have
    // deferred the level, in which case this is the only place the resolved one
    // appears.
    d->signMeta = op->signMeta();

    // Single-shot: takeSignedArtifact() MOVES the sealed fd out of the operation,
    // so it is called exactly once and every read below goes through this local.
    // The local owns the descriptor from here on and closes it when this slot
    // returns, so the streaming below does not depend on the operation outliving
    // its deleteLater().
    const Client::FdHandle artifact = op->takeSignedArtifact();
    if (!artifact.valid()) {
        fail(ErrorText::forCode(Client::ErrorCode::CommunicationError, QString()));
        return;
    }

    // The name every client of the agent gives this signature, next to the
    // input, never an existing file. The input is never modified in place —
    // even an enveloped signature lands under a derived name. The client
    // library claims the name exclusively and commits the bytes over the claim
    // in one step, so neither someone else's file nor a half-written signature
    // ever sits under that name. The last name asked for is the one a write
    // failure names.
    QString lastName;
    const auto written = Client::writeSignedFile(artifact.get(), [this, &lastName] {
        lastName = Client::signedOutputPath(d->inputPath, d->signOptions.format, d->signOptions.packaging);
        return lastName;
    });
    if (!written) {
        switch (written.error()) {
        case Client::SignedFileError::UnreadableArtifact:
            fail(ErrorText::forCode(Client::ErrorCode::CommunicationError, QString()));
            return;
        case Client::SignedFileError::NoFreeName:
            fail(i18nc("@info:status sign failed: every name for the output is taken",
                       "There is no free name left for the signed copy of %1.", QFileInfo(d->inputPath).fileName()));
            return;
        case Client::SignedFileError::EmptyArtifact:
            // Nothing failed to write: there was nothing to write.
            fail(i18nc("@info:status sign failed: the signature arrived with no content",
                       "The signature for %1 came back empty, so no signed file was saved.",
                       QFileInfo(d->inputPath).fileName()));
            return;
        case Client::SignedFileError::WriteFailed:
            break;
        }
        fail(i18nc("@info:status sign failed to write output", "Could not write the signed file %1.",
                   QFileInfo(lastName).fileName()));
        return;
    }
    const QString outPath = *written;

    d->outputPath = outPath;
    if (d->emitted) {
        return;
    }
    d->emitted = true;
    qCInfo(LibreKDE::Signing::Logging) << "signed artifact written to" << outPath;
    Q_EMIT succeeded(outPath);
}

} // namespace LibreKDE
