// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// CardWorker logic, exercised against an in-memory FakeCardDataSource (no live
// KIO process, no D-Bus). Highest-value test: listDir/stat/mimeType issue ZERO
// card-read ops; only get() does (lazy PACE). Plus the
// error-mapping table and capability-matrix edges.

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include "CardTree.h"
#include "CardWorkerLogic.h"
#include "FakeCardDataSource.h"

#include <KIO/Global>
#include <KIO/UDSEntry>

#include <QByteArray>
#include <QList>
#include <QString>
#include <gtest/gtest.h>

namespace Client = LibreSCRS::AgentClient;

using namespace LibreKDE;
using namespace LibreKDETest;

namespace {

// Test harness: the worker's pure logic (CardWorkerLogic, decoupled from the
// not-standalone-constructible KIO::WorkerBase) over the in-memory fake,
// capturing all output via the emit* hooks.
class TestableCardWorker : public CardWorkerLogic
{
public:
    explicit TestableCardWorker(CardDataSource& source) : CardWorkerLogic(source) {}

    KIO::WorkerResult listDir(const QUrl& url)
    {
        return doListDir(url);
    }
    KIO::WorkerResult stat(const QUrl& url)
    {
        return doStat(url);
    }
    KIO::WorkerResult mimetype(const QUrl& url)
    {
        return doMimetype(url);
    }
    KIO::WorkerResult get(const QUrl& url)
    {
        return doGet(url);
    }

    QStringList listedNames;
    QStringList listedDisplayNames;
    QString lastMime;
    QByteArray gotBytes;

protected:
    void emitListEntry(const KIO::UDSEntry& entry) override
    {
        const QString name = entry.stringValue(KIO::UDSEntry::UDS_NAME);
        if (name == QLatin1String(".")) {
            return; // the KIO "." self-entry is protocol scaffolding, not a content child
        }
        listedNames << name;
        listedDisplayNames << entry.stringValue(KIO::UDSEntry::UDS_DISPLAY_NAME);
    }
    void emitStatEntry(const KIO::UDSEntry& entry) override
    {
        listedNames << entry.stringValue(KIO::UDSEntry::UDS_NAME);
    }
    void emitMimeType(const QString& mime) override
    {
        lastMime = mime;
    }
    void emitData(const QByteArray& bytes) override
    {
        if (!bytes.isEmpty()) {
            gotBytes += bytes;
        }
    }
};

CardPresence presence(const QString& reader, const QString& path, std::uint32_t caps, const QString& preAuth)
{
    return CardPresence{reader, path, caps, preAuth};
}

// A SIGNING certificate's KeyUsage: digitalSignature + nonRepudiation (ordinals
// 0 and 1, agent wire 1u<<ordinal). nonRepudiation is the half that makes it a
// signing certificate — digitalSignature alone is an authentication key, and is
// named as one.
constexpr quint32 kSigningKeyUsage = 0x03u;

LibreSCRS::AgentClient::CertificateInfo signingCert(const QString& id, const QString& subject, quint32 ku)
{
    LibreSCRS::AgentClient::CertificateInfo c;
    c.id = id;
    c.signingCapable = true;
    c.subject = subject;
    c.keyUsageBits = ku;
    return c;
}

} // namespace

// ============================================================================
// Lazy PACE: the load-bearing zero-ops assertion.
// ============================================================================
TEST(CardWorker, LazyPaceListStatMimeDoZeroCardIo)
{
    FakeCardDataSource src{{presence(QStringLiteral("NFC"), QStringLiteral("/card/0"),
                                     Client::Cap::IdentityData | Client::Cap::EmrtdCrypto, QStringLiteral("Can"))}};
    IdentityResult id;
    id.status = ReadStatus::Ok;
    id.fields << IdentityFieldView{QStringLiteral("personal"), QStringLiteral("given_name"),
                                   QStringLiteral("Given name"), QStringLiteral("Ana")};
    src.setIdentity(id);

    TestableCardWorker w(src);

    // Browse everything reachable WITHOUT a card read.
    ASSERT_TRUE(w.listDir(QUrl(QStringLiteral("card:/"))).success());
    ASSERT_TRUE(w.listDir(QUrl(QStringLiteral("card:/NFC"))).success());
    ASSERT_TRUE(w.listDir(QUrl(QStringLiteral("card:/NFC/Identity"))).success());
    ASSERT_TRUE(w.stat(QUrl(QStringLiteral("card:/NFC"))).success());
    ASSERT_TRUE(w.stat(QUrl(QStringLiteral("card:/NFC/Identity/identity.txt"))).success());
    ASSERT_TRUE(w.stat(QUrl(QStringLiteral("card:/NFC/Identity/photo.jp2"))).success());
    ASSERT_TRUE(w.mimetype(QUrl(QStringLiteral("card:/NFC/Identity/identity.txt"))).success());
    ASSERT_TRUE(w.mimetype(QUrl(QStringLiteral("card:/NFC/Identity/photo.jp2"))).success());

    EXPECT_EQ(src.ioCallCount(), 0) << "listDir/stat/mimeType must issue ZERO card reads (lazy PACE)";

    // Only get() of a leaf reads the card.
    ASSERT_TRUE(w.get(QUrl(QStringLiteral("card:/NFC/Identity/identity.txt"))).success());
    EXPECT_EQ(src.ioCallCount(), 1) << "exactly one readIdentity op on get()";
    EXPECT_TRUE(w.gotBytes.contains("Ana"));
}

// info.txt is derived from capabilities — it must NOT trigger a card read either.
TEST(CardWorker, GetInfoTxtDoesNoCardIo)
{
    FakeCardDataSource src{{presence(QStringLiteral("R"), QStringLiteral("/card/0"),
                                     Client::Cap::Pki | Client::Cap::IdentityData, QStringLiteral("None"))}};
    TestableCardWorker w(src);
    ASSERT_TRUE(w.get(QUrl(QStringLiteral("card:/R/info.txt"))).success());
    EXPECT_EQ(src.ioCallCount(), 0);
    EXPECT_TRUE(w.gotBytes.contains("Reader: R"));
}

// ============================================================================
// PKI: listDir(PKI/) reads the cert list (allowed — entering the data folder).
// ============================================================================
TEST(CardWorker, ListPkiResolvesCertFoldersByPurpose)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("Gemalto"), QStringLiteral("/card/0"), Client::Cap::Pki, QStringLiteral("None"))}};
    CertListResult certs;
    certs.status = ReadStatus::Ok;
    certs.certs << signingCert(QStringLiteral("aabbccdd1122"), QStringLiteral("Pera"), kSigningKeyUsage);
    src.setCertificates(certs);

    TestableCardWorker w(src);
    const auto result = w.listDir(QUrl(QStringLiteral("card:/Gemalto/PKI")));
    ASSERT_TRUE(result.success());
    EXPECT_EQ(src.ioCallCount(), 1) << "entering PKI/ reads the cert list (lazy PACE acceptable here)";
    ASSERT_EQ(w.listedNames.size(), 1);
    EXPECT_TRUE(w.listedNames.first().contains(QStringLiteral("Digital Signature")));
    EXPECT_TRUE(w.listedNames.first().contains(QStringLiteral("aabbccdd"))) << "short certId disambiguator";
}

TEST(CardWorker, GetCertInfoTxtRenders)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("G"), QStringLiteral("/card/0"), Client::Cap::Pki, QStringLiteral("None"))}};
    CertListResult certs;
    certs.status = ReadStatus::Ok;
    certs.certs << signingCert(QStringLiteral("deadbeefcafe"), QStringLiteral("Mika"), kSigningKeyUsage);
    src.setCertificates(certs);

    TestableCardWorker w(src);
    // Folder name as the worker derives it.
    const QString folder = w.listDir(QUrl(QStringLiteral("card:/G/PKI"))).success() ? w.listedNames.first() : QString();
    ASSERT_FALSE(folder.isEmpty());

    TestableCardWorker w2(src);
    const QUrl infoUrl(QStringLiteral("card:/G/PKI/%1/info.txt").arg(folder));
    ASSERT_TRUE(w2.get(infoUrl).success());
    EXPECT_TRUE(w2.gotBytes.contains("Subject: Mika"));
    EXPECT_TRUE(w2.gotBytes.contains("Trust: not yet evaluated"));
    EXPECT_TRUE(w2.gotBytes.contains("Qualified: unknown"));
}

// P1 regression: the cert-folder UDS_NAME is a STABLE, locale-independent URL
// segment (derived from defaultRenderLabels + QStringLiteral, NOT i18n), and the
// get() resolves by certId — so a folder listed under one locale resolves under
// another. The worker derives the segment from defaultRenderLabels() regardless
// of the active translation; the localized form rides on UDS_DISPLAY_NAME. (The
// test process loads no catalog, so i18n == English here; the load-bearing
// assertion is that the URL segment matches the English-stable form exactly and
// the get() round-trips against that exact segment.)
TEST(CardWorker, CertFolderNameIsStableUrlSegmentAndResolvesByCertId)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("G"), QStringLiteral("/card/0"), Client::Cap::Pki, QStringLiteral("None"))}};
    CertListResult certs;
    certs.status = ReadStatus::Ok;
    certs.certs << signingCert(QStringLiteral("aabbccdd11223344"), QStringLiteral("Pera"), kSigningKeyUsage);
    src.setCertificates(certs);

    TestableCardWorker w(src);
    ASSERT_TRUE(w.listDir(QUrl(QStringLiteral("card:/G/PKI"))).success());
    ASSERT_EQ(w.listedNames.size(), 1);
    // The UDS_NAME is the stable English segment, byte-for-byte.
    const QString segment = w.listedNames.first();
    EXPECT_EQ(segment, QStringLiteral("Digital Signature (aabbccdd)"));

    // get() of that exact segment resolves (by certId, not by re-deriving a name).
    TestableCardWorker w2(src);
    const QUrl infoUrl(QStringLiteral("card:/G/PKI/%1/info.txt").arg(segment));
    ASSERT_TRUE(w2.get(infoUrl).success());
    EXPECT_TRUE(w2.gotBytes.contains("Subject: Pera"));

    // A second listDir produces the IDENTICAL segment (no locale/call-order drift).
    TestableCardWorker w3(src);
    ASSERT_TRUE(w3.listDir(QUrl(QStringLiteral("card:/G/PKI"))).success());
    EXPECT_EQ(w3.listedNames.first(), segment);
}

// P2 dedup: two signing certs that share purpose AND the first 8 hex of certId
// must still get distinct, resolvable folders (an index suffix disambiguates),
// and each info.txt resolves to its OWN cert by certId.
TEST(CardWorker, CertFoldersSharingPurposeAndPrefixGetDistinctResolvableFolders)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("G"), QStringLiteral("/card/0"), Client::Cap::Pki, QStringLiteral("None"))}};
    CertListResult certs;
    certs.status = ReadStatus::Ok;
    // Same purpose (digitalSignature) AND same first 8 hex "aabbccdd".
    certs.certs << signingCert(QStringLiteral("aabbccdd00000001"), QStringLiteral("First"), kSigningKeyUsage);
    certs.certs << signingCert(QStringLiteral("aabbccdd00000002"), QStringLiteral("Second"), kSigningKeyUsage);
    src.setCertificates(certs);

    TestableCardWorker w(src);
    ASSERT_TRUE(w.listDir(QUrl(QStringLiteral("card:/G/PKI"))).success());
    ASSERT_EQ(w.listedNames.size(), 2);
    EXPECT_NE(w.listedNames.at(0), w.listedNames.at(1)) << "colliding names must be disambiguated";
    // the VISIBLE display names must ALSO be distinct — the dedup index has
    // to ride the displayName too, or two same-purpose+prefix certs render two
    // identical visible labels.
    ASSERT_EQ(w.listedDisplayNames.size(), 2);
    EXPECT_NE(w.listedDisplayNames.at(0), w.listedDisplayNames.at(1))
        << "colliding display names must also be disambiguated: " << w.listedDisplayNames.at(0).toStdString();

    // Each folder's info.txt resolves to its OWN cert (resolve-by-certId).
    TestableCardWorker wa(src);
    ASSERT_TRUE(wa.get(QUrl(QStringLiteral("card:/G/PKI/%1/info.txt").arg(w.listedNames.at(0)))).success());
    EXPECT_TRUE(wa.gotBytes.contains("Subject: First"));

    TestableCardWorker wb(src);
    ASSERT_TRUE(wb.get(QUrl(QStringLiteral("card:/G/PKI/%1/info.txt").arg(w.listedNames.at(1)))).success());
    EXPECT_TRUE(wb.gotBytes.contains("Subject: Second"));
}

// cert info.txt get() does exactly ONE readCertificates per get (no per-leaf
// re-prompt, no in-client cache). Pins the contractual read count.
TEST(CardWorker, CertInfoTxtGetReadsCertListExactlyOnce)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("G"), QStringLiteral("/card/0"), Client::Cap::Pki, QStringLiteral("None"))}};
    CertListResult certs;
    certs.status = ReadStatus::Ok;
    certs.certs << signingCert(QStringLiteral("deadbeefcafe"), QStringLiteral("Mika"), kSigningKeyUsage);
    src.setCertificates(certs);

    TestableCardWorker w(src);
    const QString folder = w.listDir(QUrl(QStringLiteral("card:/G/PKI"))).success() ? w.listedNames.first() : QString();
    ASSERT_FALSE(folder.isEmpty());

    TestableCardWorker w2(src); // fresh worker; count only this get()'s reads
    ASSERT_TRUE(w2.get(QUrl(QStringLiteral("card:/G/PKI/%1/info.txt").arg(folder))).success());
    EXPECT_EQ(src.ioCallCount(), 1 /*listDir(PKI)*/ + 1 /*this get*/)
        << "one readCertificates per CertInfoLeaf get(); no caching, no PACE re-prompt-per-leaf";
}

// certificate.der / certificate.pem are a live export over the agent's public
// Pkcs11_1.CertDer surface: the cert folder lists them, stat/mimetype report the
// right type WITHOUT a card read, and get() serves the raw DER (resp. PEM-wrapped
// DER) bytes. A source-side absence maps to "does not exist", never a hang.
TEST(CardWorker, CertDerPemListedStatMimedAndServed)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("G"), QStringLiteral("/card/0"), Client::Cap::Pki, QStringLiteral("None"))}};
    CertListResult certs;
    certs.status = ReadStatus::Ok;
    certs.certs << signingCert(QStringLiteral("0011223344"), QStringLiteral("X"), kSigningKeyUsage);
    src.setCertificates(certs);

    // Scripted DER the agent's CertDer would return (arbitrary bytes; the worker
    // never parses them).
    const QByteArray fakeDer = QByteArrayLiteral("\x30\x82\x01\x0a"
                                                 "DER-CERT-BYTES");
    CertDerResult derResult;
    derResult.status = ReadStatus::Ok;
    derResult.der = fakeDer;
    src.setCertDer(derResult);

    TestableCardWorker w(src);
    const QString folder = w.listDir(QUrl(QStringLiteral("card:/G/PKI"))).success() ? w.listedNames.first() : QString();
    ASSERT_FALSE(folder.isEmpty());

    // The cert folder lists info.txt AND both exports — and listing does NO card
    // read (the names are static; only get() reads).
    TestableCardWorker wl(src);
    const int ioBefore = src.ioCallCount();
    ASSERT_TRUE(wl.listDir(QUrl(QStringLiteral("card:/G/PKI/%1").arg(folder))).success());
    EXPECT_EQ(src.ioCallCount(), ioBefore) << "listing the cert folder must issue zero card reads";
    EXPECT_TRUE(wl.listedNames.contains(QStringLiteral("info.txt")));
    EXPECT_TRUE(wl.listedNames.contains(QStringLiteral("certificate.der")));
    EXPECT_TRUE(wl.listedNames.contains(QStringLiteral("certificate.pem")));

    // stat + mimetype succeed without a card read and report the cert MIME.
    TestableCardWorker wsm(src);
    const int ioBeforeStat = src.ioCallCount();
    EXPECT_TRUE(wsm.stat(QUrl(QStringLiteral("card:/G/PKI/%1/certificate.der").arg(folder))).success());
    EXPECT_TRUE(wsm.mimetype(QUrl(QStringLiteral("card:/G/PKI/%1/certificate.der").arg(folder))).success());
    EXPECT_EQ(wsm.lastMime, QStringLiteral("application/pkix-cert"));
    EXPECT_TRUE(wsm.mimetype(QUrl(QStringLiteral("card:/G/PKI/%1/certificate.pem").arg(folder))).success());
    EXPECT_EQ(wsm.lastMime, QStringLiteral("application/x-pem-file"));
    EXPECT_EQ(src.ioCallCount(), ioBeforeStat) << "stat/mimetype of der/pem must issue zero card reads";

    // get(certificate.der) serves the raw DER verbatim.
    TestableCardWorker wder(src);
    ASSERT_TRUE(wder.get(QUrl(QStringLiteral("card:/G/PKI/%1/certificate.der").arg(folder))).success());
    EXPECT_EQ(wder.lastMime, QStringLiteral("application/pkix-cert"));
    EXPECT_EQ(wder.gotBytes, fakeDer);

    // get(certificate.pem) serves the SAME DER wrapped in PEM framing.
    TestableCardWorker wpem(src);
    ASSERT_TRUE(wpem.get(QUrl(QStringLiteral("card:/G/PKI/%1/certificate.pem").arg(folder))).success());
    EXPECT_EQ(wpem.lastMime, QStringLiteral("application/x-pem-file"));
    EXPECT_TRUE(wpem.gotBytes.contains("-----BEGIN CERTIFICATE-----"));
    EXPECT_TRUE(wpem.gotBytes.contains("-----END CERTIFICATE-----"));

    // A source-side absence (e.g. agent KeyNotFound/UnknownCard) → does-not-exist.
    FakeCardDataSource absent{
        {presence(QStringLiteral("G"), QStringLiteral("/card/0"), Client::Cap::Pki, QStringLiteral("None"))}};
    absent.setCertificates(certs);
    CertDerResult missing;
    missing.status = ReadStatus::NotAvailable;
    absent.setCertDer(missing);
    TestableCardWorker wmiss(absent);
    EXPECT_EQ(wmiss.get(QUrl(QStringLiteral("card:/G/PKI/%1/certificate.der").arg(folder))).error(),
              KIO::ERR_DOES_NOT_EXIST);
}

// ============================================================================
// Error-mapping table.
// ============================================================================
TEST(CardWorker, ErrorMappingTable)
{
    struct Case
    {
        ReadStatus status;
        int expectedError;
    };
    const QList<Case> cases{
        {ReadStatus::Cancelled, KIO::ERR_USER_CANCELED},
        {ReadStatus::AuthFailed, KIO::ERR_ACCESS_DENIED},
        {ReadStatus::CardRemoved, KIO::ERR_WORKER_DIED},
        // A LEAF get() that hits Unavailable reports "could not open for reading",
        // NOT "could not enter folder" (that is the dir-op mapping, asserted
        // separately below).
        {ReadStatus::Unavailable, KIO::ERR_CANNOT_OPEN_FOR_READING},
        {ReadStatus::CapabilityMissing, KIO::ERR_DOES_NOT_EXIST},
        {ReadStatus::Error, KIO::ERR_WORKER_DIED},
    };

    for (const Case& c : cases) {
        FakeCardDataSource src{{presence(QStringLiteral("NFC"), QStringLiteral("/card/0"), Client::Cap::IdentityData,
                                         QStringLiteral("Can"))}};
        IdentityResult id;
        id.status = c.status;
        src.setIdentity(id);

        TestableCardWorker w(src);
        const auto result = w.get(QUrl(QStringLiteral("card:/NFC/Identity/identity.txt")));
        EXPECT_FALSE(result.success());
        EXPECT_EQ(result.error(), c.expectedError)
            << "ReadStatus " << static_cast<int>(c.status) << " -> ERR " << result.error();
    }
}

// Operation-aware Unavailable mapping: entering PKI/ (a dir op) when the agent is
// unavailable reports "could not enter folder", while a leaf get() reports "could
// not open for reading" (asserted in ErrorMappingTable). Same ReadStatus, two ops.
TEST(CardWorker, UnavailableMapsByOperation)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("G"), QStringLiteral("/card/0"), Client::Cap::Pki, QStringLiteral("None"))}};
    CertListResult certs;
    certs.status = ReadStatus::Unavailable;
    src.setCertificates(certs);

    TestableCardWorker wDir(src);
    EXPECT_EQ(wDir.listDir(QUrl(QStringLiteral("card:/G/PKI"))).error(), KIO::ERR_CANNOT_ENTER_DIRECTORY);

    // A CertInfoLeaf get() that hits Unavailable while reading the cert list maps
    // to the leaf code instead.
    TestableCardWorker wGet(src);
    EXPECT_EQ(wGet.get(QUrl(QStringLiteral("card:/G/PKI/Digital Signature (00112233)/info.txt"))).error(),
              KIO::ERR_CANNOT_OPEN_FOR_READING);
}

// A photo with no bytes on the card surfaces as a not-found leaf, not a hang.
TEST(CardWorker, PhotoAbsentMapsToDoesNotExist)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("NFC"), QStringLiteral("/card/0"), Client::Cap::IdentityData, QStringLiteral("Can"))}};
    PhotoResult ph;
    ph.status = ReadStatus::NotAvailable;
    src.setPhoto(ph);

    TestableCardWorker w(src);
    const auto result = w.get(QUrl(QStringLiteral("card:/NFC/Identity/photo.jpg")));
    EXPECT_EQ(result.error(), KIO::ERR_DOES_NOT_EXIST);
}

// A photo get() sniffs the true MIME (JP2) — no rename, no transcode.
TEST(CardWorker, PhotoGetSniffsTrueMime)
{
    FakeCardDataSource src{{presence(QStringLiteral("NFC"), QStringLiteral("/card/0"),
                                     Client::Cap::IdentityData | Client::Cap::EmrtdCrypto, QStringLiteral("Can"))}};
    PhotoResult ph;
    ph.status = ReadStatus::Ok;
    ph.bytes = QByteArray::fromHex("0000000C6A5020200D0A") + QByteArray(20, '\x00'); // JP2 signature
    src.setPhoto(ph);

    TestableCardWorker w(src);
    ASSERT_TRUE(w.get(QUrl(QStringLiteral("card:/NFC/Identity/photo.jp2"))).success());
    EXPECT_EQ(w.lastMime, QStringLiteral("image/jp2"));
    EXPECT_FALSE(w.gotBytes.isEmpty());
}

// ============================================================================
// Capability-matrix edges.
// ============================================================================
TEST(CardWorker, PinManagementOnlyListsNeitherIdentityNorPki)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("R"), QStringLiteral("/card/0"), Client::Cap::PinManagement, QStringLiteral("None"))}};
    TestableCardWorker w(src);
    ASSERT_TRUE(w.listDir(QUrl(QStringLiteral("card:/R"))).success());
    EXPECT_TRUE(w.listedNames.contains(QStringLiteral("info.txt")));
    EXPECT_FALSE(w.listedNames.contains(QStringLiteral("Identity")));
    EXPECT_FALSE(w.listedNames.contains(QStringLiteral("PKI")));
    EXPECT_EQ(src.ioCallCount(), 0);
}

TEST(CardWorker, PassportListsIdentityNotPki)
{
    FakeCardDataSource src{{presence(QStringLiteral("NFC"), QStringLiteral("/card/0"),
                                     Client::Cap::IdentityData | Client::Cap::EmrtdCrypto, QStringLiteral("Can"))}};
    TestableCardWorker w(src);
    ASSERT_TRUE(w.listDir(QUrl(QStringLiteral("card:/NFC"))).success());
    EXPECT_TRUE(w.listedNames.contains(QStringLiteral("Identity")));
    EXPECT_FALSE(w.listedNames.contains(QStringLiteral("PKI")));
}

TEST(CardWorker, UnknownPathErrors)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("R"), QStringLiteral("/card/0"), Client::Cap::Pki, QStringLiteral("None"))}};
    TestableCardWorker w(src);
    EXPECT_EQ(w.listDir(QUrl(QStringLiteral("card:/Nope"))).error(), KIO::ERR_CANNOT_ENTER_DIRECTORY);
    EXPECT_EQ(w.stat(QUrl(QStringLiteral("card:/Nope"))).error(), KIO::ERR_DOES_NOT_EXIST);
    // PKI-only card has no Identity dir.
    EXPECT_EQ(w.stat(QUrl(QStringLiteral("card:/R/Identity"))).error(), KIO::ERR_DOES_NOT_EXIST);
}
