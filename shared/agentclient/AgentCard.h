// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "AgentCapabilities.h" // LibreKDE::PreReadAuth
#include "AgentOperation.h"

#include <QDBusConnection>
#include <QDBusUnixFileDescriptor>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <cstdint>

class QDBusPendingCallWatcher;

/// @file
/// @brief Typed live proxy for an `org.librescrs.Agent.Card1`, minting
///        `AgentOperation` objects for its async methods.

namespace LibreKDE {

// PreReadAuth lives in AgentCapabilities.h (pure header) so the resolveCardState
// resolver can take it without depending on QtDBus.

/// @brief Live proxy for a Card1 object. Tracks Capabilities /
///        PreReadAuthMethod / Reader via `PropertiesChanged`; `readIdentity()`,
///        `readCertificates()` and `sign()` start an agent Operation1 and return
///        an `AgentOperation` bound to the right typed result interface. The
///        returned operation is parented to this card.
class AgentCard : public QObject
{
    Q_OBJECT
public:
    AgentCard(const QDBusConnection& connection, const QString& service, const QString& path,
              QObject* parent = nullptr);
    ~AgentCard() override;

    AgentCard(const AgentCard&) = delete;
    AgentCard& operator=(const AgentCard&) = delete;

    [[nodiscard]] QString path() const;
    [[nodiscard]] std::uint32_t capabilities() const;
    [[nodiscard]] PreReadAuth preReadAuthMethod() const;
    /// @brief The verbatim `Card1.PreReadAuthMethod` wire token ("None" /
    ///        "Can" / "Mrz"), for consumers that forward it as-is (e.g.
    ///        the `card:/` info.txt) without a decode→re-encode round-trip.
    [[nodiscard]] QString preReadAuthWire() const;
    [[nodiscard]] QString readerPath() const;

    /// @brief Seed properties from an ObjectManager interface map.
    void primeFrom(const QVariantMap& card1Props);

    /// @brief Start ReadIdentity; returns the operation (Identity1 result) or
    ///        nullptr if the method threw at entry.
    [[nodiscard]] AgentOperation* readIdentity();
    /// @brief Start GetPhoto; returns the operation (Photo1 `a{sh}` result) or
    ///        nullptr if the method threw at entry. Gates on IdentityData.
    [[nodiscard]] AgentOperation* getPhoto();
    /// @brief Start ReadCertificates (Certificates1 result).
    [[nodiscard]] AgentOperation* readCertificates();
    /// @brief Fire-and-forget ReadCertificates for cache pre-warming: issues the
    ///        entry call ASYNCHRONOUSLY (never blocks the caller's thread) and
    ///        deliberately mints NO `AgentOperation` for the reply — the warm's
    ///        whole effect is agent-side (its shared read cache fills, and
    ///        concurrent cert reads dedup onto one card read); the minted
    ///        operation's path/result are never consumed client-side. A card
    ///        without certificates refuses the method at entry; that error reply
    ///        is discarded like any other. Returns the watcher (parented to this
    ///        card, self-deleting once the entry reply or its timeout lands) so
    ///        a caller may hold a QPointer to it as a re-issue guard.
    QDBusPendingCallWatcher* warmCertificates();
    /// @brief Start Sign over @p input with cert @p certId (Sign1 result).
    [[nodiscard]] AgentOperation* sign(const QString& certId, const QDBusUnixFileDescriptor& input,
                                       const QVariantMap& options);

    /// @brief Start Credentials1.ListCredentials; returns the operation or
    ///        nullptr if the method threw at entry (see `lastCredentialError()`).
    [[nodiscard]] AgentOperation* listCredentials();
    /// @brief Start Credentials1.ManagePin(pinId, verb, options); returns the
    ///        operation or nullptr if the method threw at entry — the caller
    ///        branches on `lastCredentialError()` (e.g. `UnknownCredential` /
    ///        `RateLimited`) to render the right guidance.
    [[nodiscard]] AgentOperation* managePin(const QString& pinId, const QString& verb, const QVariantMap& options);
    /// @brief Start Credentials1.ActivateSigningKey; returns the operation or
    ///        nullptr if the method threw at entry (see `lastCredentialError()`).
    [[nodiscard]] AgentOperation* activateSigningKey();

    /// @brief The D-Bus error name from the most recent `listCredentials()` /
    ///        `managePin()` / `activateSigningKey()` entry throw, or empty if
    ///        that call succeeded (an Operation was minted).
    [[nodiscard]] QString lastCredentialError() const;

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& invalidated);

private:
    /// @brief Apply a Card1 property subset (from `changed`, `GetAll`, or
    ///        ObjectManager) onto the cached fields.
    void applyProps(const QVariantMap& props);
    /// @brief Non-blocking `GetAll` refresh (the `invalidated` fallback).
    ///        Supersedes any refresh still in flight; the reply applies (and
    ///        emits `changed()`) on this object's thread via the watcher.
    void refreshAll();
    AgentOperation* startOperation(const QString& method, const QString& typedInterface,
                                   const QList<QVariant>& args = {});
    /// @brief Credentials1-specific start: mirrors `startOperation`, but writes
    ///        the D-Bus error name to @p errorNameOut (not just the log) on an
    ///        entry throw, so the caller can branch on it. Cleared on success.
    AgentOperation* startCredentialOp(const QString& method, const QList<QVariant>& args, QString& errorNameOut);

    QDBusConnection m_connection;
    QString m_service;
    QString m_path;
    /// Version counter for the cached properties. Every direct apply (primeFrom
    /// / a PropertiesChanged `changed` map) and every new refresh request bumps
    /// it; a GetAll reply whose captured generation no longer matches is stale
    /// (newer state landed while it was in flight) and is discarded.
    quint64 m_propsGeneration = 0;
    /// The single in-flight refresh watcher (parented to this), or nullptr.
    QDBusPendingCallWatcher* m_refreshWatcher = nullptr;
    std::uint32_t m_capabilities = 0;
    // Stored as the raw wire token; preReadAuthMethod() decodes on demand. Keeping
    // the verbatim string lets forwarding consumers avoid a decode→re-encode.
    QString m_preReadAuth = QStringLiteral("None");
    QString m_readerPath;
    /// D-Bus error name from the most recent Credentials1 method-entry throw
    /// (listCredentials/managePin/activateSigningKey); empty on success.
    QString m_lastCredentialError;
};

} // namespace LibreKDE
