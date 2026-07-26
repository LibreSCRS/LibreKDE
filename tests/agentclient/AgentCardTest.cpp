// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// AgentCard / AgentReader typed proxies: properties reflect the fake, update on
// PropertiesChanged, and sign() mints a working AgentOperation.

#include "AgentCapabilities.h"
#include "AgentCard.h"
#include "AgentOperation.h"
#include "AgentReader.h"
#include "TestBus.h"

#include "AgentDBus.h" // LibreKDE::kPropTimeoutMs

#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <gtest/gtest.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace LibreKDE;
using namespace LibreKDETest;

namespace {
// Build a memfd holding @p bytes, rewound for reading — a stand-in for the
// document fd a real Sign caller would pass.
int makeDocumentFd(const QByteArray& bytes)
{
    int fd = memfd_create("fake-doc", 0);
    if (fd < 0) {
        return -1;
    }
    if (!bytes.isEmpty()) {
        ssize_t w = ::write(fd, bytes.constData(), static_cast<size_t>(bytes.size()));
        (void)w;
    }
    ::lseek(fd, 0, SEEK_SET);
    return fd;
}
} // namespace

TEST(AgentCard, PropertiesReflectFake)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData | Cap::Pki;
    cfg.preReadAuth = QStringLiteral("Can");
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    card.primeFrom(h.cardProps()); // production primes via AgentClient::addCard
    EXPECT_EQ(card.capabilities(), static_cast<std::uint32_t>(Cap::IdentityData | Cap::Pki));
    EXPECT_EQ(card.preReadAuthMethod(), PreReadAuth::Can);
    EXPECT_EQ(card.readerPath(), h.readerPath());
}

TEST(AgentCard, PreReadAuthAllThreeVocabularyValues)
{
    for (const auto& [wire, expected] :
         {std::pair{QStringLiteral("None"), PreReadAuth::None}, std::pair{QStringLiteral("Mrz"), PreReadAuth::Mrz},
          std::pair{QStringLiteral("Can"), PreReadAuth::Can}}) {
        FakeAgent::Config cfg;
        cfg.capabilities = Cap::IdentityData;
        cfg.preReadAuth = wire;
        Harness h(cfg);
        AgentCard card(h.client(), h.service(), h.cardPath());
        card.primeFrom(h.cardProps());
        EXPECT_EQ(card.preReadAuthMethod(), expected) << wire.toStdString();
    }
}

TEST(AgentCard, SignReturnsOperationThatFinishesOk)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.operationDelayMs = 5;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());

    int fd = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(fd, 0);
    AgentOperation* op = card.sign(QStringLiteral("certid"), QDBusUnixFileDescriptor(fd), {});
    ::close(fd);
    ASSERT_NE(op, nullptr);

    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(op->status(), OperationStatus::Ok);
    EXPECT_TRUE(op->signResult().artifact.isValid());
}

TEST(AgentCard, ReadIdentityDeliversFieldMap)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::IdentityData;
    cfg.operationDelayMs = 20;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.readIdentity();
    ASSERT_NE(op, nullptr);

    bool identitySeen = false;
    QObject::connect(op, &AgentOperation::identityResultReady, op, [&]() { identitySeen = true; });

    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(op->status(), OperationStatus::Ok);
    EXPECT_TRUE(identitySeen);
    // FakeAgent scripts one group ("personal"); the demarshaled outer map carries it.
    EXPECT_TRUE(op->identityResult().contains(QStringLiteral("personal")));
}

// a PropertiesChanged whose `changed` map carries the full new value must
// be applied directly — the cached property updates without any blocking Get.
TEST(AgentCard, PropertiesChangedAppliesFromChangedMapDirectly)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    card.primeFrom(h.cardProps());
    EXPECT_EQ(card.capabilities(), static_cast<std::uint32_t>(Cap::Pki));

    QSignalSpy changedSpy(&card, &AgentCard::changed);
    const auto next = static_cast<unsigned>(Cap::IdentityData | Cap::Pki);
    h.emitCardCapabilitiesChanged(next);

    ASSERT_TRUE(waitFor([&]() { return card.capabilities() == next; }));
    EXPECT_GE(changedSpy.count(), 1);
}

// when a property is `invalidated` (empty `changed`), the client takes its
// single-GetAll fallback and still ends up with the current value.
TEST(AgentCard, PropertiesChangedInvalidatedTakesGetAllFallback)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    card.primeFrom(h.cardProps());
    EXPECT_EQ(card.capabilities(), static_cast<std::uint32_t>(Cap::Pki));

    const auto next = static_cast<unsigned>(Cap::IdentityData | Cap::Pki);
    h.invalidateCardCapabilities(next); // server now reports `next` via GetAll

    ASSERT_TRUE(waitFor([&]() { return card.capabilities() == next; }))
        << "invalidated property should be recovered via the GetAll fallback";
}

// the client's Certificates1 operator>> must demarshal a REAL-shaped,
// foreign-marshalled (sba{sa{s(ssv)}}uasasu) payload — built by the FakeAgent
// with raw beginStructure/beginMap, NOT the client's own operator<< — carrying
// issuer + notAfter field-groups, a non-empty EKU list, a multi-entry CN chain,
// and a non-255 trustStatus. operator>> must populate subject/issuer/notAfter
// and tolerate/skip the trailing members.
TEST(AgentCard, DemarshalRealShapedCertPayload)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.operationDelayMs = 20; // result arrives after we subscribe
    cfg.rawCertResult = true;  // hand-marshalled raw signal, bypasses operator<<
    FakeCert fc;
    fc.certId = QStringLiteral("sha256-handle");
    fc.signingCapable = true;
    fc.subjectCn = QStringLiteral("Ana Anić");
    fc.issuerCn = QStringLiteral("MUP CA Građani");
    fc.notAfter = QStringLiteral("2030-12-31T23:59:59Z");
    fc.keyUsageBits = 0x80u;
    fc.extendedKeyUsageOids = QStringList{QStringLiteral("1.3.6.1.5.5.7.3.4")};
    fc.chainSubjectCns = QStringList{QStringLiteral("Ana Anić"), QStringLiteral("MUP CA Građani")};
    fc.trustStatus = 2u; // BrokenChain (non-255)
    cfg.certScript = FakeCertList{fc};
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.readCertificates();
    ASSERT_NE(op, nullptr);

    bool certsSeen = false;
    QObject::connect(op, &AgentOperation::certificatesResultReady, op, [&]() { certsSeen = true; });

    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));
    EXPECT_EQ(op->status(), OperationStatus::Ok);
    ASSERT_TRUE(certsSeen);

    const CertificateList& certs = op->certificatesResult();
    ASSERT_EQ(certs.size(), 1);
    const CertificateInfo& c = certs.constFirst();
    EXPECT_EQ(c.certId, QStringLiteral("sha256-handle"));
    EXPECT_TRUE(c.signingCapable);
    EXPECT_EQ(c.subjectCn, QStringLiteral("Ana Anić"));
    EXPECT_EQ(c.issuerCn, QStringLiteral("MUP CA Građani"));
    EXPECT_EQ(c.notAfter, QStringLiteral("2030-12-31T23:59:59Z"));

    // The trailing wire members (u keyUsageBits, as EKU, as chain, u
    // trustStatus) are now RETAINED, not discarded — the KIO worker renders them.
    EXPECT_EQ(c.keyUsageBits, 0x80u);
    EXPECT_EQ(c.extendedKeyUsageOids, (QStringList{QStringLiteral("1.3.6.1.5.5.7.3.4")}));
    EXPECT_EQ(c.chainSubjectCns, (QStringList{QStringLiteral("Ana Anić"), QStringLiteral("MUP CA Građani")}));
    EXPECT_EQ(c.trustStatus, 2u); // BrokenChain
}

// the FakeAgent's Sign must honor + expose its in-args. Assert verbatim
// certId forwarding, fd readback == the document bytes (read synchronously
// inside Sign() before the client closes its fd), and the exact options map
// with correct variant types — PAdES enveloped.
TEST(AgentCard, SignForwardsCertIdInputAndPadesOptionsVerbatim)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.operationDelayMs = 5;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());

    const QByteArray doc = QByteArrayLiteral("%PDF-1.7\nthe-document-bytes\n");
    int fd = makeDocumentFd(doc);
    ASSERT_GE(fd, 0);

    QVariantMap options;
    options.insert(QStringLiteral("format"), QStringLiteral("pades"));
    options.insert(QStringLiteral("packaging"), QStringLiteral("enveloped"));

    AgentOperation* op = card.sign(QStringLiteral("cert-handle-42"), QDBusUnixFileDescriptor(fd), options);
    ::close(fd);
    ASSERT_NE(op, nullptr);
    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));

    EXPECT_EQ(h.lastSignCertId(), QStringLiteral("cert-handle-42"));
    EXPECT_EQ(h.lastSignInputBytes(), doc);

    const QVariantMap seen = h.lastSignOptions();
    ASSERT_TRUE(seen.value(QStringLiteral("format")).metaType() == QMetaType::fromType<QString>());
    EXPECT_EQ(seen.value(QStringLiteral("format")).toString(), QStringLiteral("pades"));
    EXPECT_EQ(seen.value(QStringLiteral("packaging")).toString(), QStringLiteral("enveloped"));
}

// Second shape: CAdES detached, to prove the options map is not hard-coded.
TEST(AgentCard, SignForwardsCadesDetachedOptions)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.operationDelayMs = 5;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());

    const QByteArray doc = QByteArrayLiteral("arbitrary-bytes-for-cades");
    int fd = makeDocumentFd(doc);
    ASSERT_GE(fd, 0);

    QVariantMap options;
    options.insert(QStringLiteral("format"), QStringLiteral("cades"));
    options.insert(QStringLiteral("packaging"), QStringLiteral("detached"));

    AgentOperation* op = card.sign(QStringLiteral("cid"), QDBusUnixFileDescriptor(fd), options);
    ::close(fd);
    ASSERT_NE(op, nullptr);
    ASSERT_TRUE(waitFor([&]() { return op->isFinished(); }));

    EXPECT_EQ(h.lastSignInputBytes(), doc);
    const QVariantMap seen = h.lastSignOptions();
    EXPECT_EQ(seen.value(QStringLiteral("format")).toString(), QStringLiteral("cades"));
    EXPECT_EQ(seen.value(QStringLiteral("packaging")).toString(), QStringLiteral("detached"));
}

// a method-entry error (no Operation minted) must surface as nullptr from
// sign()/readCertificates() — the client's CapabilityMissing path.
TEST(AgentCard, MethodEntryErrorYieldsNullptr)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.failMethodEntry = true;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());

    int fd = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(fd, 0);
    AgentOperation* signOp = card.sign(QStringLiteral("cid"), QDBusUnixFileDescriptor(fd), {});
    ::close(fd);
    EXPECT_EQ(signOp, nullptr) << "Sign that errors at entry mints no Operation; client returns nullptr";

    AgentOperation* certOp = card.readCertificates();
    EXPECT_EQ(certOp, nullptr) << "ReadCertificates that errors at entry mints no Operation; client returns nullptr";
}

// AgentCard/AgentReader construction must NOT issue any blocking property
// call. With the agent's Properties.GetAll wedged (never answers), constructing
// a proxy pointed at the wedged path must still return promptly — the ctor no
// longer introspects (the QDBusInterface trap is gone, the redundant ctor
// refreshAll() is dropped; primeFrom carries the props from discovery).
TEST(AgentCard, ConstructionDoesNotBlockOnWedgedProperties)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    Harness h(cfg);

    const QString wedged = h.wedgedPropertiesPath();

    QElapsedTimer t;
    t.start();
    AgentCard card(h.client(), h.service(), wedged);
    AgentReader reader(h.client(), h.service(), wedged);
    const qint64 elapsed = t.elapsed();

    // No blocking GetAll in either ctor → far under the cap (no round-trip at all).
    EXPECT_LT(elapsed, kPropTimeoutMs) << "ctor took " << elapsed << "ms — it must not block on a wedged GetAll";
    // primeFrom is what carries the real props now; the ctor leaves defaults.
    EXPECT_EQ(card.capabilities(), 0u);
    card.primeFrom(QVariantMap{{QStringLiteral("Capabilities"), static_cast<uint>(Cap::Pki)}});
    EXPECT_EQ(card.capabilities(), static_cast<std::uint32_t>(Cap::Pki));
}

// the invalidated-fallback GetAll is ASYNC: with the agent's Properties wedged
// (never answers), the PropertiesChanged slot must return — and emit changed()
// — near-immediately, far below the cap, and NO state update may land while
// the GetAll hangs. Once the wedge is lifted (a scripted reply), a later
// invalidation's refresh must fetch and apply the current value.
TEST(AgentCard, InvalidatedFallbackDoesNotBlockAndRecoversOnceUnwedged)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    Harness h(cfg);

    const QString wedged = h.wedgedPropertiesPath();
    AgentCard card(h.client(), h.service(), wedged);

    bool changedSeen = false;
    QObject::connect(&card, &AgentCard::changed, &card, [&]() { changedSeen = true; });

    QElapsedTimer t;
    t.start();
    h.emitWedgedCardInvalidated();
    ASSERT_TRUE(waitFor([&]() { return changedSeen; }))
        << "onPropertiesChanged(invalidated) must emit changed() without waiting on the GetAll";
    const qint64 elapsed = t.elapsed();
    EXPECT_LT(elapsed, 1000) << "slot took " << elapsed << "ms — it must not block on the wedged GetAll at all";
    EXPECT_EQ(card.capabilities(), 0u) << "no state may land while the wedged GetAll hangs";

    // Unwedge: script an immediate reply carrying the current value, then drive
    // another invalidation — the (new) async refresh must apply it. The first,
    // still-wedged GetAll is superseded and its eventual timeout is discarded.
    const auto next = static_cast<unsigned>(Cap::IdentityData | Cap::Pki);
    h.scriptWedgedGetAll(0, QVariantMap{{QStringLiteral("Capabilities"), next}});
    h.emitWedgedCardInvalidated();
    ASSERT_TRUE(waitFor([&]() { return card.capabilities() == next; }))
        << "the post-unwedge refresh must fetch and apply the value";
}

// a SLOW (not hung) agent: GetAll answers only after a delay. While the refresh
// is pending the proxy's event loop must stay responsive (a 0-ms timer fires
// within a tight bound), the delayed value must eventually land, and every
// changed() — the slot burst and the async apply alike — must be delivered on
// the proxy's own thread (the watcher completes via its owner's event loop).
TEST(AgentCard, SlowGetAllKeepsEventLoopResponsiveThenApplies)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    Harness h(cfg);

    const QString wedged = h.wedgedPropertiesPath();
    const auto next = static_cast<unsigned>(Cap::IdentityData | Cap::Pki);
    h.scriptWedgedGetAll(400, QVariantMap{{QStringLiteral("Capabilities"), next}});

    AgentCard card(h.client(), h.service(), wedged);

    int changedCount = 0;
    QThread* lastDeliveryThread = nullptr;
    QObject::connect(&card, &AgentCard::changed, &card, [&]() {
        ++changedCount;
        lastDeliveryThread = QThread::currentThread();
    });

    h.emitWedgedCardInvalidated();
    ASSERT_TRUE(waitFor([&]() { return changedCount >= 1; })) << "slot must run (and start the refresh) promptly";
    EXPECT_EQ(card.capabilities(), 0u) << "the delayed GetAll reply must not have landed yet";
    EXPECT_EQ(lastDeliveryThread, QThread::currentThread());

    // Responsiveness probe while the refresh is in flight: a 0-ms timer must
    // fire promptly — the loop is NOT being held by the pending GetAll.
    bool timerFired = false;
    qint64 firedAfterMs = -1;
    QElapsedTimer t;
    t.start();
    QTimer::singleShot(0, [&]() {
        timerFired = true;
        firedAfterMs = t.elapsed();
    });
    ASSERT_TRUE(waitFor([&]() { return timerFired; }, 1000));
    EXPECT_LT(firedAfterMs, 200) << "event loop stalled " << firedAfterMs << "ms while the refresh was pending";

    ASSERT_TRUE(waitFor([&]() { return card.capabilities() == next; }, kPropTimeoutMs + 2000))
        << "the slow agent's delayed reply must eventually apply";
    EXPECT_GE(changedCount, 2) << "the async apply must emit changed() again";
    EXPECT_EQ(lastDeliveryThread, QThread::currentThread())
        << "the watcher's apply must be delivered on the proxy's own thread";
}

// stale-reply ordering: a GetAll still in flight (older snapshot) must never
// clobber a newer direct `changed`-map apply that lands while it is pending.
// Discarding a raced refresh also RE-ISSUES it (the invalidated property that
// motivated the fetch has not converged), so a coherent agent's GetAll — which
// serves the post-change state — is re-scripted to the fresh value; the
// recovery fetch must converge there, and the stale snapshot must never show.
TEST(AgentCard, StaleGetAllReplyDoesNotClobberNewerDirectApply)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    Harness h(cfg);

    const QString wedged = h.wedgedPropertiesPath();
    const auto stale = static_cast<unsigned>(Cap::Pki);
    const auto fresh = static_cast<unsigned>(Cap::IdentityData | Cap::Pki);
    h.scriptWedgedGetAll(400, QVariantMap{{QStringLiteral("Capabilities"), stale}});

    AgentCard card(h.client(), h.service(), wedged);

    int changedCount = 0;
    QObject::connect(&card, &AgentCard::changed, &card, [&]() { ++changedCount; });

    h.emitWedgedCardInvalidated(); // starts the slow (older-snapshot) GetAll
    ASSERT_TRUE(waitFor([&]() { return changedCount >= 1; }));

    h.emitWedgedCardPropsChanged(QVariantMap{{QStringLiteral("Capabilities"), fresh}}); // newer, applied directly
    ASSERT_TRUE(waitFor([&]() { return card.capabilities() == fresh; }));
    // From here a coherent agent's GetAll serves the post-change snapshot; the
    // in-flight call already captured the stale script at arrival time.
    h.scriptWedgedGetAll(100, QVariantMap{{QStringLiteral("Capabilities"), fresh}});

    // The stale reply lands (~400 ms), is discarded, and the re-issued recovery
    // fetch applies the fresh snapshot — the third changed() emission.
    ASSERT_TRUE(waitFor([&]() { return changedCount >= 3; }, kPropTimeoutMs + 2000))
        << "the discarded raced refresh must re-issue a GetAll that applies";
    EXPECT_EQ(card.capabilities(), fresh) << "an older GetAll reply clobbered a newer direct apply";

    // Settle a little longer and assert no late flip back to the stale value.
    QDeadlineTimer settle(300);
    while (!settle.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    EXPECT_EQ(card.capabilities(), fresh) << "the stale snapshot must never be applied";
}

// pin the raced-refresh recovery exactly: invalidated(P=Capabilities) starts a
// slow GetAll; while it is in flight a direct changed-map for a DIFFERENT
// property (Q=PreReadAuthMethod) bumps the generation, so the old reply — a
// pre-Q snapshot that would revert Q — must be discarded; the client must then
// RE-ISSUE the fetch (assert via the server-side GetAll call count) so P still
// converges to its post-invalidation value.
TEST(AgentCard, DirectApplyRacingRefreshReissuesGetAllAndConverges)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.preReadAuth = QStringLiteral("None");
    Harness h(cfg);

    const QString wedged = h.wedgedPropertiesPath();
    const auto newCaps = static_cast<unsigned>(Cap::IdentityData | Cap::Pki);
    // Coherent snapshot at invalidation time: P already new, Q still old.
    h.scriptWedgedGetAll(400, QVariantMap{{QStringLiteral("Capabilities"), newCaps},
                                          {QStringLiteral("PreReadAuthMethod"), QStringLiteral("None")}});

    AgentCard card(h.client(), h.service(), wedged);

    int changedCount = 0;
    QObject::connect(&card, &AgentCard::changed, &card, [&]() { ++changedCount; });

    h.emitWedgedCardInvalidated(); // invalidates P; slow GetAll in flight
    ASSERT_TRUE(waitFor([&]() { return changedCount >= 1; }));
    EXPECT_EQ(card.capabilities(), 0u) << "P must still be unconverged while the GetAll is pending";
    ASSERT_TRUE(waitFor([&]() { return h.wedgedGetAllCallCount() >= 1; })) << "the first GetAll must reach the agent";

    // Q changes and arrives as a direct full-value apply (no new invalidation).
    h.emitWedgedCardPropsChanged(QVariantMap{{QStringLiteral("PreReadAuthMethod"), QStringLiteral("Can")}});
    ASSERT_TRUE(waitFor([&]() { return card.preReadAuthMethod() == PreReadAuth::Can; }));
    // A coherent agent's GetAll now serves the post-Q state; the in-flight call
    // already captured the pre-Q script at arrival time.
    h.scriptWedgedGetAll(100, QVariantMap{{QStringLiteral("Capabilities"), newCaps},
                                          {QStringLiteral("PreReadAuthMethod"), QStringLiteral("Can")}});

    // The pre-Q reply lands, is discarded (it would revert Q to None), and the
    // refresh is re-issued: P converges, Q never flickers, and the server saw a
    // second GetAll.
    ASSERT_TRUE(waitFor([&]() { return card.capabilities() == newCaps; }, kPropTimeoutMs + 2000))
        << "the invalidated property must converge via the re-issued GetAll";
    EXPECT_EQ(card.preReadAuthMethod(), PreReadAuth::Can)
        << "the discarded pre-Q snapshot must not have reverted the newer direct apply";
    EXPECT_GE(h.wedgedGetAllCallCount(), 2) << "the discard must re-issue a GetAll";
}

// AgentReader mirrors AgentCard's async invalidated-fallback: with Properties
// wedged the slot must not block, and once a reply is scripted a later
// invalidation refresh applies the fetched value.
TEST(AgentReader, InvalidatedFallbackDoesNotBlockAndRecoversOnceUnwedged)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    Harness h(cfg);

    const QString wedged = h.wedgedPropertiesPath();
    AgentReader reader(h.client(), h.service(), wedged);

    bool changedSeen = false;
    QObject::connect(&reader, &AgentReader::changed, &reader, [&]() { changedSeen = true; });

    QElapsedTimer t;
    t.start();
    h.emitWedgedReaderInvalidated();
    ASSERT_TRUE(waitFor([&]() { return changedSeen; }));
    EXPECT_LT(t.elapsed(), 1000) << "reader slot must not block on the wedged GetAll";
    EXPECT_TRUE(reader.name().isEmpty()) << "no state may land while the wedged GetAll hangs";

    h.scriptWedgedGetAll(0, QVariantMap{{QStringLiteral("Name"), QStringLiteral("Unwedged")}});
    h.emitWedgedReaderInvalidated();
    ASSERT_TRUE(waitFor([&]() { return reader.name() == QStringLiteral("Unwedged"); }))
        << "the post-unwedge refresh must fetch and apply the value";
}

TEST(AgentReader, TracksCardPath)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    Harness h(cfg);

    AgentReader reader(h.client(), h.service(), h.readerPath());
    reader.primeFrom(h.readerProps()); // production primes via AgentClient::addReader
    EXPECT_EQ(reader.name(), QStringLiteral("Fake"));
    EXPECT_TRUE(reader.hasCard());
    EXPECT_EQ(reader.cardPath(), h.cardPath());
}

// listCredentials mints an Operation the same way readIdentity/readCertificates
// do: the entry succeeds, a non-null AgentOperation comes back, and
// lastCredentialError() stays empty (no entry throw). The op's typed
// Operation.Credentials1 result is AgentOperationTest's job — not asserted here.
TEST(AgentCard, ListCredentialsMintsOperation)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.listCredentials();
    EXPECT_NE(op, nullptr);
    EXPECT_TRUE(card.lastCredentialError().isEmpty());
}

// The only Credentials1 mint method that had no AgentCard-level mint test: a
// wire-method-name typo in startCredentialOp("ActivateSigningKey", …) must not
// ship silently. Id-less on the wire; gated on the capability bit alone.
TEST(AgentCard, ActivateSigningKeyMintsOperation)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement;
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.activateSigningKey();
    EXPECT_NE(op, nullptr);
    EXPECT_TRUE(card.lastCredentialError().isEmpty());
}

// The agent's Credentials1 capability entry gate (Credentials1.xml): without
// Card1 bit 3 (PinManagement) ALL THREE methods throw UnsupportedOnThisCard at
// entry and mint NO Operation. The fake enforces this, so a regression that
// drops the client-side gate cannot pass the suite while throwing against the
// real agent.
TEST(AgentCard, CredentialsMethodsRequirePinManagementCapability)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki | Cap::IdentityData; // NO PinManagement
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    const QString unsupported = QStringLiteral("org.librescrs.Agent.Error.UnsupportedOnThisCard");

    EXPECT_EQ(card.listCredentials(), nullptr) << "ListCredentials must entry-throw without the capability bit";
    EXPECT_EQ(card.lastCredentialError(), unsupported);
    EXPECT_EQ(card.managePin(QStringLiteral("user:0x86"), QStringLiteral("change"), {}), nullptr);
    EXPECT_EQ(card.lastCredentialError(), unsupported);
    EXPECT_EQ(card.activateSigningKey(), nullptr);
    EXPECT_EQ(card.lastCredentialError(), unsupported);
}

// List-before-mutate is a REAL agent-side contract, not a
// client courtesy: an id-bearing ManagePin without a current listing is refused
// UnknownCredential, and any mutation drops the agent's listing cache — so a
// second mutate without the mandatory re-list is refused too. The fake enforces
// both halves so a client that skips the re-list fails the suite.
TEST(AgentCard, ManagePinRequiresCurrentListingAndMutationDropsIt)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement;
    // The listing must carry the mutated id — the fake also resolves pinIds
    // against the current listing snapshot.
    cfg.credRecords = {QVariantMap{{QStringLiteral("id"), QStringLiteral("user:0x86")},
                                   {QStringLiteral("kind"), QStringLiteral("user")},
                                   {QStringLiteral("state"), QStringLiteral("operational")}}};
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    const QString unknown = QStringLiteral("org.librescrs.Agent.Error.UnknownCredential");

    // Never listed on this card -> the id cannot be from a current listing.
    EXPECT_EQ(card.managePin(QStringLiteral("user:0x86"), QStringLiteral("change"), {}), nullptr)
        << "a mutate before any list must be refused";
    EXPECT_EQ(card.lastCredentialError(), unknown);

    // List, then mutate: mints.
    ASSERT_NE(card.listCredentials(), nullptr);
    AgentOperation* op = card.managePin(QStringLiteral("user:0x86"), QStringLiteral("change"), {});
    ASSERT_NE(op, nullptr);
    EXPECT_TRUE(card.lastCredentialError().isEmpty());

    // The mutation invalidated the listing cache: a stale-id reuse without the
    // mandatory re-list is refused until a fresh ListCredentials.
    EXPECT_EQ(card.managePin(QStringLiteral("user:0x86"), QStringLiteral("change"), {}), nullptr)
        << "a mutation must drop the listing cache (mandatory re-list before the next mutate)";
    EXPECT_EQ(card.lastCredentialError(), unknown);
    ASSERT_NE(card.listCredentials(), nullptr);
    EXPECT_NE(card.managePin(QStringLiteral("user:0x86"), QStringLiteral("change"), {}), nullptr)
        << "a fresh list restores mutability";
}

// The fake enforces the agent's REQUEST-side wire vocabulary, so a client
// regression in verb spelling or the options shape fails the suite instead of
// passing against a permissive double: a verb outside {change, unblock,
// activate_pin} is refused InvalidRequest; options outside the closed key set
// {activateKey} are refused InvalidRequest; activateKey mistyped, or attached
// to any verb but activate_pin, is refused InvalidRequest. A vocabulary
// refusal never reaches the card, so it must NOT drop the listing cache — a
// well-formed request afterwards still mints.
TEST(AgentCard, ManagePinEnforcesVerbAndOptionsVocabulary)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement;
    cfg.credRecords = {QVariantMap{{QStringLiteral("id"), QStringLiteral("user:0x86")},
                                   {QStringLiteral("kind"), QStringLiteral("user")},
                                   {QStringLiteral("state"), QStringLiteral("transport")}}};
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    const QString invalid = QStringLiteral("org.librescrs.Agent.Error.InvalidRequest");

    ASSERT_NE(card.listCredentials(), nullptr);

    // Unknown verb.
    EXPECT_EQ(card.managePin(QStringLiteral("user:0x86"), QStringLiteral("reset"), {}), nullptr);
    EXPECT_EQ(card.lastCredentialError(), invalid);
    // Unknown option key.
    EXPECT_EQ(card.managePin(QStringLiteral("user:0x86"), QStringLiteral("change"),
                             QVariantMap{{QStringLiteral("frobnicate"), true}}),
              nullptr);
    EXPECT_EQ(card.lastCredentialError(), invalid);
    // activateKey is legal ONLY with activate_pin.
    EXPECT_EQ(card.managePin(QStringLiteral("user:0x86"), QStringLiteral("change"),
                             QVariantMap{{QStringLiteral("activateKey"), true}}),
              nullptr);
    EXPECT_EQ(card.lastCredentialError(), invalid);
    // activateKey mistyped (a string, not a bool).
    EXPECT_EQ(card.managePin(QStringLiteral("user:0x86"), QStringLiteral("activate_pin"),
                             QVariantMap{{QStringLiteral("activateKey"), QStringLiteral("yes")}}),
              nullptr);
    EXPECT_EQ(card.lastCredentialError(), invalid);

    // The refusals never reached the card, so the listing survives: a
    // well-formed activate_pin with a typed activateKey still mints.
    AgentOperation* op = card.managePin(QStringLiteral("user:0x86"), QStringLiteral("activate_pin"),
                                        QVariantMap{{QStringLiteral("activateKey"), true}});
    EXPECT_NE(op, nullptr);
    EXPECT_TRUE(card.lastCredentialError().isEmpty());
}

// An id absent from the CURRENT listing snapshot is refused UnknownCredential —
// the agent resolves ids only against what it last listed, so a stale/foreign
// id from a client-side cache bug fails the suite. The refusal itself does not
// drop the listing (nothing reached the card).
TEST(AgentCard, ManagePinRefusesIdOutsideCurrentListing)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement;
    cfg.credRecords = {QVariantMap{{QStringLiteral("id"), QStringLiteral("user:0x86")},
                                   {QStringLiteral("kind"), QStringLiteral("user")},
                                   {QStringLiteral("state"), QStringLiteral("operational")}}};
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    ASSERT_NE(card.listCredentials(), nullptr);

    EXPECT_EQ(card.managePin(QStringLiteral("user:0x99"), QStringLiteral("change"), {}), nullptr)
        << "an id absent from the current listing cannot be mutated";
    EXPECT_EQ(card.lastCredentialError(), QStringLiteral("org.librescrs.Agent.Error.UnknownCredential"));

    // The listed id still mints against the surviving listing.
    EXPECT_NE(card.managePin(QStringLiteral("user:0x86"), QStringLiteral("change"), {}), nullptr);
    EXPECT_TRUE(card.lastCredentialError().isEmpty());
}

// a Credentials1 method-entry error (no Operation minted) must surface as
// nullptr from managePin(), with the D-Bus error name captured by
// lastCredentialError() — the window branches on UnknownCredential/RateLimited/
// etc. to render the right guidance.
TEST(AgentCard, ManagePinEntryErrorSurfacesName)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement;
    cfg.credEntryError = true;
    cfg.credEntryErrorName = QStringLiteral("org.librescrs.Agent.Error.UnknownCredential");
    Harness h(cfg);

    AgentCard card(h.client(), h.service(), h.cardPath());
    AgentOperation* op = card.managePin(QStringLiteral("user:stale"), QStringLiteral("change"), {});
    EXPECT_EQ(op, nullptr) << "ManagePin that errors at entry mints no Operation; client returns nullptr";
    EXPECT_EQ(card.lastCredentialError(), QStringLiteral("org.librescrs.Agent.Error.UnknownCredential"));
}
