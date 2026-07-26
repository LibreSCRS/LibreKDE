// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <cstdint>

/// @file
/// @brief The pure data seam between the `card:/` KIO worker's layout/render
///        core and the agent. KIO-free and D-Bus-free on purpose: the pure
///        `CardTree` + renderers depend ONLY on this interface and the simple
///        result structs below, so the whole tree/render layer is unit-testable
///        against an in-memory fake with zero D-Bus.
///
/// The single I/O-bearing implementation (`AgentCardDataSource`) lives over
/// `AgentClient`; everything here is plain value types — no `QDBus*`.

namespace LibreKDE {

/// @brief A reader currently holding a resolvable card, with its capability
///        bitfield and pre-read auth method — derived from the agent's
///        ObjectManager registry WITHOUT any card I/O (no PACE, no read).
///
/// `capabilities` mirrors `Card1.Capabilities` (`LibreKDE::Cap::*`).
/// `cardPath` is the agent object path the I/O methods take.
struct CardPresence
{
    QString readerName;             ///< Reader friendly name — the `card:/<readerName>` segment.
    QString cardPath;               ///< Agent Card1 object path (opaque handle for the I/O methods).
    std::uint32_t capabilities = 0; ///< `Card1.Capabilities` bitfield (LibreKDE::Cap::*).
    QString preReadAuth;            ///< `Card1.PreReadAuthMethod` wire string ("None"/"Can"/"Mrz").
};

/// @brief How a read finished, mapped from the agent op's terminal status. The
///        worker maps these onto KIO `ERR_*` codes.
enum class ReadStatus {
    Ok,                ///< The op finished Ok and a payload is present.
    Cancelled,         ///< User/consent cancel → ERR_USER_CANCELED.
    AuthFailed,        ///< Wrong/blocked credential or auth failure → ERR_ACCESS_DENIED.
    CardRemoved,       ///< Card pulled / agent vanished mid-op → ERR_WORKER_DIED.
    Unavailable,       ///< Agent not running / no such card → ERR_CANNOT_ENTER_DIRECTORY.
    CapabilityMissing, ///< The card does not support this read → ERR_DOES_NOT_EXIST.
    Error,             ///< Any other failure (communication/parse/…) → ERR_WORKER_DIED.
    NotAvailable,      ///< The datum is legitimately absent on this card (e.g. no photo) → ERR_DOES_NOT_EXIST.
};

/// @brief One identity field, rendered flat for `identity.txt` (the worker's
///        renderers consume this — no `QDBusVariant`, just display strings).
struct IdentityFieldView
{
    QString group;         ///< Group key (e.g. "personal").
    QString fieldKey;      ///< Field key (e.g. "given_name").
    QString labelFallback; ///< Agent-authored human label (display only).
    QString value;         ///< Stringified value ("text"/"date"; binary fields are skipped).
};

/// @brief Result of `readIdentity`: a flat, render-ready field list.
struct IdentityResult
{
    ReadStatus status = ReadStatus::Error;
    QList<IdentityFieldView> fields;
};

/// @brief One certificate's render-ready metadata (the widened CertificateInfo
///        flattened into a KIO/render-layer view — no D-Bus types).
struct CertInfoView
{
    QString certId;                   ///< Opaque SHA-256(DER) handle (folder disambiguation).
    bool signingCapable = false;      ///< Paired on-card key + signing-suitable keyUsage.
    QString subjectCn;                ///< Subject CN (display only).
    QString issuerCn;                 ///< Issuer CN (display only).
    QString notAfter;                 ///< Validity notAfter (display only, ISO-8601 UTC).
    quint32 keyUsageBits = 0;         ///< X.509 KeyUsage bitmask; the renderer localizes bit names.
    QStringList extendedKeyUsageOids; ///< EKU OIDs (dotted, display only).
    QStringList chainSubjectCns;      ///< Ordered leaf..root subject CNs (display only).
    quint32 trustStatus = 255;        ///< 255 = Unknown (until the trust verdict).
};

/// @brief Result of `readCertificates`.
struct CertListResult
{
    ReadStatus status = ReadStatus::Error;
    QList<CertInfoView> certs;
};

/// @brief Result of `getPhoto`: the raw photo bytes for the (group:field) node.
///        No transcoding — the worker sniffs the bytes for the true MIME.
struct PhotoResult
{
    ReadStatus status = ReadStatus::Error;
    QByteArray bytes; ///< Raw photo bytes (often JPEG2000 from eMRTD DG2).
};

/// @brief Result of `getCertificateDer`: the raw certificate DER bytes.
struct CertDerResult
{
    ReadStatus status = ReadStatus::Error;
    QByteArray der;
};

/// @brief The pure seam the `card:/` layout + render core depends on.
///
/// `listReadersWithCards()` is the ONLY zero-I/O method and is the sole input to
/// `CardTree` (capability-driven layout). The remaining methods perform card
/// I/O (and on a PACE card trigger the agent's CAN prompt) — the worker calls
/// them ONLY on `get()` of a leaf (and on `listDir(PKI/)`, where entering the
/// data folder legitimately reads the cert list).
class CardDataSource
{
public:
    virtual ~CardDataSource() = default;

    /// @brief Present readers + their capabilities, from the agent registry.
    ///        NO card I/O — never prompts. The sole input to `CardTree`.
    [[nodiscard]] virtual QList<CardPresence> listReadersWithCards() = 0;

    /// @brief Read the demographic identity (I/O; lazy PACE on a PACE card).
    [[nodiscard]] virtual IdentityResult readIdentity(const QString& cardPath) = 0;

    /// @brief Read the certificate metadata list (I/O; lazy PACE).
    [[nodiscard]] virtual CertListResult readCertificates(const QString& cardPath) = 0;

    /// @brief Read the ID photo bytes (I/O; lazy PACE).
    [[nodiscard]] virtual PhotoResult getPhoto(const QString& cardPath) = 0;

    /// @brief Export a certificate's raw DER (the agent's public Pkcs11_1.CertDer
    ///        surface — no consent, no lease). @p certId is the Certificates1 id.
    [[nodiscard]] virtual CertDerResult getCertificateDer(const QString& cardPath, const QString& certId) = 0;
};

} // namespace LibreKDE
