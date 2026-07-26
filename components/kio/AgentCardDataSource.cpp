// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "AgentCardDataSource.h"

#include "AgentCapabilities.h"
#include "AgentCard.h"
#include "AgentClient.h"
#include "AgentDBus.h"
#include "AgentOperation.h"
#include "AgentReader.h"
#include "IdentityRows.h"
#include "SealedFd.h"

#include <QDBusUnixFileDescriptor>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>

namespace LibreKDE {

namespace {

/// @brief Map an agent op's terminal (status, errorCode) onto a `ReadStatus`.
ReadStatus mapStatus(OperationStatus status, ErrorCode code)
{
    if (status == OperationStatus::Ok) {
        return ReadStatus::Ok;
    }
    if (status == OperationStatus::Cancelled) {
        return ReadStatus::Cancelled;
    }
    // status == Error: refine by code.
    switch (code) {
    case ErrorCode::CredentialWrong:
    case ErrorCode::CredentialBlocked:
    case ErrorCode::AuthFailed:
        return ReadStatus::AuthFailed;
    case ErrorCode::CardRemoved:
        return ReadStatus::CardRemoved;
    case ErrorCode::CapabilityMissing:
    case ErrorCode::UnsupportedCard:
        return ReadStatus::CapabilityMissing;
    default:
        return ReadStatus::Error;
    }
}

/// @brief Drive @p op to its terminal state on a scoped QEventLoop, with a
///        PHASE-AWARE stall backstop so the doGet path can never hang forever
/// — yet a legitimate PACE/CAN wait is never aborted.
///
/// The agent's own WatchdogTimeout and AgentClient's agent-death / card-removal
/// sweeps normally drive `finished` in every real failure mode; @p opStallTimeoutMs
/// is the last-resort backstop for an orphaned op the agent never terminalizes.
/// It runs ONLY during machine phases (Created/Connecting/Reading/Signing/…) and
/// is (re)started on every phase/progress tick; entering AwaitingConsent or
/// Authenticating STOPS it — a human at the prompter takes as long as they need,
/// bounded by the agent's prompter, not by us.
///
/// @return true if @p op reached a terminal state; false if the stall backstop
///         fired first (a genuine machine-phase stall — never a consent wait).
bool driveToFinished(AgentOperation* op, int opStallTimeoutMs)
{
    if (op->isFinished()) {
        return true; // recovered synchronously (lost-Finished path)
    }
    QEventLoop loop;
    bool finished = false;
    QObject::connect(op, &AgentOperation::finished, &loop, [&loop, &finished]() {
        finished = true;
        loop.quit();
    });

    QTimer stall;
    stall.setSingleShot(true);
    QObject::connect(&stall, &QTimer::timeout, &loop, &QEventLoop::quit);
    const auto armForPhase = [&stall, opStallTimeoutMs](OperationPhase phase) {
        const bool humanInLoop = (phase == OperationPhase::AwaitingConsent || phase == OperationPhase::Authenticating);
        if (humanInLoop) {
            stall.stop(); // the human is thinking at the prompter — never time out
        } else {
            stall.start(opStallTimeoutMs); // machine phase — bound it, reset on each tick
        }
    };
    QObject::connect(op, &AgentOperation::phaseChanged, &loop,
                     [&armForPhase](OperationPhase phase, double) { armForPhase(phase); });
    armForPhase(OperationPhase::Created); // arm for the pre-first-signal machine phase

    // Re-check after wiring: finished may have arrived between the guard and the
    // connect on a queued emit.
    if (op->isFinished()) {
        return true;
    }
    loop.exec();
    return finished || op->isFinished();
}

CertInfoView toCertView(const CertificateInfo& c)
{
    CertInfoView v;
    v.certId = c.certId;
    v.signingCapable = c.signingCapable;
    v.subjectCn = c.subjectCn;
    v.issuerCn = c.issuerCn;
    v.notAfter = c.notAfter;
    v.keyUsageBits = c.keyUsageBits;
    v.extendedKeyUsageOids = c.extendedKeyUsageOids;
    v.chainSubjectCns = c.chainSubjectCns;
    v.trustStatus = c.trustStatus;
    return v;
}

} // namespace

AgentCardDataSource::AgentCardDataSource(AgentClient& client) : AgentCardDataSource(client, kOpStallTimeoutMs) {}

AgentCardDataSource::AgentCardDataSource(AgentClient& client, int opStallTimeoutMs)
    : m_client(client), m_opStallTimeoutMs(opStallTimeoutMs)
{}

AgentCardDataSource::~AgentCardDataSource() = default;

QList<CardPresence> AgentCardDataSource::listReadersWithCards()
{
    // Zero card I/O: purely the agent's ObjectManager registry.
    QList<CardPresence> out;
    const QList<AgentReader*> readers = m_client.readers();
    for (AgentReader* reader : readers) {
        if (reader == nullptr || !reader->hasCard()) {
            continue;
        }
        AgentCard* card = m_client.card(reader->cardPath());
        if (card == nullptr) {
            continue;
        }
        CardPresence p;
        p.readerName = reader->name();
        p.cardPath = card->path();
        p.capabilities = card->capabilities();
        p.preReadAuth = card->preReadAuthWire(); // verbatim wire token, no round-trip
        out << p;
    }
    return out;
}

IdentityResult AgentCardDataSource::readIdentity(const QString& cardPath)
{
    IdentityResult result;
    AgentCard* card = m_client.card(cardPath);
    if (card == nullptr) {
        result.status = ReadStatus::Unavailable;
        return result;
    }
    AgentOperation* op = card->readIdentity();
    if (op == nullptr) {
        result.status = ReadStatus::CapabilityMissing;
        return result;
    }
    // If the card is pulled (or the agent dies) while driveToFinished() spins its
    // nested loop, AgentClient::onInterfacesRemoved terminalizes the op (quitting
    // the loop) and then `delete card` destroys this QObject-parented `op`. Guard
    // the op with a QPointer and re-check it BEFORE any post-loop deref, mirroring
    // SignJob's QPointer<AgentCard> idiom (SignJob.cpp:29-33,174). Without this the
    // op->status()/identityResult() below would dereference freed memory (UAF).
    QPointer<AgentOperation> guard(op);
    const bool reachedTerminal = driveToFinished(op, m_opStallTimeoutMs);
    if (!guard) {
        result.status = ReadStatus::CardRemoved;
        return result;
    }
    if (!reachedTerminal) {
        // Genuine machine-phase stall (a consent wait suppresses the backstop, so
        // this is never a CAN wait). Ask the agent to abandon; surface Unavailable.
        op->cancel();
        result.status = ReadStatus::Unavailable;
        return result;
    }
    result.status = mapStatus(op->status(), op->errorCode());
    if (result.status != ReadStatus::Ok) {
        return result;
    }

    for (const IdentityRow& row : flattenIdentityFields(op->identityResult())) {
        IdentityFieldView view;
        view.group = row.groupKey;
        view.fieldKey = row.fieldKey;
        // Localize via the frozen labelKey (shared resolver — the SAME rule the
        // plasmoid uses); this is the display label renderIdentityTxt emits.
        view.labelFallback = localizedFieldLabel(row);
        view.value = row.value;
        result.fields << view;
    }
    return result;
}

CertListResult AgentCardDataSource::readCertificates(const QString& cardPath)
{
    CertListResult result;
    AgentCard* card = m_client.card(cardPath);
    if (card == nullptr) {
        result.status = ReadStatus::Unavailable;
        return result;
    }
    AgentOperation* op = card->readCertificates();
    if (op == nullptr) {
        result.status = ReadStatus::CapabilityMissing;
        return result;
    }
    // See readIdentity(): driveToFinished() may spin while the card is pulled, in
    // which case onInterfacesRemoved deletes the op mid-loop. Re-check the QPointer
    // before any post-loop deref to avoid a UAF.
    QPointer<AgentOperation> guard(op);
    const bool reachedTerminal = driveToFinished(op, m_opStallTimeoutMs);
    if (!guard) {
        result.status = ReadStatus::CardRemoved;
        return result;
    }
    if (!reachedTerminal) {
        // Genuine machine-phase stall (a consent wait suppresses the backstop, so
        // this is never a CAN wait). Ask the agent to abandon; surface Unavailable.
        op->cancel();
        result.status = ReadStatus::Unavailable;
        return result;
    }
    result.status = mapStatus(op->status(), op->errorCode());
    if (result.status != ReadStatus::Ok) {
        return result;
    }
    for (const CertificateInfo& c : op->certificatesResult()) {
        result.certs << toCertView(c);
    }
    return result;
}

PhotoResult AgentCardDataSource::getPhoto(const QString& cardPath)
{
    PhotoResult result;
    AgentCard* card = m_client.card(cardPath);
    if (card == nullptr) {
        result.status = ReadStatus::Unavailable;
        return result;
    }
    AgentOperation* op = card->getPhoto();
    if (op == nullptr) {
        result.status = ReadStatus::CapabilityMissing;
        return result;
    }
    // See readIdentity(): driveToFinished() may spin while the card is pulled, in
    // which case onInterfacesRemoved deletes the op mid-loop. Re-check the QPointer
    // before any post-loop deref to avoid a UAF.
    QPointer<AgentOperation> guard(op);
    const bool reachedTerminal = driveToFinished(op, m_opStallTimeoutMs);
    if (!guard) {
        result.status = ReadStatus::CardRemoved;
        return result;
    }
    if (!reachedTerminal) {
        // Genuine machine-phase stall (a consent wait suppresses the backstop, so
        // this is never a CAN wait). Ask the agent to abandon; surface Unavailable.
        op->cancel();
        result.status = ReadStatus::Unavailable;
        return result;
    }
    result.status = mapStatus(op->status(), op->errorCode());
    if (result.status != ReadStatus::Ok) {
        return result;
    }
    const PhotoMap& photos = op->photoResult();
    if (photos.isEmpty()) {
        // No photo on this card — surface as a not-available leaf, not an error.
        result.status = ReadStatus::NotAvailable;
        return result;
    }
    // DEFERRED: a multi-photo card exposes only the first PhotoMap
    // entry here; a future layout will surface every "groupKey:fieldKey" photo.
    result.bytes = readSealedFd(photos.constBegin().value());
    if (result.bytes.isEmpty()) {
        // An entry present but empty-on-read is a benign absent photo, matching the
        // empty-map branch above (NotAvailable → ERR_DOES_NOT_EXIST) and the
        // plasmoid's benign-absence handling — NOT a worker-died error.
        result.status = ReadStatus::NotAvailable;
    }
    return result;
}

CertDerResult AgentCardDataSource::getCertificateDer(const QString& cardPath, const QString& certId)
{
    CertDerResult result;
    AgentCard* card = m_client.card(cardPath);
    if (card == nullptr) {
        result.status = ReadStatus::Unavailable;
        return result;
    }
    // CertDer is public data (no consent, no lease) addressed by the card's
    // Reader1 path + certId. No card I/O op / Operation is minted — a single
    // capped blocking call straight to the manager's Pkcs11_1 surface.
    const AgentCertDer reply = m_client.certificateDer(card->readerPath(), certId);
    if (reply.ok) {
        result.status = ReadStatus::Ok;
        result.der = reply.der;
        return result;
    }
    // A missing cert/card is "does not exist", not a worker death; anything else
    // (comms/agent-internal) is a generic read error.
    if (reply.errorName.endsWith(QLatin1String(".KeyNotFound")) ||
        reply.errorName.endsWith(QLatin1String(".UnknownCard"))) {
        result.status = ReadStatus::NotAvailable;
    } else if (reply.errorName.endsWith(QLatin1String(".Unavailable"))) {
        result.status = ReadStatus::Unavailable;
    } else {
        result.status = ReadStatus::Error;
    }
    return result;
}

} // namespace LibreKDE
