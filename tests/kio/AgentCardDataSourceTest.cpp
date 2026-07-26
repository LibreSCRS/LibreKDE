// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// AgentCardDataSource over a live FakeAgent: the P0 regression. Each
// I/O method drives an AgentCard op to `finished` via driveToFinished()'s nested
// QEventLoop. If the card is pulled WHILE that loop spins,
// AgentClient::onInterfacesRemoved terminalizes the op (quitting the loop) and
// then deletes the QObject-parented op. The post-loop op->status()/result()
// deref would then be a use-after-free. These tests pull the card mid-read (via
// a queued setCardPresent(false) the nested loop dispatches) and assert each
// method returns the CardRemoved failure cleanly, with no crash / no UAF.

#include "AgentCapabilities.h"
#include "AgentCardDataSource.h"
#include "AgentClient.h"
#include "CardDataSource.h"
#include "TestBus.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <gtest/gtest.h>

using namespace LibreKDE;
using namespace LibreKDETest;

namespace {

// Build a data source over a client bound to the harness's FakeAgent, with an
// operation delay so long the op never finishes on its own — the only way the
// in-flight op terminalizes within the test is the card-removal sweep, which is
// exactly the UAF window. The card must already be tracked before we read.
FakeAgent::Config configWithSlowOps(std::uint32_t caps)
{
    FakeAgent::Config cfg;
    cfg.capabilities = caps;
    cfg.operationDelayMs = 60000; // never fires on its own within the test
    return cfg;
}

// Schedule a card pull onto the next main-loop turn. driveToFinished()'s nested
// loop.exec() will dispatch this singleShot, which triggers InterfacesRemoved →
// onInterfacesRemoved (terminate + delete op) from INSIDE the loop. The read
// method then returns from driveToFinished() with a dangling op (pre-fix) — the
// QPointer guard is what makes the post-loop path safe.
void pullCardMidRead(Harness& h)
{
    QTimer::singleShot(0, [&h]() { h.setCardPresent(false); });
}

} // namespace

TEST(AgentCardDataSource, ReadIdentityCardRemovedMidReadReturnsCleanlyNoUaf)
{
    Harness h(configWithSlowOps(Cap::IdentityData));
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    pullCardMidRead(h);

    // Blocks in driveToFinished()'s nested loop; the queued pull deletes the op
    // mid-loop. Must return cleanly (no UAF) with the card-removed failure.
    const IdentityResult result = source.readIdentity(h.cardPath());
    EXPECT_EQ(result.status, ReadStatus::CardRemoved);
    EXPECT_TRUE(result.fields.isEmpty());
}

TEST(AgentCardDataSource, ReadCertificatesCardRemovedMidReadReturnsCleanlyNoUaf)
{
    Harness h(configWithSlowOps(Cap::Pki));
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    pullCardMidRead(h);

    const CertListResult result = source.readCertificates(h.cardPath());
    EXPECT_EQ(result.status, ReadStatus::CardRemoved);
    EXPECT_TRUE(result.certs.isEmpty());
}

TEST(AgentCardDataSource, GetPhotoCardRemovedMidReadReturnsCleanlyNoUaf)
{
    Harness h(configWithSlowOps(Cap::IdentityData));
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    pullCardMidRead(h);

    const PhotoResult result = source.getPhoto(h.cardPath());
    EXPECT_EQ(result.status, ReadStatus::CardRemoved);
    EXPECT_TRUE(result.bytes.isEmpty());
}

// ============================================================================
// Happy-path field fidelity over the live FakeAgent — the post-demarshal
// translation layer (toCertView / identity-field unwrap + binary skip) and the
// sealed-memfd mmap (readSealedFd) are the ONLY production code on the real wire
// and were previously exercised by nothing but the UAF path. (Raw operator>>
// demarshalling is already covered by AgentCardTest::DemarshalRealShapedCertPayload.)
// ============================================================================

// A tiny PNG so the GetPhoto sealed memfd carries real bytes the mmap reads back.
QByteArray tinyPngBytes()
{
    QImage img(2, 2, QImage::Format_RGB32);
    img.fill(Qt::blue);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

TEST(AgentCardDataSource, ReadIdentityHappyPathTranslatesFieldsAndSkipsBinary)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    const IdentityResult result = source.readIdentity(h.cardPath());
    ASSERT_EQ(result.status, ReadStatus::Ok);
    // FakeAgent emits group "personal" / field "given_name" labelFallback "Given
    // name" type "text" value "Ana" — the adapter unwraps the QDBusVariant.
    ASSERT_EQ(result.fields.size(), 1);
    EXPECT_EQ(result.fields.first().group, QStringLiteral("personal"));
    EXPECT_EQ(result.fields.first().fieldKey, QStringLiteral("given_name"));
    EXPECT_EQ(result.fields.first().labelFallback, QStringLiteral("Given name"));
    EXPECT_EQ(result.fields.first().value, QStringLiteral("Ana"));
}

TEST(AgentCardDataSource, ReadCertificatesHappyPathRoundTripsEveryToCertViewField)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = FakeCertList{FakeCert{QStringLiteral("aabbccdd11223344"), true, QStringLiteral("Pera Peric"),
                                           QStringLiteral("MUP CA"), QStringLiteral("2030-01-01T00:00:00Z"), 0x80u,
                                           QStringList{QStringLiteral("1.3.6.1.5.5.7.3.2")},
                                           QStringList{QStringLiteral("Pera Peric"), QStringLiteral("MUP CA")}, 2u}};
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    const CertListResult result = source.readCertificates(h.cardPath());
    ASSERT_EQ(result.status, ReadStatus::Ok);
    ASSERT_EQ(result.certs.size(), 1);
    const CertInfoView& v = result.certs.first();
    EXPECT_EQ(v.certId, QStringLiteral("aabbccdd11223344"));
    EXPECT_TRUE(v.signingCapable);
    EXPECT_EQ(v.subjectCn, QStringLiteral("Pera Peric"));
    EXPECT_EQ(v.issuerCn, QStringLiteral("MUP CA"));
    EXPECT_EQ(v.notAfter, QStringLiteral("2030-01-01T00:00:00Z"));
    EXPECT_EQ(v.keyUsageBits, 0x80u);
    EXPECT_EQ(v.extendedKeyUsageOids, QStringList{QStringLiteral("1.3.6.1.5.5.7.3.2")});
    EXPECT_EQ(v.chainSubjectCns, (QStringList{QStringLiteral("Pera Peric"), QStringLiteral("MUP CA")}));
    EXPECT_EQ(v.trustStatus, 2u);
}

TEST(AgentCardDataSource, GetPhotoHappyPathReadsSealedMemfdBytes)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.photoBytes = tinyPngBytes();
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    const PhotoResult result = source.getPhoto(h.cardPath());
    ASSERT_EQ(result.status, ReadStatus::Ok);
    // The bytes must match the sealed-memfd contents exactly (exercises readSealedFd's mmap).
    EXPECT_EQ(result.bytes, cfg.photoBytes);
}

// ============================================================================
// Capability-missing: the agent returns NO Operation (failMethodEntry → method-
// entry error → op==nullptr), driving the data source's CapabilityMissing branch.
// ============================================================================
TEST(AgentCardDataSource, MethodEntryErrorYieldsCapabilityMissingNoCrash)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.failMethodEntry = true; // ReadIdentity/GetPhoto error at entry, mint no op
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    EXPECT_EQ(source.readIdentity(h.cardPath()).status, ReadStatus::CapabilityMissing);
    EXPECT_EQ(source.getPhoto(h.cardPath()).status, ReadStatus::CapabilityMissing);
}

// ============================================================================
// Lost/late typed Result: a non-Sign op finishes Ok WITHOUT its typed Result →
// finalizeTerminal → CommunicationError → ReadStatus::Error. Modeled for Photo in
// SmartCardHandler; here on the read paths through the KIO data source.
// ============================================================================
TEST(AgentCardDataSource, ReadIdentityLostResultMapsToError)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.finalStatus = 0;       // Ok terminal...
    cfg.suppressResult = true; // ...but no typed Result ever arrives
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    EXPECT_EQ(source.readIdentity(h.cardPath()).status, ReadStatus::Error);
}

TEST(AgentCardDataSource, ReadCertificatesLostResultMapsToError)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.finalStatus = 0;
    cfg.suppressResult = true;
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    EXPECT_EQ(source.readCertificates(h.cardPath()).status, ReadStatus::Error);
}

// ============================================================================
// getPhoto no-photo shapes at the data source: both an empty PhotoMap and a
// present entry whose sealed memfd reads back empty are an honest absent photo
// → NotAvailable (→ ERR_DOES_NOT_EXIST on the leaf), matching the plasmoid's
// benign-absence handling. Neither is a worker-died Error.
// ============================================================================
TEST(AgentCardDataSource, GetPhotoEmptyMapMapsToNotAvailable)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.photoEmptyMap = true; // genuinely empty a{sh}
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    EXPECT_EQ(source.getPhoto(h.cardPath()).status, ReadStatus::NotAvailable);
}

TEST(AgentCardDataSource, GetPhotoEmptyFdMapsToNotAvailable)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.photoBytes = QByteArray(); // entry present, but its memfd reads back empty
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    // an entry-present-but-empty photo is a benign absence, NOT an error —
    // matches the empty-map sibling above and the plasmoid.
    EXPECT_EQ(source.getPhoto(h.cardPath()).status, ReadStatus::NotAvailable);
}

// ============================================================================
// Certificate DER export over the agent's public Pkcs11_1.CertDer surface.
// ============================================================================

TEST(AgentCardDataSource, GetCertificateDerHappyPathReturnsBytesAddressedByReaderAndCertId)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certDerBytes = QByteArrayLiteral("\x30\x82\x01\x0a"
                                         "RAW-DER-BYTES");
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    const CertDerResult result = source.getCertificateDer(h.cardPath(), QStringLiteral("deadbeefcafe"));
    EXPECT_EQ(result.status, ReadStatus::Ok);
    EXPECT_EQ(result.der, cfg.certDerBytes);
    // CertDer is addressed by the card's Reader1 path + the requested certId.
    EXPECT_EQ(h.lastCertDerReader(), h.readerPath());
    EXPECT_EQ(h.lastCertDerCertId(), QStringLiteral("deadbeefcafe"));
}

TEST(AgentCardDataSource, GetCertificateDerKeyNotFoundMapsToNotAvailable)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certDerKeyNotFound = true; // agent answers …Error.KeyNotFound
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    const CertDerResult result = source.getCertificateDer(h.cardPath(), QStringLiteral("nope"));
    EXPECT_EQ(result.status, ReadStatus::NotAvailable); // does-not-exist, not a worker death
    EXPECT_TRUE(result.der.isEmpty());
}

// ============================================================================
// NEVER-HANG. Two blocking sites are covered here:
//   (1) DISCOVERY: the AgentClient ctor's GetManagedObjects. `ls card:/` reads
//       the in-memory registry (listReadersWithCards) that discovery populates,
//       so a wedged GetManagedObjects must be hard-bounded, not left to hang.
//   (2) The doGet operation path — see the machine-phase-stall tests below.
// ============================================================================

// Against a wedged GetManagedObjects, constructing the client and
// listing readers must return within a tight wall-clock bound. RED before the
// discovery-timeout fix (the ctor's QDBus::Block GetManagedObjects sits ~kPropTimeoutMs,
// ~3 s, exceeding the 2 s bound); GREEN after (capped at kDiscoveryTimeoutMs).
TEST(AgentCardDataSource, DiscoveryStallReturnsBoundedNotHang)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.wedgeGetManagedObjects = true; // agent never answers discovery
    Harness h(cfg);

    QElapsedTimer t;
    t.start();
    AgentClient client(h.client(), h.service()); // ctor discovery must not hang
    AgentCardDataSource source(client);
    const QList<CardPresence> readers = source.listReadersWithCards();
    const qint64 elapsed = t.elapsed();

    EXPECT_LT(elapsed, 2000) << "discovery took " << elapsed << " ms (must be hard-bounded)";
    EXPECT_TRUE(readers.isEmpty()) << "a wedged agent yields an empty registry, not a hang";
}

// A MACHINE-phase stall (op never advances, agent alive, card seated)
// maps to ReadStatus::Unavailable within the injected backstop — no hang.
TEST(AgentCardDataSource, ReadIdentityMachinePhaseStallMapsToUnavailable)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.operationDelayMs = 60000; // the op never fires on its own; no phase is announced
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client, /*opStallTimeoutMs=*/250);
    QElapsedTimer t;
    t.start();
    const IdentityResult result = source.readIdentity(h.cardPath());
    EXPECT_LT(t.elapsed(), 3000) << "a stalled read must return via the backstop, not hang";
    EXPECT_EQ(result.status, ReadStatus::Unavailable);
    EXPECT_TRUE(result.fields.isEmpty());
}

TEST(AgentCardDataSource, GetPhotoMachinePhaseStallMapsToUnavailable)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.operationDelayMs = 60000;
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client, /*opStallTimeoutMs=*/250);
    QElapsedTimer t;
    t.start();
    const PhotoResult result = source.getPhoto(h.cardPath());
    EXPECT_LT(t.elapsed(), 3000);
    EXPECT_EQ(result.status, ReadStatus::Unavailable);
    EXPECT_TRUE(result.bytes.isEmpty());
}

// The CRITICAL NUANCE — a PACE/CAN wait must NOT be aborted. The op sits
// in AwaitingConsent (a human at the prompter) for LONGER than the backstop, then
// finishes Ok. A NON-phase-aware timeout would abort it (Unavailable); the
// phase-aware backstop suppresses the timer during consent, so the read succeeds.
TEST(AgentCardDataSource, ReadIdentityConsentPhaseNotAbortedByStallBackstop)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.announceConsentPhase = true; // op announces AwaitingConsent ~50 ms in
    cfg.operationDelayMs = 900;      // "human types the CAN" — finishes Ok well past the 250 ms backstop
    Harness h(cfg);
    AgentClient client(h.client(), h.service());
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client, /*opStallTimeoutMs=*/250);
    QElapsedTimer t;
    t.start();
    const IdentityResult result = source.readIdentity(h.cardPath());
    // Finished Ok only AFTER a wall-clock that exceeds the backstop — proof the
    // AwaitingConsent phase suppressed it.
    EXPECT_GE(t.elapsed(), 800) << "op finished before the consent window — test not exercising the backstop";
    EXPECT_EQ(result.status, ReadStatus::Ok);
    ASSERT_EQ(result.fields.size(), 1);
    EXPECT_EQ(result.fields.first().value, QStringLiteral("Ana"));
}
