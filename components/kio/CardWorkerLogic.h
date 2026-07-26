// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CardDataSource.h"
#include "CardRenderers.h" // RenderLabels
#include "CardTree.h"

#include <KIO/UDSEntry>
#include <KIO/WorkerBase> // KIO::WorkerResult

#include <QByteArray>
#include <QString>

/// @file
/// @brief All `card:/` protocol logic, DECOUPLED from `KIO::WorkerBase`.
///
/// `KIO::WorkerBase` is not standalone-constructible (its ctor needs a live KIO
/// process), so the logic lives here and is unit-tested directly. The
/// production `CardWorker` (CardWorker.h) inherits this AND `WorkerBase` and
/// forwards the `emit*` hooks to KIO; the test subclasses ONLY this.

namespace LibreKDE {

class CardWorkerLogic
{
public:
    explicit CardWorkerLogic(CardDataSource& source);
    virtual ~CardWorkerLogic() = default;

    KIO::WorkerResult doListDir(const QUrl& url);
    KIO::WorkerResult doStat(const QUrl& url);
    KIO::WorkerResult doMimetype(const QUrl& url);
    KIO::WorkerResult doGet(const QUrl& url);

protected:
    // Output hooks the production worker forwards to KIO::WorkerBase; the test
    // overrides them to capture.
    virtual void emitListEntry(const KIO::UDSEntry& entry) = 0;
    virtual void emitStatEntry(const KIO::UDSEntry& entry) = 0;
    virtual void emitMimeType(const QString& mime) = 0;
    virtual void emitData(const QByteArray& bytes) = 0;

    /// @brief One signing-capable cert's PKI folder: its STABLE URL segment name,
    ///        a localized display name, and the resolving certId identity.
    struct CertFolder
    {
        QString name;        ///< Stable, locale-independent URL segment (UDS_NAME).
        QString displayName; ///< Localized presentation form (UDS_DISPLAY_NAME).
        QString certId;      ///< Opaque identity the get() resolves by (NOT the name).
    };

    /// @brief Build the cert-folder set for a card (the ONE listDir path that
    ///        reads the card — entering the PKI data folder). Stable
    ///        URL names are dedup-guarded with an index suffix so two certs that
    ///        share purpose + short certId prefix still get distinct, resolvable
    ///        folders. The returned names/certIds are the SOLE resolution key.
    [[nodiscard]] QList<CertFolder> pkiCertFolders(const QString& cardPath, CertListResult& outResult);
    /// @brief Build the dedup-guarded folder set from an already-read cert list
    ///        (no I/O) — the single source of truth shared by listDir and doGet so
    ///        the URL segment a get() resolves against is identical to the listed
    ///        one, independent of locale.
    [[nodiscard]] static QList<CertFolder> buildCertFolders(const CertListResult& certs);
    /// @brief Resolve a stable cert-folder URL segment to its certId, reading the
    ///        cert list ONCE (folder name → certId via the same dedup-guarded set
    ///        listDir built). Shared by every per-cert leaf get (info.txt + the
    ///        der/pem export). On a read failure @p outCerts carries the non-Ok
    ///        status (caller routes through failForRead); on an unknown folder the
    ///        returned id is empty with @p outCerts Ok.
    [[nodiscard]] QString resolveCertId(const QString& cardPath, const QString& certFolder, CertListResult& outCerts);
    /// @brief The STABLE (locale-independent) folder name for a cert: English
    ///        purpose (from keyUsage) + short certId suffix. Used as a URL
    ///        segment, so it must NOT depend on the active locale.
    [[nodiscard]] static QString stableCertFolderName(const CertInfoView& cert);
    /// @brief The localized presentation form of @p cert's folder (UDS_DISPLAY_NAME).
    [[nodiscard]] static QString displayCertFolderName(const CertInfoView& cert, const RenderLabels& labels);
    /// @brief Map a non-Ok ReadStatus to a KIO error result with localized text.
    ///        @p isDirOp distinguishes a directory enter (ERR_CANNOT_ENTER_DIRECTORY
    ///        for Unavailable) from a leaf get() (ERR_CANNOT_OPEN_FOR_READING). KIO
    ///        synthesizes the standard messages, so the localized defaults here are
    ///        the only text the result carries.
    [[nodiscard]] KIO::WorkerResult failForRead(ReadStatus status, bool isDirOp) const;

    void addDirEntry(KIO::UDSEntry& entry, const QString& name) const;
    void addFileEntry(KIO::UDSEntry& entry, const QString& name, const QString& mime) const;

    CardDataSource& m_source;
    CardTree m_tree;
};

} // namespace LibreKDE
