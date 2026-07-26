// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "AgentOperation.h"

#include "AgentDBus.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusVariant>
#include <QLoggingCategory>
#include <QVariant>

namespace LibreKDE {

namespace {
Q_LOGGING_CATEGORY(lcAgentOp, "librekde.agentclient.operation")
} // namespace

// Wire interface names live in AgentDBus.h (shared across agentclient TUs).

// (sssv) struct marshalling for the Identity1 field tuple.
QDBusArgument& operator<<(QDBusArgument& arg, const IdentityField& f)
{
    arg.beginStructure();
    arg << f.labelKey << f.labelFallback << f.type << f.value;
    arg.endStructure();
    return arg;
}
const QDBusArgument& operator>>(const QDBusArgument& arg, IdentityField& f)
{
    arg.beginStructure();
    arg >> f.labelKey >> f.labelFallback >> f.type >> f.value;
    arg.endStructure();
    return arg;
}

// (ssv) struct marshalling for the Certificates1 field tuple (NO type string —
// distinct from Identity1's (sssv)).
QDBusArgument& operator<<(QDBusArgument& arg, const CertField& f)
{
    arg.beginStructure();
    arg << f.labelKey << f.labelFallback << f.value;
    arg.endStructure();
    return arg;
}
const QDBusArgument& operator>>(const QDBusArgument& arg, CertField& f)
{
    arg.beginStructure();
    arg >> f.labelKey >> f.labelFallback >> f.value;
    arg.endStructure();
    return arg;
}

// Certificates1 cert struct `(s b a{sa{s(ssv)}} u as as u)`. The nested
// field-group maps are the registered CertFieldGroups type, so Qt derives the
// exact `a{sa{s(ssv)}}` signature in both directions (the FakeAgent reuses the
// `<<` to emit a faithful payload; the client uses `>>`). Both must exist for
// qDBusRegisterMetaType<CertificateList>().
QDBusArgument& operator<<(QDBusArgument& arg, const CertificateInfo& c)
{
    CertFieldGroups fields;
    if (!c.subjectCn.isEmpty()) {
        fields[QStringLiteral("subject")][QStringLiteral("cn")] = {
            QStringLiteral("label_subject_cn"), QStringLiteral("Subject CN"), QDBusVariant(c.subjectCn)};
    }
    if (!c.issuerCn.isEmpty()) {
        fields[QStringLiteral("issuer")][QStringLiteral("cn")] = {
            QStringLiteral("label_issuer_cn"), QStringLiteral("Issuer CN"), QDBusVariant(c.issuerCn)};
    }
    if (!c.notAfter.isEmpty()) {
        fields[QStringLiteral("validity")][QStringLiteral("notAfter")] = {
            QStringLiteral("label_not_after"), QStringLiteral("Not after"), QDBusVariant(c.notAfter)};
    }

    // chainSubjectCns falls back to [leaf CN] when the caller left it empty (the
    // single-entry chain for now), so an operator<<-built payload always has a
    // non-empty chain like the agent's.
    const QStringList chain = c.chainSubjectCns.isEmpty() ? QStringList{c.subjectCn} : c.chainSubjectCns;

    arg.beginStructure();
    arg << c.certId << c.signingCapable << fields;
    arg << static_cast<uint>(c.keyUsageBits); // keyUsageBits
    arg << c.extendedKeyUsageOids;            // extendedKeyUsageOids
    arg << chain;                             // chainSubjectCns
    arg << static_cast<uint>(c.trustStatus);  // trustStatus
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, CertificateInfo& c)
{
    CertFieldGroups fields;
    arg.beginStructure();
    arg >> c.certId >> c.signingCapable >> fields;

    // Trailing members: u keyUsageBits, as EKU OIDs, as chainSubjectCns,
    // u trustStatus — RETAINED for the card:/ KIO worker (purpose-named folders +
    // cert info.txt). keyUsageBits/trustStatus are `u`; the client localizes the
    // KeyUsage bitmask and renders the trust verdict (Unknown until evaluated).
    uint keyUsageBits = 0;
    uint trustStatus = 0;
    arg >> keyUsageBits >> c.extendedKeyUsageOids >> c.chainSubjectCns >> trustStatus;
    c.keyUsageBits = keyUsageBits;
    c.trustStatus = trustStatus;
    arg.endStructure();

    // Pull the display CNs out of the demarshaled field-groups. Each field
    // value is a D-Bus variant `v` (the agent wire is (ssv)); the string lives
    // inside it.
    if (const auto subj = fields.constFind(QStringLiteral("subject")); subj != fields.constEnd()) {
        c.subjectCn = subj->value(QStringLiteral("cn")).value.variant().toString();
    }
    if (const auto iss = fields.constFind(QStringLiteral("issuer")); iss != fields.constEnd()) {
        c.issuerCn = iss->value(QStringLiteral("cn")).value.variant().toString();
    }
    if (const auto val = fields.constFind(QStringLiteral("validity")); val != fields.constEnd()) {
        c.notAfter = val->value(QStringLiteral("notAfter")).value.variant().toString();
    }
    return arg;
}

namespace {
void registerIdentityMetatypes()
{
    static bool done = false;
    if (done) {
        return;
    }
    qDBusRegisterMetaType<IdentityField>();
    qDBusRegisterMetaType<IdentityFieldGroup>();
    qDBusRegisterMetaType<IdentityFields>();
    qDBusRegisterMetaType<CertField>();
    qDBusRegisterMetaType<CertFieldGroup>();
    qDBusRegisterMetaType<CertFieldGroups>();
    qDBusRegisterMetaType<CertificateInfo>();
    qDBusRegisterMetaType<CertificateList>();
    qDBusRegisterMetaType<PhotoMap>();
    done = true;
}
} // namespace

struct AgentOperation::Private
{
    QDBusConnection connection;
    QString service;
    QString path;
    QString typedInterface;

    bool finishedEmitted = false;
    bool resultSeen = false;
    // True only while the ctor's lost-Finished recovery runs — BEFORE
    // AgentCard::startOperation returns the op and the consumer connects. In that
    // window the terminal `finished` + typed `*ResultReady` emits must be QUEUED
    // (a synchronous Q_EMIT would be lost). On every other path (the live
    // onFinished signal, the terminate() death-sweep) the consumer is already
    // subscribed AND — for the death-sweep — the op is deleted synchronously
    // right after, so the emit MUST be synchronous or it is never delivered.
    bool inCtorRecovery = false;
    OperationStatus status = OperationStatus::Error;
    ErrorCode errorCode = ErrorCode::None;

    SignResult signResult;
    IdentityFields identityResult;
    CertificateList certificatesResult;
    PhotoMap photoResult;
    PinResult pinResult;
    CredentialList credentialsResult;

    explicit Private(const QDBusConnection& c) : connection(c) {}
};

AgentOperation::AgentOperation(const QDBusConnection& connection, const QString& service, const QString& operationPath,
                               const QString& typedInterface, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>(connection))
{
    qRegisterMetaType<OperationPhase>();
    qRegisterMetaType<OperationStatus>();
    qRegisterMetaType<ErrorCode>();
    qRegisterMetaType<CertificateList>();
    registerIdentityMetatypes();

    d->service = service;
    d->path = operationPath;
    d->typedInterface = typedInterface;

    // Operation1.Finished(u status, u errorCode, s msgKey, s msgFallback).
    d->connection.connect(d->service, d->path, QLatin1String(kOperationIface), QStringLiteral("Finished"), this,
                          SLOT(onFinished(uint, uint, QString, QString)));

    // Properties.PropertiesChanged for live Phase / Progress.
    d->connection.connect(d->service, d->path, QLatin1String(kPropertiesIface), QStringLiteral("PropertiesChanged"),
                          this, SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));

    // Typed Result signal — connected before any recovery read so we never miss
    // a result that fires between construction and the recovery probe.
    if (d->typedInterface == QLatin1String(kSignIface)) {
        d->connection.connect(d->service, d->path, QLatin1String(kSignIface), QStringLiteral("Result"), this,
                              SLOT(onSignResult(QDBusUnixFileDescriptor, QVariantMap)));
    } else if (d->typedInterface == QLatin1String(kIdentityIface)) {
        // a{sa{s(sssv)}} demarshaled into the registered IdentityFields type.
        d->connection.connect(d->service, d->path, QLatin1String(kIdentityIface), QStringLiteral("Result"), this,
                              SLOT(onIdentityResult(LibreKDE::IdentityFields)));
    } else if (d->typedInterface == QLatin1String(kCertificatesIface)) {
        // a(sba{sa{s(ssv)}}uasasu) demarshaled into the registered CertificateList.
        d->connection.connect(d->service, d->path, QLatin1String(kCertificatesIface), QStringLiteral("Result"), this,
                              SLOT(onCertificatesResult(LibreKDE::CertificateList)));
    } else if (d->typedInterface == QLatin1String(kPhotoIface)) {
        // a{sh} (sealed-memfd photo map) demarshaled into the registered PhotoMap.
        d->connection.connect(d->service, d->path, QLatin1String(kPhotoIface), QStringLiteral("Result"), this,
                              SLOT(onPhotoResult(LibreKDE::PhotoMap)));
    } else if (d->typedInterface == QLatin1String(kOperationCredentialsIface)) {
        // (a{sv} result, aa{sv} records) — the two-out-arg credentials Result.
        // registerCredentialMetatypes() MUST run BEFORE connect so the aa{sv}
        // records demarshal into the registered CredentialRecordsWire.
        registerCredentialMetatypes();
        d->connection.connect(d->service, d->path, QLatin1String(kOperationCredentialsIface), QStringLiteral("Result"),
                              this, SLOT(onCredentialsResult(QVariantMap, LibreKDE::CredentialRecordsWire)));
    }

    // Guard the recovery window: any terminal/result emit it triggers is queued
    // so a consumer connecting right after this ctor returns still receives it.
    d->inCtorRecovery = true;
    recoverIfAlreadyFinished();
    d->inCtorRecovery = false;
}

AgentOperation::~AgentOperation() = default;

QString AgentOperation::path() const
{
    return d->path;
}

bool AgentOperation::isFinished() const
{
    return d->finishedEmitted;
}

OperationStatus AgentOperation::status() const
{
    return d->status;
}

ErrorCode AgentOperation::errorCode() const
{
    return d->errorCode;
}

const SignResult& AgentOperation::signResult() const
{
    return d->signResult;
}

const IdentityFields& AgentOperation::identityResult() const
{
    return d->identityResult;
}

const CertificateList& AgentOperation::certificatesResult() const
{
    return d->certificatesResult;
}

const PhotoMap& AgentOperation::photoResult() const
{
    return d->photoResult;
}

const PinResult& AgentOperation::pinResult() const
{
    return d->pinResult;
}

const CredentialList& AgentOperation::credentialsResult() const
{
    return d->credentialsResult;
}

void AgentOperation::cancel()
{
    // Fire-and-forget the Cancel via a low-level method call. A QDBusInterface
    // would block on a synchronous Introspect() before the asyncCall — a
    // multi-second GUI freeze on a wedged agent precisely when the user clicked
    // Cancel to escape it.
    QDBusMessage call =
        QDBusMessage::createMethodCall(d->service, d->path, QLatin1String(kOperationIface), QStringLiteral("Cancel"));
    d->connection.asyncCall(call);
}

void AgentOperation::terminate(OperationStatus status, ErrorCode code, const QString& msgKey,
                               const QString& msgFallback)
{
    // Pure local terminalization — no bus call (the peer is gone). emitFinishedOnce
    // is idempotent, so a racing real Finished that already fired wins and this
    // is a no-op.
    emitFinishedOnce(status, code, msgKey, msgFallback);
}

void AgentOperation::onPropertiesChanged(const QString& iface, const QVariantMap& changed,
                                         const QStringList& /*invalidated*/)
{
    if (iface != QLatin1String(kOperationIface)) {
        return;
    }
    if (changed.contains(QStringLiteral("Phase")) || changed.contains(QStringLiteral("Progress"))) {
        // Phase/Progress are read straight from the `changed` map (which may
        // carry only one of them); no per-tick property Get is needed.
        const QVariant phaseV = changed.value(QStringLiteral("Phase"));
        const QVariant progressV = changed.value(QStringLiteral("Progress"));
        const auto phase = static_cast<OperationPhase>(phaseV.isValid() ? phaseV.toUInt() : 0u);
        Q_EMIT phaseChanged(phase, progressV.isValid() ? progressV.toDouble() : 0.0);
    }
}

void AgentOperation::onSignResult(const QDBusUnixFileDescriptor& fd, const QVariantMap& meta)
{
    // A live typed-Result slot, only ever dispatched by the event loop (never
    // synchronously in the ctor — the ctor's Sign recovery uses GetResult below),
    // so the consumer is already subscribed: emit synchronously.
    d->resultSeen = true;
    d->signResult.artifact = fd; // QDBusUnixFileDescriptor dup's on copy.
    d->signResult.meta = meta;
    Q_EMIT signResultReady();
}

void AgentOperation::onIdentityResult(IdentityFields fields)
{
    d->resultSeen = true;
    d->identityResult = std::move(fields);
    Q_EMIT identityResultReady();
}

void AgentOperation::onCertificatesResult(CertificateList certificates)
{
    d->resultSeen = true;
    d->certificatesResult = std::move(certificates);
    Q_EMIT certificatesResultReady();
}

void AgentOperation::onPhotoResult(PhotoMap photos)
{
    // Live typed-Result slot (dispatched by the event loop, never synchronously
    // in the ctor — Photo has no GetResult, the ctor lost-Result path fails
    // loudly via finalizeTerminal like Identity/Certificates), so the consumer
    // is already subscribed: emit synchronously. The QDBusUnixFileDescriptor
    // values dup the sealed memfds on copy (RAII); the caller mmap-reads + closes.
    d->resultSeen = true;
    d->photoResult = std::move(photos);
    Q_EMIT photoResultReady();
}

void AgentOperation::onCredentialsResult(QVariantMap result, CredentialRecordsWire records)
{
    // A live typed-Result slot (dispatched by the event loop, never synchronously
    // in the ctor — the ctor lost-Result path uses recoverCredentialsResultViaGetResult
    // below), so the consumer is already subscribed: emit synchronously. Parse
    // the uniform a{sv} mutation result AND the aa{sv} records (empty for a
    // mutation — a legitimate result, never a sentinel).
    d->resultSeen = true;
    d->pinResult = PinResult::fromVariantMap(result);
    d->credentialsResult.clear();
    d->credentialsResult.reserve(records.size());
    for (const QVariantMap& r : records) {
        d->credentialsResult.push_back(CredentialRecord::fromVariantMap(r));
    }
    Q_EMIT credentialsResultReady();
}

void AgentOperation::onFinished(uint status, uint errorCode, const QString& msgKey, const QString& msgFallback)
{
    const auto st = static_cast<OperationStatus>(status);
    const auto ec = static_cast<ErrorCode>(errorCode);
    finalizeTerminal(st, ec, msgKey, msgFallback);
}

void AgentOperation::finalizeTerminal(OperationStatus st, ErrorCode ec, const QString& msgKey,
                                      const QString& msgFallback)
{
    // Operation.Credentials1 diverges from the other typed results. Its Result fires for EVERY
    // completed attempt — the a{sv} payload carries the per-attempt outcome, so a
    // soft-fail (invalidPin / blocked) legitimately finishes Error WITH a Result.
    // Recover via GetResult whenever we have not yet seen it, REGARDLESS of Ok vs
    // Error — placed BEFORE the generic `st == Ok` block so non-Ok credentials
    // terminals are recovered too. A non-Ok outcome is NOT rewritten into a
    // CommunicationError merely because status != Ok: the loud fallback fires ONLY
    // when there is genuinely no recoverable payload (for BOTH Ok and Error). On a
    // recovered/seen payload we emit the REAL terminal (st, ec).
    if (!d->resultSeen && d->typedInterface == QLatin1String(kOperationCredentialsIface)) {
        recoverCredentialsResultViaGetResult();
        if (!d->resultSeen) {
            qCWarning(lcAgentOp)
                << "credentials op" << d->path
                << "finished but its Result was not delivered and GetResult recovery returned nothing "
                   "(lost/late signal, grace window elapsed, or an agent without GetResult); surfacing "
                   "as a communication error";
            emitFinishedOnce(OperationStatus::Error, ErrorCode::CommunicationError, msgKey, msgFallback);
            return;
        }
        emitFinishedOnce(st, ec, msgKey, msgFallback);
        return;
    }
    if (st == OperationStatus::Ok && !d->resultSeen) {
        // The typed Result signal is a one-shot: if we finished Ok but never saw
        // it (subscribed late — a hot card session finishes faster than our
        // match rules install, or the signal was otherwise lost), EVERY typed
        // interface now offers a GetResult pull that re-serves the retained
        // payload while the op survives the agent's cleanup-grace window. Try it
        // before surfacing a communication error. Version-skew safe: an agent
        // WITHOUT the method answers GetResult with an UnknownMethod error reply,
        // which leaves resultSeen false and drops us into the loud fallback below
        // (today's behavior, preserved).
        if (d->typedInterface == QLatin1String(kSignIface)) {
            recoverSignResultViaGetResult();
        } else {
            recoverInlineResultViaGetResult();
        }
        if (!d->resultSeen) {
            // GetResult yielded nothing (NoResult / grace window elapsed / an
            // agent without GetResult). Don't claim Ok with an empty payload the
            // caller cannot distinguish from a genuinely empty read (Sign: a null
            // fd). Fail loudly so the caller can retry / report a communication
            // fault.
            qCWarning(lcAgentOp) << "operation" << d->path
                                 << "finished Ok but its typed Result was not delivered and GetResult recovery "
                                    "returned no result (lost/late signal, grace window elapsed, or an agent "
                                    "without GetResult); surfacing as a communication error";
            emitFinishedOnce(OperationStatus::Error, ErrorCode::CommunicationError, msgKey, msgFallback);
            return;
        }
    }
    emitFinishedOnce(st, ec, msgKey, msgFallback);
}

void AgentOperation::emitFinishedOnce(OperationStatus status, ErrorCode code, const QString& msgKey,
                                      const QString& msgFallback)
{
    if (d->finishedEmitted) {
        return;
    }
    // Set the polled terminal state SYNCHRONOUSLY: isFinished()/status()/
    // errorCode() (+ the result members, already set by the onXResult slots /
    // GetResult recovery) must be true the instant this returns, so the ctor's
    // synchronous recovery still reports a terminal op and existing polled-state
    // asserts stay green.
    d->finishedEmitted = true;
    d->status = status;
    d->errorCode = code;

    // Defer the signal to a queued event-loop turn ONLY inside the ctor's
    // lost-Finished recovery (before the consumer connects — a synchronous emit
    // would be lost). On the live onFinished path the consumer is already
    // subscribed; on the terminate() death-sweep the op is deleted synchronously
    // right after, so the emit MUST be synchronous there or it is never
    // delivered (a queued event on a deleted QObject is dropped).
    if (d->inCtorRecovery) {
        QMetaObject::invokeMethod(
            this, [this, status, code, msgKey, msgFallback]() { Q_EMIT finished(status, code, msgKey, msgFallback); },
            Qt::QueuedConnection);
    } else {
        Q_EMIT finished(status, code, msgKey, msgFallback);
    }
}

void AgentOperation::recoverSignResultViaGetResult()
{
    // Direct method call with a capped timeout — avoids QDBusInterface's blocking
    // introspection round-trip and never lets a wedged agent stall the (often
    // main) thread waiting out the multi-second default D-Bus timeout.
    QDBusMessage call =
        QDBusMessage::createMethodCall(d->service, d->path, QLatin1String(kSignIface), QStringLiteral("GetResult"));
    QDBusMessage reply = d->connection.call(call, QDBus::Block, kPropTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        return; // NoResult / grace window elapsed / timeout — leave signResult null.
    }
    const QList<QVariant> args = reply.arguments();
    if (args.size() < 2) {
        return;
    }
    // Positional extraction matching the frozen Sign1.xml out-arg order
    // (h signedArtifact, a{sv} meta). args[0] is the fd, args[1] the meta map.
    QDBusUnixFileDescriptor fd = qvariant_cast<QDBusUnixFileDescriptor>(args.at(0));
    QVariantMap meta;
    const QVariant& metaArg = args.at(1);
    if (metaArg.metaType().id() == qMetaTypeId<QDBusArgument>()) {
        metaArg.value<QDBusArgument>() >> meta;
    } else {
        meta = metaArg.toMap();
    }
    // Defensive fallback: if a future binding ever reorders the out-args, locate
    // the fd by type. (The wire order is frozen; this should never fire.)
    if (!fd.isValid() && args.at(1).metaType().id() == qMetaTypeId<QDBusUnixFileDescriptor>()) {
        fd = qvariant_cast<QDBusUnixFileDescriptor>(args.at(1));
        if (args.at(0).metaType().id() == qMetaTypeId<QDBusArgument>()) {
            meta.clear();
            args.at(0).value<QDBusArgument>() >> meta;
        }
    }
    if (!fd.isValid()) {
        return; // NoResult — leave signResult null.
    }
    d->signResult.artifact = fd;
    d->signResult.meta = meta;
    d->resultSeen = true;
    emitResultReadyQueuedOrSync(&AgentOperation::signResultReady);
}

void AgentOperation::recoverInlineResultViaGetResult()
{
    // The inline typed results (Identity/Certificates/Photo) each expose a
    // GetResult returning the SAME single-out-arg wire shape as their Result
    // signal, so ONE parameterised recovery covers all three: a capped
    // low-level call (never QDBusInterface's blocking introspection, never the
    // multi-second default D-Bus timeout on a wedged agent), then demarshal the
    // reply's sole argument into the matching registered type. A non-reply
    // (Error.NoResult / grace elapsed / an agent without the method / timeout)
    // leaves resultSeen false so finalizeTerminal falls through to the loud
    // CommunicationError. Photo values arrive as sealed memfds the caller
    // mmap-reads + closes (QDBusUnixFileDescriptor dups on copy).
    QDBusMessage call =
        QDBusMessage::createMethodCall(d->service, d->path, d->typedInterface, QStringLiteral("GetResult"));
    QDBusMessage reply = d->connection.call(call, QDBus::Block, kPropTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        return; // NoResult / UnknownMethod (version skew) / timeout — leave result empty.
    }
    const QList<QVariant> args = reply.arguments();
    if (args.isEmpty()) {
        return;
    }
    const QVariant& arg = args.at(0);
    const bool isDbusArg = arg.metaType().id() == qMetaTypeId<QDBusArgument>();

    if (d->typedInterface == QLatin1String(kIdentityIface)) {
        IdentityFields fields;
        if (isDbusArg) {
            arg.value<QDBusArgument>() >> fields;
        }
        if (fields.isEmpty()) {
            return; // an empty payload is indistinguishable from a genuine empty read; fail loud.
        }
        d->identityResult = std::move(fields);
        d->resultSeen = true;
        emitResultReadyQueuedOrSync(&AgentOperation::identityResultReady);
    } else if (d->typedInterface == QLatin1String(kCertificatesIface)) {
        CertificateList certs;
        if (isDbusArg) {
            arg.value<QDBusArgument>() >> certs;
        }
        if (certs.isEmpty()) {
            return;
        }
        d->certificatesResult = std::move(certs);
        d->resultSeen = true;
        emitResultReadyQueuedOrSync(&AgentOperation::certificatesResultReady);
    } else if (d->typedInterface == QLatin1String(kPhotoIface)) {
        PhotoMap photos;
        if (isDbusArg) {
            arg.value<QDBusArgument>() >> photos;
        }
        if (photos.isEmpty()) {
            return;
        }
        d->photoResult = std::move(photos);
        d->resultSeen = true;
        emitResultReadyQueuedOrSync(&AgentOperation::photoResultReady);
    }
}

void AgentOperation::recoverCredentialsResultViaGetResult()
{
    // The credentials GetResult returns the SAME two-out-arg shape as its Result
    // signal — (a{sv} result, aa{sv} records) — so this mirrors
    // recoverSignResultViaGetResult (two out-args), NOT the single-arg inline
    // path. A capped low-level call (never QDBusInterface's blocking
    // introspection, never the multi-second default D-Bus timeout on a wedged
    // agent), then demarshal both out-args. A non-reply (Error.NoResult / grace
    // elapsed / UnknownMethod version skew / timeout) leaves resultSeen false so
    // finalizeTerminal falls through to the loud CommunicationError.
    QDBusMessage call = QDBusMessage::createMethodCall(d->service, d->path, QLatin1String(kOperationCredentialsIface),
                                                       QStringLiteral("GetResult"));
    QDBusMessage reply = d->connection.call(call, QDBus::Block, kPropTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        return; // NoResult / grace window elapsed / timeout — leave the result unset.
    }
    const QList<QVariant> args = reply.arguments();
    if (args.size() < 2) {
        return;
    }
    // Positional extraction matching the frozen Operation.Credentials1 out-arg
    // order (a{sv} result, aa{sv} records). A remote reply arrives as a
    // QDBusArgument, a local one as a plain container.
    QVariantMap result;
    const QVariant& resultArg = args.at(0);
    if (resultArg.metaType().id() == qMetaTypeId<QDBusArgument>()) {
        resultArg.value<QDBusArgument>() >> result;
    } else {
        result = resultArg.toMap();
    }
    CredentialRecordsWire records;
    const QVariant& recordsArg = args.at(1);
    if (recordsArg.metaType().id() == qMetaTypeId<QDBusArgument>()) {
        recordsArg.value<QDBusArgument>() >> records;
    }
    // A mutation legitimately returns an EMPTY records list, so do NOT treat
    // empty-records as "no result": the presence of a Result REPLY is the signal.
    // Parse `result` regardless (credentialsResult may be empty).
    d->pinResult = PinResult::fromVariantMap(result);
    d->credentialsResult.clear();
    d->credentialsResult.reserve(records.size());
    for (const QVariantMap& r : records) {
        d->credentialsResult.push_back(CredentialRecord::fromVariantMap(r));
    }
    d->resultSeen = true;
    emitResultReadyQueuedOrSync(&AgentOperation::credentialsResultReady);
}

void AgentOperation::emitResultReadyQueuedOrSync(void (AgentOperation::*signal)())
{
    // The ctor's lost-Finished recovery runs BEFORE the consumer connects, so a
    // synchronous Q_EMIT would be lost — queue it to the next event-loop turn.
    // On every live path the consumer is already subscribed, so emit
    // synchronously. Mirrors emitFinishedOnce's inCtorRecovery gate.
    if (d->inCtorRecovery) {
        QMetaObject::invokeMethod(this, [this, signal]() { (this->*signal)(); }, Qt::QueuedConnection);
    } else {
        (this->*signal)();
    }
}

void AgentOperation::recoverIfAlreadyFinished()
{
    // Read the terminal triple (Completed/Status/ErrorCode). If the op already
    // finished before our match rules installed, synthesize the missed
    // Finished + recover the typed payload.
    //
    // ONE capped low-level Properties.GetAll — not a QDBusInterface (blocking
    // introspection round-trip) plus three uncapped Get calls (each waiting out
    // the ~25 s default D-Bus timeout on a wedged agent, ~75-100 s of GUI
    // freeze in the ctor). Mirrors recoverSignResultViaGetResult's capped
    // pattern.
    QDBusMessage call =
        QDBusMessage::createMethodCall(d->service, d->path, QLatin1String(kPropertiesIface), QStringLiteral("GetAll"));
    call.setArguments(QList<QVariant>{QLatin1String(kOperationIface)});
    QDBusMessage reply = d->connection.call(call, QDBus::Block, kPropTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return; // Introspection/read failed or timed out — the live signal drives us.
    }

    QVariantMap props;
    const QList<QVariant> replyArgs = reply.arguments();
    const QVariant& mapArg = replyArgs.constFirst();
    if (mapArg.metaType().id() == qMetaTypeId<QDBusArgument>()) {
        mapArg.value<QDBusArgument>() >> props;
    } else {
        props = mapArg.toMap();
    }

    if (!props.value(QStringLiteral("Completed")).toBool()) {
        return; // Not finished yet — the live signal will drive us.
    }

    const QVariant statusV = props.value(QStringLiteral("Status"));
    const QVariant errorV = props.value(QStringLiteral("ErrorCode"));
    const auto st = static_cast<OperationStatus>(statusV.isValid() ? statusV.toUInt() : 2u);
    const auto ec = static_cast<ErrorCode>(errorV.isValid() ? errorV.toUInt() : 0u);

    // The terminal triple carries no msgKey/msgFallback; clients map ErrorCode.
    finalizeTerminal(st, ec, QString(), QString());
}

} // namespace LibreKDE
