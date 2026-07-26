// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#include "CredentialTypes.h"
#include <QDBusMetaType>

namespace LibreKDE {
namespace {
CredentialKind kindFrom(const QString& t)
{
    if (t == QLatin1String("user"))
        return CredentialKind::User;
    if (t == QLatin1String("sign"))
        return CredentialKind::Sign;
    if (t == QLatin1String("puk"))
        return CredentialKind::Puk;
    if (t == QLatin1String("can"))
        return CredentialKind::Can;
    return CredentialKind::Unknown;
}
CredentialState stateFrom(const QString& t)
{
    if (t == QLatin1String("transport"))
        return CredentialState::Transport;
    if (t == QLatin1String("operational"))
        return CredentialState::Operational;
    if (t == QLatin1String("needsChange"))
        return CredentialState::NeedsChange;
    if (t == QLatin1String("blocked"))
        return CredentialState::Blocked;
    return CredentialState::Unknown;
}
UnblockStyle styleFrom(const QString& t)
{
    if (t == QLatin1String("resetOnly"))
        return UnblockStyle::ResetOnly;
    if (t == QLatin1String("setsNewPin"))
        return UnblockStyle::SetsNewPin;
    if (t == QLatin1String("unblockAndChange"))
        return UnblockStyle::UnblockAndChange;
    return UnblockStyle::Unknown;
}
RecoveryPath recoveryFrom(const QString& t)
{
    if (t == QLatin1String("holderViaPuk"))
        return RecoveryPath::HolderViaPuk;
    if (t == QLatin1String("issuerProcess"))
        return RecoveryPath::IssuerProcess;
    if (t == QLatin1String("none"))
        return RecoveryPath::None;
    return RecoveryPath::Unknown;
}
CredentialOutcome outcomeFrom(const QString& t)
{
    if (t == QLatin1String("ok"))
        return CredentialOutcome::Ok;
    if (t == QLatin1String("userCancelled"))
        return CredentialOutcome::UserCancelled;
    if (t == QLatin1String("missingFields"))
        return CredentialOutcome::MissingFields;
    if (t == QLatin1String("invalidPin"))
        return CredentialOutcome::InvalidPin;
    if (t == QLatin1String("blocked"))
        return CredentialOutcome::Blocked;
    if (t == QLatin1String("pluginError"))
        return CredentialOutcome::PluginError;
    if (t == QLatin1String("unsupported"))
        return CredentialOutcome::Unsupported;
    if (t == QLatin1String("keyActivationFailed"))
        return CredentialOutcome::KeyActivationFailed;
    if (t == QLatin1String("cardRemoved"))
        return CredentialOutcome::CardRemoved;
    return CredentialOutcome::Unspecified;
}
// Optional int/string only when the key is present (wire omits absent values).
std::optional<int> optInt(const QVariantMap& m, const char* k)
{
    const auto it = m.constFind(QLatin1String(k));
    return it == m.constEnd() ? std::nullopt : std::optional<int>(it->toInt());
}
std::optional<QString> optStr(const QVariantMap& m, const char* k)
{
    const auto it = m.constFind(QLatin1String(k));
    return it == m.constEnd() ? std::nullopt : std::optional<QString>(it->toString());
}
std::optional<bool> optBool(const QVariantMap& m, const char* k)
{
    const auto it = m.constFind(QLatin1String(k));
    return it == m.constEnd() ? std::nullopt : std::optional<bool>(it->toBool());
}
} // namespace

CredentialRecord CredentialRecord::fromVariantMap(const QVariantMap& m)
{
    CredentialRecord r;
    r.id = m.value(QStringLiteral("id")).toString();
    r.label = m.value(QStringLiteral("label")).toString();
    r.kind = kindFrom(m.value(QStringLiteral("kind")).toString());
    r.state = stateFrom(m.value(QStringLiteral("state")).toString());
    r.retriesLeft = optInt(m, "retries_left");
    r.retriesMax = optInt(m, "retries_max");
    r.usesLeft = optInt(m, "uses_left");
    r.usesMax = optInt(m, "uses_max");
    r.unblocksLeft = optInt(m, "unblocks_left");
    r.minLength = optInt(m, "min_length");
    r.maxLength = optInt(m, "max_length");
    r.canChange = m.value(QStringLiteral("can_change")).toBool();
    r.unblockable = m.value(QStringLiteral("unblockable")).toBool();
    r.activatable = m.value(QStringLiteral("activatable")).toBool();
    r.keyActivationPending = m.value(QStringLiteral("key_activation_pending")).toBool();
    r.keyActivatable = m.value(QStringLiteral("key_activatable")).toBool();
    r.probeSafe = m.value(QStringLiteral("probe_safe")).toBool();
    r.unblockStyle = styleFrom(m.value(QStringLiteral("unblock_style")).toString());
    r.recovery = recoveryFrom(m.value(QStringLiteral("recovery")).toString());
    r.blockedGuidanceKey = optStr(m, "blocked_guidance_key");
    r.blockedGuidanceFallback = optStr(m, "blocked_guidance_fallback");
    r.keyActivationGuidanceKey = optStr(m, "key_activation_guidance_key");
    r.keyActivationGuidanceFallback = optStr(m, "key_activation_guidance_fallback");
    return r;
}

PinResult PinResult::fromVariantMap(const QVariantMap& m)
{
    PinResult r;
    r.outcome = outcomeFrom(m.value(QStringLiteral("outcome")).toString());
    r.retriesLeft = optInt(m, "retries_left");
    r.blocked = m.value(QStringLiteral("blocked")).toBool();
    r.pinActivated = optBool(m, "pin_activated");
    r.keyActivated = optBool(m, "key_activated");
    return r;
}

void registerCredentialMetatypes()
{
    static bool done = false;
    if (done)
        return;
    qDBusRegisterMetaType<CredentialRecordsWire>();
    done = true;
}
} // namespace LibreKDE
