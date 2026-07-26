// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Drives the agent-side SignJob end-to-end against the FakeAgent on a private
// dbus-run-session bus (headless, offscreen). Covers: happy path writes the
// right output name; non-Ok surfaces ErrorText; the one/multi/zero signing-cert
// selection paths with an injected chooser; overwrite-prompt seam.

#include "AgentCapabilities.h"
#include "AgentCard.h"
#include "AgentClient.h"
#include "FakeAgent.h"
#include "MimeFormatMap.h"
#include "SignJob.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

using namespace LibreKDE;
using LibreKDETest::FakeAgent;

namespace {

constexpr int kSpinMs = 4000;

QString uniqueService()
{
    static int n = 0;
    return QStringLiteral("org.librescrs.AgentSignJobFake%1").arg(n++);
}

// Spin the event loop until `pred()` or timeout.
template <typename Pred>
bool spinUntil(Pred pred, int timeoutMs = kSpinMs)
{
    QElapsedTimer t;
    t.start();
    while (!pred() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return pred();
}

// Bring up a FakeAgent + AgentClient + the active AgentCard on a private bus.
struct Harness
{
    QDBusConnection bus;
    std::unique_ptr<FakeAgent> agent;
    std::unique_ptr<AgentClient> client;
    AgentCard* card = nullptr;

    explicit Harness(FakeAgent::Config cfg) : bus(QDBusConnection::sessionBus())
    {
        cfg.service = uniqueService();
        agent = std::make_unique<FakeAgent>(bus, cfg);
        EXPECT_TRUE(bus.registerService(cfg.service));
        client = std::make_unique<AgentClient>(bus, cfg.service);
        spinUntil([this] { return !client->readers().isEmpty(); });
        for (auto* r : client->readers()) {
            if (r->hasCard() && !r->cardPath().isEmpty()) {
                card = client->card(r->cardPath());
            }
        }
    }
    ~Harness()
    {
        client.reset();
        agent.reset();
        bus.unregisterService(bus.baseService());
    }
};

CertChooser pickFirst()
{
    return [](const CertificateList& cands) -> std::optional<QString> {
        return cands.isEmpty() ? std::nullopt : std::optional<QString>(cands.first().certId);
    };
}

CertChooser cancelChooser()
{
    return [](const CertificateList&) -> std::optional<QString> { return std::nullopt; };
}

OverwriteConfirmer alwaysOverwrite()
{
    return [](const QString&) { return true; };
}

OverwriteConfirmer neverOverwrite()
{
    return [](const QString&) { return false; };
}

} // namespace

// --- happy path: PDF → report-signed.pdf written next to the input ----------

TEST(SignJob, HappyPathWritesEnvelopedOutput)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana Anić")}};
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("report.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), pickFirst(), alwaysOverwrite());
    QSignalSpy ok(&job, &SignJob::succeeded);
    QSignalSpy bad(&job, &SignJob::failed);
    job.start();

    ASSERT_TRUE(spinUntil([&] { return ok.count() + bad.count() > 0; }));
    EXPECT_EQ(bad.count(), 0);
    ASSERT_EQ(ok.count(), 1);

    const QString out = dir.filePath(QStringLiteral("report-signed.pdf"));
    EXPECT_EQ(job.outputPath(), out);
    EXPECT_TRUE(QFile::exists(out));

    QFile of(out);
    ASSERT_TRUE(of.open(QIODevice::ReadOnly));
    EXPECT_EQ(of.readAll(), QByteArray("FAKE-SIGNED-ARTIFACT"));
}

// --- phase relay: the active op's phaseChanged reaches SignJob::phaseChanged --

TEST(SignJob, RelaysPhaseChangedFromActiveOperation)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana")}};
    cfg.announceConsentPhase = true; // every FakeOperation emits AwaitingConsent(2) at 50 ms
    cfg.operationDelayMs = 120;      // complete AFTER the 50 ms phase so it is observable
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("doc.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), pickFirst(), alwaysOverwrite());
    QSignalSpy phaseSpy(&job, &SignJob::phaseChanged);
    job.start();

    // The active operation's AwaitingConsent(2) must be relayed through
    // SignJob::phaseChanged (proves the certOp/signOp relay wiring).
    auto sawConsent = [&] {
        for (const auto& args : phaseSpy) {
            if (args.at(0).value<OperationPhase>() == OperationPhase::AwaitingConsent) {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(spinUntil(sawConsent));
}

// --- detached CAdES → name.bin.p7s ------------------------------------------

TEST(SignJob, CadesDetachedWritesP7sSidecar)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana")}};
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("data.bin"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("\x01\x02\x03", 3);
    f.close();

    SignJob job(h.card, input, QStringLiteral("application/octet-stream"), QString(), pickFirst(), alwaysOverwrite());
    QSignalSpy ok(&job, &SignJob::succeeded);
    job.start();
    ASSERT_TRUE(spinUntil([&] { return ok.count() > 0; }));
    EXPECT_EQ(job.outputPath(), dir.filePath(QStringLiteral("data.bin.p7s")));
    EXPECT_TRUE(QFile::exists(dir.filePath(QStringLiteral("data.bin.p7s"))));
}

// --- non-Ok finish surfaces ErrorText, writes nothing -----------------------

TEST(SignJob, SignFailureSurfacesErrorText)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana")}};
    cfg.finalStatus = 2;    // Error
    cfg.finalErrorCode = 2; // CredentialWrong
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("report.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), pickFirst(), alwaysOverwrite());
    QSignalSpy ok(&job, &SignJob::succeeded);
    QSignalSpy bad(&job, &SignJob::failed);
    job.start();

    ASSERT_TRUE(spinUntil([&] { return ok.count() + bad.count() > 0; }));
    EXPECT_EQ(ok.count(), 0);
    ASSERT_EQ(bad.count(), 1);
    EXPECT_FALSE(bad.at(0).at(0).toString().isEmpty());
    EXPECT_FALSE(QFile::exists(dir.filePath(QStringLiteral("report-signed.pdf"))));
}

// --- the agent's specific cause reaches the UI ---------------------

TEST(SignJob, SurfacesAgentMessageForGenericEngineError)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana")}};
    cfg.finalStatus = 2;     // Error
    cfg.finalErrorCode = 16; // SigningEngineError (the generic catch-all)
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("doc.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), pickFirst(), alwaysOverwrite());
    QSignalSpy bad(&job, &SignJob::failed);
    job.start();
    ASSERT_TRUE(spinUntil([&] { return bad.count() > 0; }));
    // The agent's specific message ("agent fallback") is surfaced, NOT the
    // hardcoded generic — the ErrorText generic engine code defers to it.
    EXPECT_EQ(bad.at(0).at(0).toString(), QStringLiteral("agent fallback"));
}

TEST(SignJob, SurfacesConfigStringForEngineUnavailable)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana")}};
    cfg.finalStatus = 2;     // Error
    cfg.finalErrorCode = 18; // EngineUnavailable (module/engine could not load)
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("doc.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), pickFirst(), alwaysOverwrite());
    QSignalSpy bad(&job, &SignJob::failed);
    job.start();
    ASSERT_TRUE(spinUntil([&] { return bad.count() > 0; }));
    // EngineUnavailable is a localized client code — it yields its own
    // config-specific message and IGNORES the agent's English fallback.
    const QString msg = bad.at(0).at(0).toString();
    EXPECT_FALSE(msg.isEmpty());
    EXPECT_NE(msg, QStringLiteral("agent fallback"));
}

TEST(SignJob, SurfacesLocalizedStringForInvalidDocument)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    // Without a cert the job aborts at the chooser BEFORE the sign op, so
    // finalErrorCode would never be reached and the test would pass for the
    // wrong reason. Give it one cert exactly as the EngineUnavailable test does.
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana")}};
    cfg.finalStatus = 2;     // Error
    cfg.finalErrorCode = 19; // InvalidDocument (client input is invalid/unreadable)
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("doc.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), pickFirst(), alwaysOverwrite());
    QSignalSpy bad(&job, &SignJob::failed);
    job.start();
    ASSERT_TRUE(spinUntil([&] { return bad.count() > 0; }));
    // InvalidDocument is a localized client code — it yields its own copy and
    // IGNORES the agent's English fallback; assert that precisely (stronger than
    // !isEmpty(), which would pass even if the fallback leaked through).
    const QString msg = bad.at(0).at(0).toString();
    EXPECT_FALSE(msg.isEmpty());
    EXPECT_NE(msg, QStringLiteral("agent fallback"));
}

// --- multiple signing certs: the chooser decides ---------------------------

TEST(SignJob, MultipleCertsUsesChooser)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana — sign")},
                      {QStringLiteral("cert-B"), true, QStringLiteral("Ana — auth")}};
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("doc.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    QString sawCount;
    CertChooser chooser = [&](const CertificateList& cands) -> std::optional<QString> {
        sawCount = QString::number(cands.size());
        return QStringLiteral("cert-B");
    };

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), chooser, alwaysOverwrite());
    QSignalSpy ok(&job, &SignJob::succeeded);
    job.start();
    ASSERT_TRUE(spinUntil([&] { return ok.count() > 0; }));
    EXPECT_EQ(sawCount, QStringLiteral("2"));
    EXPECT_TRUE(QFile::exists(dir.filePath(QStringLiteral("doc-signed.pdf"))));
}

// --- chooser cancellation aborts cleanly ------------------------------------

TEST(SignJob, ChooserCancelAborts)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana — sign")},
                      {QStringLiteral("cert-B"), true, QStringLiteral("Ana — auth")}};
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("doc.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), cancelChooser(), alwaysOverwrite());
    QSignalSpy ok(&job, &SignJob::succeeded);
    QSignalSpy bad(&job, &SignJob::failed);
    job.start();
    ASSERT_TRUE(spinUntil([&] { return ok.count() + bad.count() > 0; }));
    EXPECT_EQ(ok.count(), 0);
    EXPECT_EQ(bad.count(), 1); // user-cancelled is surfaced as a failure message
    EXPECT_FALSE(QFile::exists(dir.filePath(QStringLiteral("doc-signed.pdf"))));
}

// --- zero signing-capable certs → CapabilityMissing -------------------------

TEST(SignJob, NoSigningCertSurfacesCapabilityMissing)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    // A cert exists but it is NOT signing-capable (e.g. an auth-only key).
    cfg.certScript = {{QStringLiteral("cert-auth"), false, QStringLiteral("Ana — auth")}};
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("doc.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), pickFirst(), alwaysOverwrite());
    QSignalSpy ok(&job, &SignJob::succeeded);
    QSignalSpy bad(&job, &SignJob::failed);
    job.start();
    ASSERT_TRUE(spinUntil([&] { return ok.count() + bad.count() > 0; }));
    EXPECT_EQ(ok.count(), 0);
    ASSERT_EQ(bad.count(), 1);
    // The CapabilityMissing copy ("does not support signing") is what surfaces.
    EXPECT_TRUE(bad.at(0).at(0).toString().contains(QStringLiteral("sign"), Qt::CaseInsensitive));
}

// --- card pulled while the chooser is pending: fail cleanly, write nothing ---
//
// makeCertChooser() spins a modal nested event loop in production. We model that
// with an injected chooser that, while "open", pulls the card (the agent emits
// InterfacesRemoved) and spins the loop so AgentClient deletes the AgentCard —
// then returns a chosen cert. The queued beginSign() must observe the now-null
// QPointer and fail cleanly instead of dereferencing the freed card.
TEST(SignJob, CardRemovedDuringChooserFailsCleanly)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana — sign")},
                      {QStringLiteral("cert-B"), true, QStringLiteral("Ana — auth")}};
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("doc.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    // The chooser stands in for a modal dialog: pull the card and pump the loop
    // until AgentClient has dropped the AgentCard, then "choose" a cert.
    CertChooser pullThenChoose = [&](const CertificateList& cands) -> std::optional<QString> {
        h.agent->setCardPresent(false);
        spinUntil([&] {
            for (auto* r : h.client->readers()) {
                if (r->hasCard()) {
                    return false;
                }
            }
            return true;
        });
        return cands.isEmpty() ? std::nullopt : std::optional<QString>(cands.first().certId);
    };

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), pullThenChoose, alwaysOverwrite());
    QSignalSpy ok(&job, &SignJob::succeeded);
    QSignalSpy bad(&job, &SignJob::failed);
    job.start();

    ASSERT_TRUE(spinUntil([&] { return ok.count() + bad.count() > 0; }));
    EXPECT_EQ(ok.count(), 0);
    ASSERT_EQ(bad.count(), 1); // clean failure, no crash
    EXPECT_FALSE(bad.at(0).at(0).toString().isEmpty());
    EXPECT_FALSE(QFile::exists(dir.filePath(QStringLiteral("doc-signed.pdf")))); // nothing written
}

// the cert-enumeration op finishes BEFORE SignJob's readCertificates()
// returns the path (raceResultBeforeReturn — recovered in the AgentOperation
// ctor, before SignJob connects to `finished`). The terminal emit is queued, so
// SignJob's onCertificatesFinished still runs and the job TERMINATES (emits
// exactly one finished signal) instead of hanging forever on a Finished that
// fired before it subscribed. Certificates1 now exposes GetResult, so the raced
// cert Result is RECOVERED via the late-subscriber pull; the sign op likewise
// recovers its raced artifact via Sign1.GetResult, so the whole flow SUCCEEDS
// and writes the signed document. The load-bearing assertion is that SignJob is
// reached AT ALL, exactly once — now with a recovered success.
TEST(SignJob, FinishedBeforeSubscribeRaceStillReachesJobNoHang)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana")}};
    cfg.raceResultBeforeReturn = true; // ops fire Result+Finished before the path returns
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("report.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), pickFirst(), alwaysOverwrite());
    QSignalSpy ok(&job, &SignJob::succeeded);
    QSignalSpy bad(&job, &SignJob::failed);
    job.start();

    ASSERT_TRUE(spinUntil([&] { return ok.count() + bad.count() > 0; }))
        << "a Finished that fired before SignJob subscribed must still reach it (queued emit), not hang";
    EXPECT_EQ(ok.count() + bad.count(), 1) << "exactly one terminal signal";
    EXPECT_EQ(ok.count(), 1) << "the raced cert + sign Results are recovered via GetResult, so the job succeeds";
    EXPECT_TRUE(QFile::exists(dir.filePath(QStringLiteral("report-signed.pdf")))); // recovered -> signed doc written
}

// the card is pulled while a SignJob has an agent operation in flight (the
// agent stays on the bus, so it emits NO Operation1.Finished — a card-only pull).
// AgentClient::onInterfacesRemoved must sweep + terminalize the in-flight op
// before deleting the card, so SignJob's `finished` slot fires (Cancelled /
// CardRemoved) and the job fails cleanly — instead of hanging forever with the
// op silently destroyed. Nothing is written.
TEST(SignJob, CardRemovedMidOperationFailsCleanlyNoHang)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana")}};
    cfg.operationDelayMs = 60000; // the cert op stays in flight; we pull the card under it
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("report.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), pickFirst(), alwaysOverwrite());
    QSignalSpy ok(&job, &SignJob::succeeded);
    QSignalSpy bad(&job, &SignJob::failed);
    job.start();

    // The cert op is now in flight (60 s delay). Pull the card out from under it.
    ASSERT_TRUE(spinUntil([&] { return h.client->card(h.agent->cardPath()) != nullptr; }));
    h.agent->setCardPresent(false);

    ASSERT_TRUE(spinUntil([&] { return ok.count() + bad.count() > 0; }))
        << "card removal must terminalize the in-flight op, not hang the SignJob forever";
    EXPECT_EQ(ok.count(), 0);
    EXPECT_EQ(bad.count(), 1);                                                      // exactly once, clean failure
    EXPECT_FALSE(QFile::exists(dir.filePath(QStringLiteral("report-signed.pdf")))); // nothing written
}

// --- overwrite declined: existing output is left untouched ------------------

TEST(SignJob, OverwriteDeclinedAborts)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-A"), true, QStringLiteral("Ana")}};
    Harness h(cfg);
    ASSERT_NE(h.card, nullptr);

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("report.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\n");
    f.close();

    // Pre-create the output so the confirmer path fires.
    const QString out = dir.filePath(QStringLiteral("report-signed.pdf"));
    QFile pre(out);
    ASSERT_TRUE(pre.open(QIODevice::WriteOnly));
    pre.write("OLD");
    pre.close();

    SignJob job(h.card, input, QStringLiteral("application/pdf"), QString(), pickFirst(), neverOverwrite());
    QSignalSpy ok(&job, &SignJob::succeeded);
    QSignalSpy bad(&job, &SignJob::failed);
    job.start();
    ASSERT_TRUE(spinUntil([&] { return ok.count() + bad.count() > 0; }));
    EXPECT_EQ(ok.count(), 0);
    EXPECT_EQ(bad.count(), 1);

    QFile of(out);
    ASSERT_TRUE(of.open(QIODevice::ReadOnly));
    EXPECT_EQ(of.readAll(), QByteArray("OLD")); // untouched
}
