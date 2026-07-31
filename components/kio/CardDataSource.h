// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <LibreSCRS/AgentClient/Types.h> // LibreSCRS::AgentClient::CertificateInfo

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <cstdint>

/// @file
/// @brief The pure data seam between the `card:/` KIO worker's layout/render
///        core and the agent. KIO-free and transport-free on purpose: the pure
///        `CardTree` + renderers depend ONLY on this interface and the simple
///        result structs below, so the whole tree/render layer is unit-testable
///        against an in-memory fake with no agent connection at all.
///
/// The single I/O-bearing implementation (`AgentCardDataSource`) lives over the
/// shared agent client library. Certificate metadata is that library's own
/// `CertificateInfo` value type rather than a re-copied view: it is a header-only
/// Qt aggregate with no transport in it, and re-declaring it here would be a
/// second hand-kept mirror of the same fields — with the certificate id, which
/// the worker turns into a URL path segment, riding on the copy.

namespace LibreKDE {

/// @brief A reader currently holding a resolvable card, with its capability
///        bitfield and pre-read auth method — derived from the agent's
///        registry WITHOUT any card I/O (no PACE, no read).
///
/// `capabilities` mirrors the card's capability bitfield
/// (`LibreSCRS::AgentClient::Cap::*`).
/// `cardId` is the opaque card handle the I/O methods take — compare and pass
/// back as-is, never parse.
struct CardPresence
{
    QString readerName;             ///< Reader friendly name — the `card:/<readerName>` segment.
    QString cardId;                 ///< Opaque agent card id (handle for the I/O methods).
    std::uint32_t capabilities = 0; ///< Card capability bitfield (LibreSCRS::AgentClient::Cap::*).
    QString preReadAuth;            ///< Pre-read auth wire token ("None"/"Can"/"Mrz").
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
///        renderers consume this — display strings only, already stringified).
struct IdentityFieldView
{
    QString group;         ///< Group key (e.g. "personal").
    QString fieldKey;      ///< Field key (e.g. "given_name").
    QString labelFallback; ///< Agent-authored human label (display only).
    QString value;         ///< Stringified value ("text"/"date"; binary fields are skipped).
};

// Each result below carries a `message`: the localized reason the read failed,
// when one is available. It is populated ONLY for the statuses whose worker-side
// text is generic (`Error` and `Unavailable`), and only from the shared outcome
// rule — never composed at the call site. It is empty everywhere else, a user
// cancel included: that one must stay silent rather than acquire a "did not
// finish" sentence. The worker falls back to its own copy when it is empty, so a
// data source that produces no reason (the in-memory fake; a stall that never
// reached a terminal outcome) renders exactly what it rendered before.

/// @brief Result of `readIdentity`: a flat, render-ready field list.
struct IdentityResult
{
    ReadStatus status = ReadStatus::Error;
    QList<IdentityFieldView> fields;
    QString message; ///< See the localized-reason note above.
};

/// @brief Result of `readCertificates`.
struct CertListResult
{
    ReadStatus status = ReadStatus::Error;
    QList<LibreSCRS::AgentClient::CertificateInfo> certs;
    QString message; ///< See the localized-reason note above.
};

/// @brief Result of `getPhoto`: the raw photo bytes for the (group:field) node.
///        No transcoding — the worker sniffs the bytes for the true MIME.
struct PhotoResult
{
    ReadStatus status = ReadStatus::Error;
    QByteArray bytes; ///< Raw photo bytes (often JPEG2000 from eMRTD DG2).
    QString message;  ///< See the localized-reason note above.
};

/// @brief Result of `getCertificateDer`: the raw certificate DER bytes.
struct CertDerResult
{
    ReadStatus status = ReadStatus::Error;
    QByteArray der;
    QString message; ///< See the localized-reason note above.
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
    [[nodiscard]] virtual IdentityResult readIdentity(const QString& cardId) = 0;

    /// @brief Read the certificate metadata list (I/O; lazy PACE).
    [[nodiscard]] virtual CertListResult readCertificates(const QString& cardId) = 0;

    /// @brief Read the ID photo bytes (I/O; lazy PACE).
    [[nodiscard]] virtual PhotoResult getPhoto(const QString& cardId) = 0;

    /// @brief Export a certificate's raw DER (the agent's public-data surface —
    ///        no consent, no lease). @p certId is a `CertificateInfo::id`.
    [[nodiscard]] virtual CertDerResult getCertificateDer(const QString& cardId, const QString& certId) = 0;
};

} // namespace LibreKDE
