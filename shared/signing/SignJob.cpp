// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "SignJob.h"

#include "ErrorText.h"
#include "signing_log_categories.h"

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include <LibreSCRS/AgentClient/AgentCard.h>
#include <LibreSCRS/AgentClient/AgentOperation.h>
#include <LibreSCRS/AgentClient/FdHandle.h>
#include <LibreSCRS/AgentClient/SignOptions.h>
#include <LibreSCRS/AgentClient/Types.h>

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMimeDatabase>
#include <QPointer>
#include <QSaveFile>

#include <cerrno>
#include <fcntl.h>
#include <optional>
#include <unistd.h>
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
/// `SignChoice::enveloped()` already makes. No timestamp authority and no
/// visible-signature placement are requested, so both stay the agent's own
/// configuration. The level does NOT: the typed options always carry one, so
/// the agent's configured default level (and its upgrade when a timestamp
/// authority is configured) no longer applies to a request made through here —
/// every signature this Job asks for is the baseline level.
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
    OverwriteConfirmer overwriteConfirmer;

    // Settled together, once, in start(): `signOptions` is derived from
    // `choice`, so anything that reassigns one must reassign the other or the
    // request and the output name stop describing the same signature.
    SignChoice choice;
    Client::SignOptions signOptions;
    QString outputPath;
    Client::AgentOperation* certOp = nullptr;
    Client::AgentOperation* signOp = nullptr;
    bool emitted = false;
};

SignJob::SignJob(Client::AgentCard* card, QString inputPath, QString mimeType, QString formatOverride,
                 CertChooser chooser, OverwriteConfirmer overwriteConfirmer, QObject* parent)
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
    if (!Client::has(Client::capabilityBits(d->card->capabilities()), Client::Cap::Pki)) {
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
        fail(ErrorText::forOutcome(op->errorCode(), op->callError(), op->messageFallback()));
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
        fail(ErrorText::forOutcome(op->errorCode(), op->callError(), op->messageFallback()));
        return;
    }

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

    // Derive the output path next to the input. The input is never
    // modified in place — even enveloped output lands under a derived name.
    // TODO: honour the agent's default output folder (the DefaultLocation
    // property on the agent's configuration interface) once the client
    // consumes it; until then output is written alongside the input.
    const QFileInfo inInfo(d->inputPath);
    const QString outName = MimeFormatMap::outputName(inInfo.fileName(), d->choice);
    const QString outPath = inInfo.absoluteDir().filePath(outName);

    // Copy the agent's artifact fd into the output file. Read from the artifact
    // fd (the agent retains no copy) and write the destination. Rewind first:
    // the sealed-memfd contract is seekable, but if the seek ever fails we'd be
    // signing from mid-stream — fail loudly rather than write a truncated file.
    const int srcFd = artifact.get();
    if (::lseek(srcFd, 0, SEEK_SET) == static_cast<off_t>(-1)) {
        fail(ErrorText::forCode(Client::ErrorCode::CommunicationError, QString()));
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
