// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "AgentReader.h"

#include "AgentDBus.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QVariant>

namespace LibreKDE {

// Wire interface names live in AgentDBus.h (shared across agentclient TUs).

AgentReader::AgentReader(const QDBusConnection& connection, const QString& service, const QString& path,
                         QObject* parent)
    : QObject(parent), m_connection(connection), m_service(service), m_path(path)
{
    m_connection.connect(m_service, m_path, QLatin1String(kPropertiesIface), QStringLiteral("PropertiesChanged"), this,
                         SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    // NO ctor refreshAll(): AgentClient::addReader always calls primeFrom() right
    // after construction with the full Reader1 map from GetManagedObjects /
    // InterfacesAdded, so an introspecting blocking GetAll here would be a
    // redundant cold-start round-trip (and the QDBusInterface ctor it used to
    // run issued a synchronous, UNCAPPED Introspect() on the GUI thread).
    // refreshAll() now exists only for the onPropertiesChanged invalidated
    // fallback, and is asynchronous + capped.
}

AgentReader::~AgentReader() = default;

QString AgentReader::path() const
{
    return m_path;
}

QString AgentReader::name() const
{
    return m_name;
}

bool AgentReader::hasCard() const
{
    return m_hasCard;
}

QString AgentReader::cardPath() const
{
    return m_cardPath;
}

void AgentReader::primeFrom(const QVariantMap& reader1Props)
{
    // Discovery-fresh data: bump the generation so a GetAll refresh still in
    // flight cannot overwrite this newer state when its older reply lands.
    ++m_propsGeneration;
    applyProps(reader1Props);
}

void AgentReader::applyProps(const QVariantMap& props)
{
    if (props.contains(QStringLiteral("Name"))) {
        m_name = props.value(QStringLiteral("Name")).toString();
    }
    if (props.contains(QStringLiteral("HasCard"))) {
        m_hasCard = props.value(QStringLiteral("HasCard")).toBool();
    }
    if (props.contains(QStringLiteral("Card"))) {
        m_cardPath = qvariant_cast<QDBusObjectPath>(props.value(QStringLiteral("Card"))).path();
    }
}

void AgentReader::onPropertiesChanged(const QString& iface, const QVariantMap& changedProps,
                                      const QStringList& invalidated)
{
    if (iface != QLatin1String(kReaderIface)) {
        return;
    }
    // The agent ships full new values in `changed` (PresenceModel emits complete
    // PropertyMaps), so apply them directly — no blocking per-property Get
    // round-trips on the (often main) thread. Fall back to a single GetAll only
    // if a needed property was `invalidated`. The direct apply is the newest
    // known state: bump the generation so a GetAll still in flight cannot
    // clobber it when its older reply lands later.
    ++m_propsGeneration;
    applyProps(changedProps);
    if (!invalidated.isEmpty()) {
        refreshAll();
    }
    Q_EMIT changed();
}

void AgentReader::refreshAll()
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
    call.setArguments(QList<QVariant>{QLatin1String(kReaderIface)});
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

} // namespace LibreKDE
