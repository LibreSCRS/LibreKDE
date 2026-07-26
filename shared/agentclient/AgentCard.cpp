// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "AgentCard.h"

#include "AgentDBus.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QLoggingCategory>
#include <QVariant>

namespace {
Q_LOGGING_CATEGORY(lcAgentCard, "librekde.agentclient.card")
} // namespace

namespace LibreKDE {

namespace {
// Wire interface names live in AgentDBus.h (shared across agentclient TUs).

PreReadAuth parseAuth(const QString& s)
{
    if (s == QLatin1String("Mrz")) {
        return PreReadAuth::Mrz;
    }
    if (s == QLatin1String("Can")) {
        return PreReadAuth::Can;
    }
    return PreReadAuth::None;
}
} // namespace

AgentCard::AgentCard(const QDBusConnection& connection, const QString& service, const QString& path, QObject* parent)
    : QObject(parent), m_connection(connection), m_service(service), m_path(path)
{
    m_connection.connect(m_service, m_path, QLatin1String(kPropertiesIface), QStringLiteral("PropertiesChanged"), this,
                         SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    // NO ctor refreshAll(): AgentClient::addCard always calls primeFrom() right
    // after construction with the full Card1 map from GetManagedObjects /
    // InterfacesAdded, so an introspecting blocking GetAll here would be a
    // redundant cold-start round-trip (and the QDBusInterface ctor it used to
    // run issued a synchronous, UNCAPPED Introspect() on the GUI thread).
    // refreshAll() now exists only for the onPropertiesChanged invalidated
    // fallback, and is asynchronous + capped.
}

AgentCard::~AgentCard() = default;

QString AgentCard::path() const
{
    return m_path;
}

std::uint32_t AgentCard::capabilities() const
{
    return m_capabilities;
}

PreReadAuth AgentCard::preReadAuthMethod() const
{
    return parseAuth(m_preReadAuth);
}

QString AgentCard::preReadAuthWire() const
{
    return m_preReadAuth;
}

QString AgentCard::readerPath() const
{
    return m_readerPath;
}

void AgentCard::primeFrom(const QVariantMap& card1Props)
{
    // Discovery-fresh data: bump the generation so a GetAll refresh still in
    // flight cannot overwrite this newer state when its older reply lands.
    ++m_propsGeneration;
    applyProps(card1Props);
}

void AgentCard::applyProps(const QVariantMap& props)
{
    if (props.contains(QStringLiteral("Capabilities"))) {
        m_capabilities = props.value(QStringLiteral("Capabilities")).toUInt();
    }
    if (props.contains(QStringLiteral("PreReadAuthMethod"))) {
        m_preReadAuth = props.value(QStringLiteral("PreReadAuthMethod")).toString();
    }
    if (props.contains(QStringLiteral("Reader"))) {
        m_readerPath = qvariant_cast<QDBusObjectPath>(props.value(QStringLiteral("Reader"))).path();
    }
}

void AgentCard::onPropertiesChanged(const QString& iface, const QVariantMap& changedProps,
                                    const QStringList& invalidated)
{
    if (iface != QLatin1String(kCardIface)) {
        return;
    }
    // The agent ships the full new values in the `changed` map (PresenceModel
    // updates emit complete PropertyMaps), so apply them directly — no blocking
    // per-property Get round-trips on the (often main) thread. Only fall back to
    // a single GetAll if the agent marked any property `invalidated`. The direct
    // apply is the newest known state: bump the generation so a GetAll still in
    // flight cannot clobber it when its older reply lands later.
    ++m_propsGeneration;
    applyProps(changedProps);
    if (!invalidated.isEmpty()) {
        refreshAll();
    }
    Q_EMIT changed();
}

void AgentCard::refreshAll()
{
    // Non-blocking, capped low-level Properties.GetAll — NOT a QDBusInterface,
    // whose ctor would issue a synchronous, uncapped Introspect() round-trip.
    // This runs inside the PropertiesChanged slot on the consumer's (often
    // main/GUI) thread, so even the capped blocking call it replaced could
    // stall the UI for seconds per burst against a slow/wedged agent; asyncCall
    // + QDBusPendingCallWatcher returns immediately instead. The watcher's
    // finished() is delivered through the event loop of the thread the watcher
    // lives in (Qt queues the completion to the watcher object), so the apply
    // below stays on this object's thread and changed() is emitted there.
    //
    // Supersede any refresh still in flight rather than stacking watchers: its
    // reply may predate this newer invalidation, and only the newest snapshot
    // request may apply. This bounds the client to one live watcher per proxy
    // no matter how fast invalidation bursts arrive. The generation guard
    // below additionally protects against a reply landing after a NEWER direct
    // apply from onPropertiesChanged/primeFrom — and a refresh cancelled that
    // way re-issues itself, so the invalidated property still converges.
    if (m_refreshWatcher != nullptr) {
        m_refreshWatcher->disconnect(this);
        m_refreshWatcher->deleteLater();
        m_refreshWatcher = nullptr;
    }
    const quint64 generation = ++m_propsGeneration;
    QDBusMessage call =
        QDBusMessage::createMethodCall(m_service, m_path, QLatin1String(kPropertiesIface), QStringLiteral("GetAll"));
    call.setArguments(QList<QVariant>{QLatin1String(kCardIface)});
    auto* watcher = new QDBusPendingCallWatcher(m_connection.asyncCall(call, kPropTimeoutMs), this);
    m_refreshWatcher = watcher;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        const bool wasCurrent = (m_refreshWatcher == w);
        if (wasCurrent) {
            m_refreshWatcher = nullptr;
        }
        if (generation != m_propsGeneration) {
            // Stale: newer state was applied after this GetAll was sent. A
            // superseded watcher never reaches this lambda (superseding
            // disconnects it), so `wasCurrent` + a generation mismatch means a
            // DIRECT apply raced this refresh with no successor GetAll behind
            // it — the invalidated property that motivated this fetch has not
            // converged. Re-issue the refresh under the newest generation so
            // it does; otherwise it would keep its pre-invalidation value
            // until the next unrelated signal.
            if (wasCurrent) {
                refreshAll();
            }
            return;
        }
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            return; // Read failed or timed out — leave cached values.
        }
        // Dual demarshal, mirroring AgentOperation::recoverIfAlreadyFinished:
        // a remote reply arrives as a QDBusArgument, a local one as a plain map.
        QVariantMap props;
        const QList<QVariant> replyArgs = reply.arguments();
        const QVariant& mapArg = replyArgs.constFirst();
        if (mapArg.metaType().id() == qMetaTypeId<QDBusArgument>()) {
            mapArg.value<QDBusArgument>() >> props;
        } else {
            props = mapArg.toMap();
        }
        applyProps(props);
        Q_EMIT changed();
    });
}

AgentOperation* AgentCard::startOperation(const QString& method, const QString& typedInterface,
                                          const QList<QVariant>& args)
{
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, m_path, QLatin1String(kCardIface), method);
    if (!args.isEmpty()) {
        call.setArguments(args);
    }
    // Deliberately synchronous, bounded by kPropTimeoutMs. Method entry replies
    // immediately in the agent design — it mints the Operation object and
    // returns its path; the long card work happens on that Operation and
    // arrives via its signals. The public contract returns the AgentOperation
    // synchronously, the call sites are explicit user actions (not background
    // property bursts), and a capped block avoids the reentrancy a nested wait
    // would allow mid-start. Worst case against a wedged agent: one bounded
    // stall, then nullptr.
    QDBusMessage reply = m_connection.call(call, QDBus::Block, kPropTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        // Method threw at entry (e.g. UnsupportedOnThisCard). The agent's error
        // name/message is the ONLY record of why — never swallow it.
        qCWarning(lcAgentCard).noquote() << method << "refused for" << m_path << ':' << reply.errorName()
                                         << reply.errorMessage();
        return nullptr;
    }
    const QString opPath = qvariant_cast<QDBusObjectPath>(reply.arguments().constFirst()).path();
    if (opPath.isEmpty()) {
        return nullptr;
    }
    return new AgentOperation(m_connection, m_service, opPath, typedInterface, this);
}

AgentOperation* AgentCard::readIdentity()
{
    return startOperation(QStringLiteral("ReadIdentity"), QLatin1String(kIdentityIface));
}

AgentOperation* AgentCard::getPhoto()
{
    // Photo1 `a{sh}` (sealed-memfd) result. Like ReadIdentity/ReadCertificates,
    // a card that lacks the capability throws at method entry → nullptr.
    return startOperation(QStringLiteral("GetPhoto"), QLatin1String(kPhotoIface));
}

AgentOperation* AgentCard::readCertificates()
{
    return startOperation(QStringLiteral("ReadCertificates"), QLatin1String(kCertificatesIface));
}

QDBusPendingCallWatcher* AgentCard::warmCertificates()
{
    // Unlike startOperation's deliberate bounded block (whose contract is to
    // return the minted AgentOperation synchronously), a warm has no consumer
    // for the operation: issue the entry call with asyncCall — same pattern as
    // refreshAll() — so a slow or wedged agent can never stall the caller's
    // (GUI) thread. The reply carries the operation path; nobody reads it, and
    // an entry error (e.g. a card without certificates) is silently dropped.
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, m_path, QLatin1String(kCardIface),
                                                       QStringLiteral("ReadCertificates"));
    auto* watcher = new QDBusPendingCallWatcher(m_connection.asyncCall(call, kPropTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, watcher, &QObject::deleteLater);
    return watcher;
}

AgentOperation* AgentCard::sign(const QString& certId, const QDBusUnixFileDescriptor& input, const QVariantMap& options)
{
    QList<QVariant> args;
    args << certId;
    args << QVariant::fromValue(input);
    args << options;
    return startOperation(QStringLiteral("Sign"), QLatin1String(kSignIface), args);
}

AgentOperation* AgentCard::startCredentialOp(const QString& method, const QList<QVariant>& args, QString& errorNameOut)
{
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, m_path, QLatin1String(kCredentialsIface), method);
    if (!args.isEmpty()) {
        call.setArguments(args);
    }
    // Same synchronous, capped call mode as startOperation — see its comment
    // for the rationale (bounded stall, no reentrancy mid-start).
    QDBusMessage reply = m_connection.call(call, QDBus::Block, kPropTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        // Method threw at entry (e.g. UnknownCredential / RateLimited). Unlike
        // startOperation, the error name is not just logged — it is written to
        // errorNameOut so the caller (lastCredentialError()) can branch on it to
        // render the right guidance.
        errorNameOut = reply.errorName();
        qCWarning(lcAgentCard).noquote() << method << "refused for" << m_path << ':' << reply.errorName()
                                         << reply.errorMessage();
        return nullptr;
    }
    errorNameOut.clear();
    const QString opPath = qvariant_cast<QDBusObjectPath>(reply.arguments().constFirst()).path();
    if (opPath.isEmpty()) {
        return nullptr;
    }
    return new AgentOperation(m_connection, m_service, opPath, QLatin1String(kOperationCredentialsIface), this);
}

AgentOperation* AgentCard::listCredentials()
{
    return startCredentialOp(QStringLiteral("ListCredentials"), {}, m_lastCredentialError);
}

AgentOperation* AgentCard::managePin(const QString& pinId, const QString& verb, const QVariantMap& options)
{
    return startCredentialOp(QStringLiteral("ManagePin"), QList<QVariant>{pinId, verb, options}, m_lastCredentialError);
}

AgentOperation* AgentCard::activateSigningKey()
{
    return startCredentialOp(QStringLiteral("ActivateSigningKey"), {}, m_lastCredentialError);
}

QString AgentCard::lastCredentialError() const
{
    return m_lastCredentialError;
}

} // namespace LibreKDE
