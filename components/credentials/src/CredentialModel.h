// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <LibreSCRS/AgentClient/CredentialTypes.h> // CredentialList / CredentialRecord / CredentialKind

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QVariant>

#include <optional>

namespace LibreKDE::Credentials {

/// @brief Read-only list model over a `CredentialList` for the dashboard's
///        `ListView`.
///
/// One row per `CredentialRecord`; the roles expose the localized display
/// strings (`CredentialText::kindName/stateName/guidance` + an
/// attribution-labelled counters line) and the per-record capability flags the
/// delegate gates its action buttons on. Owned by the `CredentialController`
/// (its `credentials` property); populated via `setRecords()` when a
/// `ListCredentials` operation completes. Pure presentation — no card I/O, no
/// secrets, no LibreMiddleware.
class CredentialModel : public QAbstractListModel
{
    Q_OBJECT
public:
    /// Roles the delegate binds against. `StateRole` carries the
    /// `CredentialState` enum (as int) for QML-side styling; every other role is
    /// a ready-to-render display value. The ORDER is not wire-visible (roles are
    /// addressed by their `roleNames()` string in QML), so it may grow freely.
    enum Roles {
        IdRole = Qt::UserRole + 1, ///< Opaque credential id — the verb invokables' argument.
        KindNameRole,              ///< Localized credential-kind name (User PIN / Signing PIN / PUK / …).
        StateNameRole,             ///< Localized state name (Operational / Transport — activation needed / …).
        StateRole,                 ///< `CredentialState` cast to int, for QML styling of the state chip.
        CountersRole,              ///< Attribution-labelled counters line (empty when the record carries none).
        GuidanceRole,              ///< Localized guidance for the record's state (empty when none applies).
        CanChangeRole,             ///< The "Change…" verb applies.
        UnblockableRole,           ///< The "Unblock…" verb applies.
        ActivatableRole,           ///< The "Activate…" verb applies (transport PIN).
        KeyActivatableRole,        ///< The "Activate signing key" verb applies…
        KeyActivationPendingRole,  ///< …but only while the key still NEEDS bringing up
                                   ///< (the QML gates the standalone button on
                                   ///< `key_activatable && key_activation_pending`).
    };
    Q_ENUM(Roles)

    explicit CredentialModel(QObject* parent = nullptr);
    ~CredentialModel() override;

    CredentialModel(const CredentialModel&) = delete;
    CredentialModel& operator=(const CredentialModel&) = delete;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// @brief Replace the backing records (a full model reset). An empty list is
    ///        a legitimate value (the controller renders the Empty placeholder).
    void setRecords(const LibreSCRS::AgentClient::CredentialList& records);

    /// @brief The record with the given opaque id, or `std::nullopt` when the
    ///        current list no longer carries it (a stale id — the controller then
    ///        lets the agent's `UnknownCredential` recovery re-list). Used by the
    ///        verb flow to read the presented kind + `keyActivationPending`.
    [[nodiscard]] std::optional<LibreSCRS::AgentClient::CredentialRecord> recordById(const QString& id) const;

    /// @brief Row of the first record of @p kind, or -1 when none is listed. Used
    ///        by the unblock pre-flight to locate the PUK row, whose usage budget
    ///        (`usesLeft`/`usesMax`) the confirm sheet surfaces.
    [[nodiscard]] int rowOfKind(LibreSCRS::AgentClient::CredentialKind kind) const;

private:
    LibreSCRS::AgentClient::CredentialList m_records;
};

} // namespace LibreKDE::Credentials
