// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "AgentCard.h"
#include "AgentReader.h"

#include <QByteArray>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <memory>

/// @file
/// @brief Top-level agent client: owns the session connection, watches the
///        `org.librescrs.Agent` name, and maintains the reader/card registry
///        from the agent's ObjectManager tree.

namespace LibreKDE {

/// @brief ObjectManager `a{sa{sv}}` interface→properties map (registered as a
///        D-Bus metatype so `InterfacesAdded` demarshals straight into a slot).
using AgentInterfaceProps = QMap<QString, QVariantMap>;

/// @brief Result of a `Pkcs11_1.CertDer` fetch. `ok` with `der` on success;
///        otherwise `errorName` carries the D-Bus error name the caller maps to
///        a UI/KIO status (e.g. `…Error.KeyNotFound` / `…Error.UnknownCard`).
///        Kept free of any KIO type so the agentclient stays LibreMiddleware-
///        AND KIO-free.
struct AgentCertDer
{
    bool ok = false;
    QByteArray der;
    QString errorName;
};

/// @brief Connection + discovery for `org.librescrs.Agent`.
///
/// Single responsibility: hold the `QDBusConnection`, track agent availability
/// (via `QDBusServiceWatcher`), populate `AgentReader`/`AgentCard` registries
/// from `GetManagedObjects` + `InterfacesAdded`/`InterfacesRemoved`, and emit
/// `readersChanged` / `cardChanged` / `availabilityChanged`.
class AgentClient : public QObject
{
    Q_OBJECT
public:
    explicit AgentClient(QObject* parent = nullptr);
    /// @brief Inject a connection (tests use a private `dbus-run-session` bus).
    AgentClient(const QDBusConnection& connection, const QString& service, QObject* parent = nullptr);
    ~AgentClient() override;

    AgentClient(const AgentClient&) = delete;
    AgentClient& operator=(const AgentClient&) = delete;

    [[nodiscard]] bool isAvailable() const;

    /// @brief Manually re-run discovery: re-probe the bus name (in case a
    ///        `NameOwnerChanged` was missed) and re-populate the reader/card
    ///        registry from `GetManagedObjects`. Reuses the existing discovery
    ///        wire — no new D-Bus method. This is the client path behind the
    ///        plasmoid's refresh affordance: it recovers from a dropped
    ///        `InterfacesAdded` (a card the agent exported but the client never
    ///        saw) and reconciles availability if the agent reappeared without a
    ///        watcher signal. Fires `availabilityChanged` only on an actual flip
    ///        and always ends with `readersChanged` so consumers recompute.
    void refreshDiscovery();

    [[nodiscard]] QList<AgentReader*> readers() const;
    [[nodiscard]] AgentReader* reader(const QString& path) const;
    [[nodiscard]] AgentCard* card(const QString& path) const;

    /// @brief First reader that currently holds a resolvable card, with a
    ///        DETERMINISTIC choice: readers are considered in lexicographically
    ///        sorted path order (never the implicit QHash iteration order). A
    ///        reader counts only if its `cardPath()` resolves to a tracked
    ///        `AgentCard`. Returns nullptr if no reader holds a usable card.
    [[nodiscard]] AgentReader* firstReaderWithCard() const;

    /// @brief The `AgentCard` of the first reader (same deterministic ordering)
    ///        whose card advertises @p requiredCap. For e.g. the Purpose plugin's
    ///        PKI filter: `cardWithCapability(Cap::Pki)`. Returns nullptr if no
    ///        present card advertises the capability.
    [[nodiscard]] AgentCard* cardWithCapability(std::uint32_t requiredCap) const;

    /// @brief All tracked readers in DETERMINISTIC lexicographically-sorted
    ///        object-path order (never QHash iteration order) — the shared
    ///        ordering primitive behind firstReaderWithCard/cardWithCapability
    ///        and the plasmoid's reader rosters. Null slots are skipped.
    [[nodiscard]] QList<AgentReader*> readersSortedByPath() const;

    /// @brief First reader (same sorted-path order) whose friendly Name equals
    ///        @p friendlyName AND currently holds a resolvable card. Returns
    ///        nullptr when no present reader of that name holds a card — the
    ///        per-widget reader binding's "bound reader absent/empty" case.
    ///        Two identically-named readers collide on the first match (v1).
    [[nodiscard]] AgentReader* readerWithCardByName(const QString& friendlyName) const;

    /// @brief Fetch a certificate's raw DER via `Pkcs11_1.CertDer(reader, certId)`
    ///        — the agent's public-data primitive (no consent, no lease). Hosted
    ///        once on the manager path; addressed by the card's Reader1 object
    ///        path (@p readerPath, from `AgentCard::readerPath()`) plus @p certId
    ///        (the Certificates1 certId == sha256hex of the DER). A capped,
    ///        blocking call (`kPropTimeoutMs`); returns `{ok=false, errorName}`
    ///        on any wire/agent error.
    [[nodiscard]] AgentCertDer certificateDer(const QString& readerPath, const QString& certId) const;

Q_SIGNALS:
    /// @brief Agent name-owner appeared/vanished on the bus.
    ///
    /// Consumers MUST treat `availabilityChanged(false)` as "all cards and
    /// readers are gone": on service loss the registry is cleared and only
    /// `readersChanged()` + `availabilityChanged(false)` fire — there is NO
    /// per-card `cardChanged()` for each dropped card (the whole tree vanishes
    /// at once, so per-path notifications would be redundant). UI that tracks
    /// individual card paths must drop them all on this signal.
    void availabilityChanged(bool available);
    void readersChanged();
    void cardChanged(const QString& readerPath);

private Q_SLOTS:
    void onServiceRegistered(const QString& service);
    void onServiceUnregistered(const QString& service);
    void onInterfacesAdded(const QDBusObjectPath& path, const LibreKDE::AgentInterfaceProps& interfacesAndProperties);
    void onInterfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces);

private:
    void connectObjectManager();
    void repopulate();
    void clearRegistry();
    void addReader(const QString& path, const QVariantMap& props);
    void addCard(const QString& path, const QVariantMap& props);
    /// Terminalize any in-flight op on the card at @p path (loud finished), then
    /// delete it and emit cardChanged. Shared by onInterfacesRemoved (a live
    /// InterfacesRemoved) and repopulate's reconcile (a card GetManagedObjects no
    /// longer reports), so a going-away card never leaks a hung operation.
    void removeCard(const QString& path);
    /// Delete the reader at @p path. Emits nothing — the caller batches a single
    /// readersChanged (live removal fires one; a reconcile fires one at the end).
    void removeReader(const QString& path);

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace LibreKDE

Q_DECLARE_METATYPE(LibreKDE::AgentInterfaceProps)
