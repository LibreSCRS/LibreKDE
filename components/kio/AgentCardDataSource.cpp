// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "AgentCardDataSource.h"

#include "ErrorText.h"
#include "IdentityRows.h" // LibreKDE::localizedFieldLabel

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include <LibreSCRS/AgentClient/AgentCard.h>
#include <LibreSCRS/AgentClient/AgentClient.h>
#include <LibreSCRS/AgentClient/AgentOperation.h>
#include <LibreSCRS/AgentClient/AgentReader.h>
#include <LibreSCRS/AgentClient/ClientTimeouts.h>
#include <LibreSCRS/AgentClient/IdentityRows.h>
#include <LibreSCRS/AgentClient/SealedPayload.h>

#include <QEventLoop>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

// Short local spelling for the agent client library, whose value types this
// worker consumes directly — the error taxonomy and the flattened identity row
// among them. The host adds only what that library will not: the localized copy
// (ErrorText) and the label table (IdentityRows), both keyed on the library's own
// types.
namespace Client = LibreSCRS::AgentClient;

namespace LibreKDE {

namespace {

/// @brief Map an agent op's terminal outcome onto a `ReadStatus`.
///
/// A failed operation classifies on TWO mutually exclusive axes. `errorCode` is
/// the agent's own answer and is `None` whenever the call never got a wire-level
/// answer at all — a refusal at method entry, an unreachable agent, a broken
/// connection — and those carry their reason on `callError` instead. Reading
/// only the first axis would drop every one of them into the catch-all `Error`,
/// which for this worker means ERR_WORKER_DIED where the honest answer is
/// "not here" or "service unavailable".
ReadStatus mapStatus(Client::OperationStatus status, Client::ErrorCode code, Client::CallError call)
{
    if (status == Client::OperationStatus::Ok) {
        return ReadStatus::Ok;
    }
    if (status == Client::OperationStatus::Cancelled && call == Client::CallError::None) {
        // A genuine cancel. Every route that produces a Cancelled terminal from
        // the wire leaves the transport axis at None, so this is the arm a real
        // cancel takes.
        //
        // The extra condition is defence in depth, not an observed path. The one
        // route that terminalizes Cancelled WITH a transport reason is the
        // agent-loss sweep, and no caller here ever sees its outcome: that sweep
        // deletes the cards straight after terminalizing, in the same slot, so
        // the QPointer each read holds is already null when its loop returns and
        // the read answers CardRemoved without consulting this function at all.
        // The condition exists so that a future operation NOT parented to a card
        // — which would survive that sweep — cannot arrive here and be reported
        // to the user as something they cancelled.
        return ReadStatus::Cancelled;
    }
    switch (code) {
    case Client::ErrorCode::CredentialWrong:
    case Client::ErrorCode::CredentialBlocked:
    case Client::ErrorCode::AuthFailed:
        return ReadStatus::AuthFailed;
    case Client::ErrorCode::CardRemoved:
        return ReadStatus::CardRemoved;
    case Client::ErrorCode::CapabilityMissing:
    case Client::ErrorCode::UnsupportedCard:
        return ReadStatus::CapabilityMissing;
    default:
        // Every other code, and `None` — which is where a call that never got a
        // wire answer lands. Keep a default arm: the taxonomy is append-only and
        // a newer agent's code decodes through verbatim.
        break;
    }
    // The transport axis. Exhaustive (no default) so an enumerator appended
    // upstream is a -Wswitch diagnostic here rather than a silent catch-all.
    switch (call) {
    case Client::CallError::AgentUnavailable:
    case Client::CallError::Timeout:
        // Nothing is wrong with the card: the service did not answer.
        return ReadStatus::Unavailable;
    case Client::CallError::AccessDenied:
        return ReadStatus::AuthFailed;
    case Client::CallError::InvalidArguments:
        // The request itself was refused before any work started — a card that
        // does not carry what was asked for, or an agent that does not implement
        // it. "Does not exist" is what a file manager can act on.
        return ReadStatus::CapabilityMissing;
    case Client::CallError::TransportFailure:
    case Client::CallError::ProtocolError:
    case Client::CallError::None:
        break;
    }
    return ReadStatus::Error;
}

/// @brief The localized reason to carry alongside @p status, or empty.
///
/// Only the two statuses whose worker-side copy is generic take one: `Error` (a
/// catch-all "could not read the card") and `Unavailable` (this component's own
/// single transport sentence). Every other status already renders text specific
/// to itself, and a user cancel renders none at all — giving that one a reason
/// would put a "did not finish" sentence in front of someone who pressed cancel.
///
/// The text itself is never composed here: it comes from the one shared rule
/// that knows how the two failure axes compose, and that rule cannot return an
/// empty string.
QString reasonFor(ReadStatus status, Client::AgentOperation* op)
{
    if (status != ReadStatus::Error && status != ReadStatus::Unavailable) {
        return {};
    }
    return ErrorText::forOutcome(op->errorCode(), op->callError(), op->messageFallback());
}

/// @brief Extra exits a caller installs on the drive loop below, wired once
///        against the live loop just before it runs. Empty for the card reads,
///        which need only the two standard exits.
using LoopWiring = std::function<void(QEventLoop&)>;

/// @brief Drive @p op to its terminal state on a scoped QEventLoop, with a
///        PHASE-AWARE stall backstop so the doGet path can never hang forever
/// — yet a legitimate PACE/CAN wait is never aborted.
///
/// The agent's own watchdog and the client's agent-death / card-removal sweeps
/// normally drive `finished` in every real failure mode; @p opStallTimeoutMs
/// is the last-resort backstop for an orphaned op the agent never terminalizes.
/// It runs ONLY during machine phases (Created/Connecting/Reading/Signing/…) and
/// is (re)started on every phase/progress tick; entering AwaitingConsent or
/// Authenticating STOPS it — a human at the prompter takes as long as they need,
/// bounded by the agent's prompter, not by us.
///
/// @param extraExits optional additional ways out of the loop, for an operation
///        the standard sweeps do not reach.
///
/// @return true if @p op reached a terminal state; false if the stall backstop
///         (or an extra exit) fired first — never a consent wait.
bool driveToFinished(Client::AgentOperation* op, int opStallTimeoutMs, const LoopWiring& extraExits = {})
{
    if (op->isFinished()) {
        return true; // recovered synchronously (lost-terminal path)
    }
    QEventLoop loop;
    bool finished = false;
    QObject::connect(op, &Client::AgentOperation::finished, &loop, [&loop, &finished]() {
        finished = true;
        loop.quit();
    });

    QTimer stall;
    stall.setSingleShot(true);
    QObject::connect(&stall, &QTimer::timeout, &loop, &QEventLoop::quit);
    const auto armForPhase = [&stall, opStallTimeoutMs](Client::OperationPhase phase) {
        const bool humanInLoop =
            (phase == Client::OperationPhase::AwaitingConsent || phase == Client::OperationPhase::Authenticating);
        if (humanInLoop) {
            stall.stop(); // the human is thinking at the prompter — never time out
        } else {
            stall.start(opStallTimeoutMs); // machine phase — bound it, reset on each tick
        }
    };
    QObject::connect(op, &Client::AgentOperation::phaseChanged, &loop,
                     [&armForPhase](Client::OperationPhase phase, double) { armForPhase(phase); });
    armForPhase(Client::OperationPhase::Created); // arm for the pre-first-signal machine phase

    if (extraExits) {
        extraExits(loop);
    }

    // Re-check after wiring: the terminal may have arrived between the guard and
    // the connect. The library queues an already-known terminal precisely so a
    // consumer connecting right after minting still observes it, but a
    // synchronous one that beat us here would otherwise leave the loop waiting
    // for a signal that already fired.
    if (op->isFinished()) {
        return true;
    }
    loop.exec();
    return finished || op->isFinished();
}

} // namespace

AgentCardDataSource::AgentCardDataSource(Client::AgentClient& client)
    : AgentCardDataSource(client, Client::kLongOperationTimeoutMs)
{}

AgentCardDataSource::AgentCardDataSource(Client::AgentClient& client, int opStallTimeoutMs)
    : m_client(client), m_opStallTimeoutMs(opStallTimeoutMs)
{}

AgentCardDataSource::~AgentCardDataSource() = default;

QList<CardPresence> AgentCardDataSource::listReadersWithCards()
{
    // Zero card I/O: purely the client's own registry.
    QList<CardPresence> out;
    const QList<Client::AgentReader*> readers = m_client.readers();
    for (Client::AgentReader* reader : readers) {
        if (reader == nullptr || !reader->hasCard()) {
            continue;
        }
        Client::AgentCard* card = m_client.card(reader->cardId());
        if (card == nullptr) {
            continue;
        }
        CardPresence p;
        p.readerName = reader->name();
        p.cardId = card->id();
        // The client publishes capabilities as stable TOKENS; the tree and the
        // renderers both consume the bitfield. capabilityBits() is the exact
        // inverse of the tokenizer — a bit this build has no name for round-trips
        // as "bit<i>" rather than being dropped — so the round-trip is lossless.
        p.capabilities = Client::capabilityBits(card->capabilities());
        p.preReadAuth = card->preReadAuth(); // verbatim wire token, no round-trip
        out << p;
    }
    return out;
}

IdentityResult AgentCardDataSource::readIdentity(const QString& cardId)
{
    IdentityResult result;
    Client::AgentCard* card = m_client.card(cardId);
    if (card == nullptr) {
        result.status = ReadStatus::Unavailable;
        return result;
    }
    // Non-null on every path, including a refusal: a call the agent rejects at
    // entry comes back as an operation that is ALREADY finished, carrying the
    // mapped failure, so there is no null to test for and the refusal is
    // classified by the same mapStatus every other outcome goes through.
    Client::AgentOperation* op = card->readIdentity();
    // If the card is pulled (or the agent dies) while driveToFinished() spins its
    // nested loop, the client terminalizes the op (quitting the loop) and then
    // deletes the card, destroying this QObject-parented op. Guard the op with a
    // QPointer and re-check it BEFORE any post-loop deref, mirroring the signing
    // core's own card guard. Without this the op->status() below would
    // dereference freed memory.
    QPointer<Client::AgentOperation> guard(op);
    const bool reachedTerminal = driveToFinished(op, m_opStallTimeoutMs);
    if (!guard) {
        result.status = ReadStatus::CardRemoved;
        return result;
    }
    // Own the surviving operation from here (see getCertificateDer): the worker
    // process lives as long as the file-manager session, so leaving each finished
    // op parented to the card would accumulate one dead QObject per open until
    // card removal.
    const std::unique_ptr<Client::AgentOperation> owned(op);
    if (!reachedTerminal) {
        // Genuine machine-phase stall (a consent wait suppresses the backstop, so
        // this is never a CAN wait). Ask the agent to abandon; surface Unavailable
        // with NO reason attached — the op never produced an outcome to render.
        op->cancel();
        result.status = ReadStatus::Unavailable;
        return result;
    }
    result.status = mapStatus(op->status(), op->errorCode(), op->callError());
    if (result.status != ReadStatus::Ok) {
        result.message = reasonFor(result.status, op);
        return result;
    }

    // Fold a security check's several `check_<N>_<suffix>` wire fields into
    // one row (tolerant of the joined shape too) BEFORE the per-row loop,
    // since it changes row count — same rule the plasmoid applies.
    for (const Client::IdentityRow& row :
         foldSecurityCheckFields(Client::flattenIdentityFields(op->identityResult()))) {
        if (isHiddenIdentityRow(row)) {
            continue;
        }
        IdentityFieldView view;
        view.group = row.groupKey;
        view.fieldKey = row.fieldKey;
        // Shared resolvers — the SAME rules the plasmoid uses; this is the
        // display label and value renderIdentityTxt emits.
        view.labelFallback = localizedFieldLabel(row);
        view.value = localizedFieldValue(row);
        result.fields << view;
    }
    return result;
}

CertListResult AgentCardDataSource::readCertificates(const QString& cardId)
{
    CertListResult result;
    Client::AgentCard* card = m_client.card(cardId);
    if (card == nullptr) {
        result.status = ReadStatus::Unavailable;
        return result;
    }
    Client::AgentOperation* op = card->readCertificates();
    // See readIdentity(): driveToFinished() may spin while the card is pulled, in
    // which case the client's removal sweep deletes the op mid-loop. Re-check the
    // QPointer before any post-loop deref, then own the survivor so it is reaped.
    QPointer<Client::AgentOperation> guard(op);
    const bool reachedTerminal = driveToFinished(op, m_opStallTimeoutMs);
    if (!guard) {
        result.status = ReadStatus::CardRemoved;
        return result;
    }
    const std::unique_ptr<Client::AgentOperation> owned(op);
    if (!reachedTerminal) {
        op->cancel();
        result.status = ReadStatus::Unavailable;
        return result;
    }
    result.status = mapStatus(op->status(), op->errorCode(), op->callError());
    if (result.status != ReadStatus::Ok) {
        result.message = reasonFor(result.status, op);
        return result;
    }
    result.certs = op->certificatesResult();
    return result;
}

PhotoResult AgentCardDataSource::getPhoto(const QString& cardId)
{
    PhotoResult result;
    Client::AgentCard* card = m_client.card(cardId);
    if (card == nullptr) {
        result.status = ReadStatus::Unavailable;
        return result;
    }
    Client::AgentOperation* op = card->getPhoto();
    // See readIdentity(): driveToFinished() may spin while the card is pulled, in
    // which case the client's removal sweep deletes the op mid-loop. Re-check the
    // QPointer before any post-loop deref, then own the survivor so it is reaped.
    QPointer<Client::AgentOperation> guard(op);
    const bool reachedTerminal = driveToFinished(op, m_opStallTimeoutMs);
    if (!guard) {
        result.status = ReadStatus::CardRemoved;
        return result;
    }
    const std::unique_ptr<Client::AgentOperation> owned(op);
    if (!reachedTerminal) {
        op->cancel();
        result.status = ReadStatus::Unavailable;
        return result;
    }
    result.status = mapStatus(op->status(), op->errorCode(), op->callError());
    if (result.status != ReadStatus::Ok) {
        result.message = reasonFor(result.status, op);
        return result;
    }
    // Single-shot and move-only: taking twice yields nothing, so hoist it into a
    // local that owns the descriptors for the rest of this function.
    const std::vector<Client::PhotoItem> photos = op->takePhotos();
    if (photos.empty()) {
        // No photo on this card — surface as a not-available leaf, not an error.
        result.status = ReadStatus::NotAvailable;
        return result;
    }
    // DEFERRED: a multi-photo card exposes only the first item here; a future
    // layout will surface every "groupKey:fieldKey" photo.
    //
    // WHICH photo that is, on a card carrying more than one, is NOT a contract:
    // the item order is whatever the transport in use produced. One wire carries
    // the photos as a key-sorted map, so "first" there is the lowest
    // "groupKey:fieldKey"; the other carries a plain sequence, so "first" there
    // is whatever the agent emitted first. A single-photo card — every card this
    // worker is used with today — cannot tell the difference, which is exactly
    // why the difference is easy to miss. The layout that surfaces every photo
    // is where this stops mattering; until then, do not build anything on the
    // assumption that this picks a particular photo.
    const std::optional<QByteArray> payload = Client::readBoundedPayload(photos.front().fd);
    if (!payload) {
        // NOT the same thing as an empty payload: the descriptor could not be
        // read at all (not a regular file, a refused seal state, over the byte
        // cap, a short read). Nothing was learned about whether the card carries
        // a photo, so this cannot be reported as a benign absence.
        result.status = ReadStatus::Error;
        result.message = ErrorText::forCode(Client::ErrorCode::CommunicationError, QString());
        return result;
    }
    if (payload->isEmpty()) {
        // An entry present but empty-on-read is a benign absent photo, matching the
        // empty-list branch above (NotAvailable → ERR_DOES_NOT_EXIST) and the
        // plasmoid's benign-absence handling — NOT a worker-died error.
        result.status = ReadStatus::NotAvailable;
        return result;
    }
    result.bytes = *payload;
    return result;
}

CertDerResult AgentCardDataSource::getCertificateDer(const QString& cardId, const QString& certId)
{
    CertDerResult result;
    Client::AgentCard* card = m_client.card(cardId);
    if (card == nullptr) {
        result.status = ReadStatus::Unavailable;
        return result;
    }
    // A certificate export is public data (no consent, no lease), addressed by
    // the reader holding the card plus the certificate id. It is asynchronous, so
    // it is driven on the same scoped loop the card reads use — with one extra
    // exit they do not need.
    //
    // The minted operation is parented to the CLIENT, not to the card, so the
    // client's card-removal sweep — which walks the CARD's children — never
    // terminalizes it. What DOES end the wait without this extra exit is the
    // transport's own reply budget on the fetch, and it is a poor thing to depend
    // on twice over: it is an implementation detail of each transport rather than
    // anything this surface promises, and what it eventually reports is a generic
    // timeout rather than the thing that actually happened. Watching the removal
    // directly ends the wait as soon as the client knows about it, and names the
    // reason. The stall backstop stays underneath both as the last resort — and
    // note it can only ever fire once here, since this operation reports no
    // phases for it to re-arm on.
    Client::AgentOperation* op = m_client.certificateDer(card->readerId(), certId);
    QPointer<Client::AgentOperation> guard(op);
    bool cardGone = false;
    const bool reachedTerminal = driveToFinished(op, m_opStallTimeoutMs, [this, &cardId, &cardGone](QEventLoop& loop) {
        QObject::connect(&m_client, &Client::AgentClient::cardChanged, &loop,
                         [this, &cardId, &cardGone, &loop](const QString& objectId) {
                             if (objectId == cardId && m_client.card(cardId) == nullptr) {
                                 cardGone = true;
                                 loop.quit();
                             }
                         });
    });
    if (!guard) {
        // Only client teardown can destroy a client-parented operation, and that
        // cannot happen inside a call the client is on the stack of — but the
        // re-check costs nothing and the alternative is a deref that assumes it.
        result.status = ReadStatus::Unavailable;
        return result;
    }
    // Own the operation from here. It is parented to the long-lived client, so
    // nothing else reaps it for the worker process's whole lifetime, and its
    // destructor cancels a fetch still in flight — which is what makes stopping
    // early on a pulled card leave nothing behind.
    const std::unique_ptr<Client::AgentOperation> owned(op);
    if (cardGone) {
        result.status = ReadStatus::CardRemoved;
        return result;
    }
    if (!reachedTerminal) {
        result.status = ReadStatus::Unavailable;
        return result;
    }
    if (op->status() == Client::OperationStatus::Ok) {
        result.status = ReadStatus::Ok;
        result.der = op->certificateDerResult();
        return result;
    }
    // A missing cert/card is "does not exist", not a worker death; anything else
    // (comms/agent-internal) is a generic read error. The first two are named
    // errors, and the named axis is the only one that distinguishes them: both
    // collapse into buckets shared with other names on the coarser axes.
    const std::optional<Client::SyncError> named = op->syncError();
    if (named == Client::SyncError::KeyNotFound || named == Client::SyncError::UnknownCard) {
        result.status = ReadStatus::NotAvailable;
        return result;
    }
    // "Unavailable" was never a wire token — an unreachable agent is a
    // client-side observation, and its discriminator is the transport axis.
    result.status =
        op->callError() == Client::CallError::AgentUnavailable ? ReadStatus::Unavailable : ReadStatus::Error;
    result.message = reasonFor(result.status, op);
    return result;
}

} // namespace LibreKDE
