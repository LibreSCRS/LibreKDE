// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// AgentCardDataSource over a live FakeAgent: the P0 regression. Each
// I/O method drives an agent operation to `finished` via driveToFinished()'s
// nested QEventLoop. If the card is pulled WHILE that loop spins, the client's
// card-removal sweep terminalizes the op (quitting the loop) and then deletes
// the QObject-parented op. The post-loop op->status()/result() deref would then
// be a use-after-free. These tests pull the card mid-read (via a queued
// setCardPresent(false) the nested loop dispatches) and assert each method
// returns the CardRemoved failure cleanly, with no crash / no UAF.
//
// Runs under dbus-run-session, QT_QPA_PLATFORM=offscreen (the shared D-Bus
// harness + its QCoreApplication TestMain).
//
// Every Harness here claims the agent's well-known bus name as well as its own
// per-test one: the worker's client binds itself to that name with no hook to
// point it elsewhere, so without it the data source would see no agent at all.

#include "AgentCardDataSource.h"
#include "CardDataSource.h"
#include "TestBus.h"

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include <LibreSCRS/AgentClient/AgentCard.h>
#include <LibreSCRS/AgentClient/AgentClient.h>
#include <LibreSCRS/AgentClient/AgentOperation.h>
#include <LibreSCRS/AgentClient/Types.h>

#include <QBuffer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <gtest/gtest.h>

using namespace LibreKDE;
using namespace LibreSCRS::AgentClient::Fakes;

// The agent client library, spelled through an alias rather than pulled in
// wholesale with a using-directive. NOT a collision fix, and the measurement
// behind that has to be the discriminating one: a using-directive added while
// every name here stays `Client::`-qualified proves nothing, because ambiguity
// between using-directives is diagnosed only at UNQUALIFIED lookup. What was
// actually run is the alias deleted, the directive put in its place, and every
// `Client::` prefix stripped — this file then compiles clean, so no name here
// collides with the host's. The alias stays for readability: it keeps each name
// below visibly the LIBRARY's rather than the host's, in a file that draws value
// types from both.
namespace Client = LibreSCRS::AgentClient;

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
// loop.exec() will dispatch this singleShot, which triggers the client's
// card-removal sweep (terminate + delete op) from INSIDE the loop. The read
// method then returns from driveToFinished() with a dangling op (pre-fix) — the
// QPointer guard is what makes the post-loop path safe.
void pullCardMidRead(Harness& h)
{
    QTimer::singleShot(0, [&h]() { h.setCardPresent(false); });
}

} // namespace

TEST(AgentCardDataSource, ReadIdentityCardRemovedMidReadReturnsCleanlyNoUaf)
{
    Harness h(configWithSlowOps(Client::Cap::IdentityData), BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
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
    Harness h(configWithSlowOps(Client::Cap::Pki), BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    pullCardMidRead(h);

    const CertListResult result = source.readCertificates(h.cardPath());
    EXPECT_EQ(result.status, ReadStatus::CardRemoved);
    EXPECT_TRUE(result.certs.isEmpty());
}

TEST(AgentCardDataSource, GetPhotoCardRemovedMidReadReturnsCleanlyNoUaf)
{
    Harness h(configWithSlowOps(Client::Cap::IdentityData), BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    pullCardMidRead(h);

    const PhotoResult result = source.getPhoto(h.cardPath());
    EXPECT_EQ(result.status, ReadStatus::CardRemoved);
    EXPECT_TRUE(result.bytes.isEmpty());
}

// ============================================================================
// Happy-path field fidelity over the live FakeAgent — the post-demarshal
// translation layer (the identity label resolve + the client's own certificate
// value type) and the sealed-payload read are the ONLY production code on the
// real wire and were previously exercised by nothing but the UAF path.
// ============================================================================

// A tiny PNG so the GetPhoto sealed memfd carries real bytes the read maps back.
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
    cfg.capabilities = Client::Cap::IdentityData;
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    const IdentityResult result = source.readIdentity(h.cardPath());
    ASSERT_EQ(result.status, ReadStatus::Ok);
    // The fake emits group "personal" / field "given_name" labelKey
    // "label_given_name" labelFallback "Given name" type "text" value "Ana".
    //
    // These four expectations say nothing about ORDER, and cannot: the fixture
    // emits exactly one field, so "the first row" is the only row whichever way
    // the rows are sequenced. Nor do they need to. The row list is a sequence
    // now rather than a map, but every producer feeding it is still keyed and
    // ordered — both wires carry the identity payload as a sorted map of sorted
    // maps, and the conversion walks them in that order into the list — so rows
    // still reach the renderer sorted by group key and then by field key,
    // exactly as before. The container changed shape; the rendered order did
    // not.
    ASSERT_EQ(result.fields.size(), 1);
    EXPECT_EQ(result.fields.first().group, QStringLiteral("personal"));
    EXPECT_EQ(result.fields.first().fieldKey, QStringLiteral("given_name"));
    // The label the worker renders comes from the shared key→label resolver.
    // "label_given_name" is deliberately NOT one of the frozen keys that table
    // carries, so this pins the resolver's SECOND arm — the agent-authored
    // label — rather than its first (a translated label) or its last (the bare
    // field key, which is what would surface if labelFallback were dropped on
    // the way through the wire).
    EXPECT_EQ(result.fields.first().labelFallback, QStringLiteral("Given name"));
    EXPECT_EQ(result.fields.first().value, QStringLiteral("Ana"));
}

TEST(AgentCardDataSource, ReadCertificatesHappyPathRoundTripsEveryCertificateField)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    // Designated rather than positional: the shared double's FakeCert carries a
    // notBefore that the copy this repository used to keep did not, so a
    // positional list would have slid the validity string one member sideways
    // and still compiled if the two happened to be the same type.
    cfg.certScript =
        FakeCertList{FakeCert{.certId = QStringLiteral("aabbccdd11223344"),
                              .signingCapable = true,
                              .subjectCn = QStringLiteral("Pera Peric"),
                              .issuerCn = QStringLiteral("MUP CA"),
                              .notAfter = QStringLiteral("2030-01-01T00:00:00Z"),
                              .keyUsageBits = 0x80u,
                              .extendedKeyUsageOids = QStringList{QStringLiteral("1.3.6.1.5.5.7.3.2")},
                              .chainSubjectCns = QStringList{QStringLiteral("Pera Peric"), QStringLiteral("MUP CA")},
                              .trustStatus = 2u}};
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    const CertListResult result = source.readCertificates(h.cardPath());
    ASSERT_EQ(result.status, ReadStatus::Ok);
    ASSERT_EQ(result.certs.size(), 1);
    const Client::CertificateInfo& v = result.certs.first();
    EXPECT_EQ(v.id, QStringLiteral("aabbccdd11223344"));
    EXPECT_TRUE(v.signingCapable);
    EXPECT_EQ(v.subject, QStringLiteral("Pera Peric"));
    EXPECT_EQ(v.issuer, QStringLiteral("MUP CA"));
    // The scripted validity string is a WIRE string; the client parses it into
    // an instant. isValid() is asserted separately so a parse that produced
    // nothing cannot pass by matching an equally-unparsed expectation.
    EXPECT_TRUE(v.notAfter.isValid());
    EXPECT_EQ(v.notAfter, QDateTime::fromString(QStringLiteral("2030-01-01T00:00:00Z"), Qt::ISODate));
    EXPECT_EQ(v.keyUsageBits, 0x80u);
    EXPECT_EQ(v.extendedKeyUsageOids, QStringList{QStringLiteral("1.3.6.1.5.5.7.3.2")});
    EXPECT_EQ(v.chainSubjectCns, (QStringList{QStringLiteral("Pera Peric"), QStringLiteral("MUP CA")}));
    // The scripted verdict 2 is a wire number; the client collapses the three
    // untrusted causes onto one display value and keeps the raw number beside
    // it. Both are asserted — the collapse, and the cause it collapsed.
    EXPECT_EQ(v.trust, Client::TrustStatus::Untrusted);
    EXPECT_EQ(v.extra.value(QStringLiteral("trustStatusWire")).toUInt(), 2u);
}

TEST(AgentCardDataSource, GetPhotoHappyPathReadsSealedMemfdBytes)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.photoBytes = tinyPngBytes();
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    const PhotoResult result = source.getPhoto(h.cardPath());
    ASSERT_EQ(result.status, ReadStatus::Ok);
    // The bytes must match the sealed-memfd contents exactly (exercises the
    // sealed-payload read).
    EXPECT_EQ(result.bytes, cfg.photoBytes);
}

// A worker process lives as long as the file-manager session, so every terminal
// read must reap its own operation — a card kept in the reader all day must not
// accumulate one dead QObject under the AgentCard per identity.txt/photo/cert
// open (getCertificateDer always owned its op; the card reads reap the same way).
TEST(AgentCardDataSource, FinishedReadsLeaveNoOperationChildrenBehind)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData | Client::Cap::Pki;
    cfg.photoBytes = tinyPngBytes();
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
    Client::AgentCard* card = client.card(h.cardPath());
    ASSERT_NE(card, nullptr);

    AgentCardDataSource source(client);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(source.readIdentity(h.cardPath()).status, ReadStatus::Ok);
        EXPECT_EQ(source.readCertificates(h.cardPath()).status, ReadStatus::Ok);
        EXPECT_EQ(source.getPhoto(h.cardPath()).status, ReadStatus::Ok);
    }
    EXPECT_TRUE(card->findChildren<Client::AgentOperation*>().isEmpty())
        << "finished card reads must not pile up as AgentCard children for the card's lifetime";
}

// ============================================================================
// Capability-missing: the agent refuses at METHOD ENTRY. The client answers
// that with an operation that is already finished and carries the mapped
// CapabilityMissing failure (it never hands back a null operation), so the read
// classifies through the same outcome mapping every other failure goes through.
// ============================================================================
TEST(AgentCardDataSource, MethodEntryErrorYieldsCapabilityMissingNoCrash)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.failMethodEntry = true; // ReadIdentity/GetPhoto error at entry, mint no op
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    EXPECT_EQ(source.readIdentity(h.cardPath()).status, ReadStatus::CapabilityMissing);
    EXPECT_EQ(source.getPhoto(h.cardPath()).status, ReadStatus::CapabilityMissing);
}

// ============================================================================
// Lost/late typed Result: a non-Sign op finishes Ok WITHOUT its typed Result →
// CommunicationError → ReadStatus::Error. Modeled for Photo in
// SmartCardHandler; here on the read paths through the KIO data source.
// ============================================================================
TEST(AgentCardDataSource, ReadIdentityLostResultMapsToError)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.finalStatus = 0;       // Ok terminal...
    cfg.suppressResult = true; // ...but no typed Result ever arrives
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    EXPECT_EQ(source.readIdentity(h.cardPath()).status, ReadStatus::Error);
}

TEST(AgentCardDataSource, ReadCertificatesLostResultMapsToError)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    cfg.finalStatus = 0;
    cfg.suppressResult = true;
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
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
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.photoEmptyMap = true; // genuinely empty a{sh}
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    EXPECT_EQ(source.getPhoto(h.cardPath()).status, ReadStatus::NotAvailable);
}

TEST(AgentCardDataSource, GetPhotoEmptyFdMapsToNotAvailable)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.photoBytes = QByteArray(); // entry present, but its memfd reads back empty
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
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
    cfg.capabilities = Client::Cap::Pki;
    cfg.certDerBytes = QByteArrayLiteral("\x30\x82\x01\x0a"
                                         "RAW-DER-BYTES");
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
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
    cfg.capabilities = Client::Cap::Pki;
    cfg.certDerKeyNotFound = true; // agent answers …Error.KeyNotFound
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
    ASSERT_NE(client.card(h.cardPath()), nullptr);

    AgentCardDataSource source(client);
    const CertDerResult result = source.getCertificateDer(h.cardPath(), QStringLiteral("nope"));
    EXPECT_EQ(result.status, ReadStatus::NotAvailable); // does-not-exist, not a worker death
    EXPECT_TRUE(result.der.isEmpty());
}

// ============================================================================
// NEVER-HANG. Two blocking sites are covered here:
//   (1) DISCOVERY: the AgentClient ctor's registry snapshot. `ls card:/` reads
//       the in-memory registry (listReadersWithCards) that discovery populates,
//       so a wedged snapshot must be hard-bounded, not left to hang.
//   (2) The doGet operation path — see the machine-phase-stall tests below.
// ============================================================================

// Against a wedged discovery snapshot, constructing the client and listing
// readers must return within a tight wall-clock bound.
//
// The bound below is deliberately close to what the client actually spends: the
// registry snapshot is capped at the library's own handshake budget, so a wedged
// agent costs about that much and no more. Raising that constant past this
// ceiling turns this case red without anything having regressed — read the two
// together.
//
// The well-known mode is load-bearing here for a reason no assertion in this
// body can restate: under UniqueOnly the client would find no agent at all, the
// registry would be empty for that reason instead of the wedge, and the case
// would pass having exercised nothing. Nothing needs to re-assert the mode,
// though — a claim that genuinely FAILS (the name already owned by something
// else) is caught by the harness's own registration check, and this case's
// readers.isEmpty() below fails as well. Both were measured by taking the name
// with a second harness first; an assertion here on the mode flag fired in
// neither run, because that flag is a copy of the argument on the line above.
TEST(AgentCardDataSource, DiscoveryStallReturnsBoundedNotHang)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.wedgeGetManagedObjects = true; // agent never answers discovery
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    QElapsedTimer t;
    t.start();
    Client::AgentClient client; // ctor discovery must not hang
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
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.operationDelayMs = 60000; // the op never fires on its own; no phase is announced
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
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
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.operationDelayMs = 60000;
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
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
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.announceConsentPhase = true; // op announces AwaitingConsent ~50 ms in
    cfg.operationDelayMs = 900;      // "human types the CAN" — finishes Ok well past the 250 ms backstop
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    Client::AgentClient client;
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
