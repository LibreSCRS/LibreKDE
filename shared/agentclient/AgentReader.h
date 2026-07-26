// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QDBusPendingCallWatcher;

/// @file
/// @brief Typed live proxy for an `org.librescrs.Agent.Reader1`.

namespace LibreKDE {

/// @brief Live proxy for a Reader1 object. Tracks Name / HasCard / Card via
///        `PropertiesChanged`; emits `changed` on any update.
class AgentReader : public QObject
{
    Q_OBJECT
public:
    AgentReader(const QDBusConnection& connection, const QString& service, const QString& path,
                QObject* parent = nullptr);
    ~AgentReader() override;

    AgentReader(const AgentReader&) = delete;
    AgentReader& operator=(const AgentReader&) = delete;

    [[nodiscard]] QString path() const;
    [[nodiscard]] QString name() const;
    [[nodiscard]] bool hasCard() const;
    [[nodiscard]] QString cardPath() const;

    /// @brief Seed properties from an ObjectManager interface map (avoids a
    ///        round-trip when discovery already carried them).
    void primeFrom(const QVariantMap& reader1Props);

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& invalidated);

private:
    /// @brief Apply a Reader1 property subset (from `changed`, `GetAll`, or
    ///        ObjectManager) onto the cached fields.
    void applyProps(const QVariantMap& props);
    /// @brief Non-blocking `GetAll` refresh (the `invalidated` fallback).
    ///        Supersedes any refresh still in flight; the reply applies (and
    ///        emits `changed()`) on this object's thread via the watcher.
    void refreshAll();

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
    QString m_name;
    bool m_hasCard = false;
    QString m_cardPath;
};

} // namespace LibreKDE
