// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "CardWorkerLogic.h"

#include "CardRenderers.h"

#include <KIO/Global>
#include <KLocalizedString>

#include <QHash>

#include <sys/stat.h>

using namespace KIO;

namespace LibreKDE {

namespace {

/// @brief Localized render labels for the worker (the pure renderers take their
///        strings — i18n lives in the worker process's `librekde` domain, C7).
// The two cert-export MIME types, single-sourced for stat/mimetype/get.
constexpr const char* kCertDerMime = "application/pkix-cert";
constexpr const char* kCertPemMime = "application/x-pem-file";

/// @brief Image MIME derived from a photo leaf's extension (no card read). The
///        extension is a capability heuristic; get()'s byte sniff is authoritative.
QString photoMimeForName(const QString& name)
{
    return name.endsWith(QLatin1String(".jp2")) ? QStringLiteral("image/jp2") : QStringLiteral("image/jpeg");
}

RenderLabels workerRenderLabels()
{
    RenderLabels l;
    l.keyUsageNames = QStringList{
        i18nc("X.509 key usage", "Digital Signature"), i18nc("X.509 key usage", "Non-Repudiation"),
        i18nc("X.509 key usage", "Key Encipherment"),  i18nc("X.509 key usage", "Data Encipherment"),
        i18nc("X.509 key usage", "Key Agreement"),     i18nc("X.509 key usage", "Certificate Signing"),
        i18nc("X.509 key usage", "CRL Signing"),       i18nc("X.509 key usage", "Encipher Only"),
        i18nc("X.509 key usage", "Decipher Only"),
    };
    l.trustNotEvaluated = i18nc("certificate trust status", "not yet evaluated");
    l.qualifiedUnknown = i18nc("certificate qualified status", "unknown");

    // info.txt body labels (acronyms PKI/eMRTD stay untranslated; the body is a
    // display payload, NOT a URL key, so localizing it is safe — unlike the
    // cert-folder name, which must stay a stable English URL segment).
    l.labelSubject = i18nc("certificate info.txt line label", "Subject");
    l.labelIssuer = i18nc("certificate info.txt line label", "Issuer");
    l.labelValidUntil = i18nc("certificate info.txt line label", "Valid until");
    l.labelPurpose = i18nc("certificate info.txt line label", "Purpose");
    l.labelExtendedKeyUsage = i18nc("certificate info.txt line label", "Extended Key Usage");
    l.labelSigningCapable = i18nc("certificate info.txt line label", "Signing capable");
    l.labelTrust = i18nc("certificate info.txt line label", "Trust");
    l.labelQualified = i18nc("certificate info.txt line label", "Qualified");
    l.labelChain = i18nc("certificate info.txt line label", "Chain");
    l.labelReader = i18nc("card info.txt line label", "Reader");
    l.labelCapabilities = i18nc("card info.txt line label", "Capabilities");
    l.labelPreReadAuth = i18nc("card info.txt line label", "Pre-read authentication");

    l.valueYes = i18nc("boolean value in info.txt", "yes");
    l.valueNo = i18nc("boolean value in info.txt", "no");
    l.valueNone = i18nc("empty capability list in info.txt", "none");
    l.valueAuthNone = i18nc("no pre-read authentication required", "None");
    l.capIdentity = i18nc("card capability flag", "Identity");
    l.capPinManagement = i18nc("card capability flag", "PIN management");
    return l;
}

} // namespace

CardWorkerLogic::CardWorkerLogic(CardDataSource& source) : m_source(source), m_tree(source) {}

void CardWorkerLogic::addDirEntry(UDSEntry& entry, const QString& name) const
{
    entry.clear();
    entry.fastInsert(UDSEntry::UDS_NAME, name);
    entry.fastInsert(UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(UDSEntry::UDS_ACCESS, 0500); // r-x, read-only browse
}

void CardWorkerLogic::addFileEntry(UDSEntry& entry, const QString& name, const QString& mime) const
{
    entry.clear();
    entry.fastInsert(UDSEntry::UDS_NAME, name);
    entry.fastInsert(UDSEntry::UDS_FILE_TYPE, S_IFREG);
    entry.fastInsert(UDSEntry::UDS_ACCESS, 0400); // r--, read-only
    // UDS_SIZE deliberately OMITTED: a read-backed leaf's true size is unknown
    // without a card read, which must NOT happen on listDir/stat.
    if (!mime.isEmpty()) {
        entry.fastInsert(UDSEntry::UDS_MIME_TYPE, mime);
    }
}

QString CardWorkerLogic::stableCertFolderName(const CertInfoView& cert)
{
    // STABLE URL segment: English purpose (defaultRenderLabels), QStringLiteral
    // format — NEVER i18n. A folder name is a URL path segment and the get()
    // resolution key; deriving it from the active locale would make folder URLs
    // diverge across a language switch and break a get() done under a different
    // locale than the listDir that produced the name (cross-locale instability).
    const QString purpose = primaryPurpose(cert.keyUsageBits, defaultRenderLabels());
    const QString shortId = cert.certId.left(8);
    if (purpose.isEmpty()) {
        return QStringLiteral("Certificate (%1)").arg(shortId);
    }
    return QStringLiteral("%1 (%2)").arg(purpose, shortId);
}

QString CardWorkerLogic::displayCertFolderName(const CertInfoView& cert, const RenderLabels& labels)
{
    // Localized presentation only (UDS_DISPLAY_NAME) — never a URL key.
    const QString purpose = primaryPurpose(cert.keyUsageBits, labels);
    const QString shortId = cert.certId.left(8);
    if (purpose.isEmpty()) {
        return i18nc("certificate folder fallback name, %1 = short id", "Certificate (%1)", shortId);
    }
    return i18nc("certificate folder name: purpose + short id", "%1 (%2)", purpose, shortId);
}

QList<CardWorkerLogic::CertFolder> CardWorkerLogic::buildCertFolders(const CertListResult& certs)
{
    // The SINGLE source of truth for the folder set, shared by listDir and doGet.
    // Two signing certs that share purpose AND the first 8 hex of certId would
    // otherwise emit duplicate URL segments (the second cert unreachable); a
    // stable per-name index suffix keeps every name distinct and resolvable.
    const RenderLabels display = workerRenderLabels();
    QList<CertFolder> folders;
    QHash<QString, int> seen; // base stable name -> count
    for (const CertInfoView& c : certs.certs) {
        if (!c.signingCapable) {
            continue; // PKI folders are per signing-capable cert
        }
        const QString base = stableCertFolderName(c);
        const int n = seen[base]++;
        CertFolder f;
        // URL-segment-safe suffix ('~' is RFC 3986 unreserved; '#' would be parsed
        // as a URL fragment delimiter).
        f.name = n == 0 ? base : QStringLiteral("%1 ~%2").arg(base).arg(n + 1);
        // The display name is presentation, not a URL key, so it carries its own
        // localized disambiguator — NOT the raw '~N' URL marker — so two certs that
        // share purpose + short certId still show distinct visible labels.
        const QString displayBase = displayCertFolderName(c, display);
        f.displayName = n == 0 ? displayBase
                               : i18nc("disambiguator for same-purpose cert folders: %1 = base name, %2 = ordinal",
                                       "%1 (%2)", displayBase, n + 1);
        f.certId = c.certId;
        folders << f;
    }
    return folders;
}

QString CardWorkerLogic::resolveCertId(const QString& cardPath, const QString& certFolder, CertListResult& outCerts)
{
    outCerts = m_source.readCertificates(cardPath);
    if (outCerts.status != ReadStatus::Ok) {
        return {};
    }
    // Map the stable URL segment to a certId via the SAME dedup-guarded folder
    // set listDir built (locale-independent, dedup-collision-safe).
    for (const CertFolder& f : buildCertFolders(outCerts)) {
        if (f.name == certFolder) {
            return f.certId;
        }
    }
    return {};
}

WorkerResult CardWorkerLogic::failForRead(ReadStatus status, bool isDirOp) const
{
    switch (status) {
    case ReadStatus::Cancelled:
        return WorkerResult::fail(ERR_USER_CANCELED, QString());
    case ReadStatus::AuthFailed:
        return WorkerResult::fail(ERR_ACCESS_DENIED, QString());
    case ReadStatus::CardRemoved:
        return WorkerResult::fail(ERR_WORKER_DIED, i18n("The card was removed."));
    case ReadStatus::Unavailable: {
        // Operation-aware: a directory enter reports "could not enter folder";
        // a leaf get() reports "could not open for reading". The
        // client cannot distinguish bus-gone from card-vanished here, so do NOT
        // special-case ERR_WORKER_DIED.
        return WorkerResult::fail(isDirOp ? ERR_CANNOT_ENTER_DIRECTORY : ERR_CANNOT_OPEN_FOR_READING,
                                  i18n("The smart-card service is unavailable."));
    }
    case ReadStatus::CapabilityMissing:
    case ReadStatus::NotAvailable:
        return WorkerResult::fail(ERR_DOES_NOT_EXIST, QString());
    case ReadStatus::Error:
    case ReadStatus::Ok:
        break;
    }
    return WorkerResult::fail(ERR_WORKER_DIED, i18n("Could not read the card."));
}

QList<CardWorkerLogic::CertFolder> CardWorkerLogic::pkiCertFolders(const QString& cardPath, CertListResult& outResult)
{
    outResult = m_source.readCertificates(cardPath);
    if (outResult.status != ReadStatus::Ok) {
        return {};
    }
    return buildCertFolders(outResult);
}

WorkerResult CardWorkerLogic::doListDir(const QUrl& url)
{
    const CardNode node = m_tree.resolve(url);

    // KIO expects a "." self-entry for a directory listing; without it it warns
    // "UDSEntry for '.' not found" and synthesizes a default. Emit it up front for
    // every directory node kind.
    switch (node.kind) {
    case NodeKind::Root:
    case NodeKind::Reader:
    case NodeKind::IdentityDir:
    case NodeKind::PkiDir:
    case NodeKind::CertDir: {
        UDSEntry self;
        addDirEntry(self, QStringLiteral("."));
        emitListEntry(self);
        break;
    }
    default:
        break;
    }

    switch (node.kind) {
    case NodeKind::Root: {
        UDSEntry entry;
        for (const QString& reader : m_tree.list(url)) {
            addDirEntry(entry, reader);
            emitListEntry(entry);
        }
        return WorkerResult::pass();
    }
    case NodeKind::Reader: {
        UDSEntry entry;
        for (const QString& child : m_tree.list(url)) {
            if (child == CardTree::infoTxt()) {
                addFileEntry(entry, child, QStringLiteral("text/plain"));
            } else {
                addDirEntry(entry, child);
            }
            emitListEntry(entry);
        }
        return WorkerResult::pass();
    }
    case NodeKind::IdentityDir: {
        UDSEntry entry;
        for (const QString& child : m_tree.list(url)) {
            if (child == CardTree::identityTxt()) {
                addFileEntry(entry, child, QStringLiteral("text/plain"));
            } else {
                // photo.<ext>: an image MIME from the extension so the file manager
                // shows and opens it directly; get() emits the sniffed true MIME.
                addFileEntry(entry, child, photoMimeForName(child));
            }
            emitListEntry(entry);
        }
        return WorkerResult::pass();
    }
    case NodeKind::PkiDir: {
        // The one listDir path that reads the card: entering the data folder
        // resolves the cert list (lazy PACE acceptable here).
        CertListResult certs;
        const QList<CertFolder> folders = pkiCertFolders(node.presence.cardPath, certs);
        if (certs.status != ReadStatus::Ok) {
            return failForRead(certs.status, /*isDirOp=*/true);
        }
        UDSEntry entry;
        for (const CertFolder& folder : folders) {
            addDirEntry(entry, folder.name);
            // UDS_NAME is the STABLE URL segment; the localized form rides as the
            // display name so a file manager shows a translated label without
            // making the URL locale-dependent (spec C1 stable-handle invariant).
            if (!folder.displayName.isEmpty() && folder.displayName != folder.name) {
                entry.fastInsert(UDSEntry::UDS_DISPLAY_NAME, folder.displayName);
            }
            emitListEntry(entry);
        }
        return WorkerResult::pass();
    }
    case NodeKind::CertDir: {
        // info.txt (rendered metadata) plus the raw-cert exports the agent serves
        // from Pkcs11_1.CertDer (public data). No card read here — the names are
        // static for any cert folder; get() does the I/O.
        UDSEntry entry;
        addFileEntry(entry, CardTree::certInfoTxt(), QStringLiteral("text/plain"));
        emitListEntry(entry);
        addFileEntry(entry, CardTree::certDerName(), QLatin1String(kCertDerMime));
        emitListEntry(entry);
        addFileEntry(entry, CardTree::certPemName(), QLatin1String(kCertPemMime));
        emitListEntry(entry);
        return WorkerResult::pass();
    }
    case NodeKind::Invalid:
        return WorkerResult::fail(ERR_CANNOT_ENTER_DIRECTORY, url.toDisplayString());
    default:
        // A leaf URL passed to listDir.
        return WorkerResult::fail(ERR_IS_DIRECTORY, url.toDisplayString());
    }
}

WorkerResult CardWorkerLogic::doStat(const QUrl& url)
{
    const CardNode node = m_tree.resolve(url);
    UDSEntry entry;

    switch (node.kind) {
    case NodeKind::Root:
        addDirEntry(entry, QStringLiteral("card"));
        emitStatEntry(entry);
        return WorkerResult::pass();
    case NodeKind::Reader:
        addDirEntry(entry, node.readerName);
        emitStatEntry(entry);
        return WorkerResult::pass();
    case NodeKind::IdentityDir:
        addDirEntry(entry, CardTree::identityDirName());
        emitStatEntry(entry);
        return WorkerResult::pass();
    case NodeKind::PkiDir:
        addDirEntry(entry, CardTree::pkiDirName());
        emitStatEntry(entry);
        return WorkerResult::pass();
    case NodeKind::CertDir:
        addDirEntry(entry, node.certFolder);
        emitStatEntry(entry);
        return WorkerResult::pass();
    case NodeKind::InfoLeaf:
    case NodeKind::IdentityLeaf:
    case NodeKind::CertInfoLeaf:
        addFileEntry(entry, url.fileName(), QStringLiteral("text/plain"));
        emitStatEntry(entry);
        return WorkerResult::pass();
    case NodeKind::PhotoLeaf:
        // Extension-derived image MIME (no card read); get() confirms by sniffing.
        addFileEntry(entry, url.fileName(), photoMimeForName(url.fileName()));
        emitStatEntry(entry);
        return WorkerResult::pass();
    case NodeKind::CertDerLeaf:
        addFileEntry(entry, CardTree::certDerName(), QLatin1String(kCertDerMime));
        emitStatEntry(entry);
        return WorkerResult::pass();
    case NodeKind::CertPemLeaf:
        addFileEntry(entry, CardTree::certPemName(), QLatin1String(kCertPemMime));
        emitStatEntry(entry);
        return WorkerResult::pass();
    case NodeKind::Invalid:
        return WorkerResult::fail(ERR_DOES_NOT_EXIST, url.toDisplayString());
    }
    return WorkerResult::fail(ERR_DOES_NOT_EXIST, url.toDisplayString());
}

WorkerResult CardWorkerLogic::doMimetype(const QUrl& url)
{
    const CardNode node = m_tree.resolve(url);
    switch (node.kind) {
    case NodeKind::InfoLeaf:
    case NodeKind::IdentityLeaf:
    case NodeKind::CertInfoLeaf:
        emitMimeType(QStringLiteral("text/plain"));
        return WorkerResult::pass();
    case NodeKind::CertDerLeaf:
        // The DER's MIME is known from the leaf name alone — no card read needed.
        emitMimeType(QLatin1String(kCertDerMime));
        return WorkerResult::pass();
    case NodeKind::CertPemLeaf:
        emitMimeType(QLatin1String(kCertPemMime));
        return WorkerResult::pass();
    case NodeKind::PhotoLeaf:
        // Extension-derived image MIME (no read); get() emits the sniffed true MIME.
        emitMimeType(photoMimeForName(url.fileName()));
        return WorkerResult::pass();
    case NodeKind::Root:
    case NodeKind::Reader:
    case NodeKind::IdentityDir:
    case NodeKind::PkiDir:
    case NodeKind::CertDir:
        emitMimeType(QStringLiteral("inode/directory"));
        return WorkerResult::pass();
    case NodeKind::Invalid:
        return WorkerResult::fail(ERR_DOES_NOT_EXIST, url.toDisplayString());
    }
    return WorkerResult::fail(ERR_DOES_NOT_EXIST, url.toDisplayString());
}

WorkerResult CardWorkerLogic::doGet(const QUrl& url)
{
    const CardNode node = m_tree.resolve(url);

    switch (node.kind) {
    case NodeKind::InfoLeaf: {
        const QByteArray bytes = renderInfoTxt(node.presence, workerRenderLabels()).toUtf8();
        emitMimeType(QStringLiteral("text/plain"));
        emitData(bytes);
        emitData(QByteArray());
        return WorkerResult::pass();
    }
    case NodeKind::IdentityLeaf: {
        const IdentityResult id = m_source.readIdentity(node.presence.cardPath);
        if (id.status != ReadStatus::Ok) {
            return failForRead(id.status, /*isDirOp=*/false);
        }
        emitMimeType(QStringLiteral("text/plain"));
        emitData(renderIdentityTxt(id.fields).toUtf8());
        emitData(QByteArray());
        return WorkerResult::pass();
    }
    case NodeKind::PhotoLeaf: {
        const PhotoResult photo = m_source.getPhoto(node.presence.cardPath);
        if (photo.status != ReadStatus::Ok) {
            return failForRead(photo.status, /*isDirOp=*/false);
        }
        emitMimeType(sniffImageMime(photo.bytes)); // true MIME, no rename/transcode
        emitData(photo.bytes);
        emitData(QByteArray());
        return WorkerResult::pass();
    }
    case NodeKind::CertInfoLeaf: {
        // Resolve the URL segment to a certId by IDENTITY (one cert read), then
        // render that exact cert. resolveCertId maps via the SAME dedup-guarded
        // folder set listDir built — locale-independent and collision-safe.
        CertListResult certs;
        const QString wantCertId = resolveCertId(node.presence.cardPath, node.certFolder, certs);
        if (certs.status != ReadStatus::Ok) {
            return failForRead(certs.status, /*isDirOp=*/false);
        }
        if (!wantCertId.isEmpty()) {
            for (const CertInfoView& c : certs.certs) {
                if (c.certId == wantCertId) {
                    emitMimeType(QStringLiteral("text/plain"));
                    emitData(renderCertInfoTxt(c, workerRenderLabels()).toUtf8());
                    emitData(QByteArray());
                    return WorkerResult::pass();
                }
            }
        }
        return WorkerResult::fail(ERR_DOES_NOT_EXIST, url.toDisplayString());
    }
    case NodeKind::CertDerLeaf:
    case NodeKind::CertPemLeaf: {
        // Resolve the folder to its certId (one cert read), then fetch the raw DER
        // from the agent's public Pkcs11_1.CertDer surface. PEM is the same DER
        // wrapped in base64 framing — no parse, so the LM-free contract holds.
        CertListResult certs;
        const QString wantCertId = resolveCertId(node.presence.cardPath, node.certFolder, certs);
        if (certs.status != ReadStatus::Ok) {
            return failForRead(certs.status, /*isDirOp=*/false);
        }
        if (wantCertId.isEmpty()) {
            return WorkerResult::fail(ERR_DOES_NOT_EXIST, url.toDisplayString());
        }
        const CertDerResult der = m_source.getCertificateDer(node.presence.cardPath, wantCertId);
        if (der.status != ReadStatus::Ok) {
            return failForRead(der.status, /*isDirOp=*/false);
        }
        const bool pem = node.kind == NodeKind::CertPemLeaf;
        emitMimeType(pem ? QLatin1String(kCertPemMime) : QLatin1String(kCertDerMime));
        emitData(pem ? derToPem(der.der) : der.der);
        emitData(QByteArray());
        return WorkerResult::pass();
    }
    case NodeKind::Root:
    case NodeKind::Reader:
    case NodeKind::IdentityDir:
    case NodeKind::PkiDir:
    case NodeKind::CertDir:
        return WorkerResult::fail(ERR_IS_DIRECTORY, url.toDisplayString());
    case NodeKind::Invalid:
        return WorkerResult::fail(ERR_DOES_NOT_EXIST, url.toDisplayString());
    }
    return WorkerResult::fail(ERR_DOES_NOT_EXIST, url.toDisplayString());
}

} // namespace LibreKDE
