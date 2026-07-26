// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Capstone end-to-end test for the credential-lifecycle window: the FULL stack —
// a real `CredentialController` driving `librekde-agentclient` against a real
// `org.librescrs.Agent` peer (the FakeAgent) on a private session bus, with real
// adaptors and real signal marshaling. Where the controller/model unit tests each
// pin one seam, this one walks the whole credential-management flow the user sees
// (list → manage → result → mandatory re-list) and asserts the AGENT-side op
// SEQUENCING, not just the controller's final state.
//
// Runs under dbus-run-session, QT_QPA_PLATFORM=offscreen (reuses the agent-client
// D-Bus harness + its QCoreApplication TestMain, so ki18n has an app for locale).

#include "AgentCapabilities.h"
#include "AgentClient.h"
#include "CredentialController.h"
#include "CredentialModel.h"
#include "TestBus.h" // Harness + FakeAgent + waitFor (reused agentclient test double)

#include <QVariantMap>
#include <gtest/gtest.h>
#include <memory>

using namespace LibreKDE;
using namespace LibreKDETest;

namespace {

using State = Credentials::CredentialController::State;

// A plain operational User PIN — the record every scenario's Change verb targets.
QVariantMap userPinRecord()
{
    return QVariantMap{{QStringLiteral("id"), QStringLiteral("user:0x86")},
                       {QStringLiteral("kind"), QStringLiteral("user")},
                       {QStringLiteral("state"), QStringLiteral("operational")},
                       {QStringLiteral("can_change"), true},
                       {QStringLiteral("retries_left"), 3},
                       {QStringLiteral("retries_max"), 3}};
}

// The card's PUK, carrying a remaining unblock budget — the second listed row in
// scenario 1, proving the whole list round-trips over the wire (not just one row).
QVariantMap pukRecord()
{
    return QVariantMap{{QStringLiteral("id"), QStringLiteral("puk:0x88")},
                       {QStringLiteral("kind"), QStringLiteral("puk")},
                       {QStringLiteral("state"), QStringLiteral("operational")},
                       {QStringLiteral("unblocks_left"), 8}};
}

// Scenario 1 — the full happy path over the real bus: the agent advertises
// PinManagement and scripts a two-row ListCredentials (User PIN + PUK); the
// controller reaches Ready with both rows. A successful Change PIN then runs
// (Working → non-error Result) and MUST re-list (the agent
// invalidates its listing cache on any mutation that reached the card). Assert the
// AGENT minted exactly three operations end to end — list + manage + re-list — and
// that the re-list repopulated the dashboard.
TEST(CredentialsE2E, ListChangePinOkResultThenMandatoryRelist)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement | Cap::Pki;
    cfg.operationDelayMs = 40; // keep Working/Result observable across the full stack
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord(), pukRecord()};
    Harness h(cfg);

    auto client = std::make_shared<AgentClient>(h.client(), h.service());
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());

    // ListCredentials over the wire → Ready with BOTH rows (User PIN + PUK).
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
    ASSERT_NE(ctl.credentials(), nullptr);
    EXPECT_EQ(ctl.credentials()->rowCount(), 2);
    EXPECT_EQ(ctl.credentials()->data(ctl.credentials()->index(0, 0), Credentials::CredentialModel::IdRole).toString(),
              QStringLiteral("user:0x86"));
    EXPECT_EQ(h.operationCount(), 1) << "exactly one ListCredentials op on entry (the list)";

    // The Change verb enters Working synchronously (the agent's secure prompter
    // collects the PIN — the window holds no secret).
    ctl.changePin(QStringLiteral("user:0x86"));
    EXPECT_EQ(ctl.state(), int(State::Working)) << "the verb enters Working synchronously";

    // list(1) + manage(1) + mandatory re-list(1) == 3 ops minted end to end.
    ASSERT_TRUE(waitFor([&]() { return h.operationCount() == 3; }))
        << "a mutation that reached the card must re-list before offering another action";
    EXPECT_FALSE(ctl.resultIsError()) << "a successful change is a non-error result";
    EXPECT_FALSE(ctl.resultMessage().isEmpty()) << "the neutral \"Done.\" banner is shown";

    // The fresh re-list repopulates the model and settles the dashboard back to Ready.
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
    EXPECT_EQ(ctl.credentials()->rowCount(), 2) << "the mandatory re-list repopulated the dashboard";
    EXPECT_EQ(h.operationCount(), 3) << "no extra ops beyond list + manage + re-list";
}

// Scenario 2 — a wrong PIN is a soft-fail: the op may finish with an Ok terminal
// yet the uniform a{sv} result carries outcome=invalidPin (retries_left:1). The
// controller MUST drive off pinResult() (not the terminal status), render an ERROR
// result attributed to the presented credential (the record's kind → "User PIN"),
// and still run the mandatory re-list (the attempt reached the card).
TEST(CredentialsE2E, ChangePinInvalidPinSurfacesAttributedError)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement | Cap::Pki;
    cfg.operationDelayMs = 40;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg);

    auto client = std::make_shared<AgentClient>(h.client(), h.service());
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
    ASSERT_EQ(ctl.credentials()->rowCount(), 1);

    const int opsBefore = h.operationCount(); // the initial ListCredentials == 1

    // The next mutation presents a wrong PIN with a single retry remaining. The
    // terminal stays Ok (finalStatus 0) so the assertion proves the controller reads
    // pinResult(), never the terminal status — AND the mandatory re-list still runs.
    h.mutateConfig([](FakeAgent::Config& c) {
        c.credResult =
            QVariantMap{{QStringLiteral("outcome"), QStringLiteral("invalidPin")}, {QStringLiteral("retries_left"), 1}};
    });
    ctl.changePin(QStringLiteral("user:0x86"));

    ASSERT_TRUE(waitFor([&]() { return ctl.resultIsError(); }))
        << "a soft-fail invalidPin (Ok terminal) must still surface as an error result";
    EXPECT_TRUE(ctl.resultMessage().contains(QStringLiteral("User PIN")))
        << "the counter is attributed to the presented credential — " << ctl.resultMessage().toStdString();

    // The attempt reached the card, so the mandatory re-list runs too: manage + re-list.
    ASSERT_TRUE(waitFor([&]() { return h.operationCount() == opsBefore + 2; }))
        << "a soft-fail that reached the card still re-lists";
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }))
        << "the re-list settles the dashboard back to Ready after the error banner";
}

// Scenario 3 — a stale credential id: the agent has dropped its listing cache, so
// ManagePin throws UnknownCredential at METHOD ENTRY (no Operation minted). The
// controller must NOT show a red error — it auto-re-lists to recover the fresh ids
// (id-less ListCredentials stays exempt from the entry error) and surfaces a
// neutral notice. Assert exactly one recovery op mints (the re-list, not a manage).
TEST(CredentialsE2E, ManageEntryUnknownCredentialAutoRelistsNeutrally)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Cap::PinManagement | Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    // ManagePin/ActivateSigningKey throw UnknownCredential at entry; ListCredentials
    // is id-less and stays exempt, so the recovery re-list still succeeds.
    cfg.credEntryError = true;
    cfg.credEntryErrorName = QStringLiteral("org.librescrs.Agent.Error.UnknownCredential");
    Harness h(cfg);

    auto client = std::make_shared<AgentClient>(h.client(), h.service());
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    const int opsBefore = h.operationCount(); // the initial ListCredentials == 1
    ctl.changePin(QStringLiteral("user:stale"));

    // No manage op mints (the entry throw beat it); the auto-re-list mints exactly one.
    ASSERT_TRUE(waitFor([&]() { return h.operationCount() == opsBefore + 1; }))
        << "an UnknownCredential entry error must trigger a recovery re-list, and only that";
    EXPECT_FALSE(ctl.resultIsError()) << "a stale id is a neutral refresh, not a red error";
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
    EXPECT_EQ(h.operationCount(), opsBefore + 1) << "no manage op was minted — only the recovery re-list";
}

} // namespace
