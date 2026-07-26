// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "AgentClient.h"

#include "AgentCapabilities.h" // LibreKDE::has
#include "AgentDBus.h"
#include "CappedCall.h"
#include "AgentOperation.h"

#include <algorithm>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QMap>
#include <QSet>
#include <QVariant>

// a{oa{sa{sv}}} return of GetManagedObjects — declared for qDBusRegisterMetaType.
using ManagedObjectMapType = QMap<QDBusObjectPath, LibreKDE::AgentInterfaceProps>;
Q_DECLARE_METATYPE(ManagedObjectMapType)

namespace LibreKDE {

namespace {
// Wire names (service / paths / interfaces) live in AgentDBus.h — the single
// client-side source of truth, shared across every agentclient TU.

// ObjectManager wire types.
using InterfaceProps = AgentInterfaceProps; // a{sa{sv}}
using ManagedObjectMap = ManagedObjectMapType;

void ensureMetatypes()
{
    static bool done = false;
    if (done) {
        return;
    }
    qDBusRegisterMetaType<InterfaceProps>();
    qDBusRegisterMetaType<ManagedObjectMap>();
    done = true;
}
} // namespace

struct AgentClient::Private
{
    QDBusConnection connection;
    QString service;
    QDBusServiceWatcher* watcher = nullptr;
    bool available = false;
    QHash<QString, AgentReader*> readers;
    QHash<QString, AgentCard*> cards;

    explicit Private(const QDBusConnection& c) : connection(c) {}
};

AgentClient::AgentClient(QObject* parent) : AgentClient(QDBusConnection::sessionBus(), QLatin1String(kService), parent)
{}

AgentClient::AgentClient(const QDBusConnection& connection, const QString& service, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>(connection))
{
    ensureMetatypes();
    d->service = service;

    d->watcher = new QDBusServiceWatcher(
        d->service, d->connection,
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(d->watcher, &QDBusServiceWatcher::serviceRegistered, this, &AgentClient::onServiceRegistered);
    connect(d->watcher, &QDBusServiceWatcher::serviceUnregistered, this, &AgentClient::onServiceUnregistered);

    connectObjectManager();

    // If the agent is already on the bus, populate now. Ask the bus daemon via a
    // capped NameHasOwner instead of connection.interface()->isServiceRegistered()
    // — the latter blocks UNCAPPED (~25 s default), long enough to feel like a
    // stuck discovery hang and to freeze a running outer loop the whole time. cappedCall bounds
    // it to kDiscoveryTimeoutMs and keeps that loop responsive.
    QDBusMessage hasOwnerCall =
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                       QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameHasOwner"));
    hasOwnerCall.setArguments(QList<QVariant>{d->service});
    const QDBusMessage hasOwnerReply = cappedCall(d->connection, hasOwnerCall, kDiscoveryTimeoutMs);
    const bool registered = hasOwnerReply.type() == QDBusMessage::ReplyMessage &&
                            !hasOwnerReply.arguments().isEmpty() && hasOwnerReply.arguments().constFirst().toBool();
    if (registered) {
        d->available = true;
        repopulate();
    }
}

AgentClient::~AgentClient() = default;

bool AgentClient::isAvailable() const
{
    return d->available;
}

void AgentClient::refreshDiscovery()
{
    // Re-probe the well-known name with the SAME capped NameHasOwner the ctor
    // uses (bounded, event-loop-safe) rather than trusting the cached flag: a
    // QDBusServiceWatcher registration/unregistration signal can be missed, so a
    // manual refresh must reconcile availability from the bus itself before
    // deciding what to do.
    QDBusMessage hasOwnerCall =
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                       QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameHasOwner"));
    hasOwnerCall.setArguments(QList<QVariant>{d->service});
    const QDBusMessage hasOwnerReply = cappedCall(d->connection, hasOwnerCall, kDiscoveryTimeoutMs);
    const bool registered = hasOwnerReply.type() == QDBusMessage::ReplyMessage &&
                            !hasOwnerReply.arguments().isEmpty() && hasOwnerReply.arguments().constFirst().toBool();

    if (registered) {
        const bool wasAvailable = d->available;
        d->available = true;
        // Re-run GetManagedObjects: picks up any card the agent has since
        // exported whose InterfacesAdded we never received (the deferred-publish
        // window, or a genuinely dropped signal).
        repopulate(); // emits readersChanged()
        if (!wasAvailable) {
            Q_EMIT availabilityChanged(true);
        }
        return;
    }

    // The agent is not on the bus. If we still thought it was, terminalize like
    // onServiceUnregistered would: drop the registry and report unavailable.
    if (d->available) {
        onServiceUnregistered(d->service);
    } else {
        // Already-unavailable → still let consumers recompute (e.g. re-detect
        // whether the agent is now installed) via a roster refresh.
        Q_EMIT readersChanged();
    }
}

QList<AgentReader*> AgentClient::readers() const
{
    return d->readers.values();
}

AgentReader* AgentClient::reader(const QString& path) const
{
    return d->readers.value(path, nullptr);
}

AgentCard* AgentClient::card(const QString& path) const
{
    return d->cards.value(path, nullptr);
}

QList<AgentReader*> AgentClient::readersSortedByPath() const
{
    // Deterministic: sort reader paths, never rely on QHash order.
    QList<QString> paths = d->readers.keys();
    std::sort(paths.begin(), paths.end());
    QList<AgentReader*> ordered;
    ordered.reserve(paths.size());
    for (const QString& path : std::as_const(paths)) {
        if (AgentReader* reader = d->readers.value(path, nullptr)) {
            ordered.append(reader);
        }
    }
    return ordered;
}

AgentReader* AgentClient::firstReaderWithCard() const
{
    for (AgentReader* reader : readersSortedByPath()) {
        if (d->cards.value(reader->cardPath(), nullptr)) {
            return reader;
        }
    }
    return nullptr;
}

AgentReader* AgentClient::readerWithCardByName(const QString& friendlyName) const
{
    for (AgentReader* reader : readersSortedByPath()) {
        if (reader->name() == friendlyName && d->cards.value(reader->cardPath(), nullptr)) {
            return reader;
        }
    }
    return nullptr;
}

AgentCard* AgentClient::cardWithCapability(std::uint32_t requiredCap) const
{
    for (AgentReader* reader : readersSortedByPath()) {
        AgentCard* card = d->cards.value(reader->cardPath(), nullptr);
        if (card && has(card->capabilities(), requiredCap)) {
            return card;
        }
    }
    return nullptr;
}

AgentCertDer AgentClient::certificateDer(const QString& readerPath, const QString& certId) const
{
    AgentCertDer result;
    if (!d->available) {
        result.errorName = QStringLiteral("org.librescrs.Agent.Error.Unavailable");
        return result;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(d->service, QLatin1String(kRootPath),
                                                       QLatin1String(kPkcs11Iface), QStringLiteral("CertDer"));
    call.setArguments(QList<QVariant>{QVariant::fromValue(QDBusObjectPath(readerPath)), certId});

    // Event-loop-safe capped call (same rationale as discovery: keep a running
    // outer loop responsive, bound the wait). A wedged agent must not stall the
    // caller. kPropTimeoutMs (not the snappier discovery cap) — this is a real
    // public-data fetch, not a registry read.
    const QDBusMessage reply = cappedCall(d->connection, call, kPropTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        result.errorName = reply.errorName().isEmpty() ? QStringLiteral("org.librescrs.Agent.Error.CommunicationError")
                                                       : reply.errorName();
        return result;
    }
    if (reply.arguments().isEmpty()) {
        result.errorName = QStringLiteral("org.librescrs.Agent.Error.CommunicationError");
        return result;
    }
    result.der = reply.arguments().constFirst().toByteArray(); // 'ay' → QByteArray
    result.ok = true;
    return result;
}

void AgentClient::connectObjectManager()
{
    d->connection.connect(d->service, QLatin1String(kRootPath), QLatin1String(kObjectManagerIface),
                          QStringLiteral("InterfacesAdded"), this,
                          SLOT(onInterfacesAdded(QDBusObjectPath, LibreKDE::AgentInterfaceProps)));
    d->connection.connect(d->service, QLatin1String(kRootPath), QLatin1String(kObjectManagerIface),
                          QStringLiteral("InterfacesRemoved"), this,
                          SLOT(onInterfacesRemoved(QDBusObjectPath, QStringList)));
}

void AgentClient::onServiceRegistered(const QString& /*service*/)
{
    if (d->available) {
        return;
    }
    d->available = true;
    repopulate();
    Q_EMIT availabilityChanged(true);
}

void AgentClient::onServiceUnregistered(const QString& /*service*/)
{
    if (!d->available) {
        return;
    }
    d->available = false;

    // Agent-death terminalization: the bus name is gone, so no Operation1.Finished
    // will ever arrive for any in-flight operation. Before tearing down the card
    // registry (which would silently destroy the AgentOperations, auto-disconnecting
    // their consumers WITHOUT firing finished/failed — a KJob/plasmoid that hangs
    // forever), walk every live operation and terminalize it loudly so each
    // consumer's finished/failed slot fires exactly once.
    //
    // AgentOperations are QObject-parented to their AgentCard, which is parented to
    // this client, so findChildren reaches them all. We snapshot the list first:
    // terminate() may re-enter (a consumer's finished slot could touch the client),
    // and terminate() itself is idempotent.
    const QList<AgentOperation*> live = findChildren<AgentOperation*>();
    for (AgentOperation* op : live) {
        op->terminate(OperationStatus::Cancelled, ErrorCode::CommunicationError,
                      QStringLiteral("librekde_agent_vanished"), QStringLiteral("The card agent stopped responding."));
    }

    clearRegistry();
    Q_EMIT availabilityChanged(false);
    Q_EMIT readersChanged();
}

void AgentClient::clearRegistry()
{
    qDeleteAll(d->readers);
    d->readers.clear();
    qDeleteAll(d->cards);
    d->cards.clear();
}

void AgentClient::repopulate()
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        d->service, QLatin1String(kRootPath), QLatin1String(kObjectManagerIface), QStringLiteral("GetManagedObjects"));
    // Event-loop-safe capped call rather than QDBus::Block, so a running outer
    // loop stays responsive and the wait is bounded even with no loop at all (the
    // worker). On a wedged/absent agent this returns an error reply at
    // ~kDiscoveryTimeoutMs and discovery yields nothing new — `ls card:/`
    // returns, never hangs.
    QDBusMessage reply = cappedCall(d->connection, call, kDiscoveryTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        // Discovery failed (wedged/absent agent). RECONCILE, not rebuild: leave
        // the current registry untouched rather than nuking it — a failed re-scan
        // must not churn live objects (initial discovery starts from an empty
        // registry anyway). Still notify so consumers recompute.
        Q_EMIT readersChanged();
        return;
    }

    ManagedObjectMap managed;
    const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
    arg >> managed;

    // Split the reported tree by interface so we can reconcile each registry.
    QSet<QString> liveCardPaths;
    QSet<QString> liveReaderPaths;
    for (auto it = managed.constBegin(); it != managed.constEnd(); ++it) {
        const QString path = it.key().path();
        const InterfaceProps& ifaces = it.value();
        if (ifaces.contains(QLatin1String(kCardIface))) {
            liveCardPaths.insert(path);
        }
        if (ifaces.contains(QLatin1String(kReaderIface))) {
            liveReaderPaths.insert(path);
        }
    }

    // RECONCILE against the reported tree instead of clear-and-rebuild. Drop only
    // objects the agent no longer reports (removeCard terminalizes a going-away
    // card's in-flight ops — clearRegistry did not, so a refresh mid-op used to
    // leak it), and add/update the rest IN PLACE: addCard/addReader primeFrom() an
    // existing path, so an UNCHANGED object keeps its identity (same pointer). A
    // consumer bound to it is then not forced to re-read an identical card — the
    // collateral flicker a refresh on one widget inflicts on all the others.
    const QList<QString> knownCards = d->cards.keys();
    for (const QString& path : knownCards) {
        if (!liveCardPaths.contains(path)) {
            removeCard(path); // terminalize + delete + cardChanged
        }
    }
    const QList<QString> knownReaders = d->readers.keys();
    for (const QString& path : knownReaders) {
        if (!liveReaderPaths.contains(path)) {
            removeReader(path);
        }
    }

    // Cards first so a reader's Card path resolves immediately.
    for (auto it = managed.constBegin(); it != managed.constEnd(); ++it) {
        const QString path = it.key().path();
        const InterfaceProps& ifaces = it.value();
        if (ifaces.contains(QLatin1String(kCardIface))) {
            addCard(path, ifaces.value(QLatin1String(kCardIface)));
        }
    }
    for (auto it = managed.constBegin(); it != managed.constEnd(); ++it) {
        const QString path = it.key().path();
        const InterfaceProps& ifaces = it.value();
        if (ifaces.contains(QLatin1String(kReaderIface))) {
            addReader(path, ifaces.value(QLatin1String(kReaderIface)));
        }
    }

    Q_EMIT readersChanged();
}

void AgentClient::addReader(const QString& path, const QVariantMap& props)
{
    if (AgentReader* existing = d->readers.value(path, nullptr)) {
        // Re-add for a path we already track (the agent re-emitting InterfacesAdded
        // with changed props): UPDATE the live proxy from the new payload rather
        // than dropping it on the floor, so a caps/state change is never silently
        // lost. primeFrom applies the subset present in `props`.
        existing->primeFrom(props);
        return;
    }
    auto* reader = new AgentReader(d->connection, d->service, path, this);
    reader->primeFrom(props);
    connect(reader, &AgentReader::changed, this, [this, path]() { Q_EMIT cardChanged(path); });
    d->readers.insert(path, reader);
}

void AgentClient::addCard(const QString& path, const QVariantMap& props)
{
    if (AgentCard* existing = d->cards.value(path, nullptr)) {
        // Re-add for an already-tracked path: prime the existing card from the
        // new props so a changed capability set upgrades in place (otherwise a
        // stale card would never reflect the agent's re-emitted state).
        existing->primeFrom(props);
        return;
    }
    auto* card = new AgentCard(d->connection, d->service, path, this);
    card->primeFrom(props);
    d->cards.insert(path, card);
}

void AgentClient::onInterfacesAdded(const QDBusObjectPath& path, const AgentInterfaceProps& interfacesAndProperties)
{
    const QString p = path.path();
    // Add the card first so a reader's Card path resolves immediately.
    if (interfacesAndProperties.contains(QLatin1String(kCardIface))) {
        addCard(p, interfacesAndProperties.value(QLatin1String(kCardIface)));
        Q_EMIT cardChanged(p);
    }
    if (interfacesAndProperties.contains(QLatin1String(kReaderIface))) {
        addReader(p, interfacesAndProperties.value(QLatin1String(kReaderIface)));
        Q_EMIT readersChanged();
    }
}

void AgentClient::removeCard(const QString& path)
{
    AgentCard* card = d->cards.take(path);
    if (card == nullptr) {
        return;
    }
    // The card is going away (a live InterfacesRemoved, or a reconcile that finds
    // it gone from GetManagedObjects): no Operation1.Finished will arrive for any
    // op in flight on it. Deleting the card would destroy its QObject-parented
    // AgentOperations, auto-disconnecting their consumers WITHOUT firing finished
    // — a SignJob/plasmoid would hang forever. Mirror the onServiceUnregistered
    // death-sweep: terminalize every live op first (idempotent via
    // emitFinishedOnce).
    const QList<AgentOperation*> live = card->findChildren<AgentOperation*>();
    for (AgentOperation* op : live) {
        op->terminate(OperationStatus::Cancelled, ErrorCode::CardRemoved, QStringLiteral("librekde_card_removed"),
                      QStringLiteral("The smart card was removed."));
    }
    delete card;
    Q_EMIT cardChanged(path);
}

void AgentClient::removeReader(const QString& path)
{
    delete d->readers.take(path);
}

void AgentClient::onInterfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces)
{
    const QString p = path.path();
    if (interfaces.contains(QLatin1String(kCardIface)) && d->cards.contains(p)) {
        removeCard(p);
    }
    if (interfaces.contains(QLatin1String(kReaderIface)) && d->readers.contains(p)) {
        removeReader(p);
        Q_EMIT readersChanged();
    }
}

} // namespace LibreKDE
