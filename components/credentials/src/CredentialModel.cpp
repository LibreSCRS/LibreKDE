// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#include "CredentialModel.h"

#include "CredentialText.h"

#include <KLocalizedString>

namespace LibreKDE::Credentials {
namespace {

// The counters line: each present counter as "<Type>: m of n" (or "<Type>: m"
// when the card exposed no max), the type naming retry vs usage. The credential
// kind is shown once in the delegate header, so no per-counter kind prefix. The
// DOCP reset counter (unblocksLeft, tag 99) is intentionally NOT rendered — its
// meaning is applet-specific and it has no max; it stays on the wire, unshown.
// Returns empty when the record carries no rendered counter.
QString countersText(const CredentialRecord& r)
{
    QStringList parts;
    if (r.retriesLeft.has_value()) {
        parts
            << (r.retriesMax.has_value()
                    ? ki18ndc("librekde", "retry counter: remaining of maximum", "Attempts: %1 of %2")
                          .subs(*r.retriesLeft)
                          .subs(*r.retriesMax)
                          .toString()
                    : ki18ndc("librekde", "retry counter: remaining", "Attempts: %1").subs(*r.retriesLeft).toString());
    }
    if (r.usesLeft.has_value()) {
        parts << (r.usesMax.has_value()
                      ? ki18ndc("librekde", "usage counter: remaining of maximum", "Uses: %1 of %2")
                            .subs(*r.usesLeft)
                            .subs(*r.usesMax)
                            .toString()
                      : ki18ndc("librekde", "usage counter: remaining", "Uses: %1").subs(*r.usesLeft).toString());
    }
    return parts.join(QStringLiteral(" · "));
}

// The guidance line for a record's current state: the blocked-recovery text when
// blocked, the key-activation text when a signing key still needs bringing up,
// else empty (the delegate hides the line). Both resolve through
// CredentialText::guidance, which prefers a shipped translation of the agent's
// frozen key and falls back to the agent's English string.
QString guidanceText(const CredentialRecord& r)
{
    if (r.state == CredentialState::Blocked) {
        return LibreKDE::CredentialText::guidance(r.blockedGuidanceKey, r.blockedGuidanceFallback);
    }
    if (r.keyActivationPending) {
        return LibreKDE::CredentialText::guidance(r.keyActivationGuidanceKey, r.keyActivationGuidanceFallback);
    }
    return {};
}

} // namespace

CredentialModel::CredentialModel(QObject* parent) : QAbstractListModel(parent) {}
CredentialModel::~CredentialModel() = default;

int CredentialModel::rowCount(const QModelIndex& parent) const
{
    // A flat list: the root has all rows; any valid parent has none.
    return parent.isValid() ? 0 : int(m_records.size());
}

QVariant CredentialModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_records.size())) {
        return {};
    }
    const CredentialRecord& r = m_records.at(index.row());
    switch (role) {
    case IdRole:
        return r.id;
    case KindNameRole:
        return LibreKDE::CredentialText::kindName(r.kind);
    case StateNameRole:
        return LibreKDE::CredentialText::stateName(r.state);
    case StateRole:
        return int(r.state);
    case CountersRole:
        return countersText(r);
    case GuidanceRole:
        return guidanceText(r);
    case CanChangeRole:
        return r.canChange;
    case UnblockableRole:
        return r.unblockable;
    case ActivatableRole:
        return r.activatable;
    case KeyActivatableRole:
        return r.keyActivatable;
    case KeyActivationPendingRole:
        return r.keyActivationPending;
    default:
        return {};
    }
}

QHash<int, QByteArray> CredentialModel::roleNames() const
{
    return {
        {IdRole, QByteArrayLiteral("id")},
        {KindNameRole, QByteArrayLiteral("kindName")},
        {StateNameRole, QByteArrayLiteral("stateName")},
        {StateRole, QByteArrayLiteral("state")},
        {CountersRole, QByteArrayLiteral("counters")},
        {GuidanceRole, QByteArrayLiteral("guidance")},
        {CanChangeRole, QByteArrayLiteral("canChange")},
        {UnblockableRole, QByteArrayLiteral("unblockable")},
        {ActivatableRole, QByteArrayLiteral("activatable")},
        {KeyActivatableRole, QByteArrayLiteral("keyActivatable")},
        {KeyActivationPendingRole, QByteArrayLiteral("keyActivationPending")},
    };
}

void CredentialModel::setRecords(const CredentialList& records)
{
    beginResetModel();
    m_records = records;
    endResetModel();
}

std::optional<CredentialRecord> CredentialModel::recordById(const QString& id) const
{
    for (const CredentialRecord& r : m_records) {
        if (r.id == id) {
            return r;
        }
    }
    return std::nullopt;
}

int CredentialModel::rowOfKind(CredentialKind kind) const
{
    for (int row = 0; row < int(m_records.size()); ++row) {
        if (m_records.at(row).kind == kind) {
            return row;
        }
    }
    return -1;
}

} // namespace LibreKDE::Credentials
