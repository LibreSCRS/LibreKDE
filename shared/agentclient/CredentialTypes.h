// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once
#include <QList>
#include <QMetaType>
#include <QString>
#include <QVariantMap>
#include <optional>

/// @file
/// @brief Client-side mirror of the frozen org.librescrs.Agent.Credentials1 /
///        Operation.Credentials1 wire vocabulary (see those two XML files — this
///        header is the "KDE client" mirror they name as authoritative). Pure
///        Qt/DBus value types; no LibreMiddleware, no secrets.

namespace LibreKDE {

enum class CredentialKind { User, Sign, Puk, Can, Unknown };
enum class CredentialState { Unknown, Transport, Operational, NeedsChange, Blocked };
enum class UnblockStyle { Unknown, ResetOnly, SetsNewPin, UnblockAndChange };
enum class RecoveryPath { Unknown, HolderViaPuk, IssuerProcess, None };
enum class CredentialOutcome {
    Unspecified,
    Ok,
    UserCancelled,
    MissingFields,
    InvalidPin,
    Blocked,
    PluginError,
    Unsupported,
    KeyActivationFailed,
    CardRemoved
};

/// One ListCredentials record (`aa{sv}` entry). Optional ints are absent when
/// the wire omits the key (never a sentinel); bools default false.
struct CredentialRecord
{
    QString id;
    QString label;
    CredentialKind kind = CredentialKind::Unknown;
    CredentialState state = CredentialState::Unknown;
    std::optional<int> retriesLeft, retriesMax, usesLeft, usesMax, unblocksLeft, minLength, maxLength;
    bool canChange = false, unblockable = false, activatable = false;
    bool keyActivationPending = false, keyActivatable = false, probeSafe = false;
    UnblockStyle unblockStyle = UnblockStyle::Unknown;
    RecoveryPath recovery = RecoveryPath::Unknown;
    std::optional<QString> blockedGuidanceKey, blockedGuidanceFallback;
    std::optional<QString> keyActivationGuidanceKey, keyActivationGuidanceFallback;

    [[nodiscard]] static CredentialRecord fromVariantMap(const QVariantMap& m);
};
using CredentialList = QList<CredentialRecord>;

/// The uniform mutation result (`a{sv}`).
struct PinResult
{
    CredentialOutcome outcome = CredentialOutcome::Unspecified;
    std::optional<int> retriesLeft;
    bool blocked = false;
    std::optional<bool> pinActivated, keyActivated;

    [[nodiscard]] static PinResult fromVariantMap(const QVariantMap& m);
};

/// The `aa{sv}` records container as it arrives on the Result signal / GetResult.
using CredentialRecordsWire = QList<QVariantMap>;

/// Register CredentialRecordsWire (aa{sv}) as a D-Bus metatype. Idempotent;
/// call before connecting the Credentials Result slot / issuing GetResult.
void registerCredentialMetatypes();

} // namespace LibreKDE

Q_DECLARE_METATYPE(LibreKDE::CredentialRecordsWire)
