// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CredentialTypes.h"
#include "ErrorText.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <memory>

/// @file
/// @brief Drives one `org.librescrs.Agent.Operation1` instance and its typed
///        result sub-interface, handling the Finished/Result race + the Sign
///        `GetResult()` late-subscriber recovery contract.

namespace LibreKDE {

/// @brief Operation phase, mirroring `Operation1.xml` Phase enum (uint32).
enum class OperationPhase : std::uint32_t {
    Created = 0,
    Connecting = 1,
    AwaitingConsent = 2,
    Authenticating = 3,
    Reading = 4,
    Signing = 5,
    Timestamping = 6,
    Done = 7,
};

/// @brief Terminal status, mirroring `Operation1.xml` Finished.status enum.
enum class OperationStatus : std::uint32_t {
    Ok = 0,
    Cancelled = 1,
    Error = 2,
};

/// @brief Payload of a `Operation.Sign1.Result`: the sealed artifact fd + meta.
///
/// Owns the dup'd descriptor (RAII via `QDBusUnixFileDescriptor`). The
/// recipient must mmap-read and close promptly — the agent retains no copy.
struct SignResult
{
    QDBusUnixFileDescriptor artifact;
    QVariantMap meta;
};

/// @brief Payload of a `Operation.Photo1.Result`: the sealed-memfd photo(s),
///        the demarshaled `a{sh}` map (signature pinned in
///        AgentResultSignatureTest). The key is `"groupKey:fieldKey"`
///        (e.g. `"personal:photo"`), disambiguating multi-photo cards; the
///        value is a sealed memfd the recipient must mmap-read and close
///        promptly — the agent retains no copy. `QDBusUnixFileDescriptor`
///        owns/dup's the descriptor (RAII).
using PhotoMap = QMap<QString, QDBusUnixFileDescriptor>;

/// @brief One Identity1 field — the `(sssv)` tuple from `a{sa{s(sssv)}}`.
struct IdentityField
{
    QString labelKey;
    QString labelFallback;
    QString type;       ///< "text" | "date" | "binary"
    QDBusVariant value; ///< "s" for text/date, "ay" for binary
};

/// @brief Identity1 field map: group → (field → field-tuple), the demarshaled
///        `a{sa{s(sssv)}}` payload.
using IdentityFieldGroup = QMap<QString, IdentityField>;
using IdentityFields = QMap<QString, IdentityFieldGroup>;

QDBusArgument& operator<<(QDBusArgument& arg, const IdentityField& f);
const QDBusArgument& operator>>(const QDBusArgument& arg, IdentityField& f);

/// @brief One Certificates1 entry — the non-secret metadata a client needs to
///        pick a signing key and pass its handle to `Sign`. No DER crosses the
///        wire (Certificates1.xml): the agent parses X.509 and we render only.
///
/// The opaque `certId` handle, the `signingCapable` gate, and a display label
/// (subject/issuer CN) drawn from the `fields` field-group map cover the Purpose
/// chooser. The trailing tuple members the wire already carries — `keyUsageBits`
/// (a `u` bitmask, bit i per X.509 KeyUsage ordinal; the client localizes it),
/// `extendedKeyUsageOids`, `chainSubjectCns` (ordered leaf..root, display only),
/// and `trustStatus` (`u`; Unknown=255 until the trust verdict) — are now
/// RETAINED for the `card:/` KIO worker (purpose-named cert folders + the cert
/// `info.txt` render). No DER crosses the wire (Certificates1.xml): the agent
/// parses X.509 and we render only.
struct CertificateInfo
{
    QString certId;                   ///< Opaque SHA-256(DER) handle; the value Sign() takes.
    bool signingCapable = false;      ///< Paired on-card key + signing-suitable keyUsage.
    QString subjectCn;                ///< subject/cn field (display only).
    QString issuerCn;                 ///< issuer/cn field (display only).
    QString notAfter;                 ///< validity/notAfter (display only, ISO-8601 UTC).
    quint32 keyUsageBits = 0;         ///< `u` X.509 KeyUsage bitmask (bit i per ordinal); client localizes.
    QStringList extendedKeyUsageOids; ///< `as` EKU OIDs (dotted).
    QStringList chainSubjectCns;      ///< `as` ordered leaf..root subject CNs (display only).
    quint32 trustStatus = 255;        ///< `u` 0 Trusted .. 4 Expired, 255 Unknown (until evaluated).
};

using CertificateList = QList<CertificateInfo>;

/// @brief One Certificates1 field — the `(ssv)` tuple (labelKey, labelFallback,
///        value) of the `a{sa{s(ssv)}}` field-group map. Distinct from
///        Identity1's `(sssv)` (no redundant type string — Certificates1.xml).
///        These intermediate types are registered as D-Bus metatypes so Qt
///        derives the exact nested container signature in BOTH directions
///        (hand-rolling nested `beginMap` signatures is error-prone).
struct CertField
{
    QString labelKey;
    QString labelFallback;
    QDBusVariant value; ///< "s" UTF-8 string (the agent emits a variant `v`, mirroring IdentityField).
};
using CertFieldGroup = QMap<QString, CertField>;
using CertFieldGroups = QMap<QString, CertFieldGroup>;

QDBusArgument& operator<<(QDBusArgument& arg, const CertField& f);
const QDBusArgument& operator>>(const QDBusArgument& arg, CertField& f);

QDBusArgument& operator<<(QDBusArgument& arg, const CertificateInfo& c);
const QDBusArgument& operator>>(const QDBusArgument& arg, CertificateInfo& c);

/// @brief Drives a single Operation1 + its typed result interface.
///
/// Lifecycle: construct bound to an operation object path and the typed
/// interface name (`org.librescrs.Agent.Operation.Sign1` / `.Identity1` / …).
/// The ctor connects `Finished`, `PropertiesChanged` and the typed `Result`
/// signal, then performs the lost-Finished recovery: it reads the `Completed`
/// terminal triple synchronously, and if the op already finished Ok it pulls
/// the payload (via the typed `GetResult()` where the interface provides one,
/// i.e. Sign) so a late subscriber never misses the result.
class AgentOperation : public QObject
{
    Q_OBJECT
public:
    AgentOperation(const QDBusConnection& connection, const QString& service, const QString& operationPath,
                   const QString& typedInterface, QObject* parent = nullptr);
    ~AgentOperation() override;

    AgentOperation(const AgentOperation&) = delete;
    AgentOperation& operator=(const AgentOperation&) = delete;

    [[nodiscard]] QString path() const;

    /// @brief True once `Finished` was observed (or recovered from the triple).
    [[nodiscard]] bool isFinished() const;
    [[nodiscard]] OperationStatus status() const;
    [[nodiscard]] ErrorCode errorCode() const;

    /// @brief Typed Sign payload, valid after `signResultReady` / a Finished(Ok)
    ///        on a Sign operation. Null fd otherwise.
    [[nodiscard]] const SignResult& signResult() const;

    /// @brief Typed Identity payload (the demarshaled `a{sa{s(sssv)}}` field
    ///        map), valid after `identityResultReady`.
    [[nodiscard]] const IdentityFields& identityResult() const;

    /// @brief Typed Certificates payload (the demarshaled cert array), valid
    ///        after `certificatesResultReady`. Empty otherwise.
    [[nodiscard]] const CertificateList& certificatesResult() const;

    /// @brief Typed Photo payload (the demarshaled `a{sh}` sealed-memfd map),
    ///        valid after `photoResultReady`. Empty otherwise. Each value is a
    ///        sealed memfd the caller must mmap-read and close promptly.
    [[nodiscard]] const PhotoMap& photoResult() const;

    /// @brief The uniform mutation result (`a{sv}`) of an Operation.Credentials1
    ///        attempt (ManagePin / ActivateSigningKey / ListCredentials), valid
    ///        after `credentialsResultReady`. Unlike the Ok-only Sign/Identity
    ///        results, the credentials Result is delivered for EVERY completed
    ///        attempt — INCLUDING the soft-fail outcomes (invalidPin / blocked)
    ///        that finish Error — so `pinResult().outcome` is meaningful even when
    ///        `status()` is Error.
    [[nodiscard]] const PinResult& pinResult() const;

    /// @brief The demarshaled `aa{sv}` records of a ListCredentials attempt, valid
    ///        after `credentialsResultReady`. Empty for a mutation (ManagePin /
    ///        ActivateSigningKey) — an empty list is a legitimate result, not a
    ///        missing one.
    [[nodiscard]] const CredentialList& credentialsResult() const;

    /// @brief Request `Operation1.Cancel`.
    void cancel();

    /// @brief Force a terminal outcome WITHOUT touching the bus, firing
    ///        `finished` once if it has not already fired.
    ///
    /// The AgentClient calls this on every live operation when the agent's bus
    /// name vanishes mid-flight: no `Operation1.Finished` will ever arrive, so
    /// without this sweep the consumer's `finished`/`failed` never fires and a
    /// KJob/plasmoid hangs forever. Idempotent (a no-op once finished).
    void terminate(OperationStatus status, ErrorCode code, const QString& msgKey, const QString& msgFallback);

Q_SIGNALS:
    void phaseChanged(LibreKDE::OperationPhase phase, double progress);
    void finished(LibreKDE::OperationStatus status, LibreKDE::ErrorCode errorCode, const QString& msgKey,
                  const QString& msgFallback);
    void signResultReady();
    void identityResultReady();
    void certificatesResultReady();
    void photoResultReady();
    void credentialsResultReady();

private Q_SLOTS:
    void onFinished(uint status, uint errorCode, const QString& msgKey, const QString& msgFallback);
    void onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& invalidated);
    void onSignResult(const QDBusUnixFileDescriptor& fd, const QVariantMap& meta);
    void onIdentityResult(LibreKDE::IdentityFields fields);
    void onCertificatesResult(LibreKDE::CertificateList certificates);
    void onPhotoResult(LibreKDE::PhotoMap photos);
    void onCredentialsResult(QVariantMap result, LibreKDE::CredentialRecordsWire records);

private:
    void recoverIfAlreadyFinished();
    void recoverSignResultViaGetResult();
    /// @brief Late-subscriber recovery for the inline typed results
    ///        (Identity/Certificates/Photo): one parameterised GetResult pull
    ///        that re-serves the retained payload when the one-shot Result signal
    ///        was lost/raced. A non-reply (Error.NoResult, grace elapsed, or an
    ///        agent without the method) leaves the result unset so the caller
    ///        falls through to the loud CommunicationError.
    void recoverInlineResultViaGetResult();
    /// @brief Late-subscriber recovery for the Operation.Credentials1 result: a
    ///        dedicated 2-out-arg GetResult pull (mirroring
    ///        `recoverSignResultViaGetResult`, NOT the single-arg inline path)
    ///        that re-serves the retained `(a{sv} result, aa{sv} records)` payload
    ///        when the one-shot Result signal was lost/raced. A non-reply
    ///        (Error.NoResult, grace elapsed, or an agent without the method)
    ///        leaves the result unset so the caller falls through to the loud
    ///        CommunicationError. An EMPTY records list is a legitimate mutation
    ///        result: the presence of a Result REPLY (not a non-empty list) is the
    ///        signal that a payload was recovered.
    void recoverCredentialsResultViaGetResult();
    /// @brief Emit a *ResultReady signal, queued to the next event-loop turn
    ///        while the ctor's lost-Finished recovery runs (before the consumer
    ///        connects), synchronous otherwise. Mirrors emitFinishedOnce.
    void emitResultReadyQueuedOrSync(void (AgentOperation::*signal)());
    /// @brief Resolve a terminal (status, errorCode) into the public Finished.
    ///        On a terminal Ok with no result yet seen: recovers the typed
    ///        payload via GetResult (Sign artifact, or the inline
    ///        Identity/Certificates/Photo map), or — when GetResult yields
    ///        nothing — surfaces a loud CommunicationError so a lost/late Result
    ///        never masquerades as a silent empty success.
    void finalizeTerminal(OperationStatus status, ErrorCode code, const QString& msgKey, const QString& msgFallback);
    void emitFinishedOnce(OperationStatus status, ErrorCode code, const QString& msgKey, const QString& msgFallback);

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace LibreKDE

Q_DECLARE_METATYPE(LibreKDE::OperationPhase)
Q_DECLARE_METATYPE(LibreKDE::OperationStatus)
Q_DECLARE_METATYPE(LibreKDE::ErrorCode)
Q_DECLARE_METATYPE(LibreKDE::PhotoMap)
Q_DECLARE_METATYPE(LibreKDE::IdentityField)
Q_DECLARE_METATYPE(LibreKDE::IdentityFieldGroup)
Q_DECLARE_METATYPE(LibreKDE::IdentityFields)
Q_DECLARE_METATYPE(LibreKDE::CertField)
Q_DECLARE_METATYPE(LibreKDE::CertFieldGroup)
Q_DECLARE_METATYPE(LibreKDE::CertFieldGroups)
Q_DECLARE_METATYPE(LibreKDE::CertificateInfo)
Q_DECLARE_METATYPE(LibreKDE::CertificateList)
