// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// CredentialController state-machine skeleton, driven by the FakeAgent over a
// private session bus (the shared D-Bus test harness). Runs under
// dbus-run-session, QT_QPA_PLATFORM=offscreen.
//
// The controller's client binds itself to the agent's real well-known bus name
// and offers no hook to point it elsewhere, so every Harness here is built with
// BusNames::UniqueAndWellKnown: the fake has to answer to that name for the
// controller to see an agent at all.

#include "CredentialController.h"
#include "CredentialModel.h"
#include "TestBus.h" // Harness + FakeAgent + waitFor (the shared bus peer)

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include <LibreSCRS/AgentClient/AgentClient.h>
#include <LibreSCRS/AgentClient/AgentReader.h>
#include <LibreSCRS/AgentClient/CredentialTypes.h>

#include <QList>
#include <QSignalSpy>
#include <QTextDocument> // Qt::mightBeRichText (plainDisplay round-trip assertion)
#include <QVariantMap>
#include <gtest/gtest.h>
#include <memory>

using namespace LibreKDE;
using namespace LibreSCRS::AgentClient::Fakes;

// The agent client library, spelled through an alias rather than pulled in
// wholesale with a using-directive. NOT a collision fix, and the measurement
// behind that has to be the discriminating one: a using-directive added while
// every name here stays `Client::`-qualified proves nothing, because ambiguity
// between using-directives is diagnosed only at UNQUALIFIED lookup. What was
// actually run is the alias deleted, the directive put in its place, and all 110
// `Client::` qualifications stripped — this file then compiles clean, so no name
// collides with the host's. The alias stays for readability: it keeps each name
// below visibly the LIBRARY's rather than the host's, in a file that draws value
// types from both.
namespace Client = LibreSCRS::AgentClient;

namespace {

using ControllerState = Credentials::CredentialController::State;

// Records the controller's state at every stateChanged, so a test can assert the
// SEQUENCE of transitions rather than poll for one of them. Polling cannot see a
// state the controller passes through and leaves within one poll interval, which
// is exactly the shape of a refusal: Working, then Result on the next event-loop
// turn, then — for the arms that re-list — Ready a few milliseconds later.
class StateRecorder
{
public:
    explicit StateRecorder(Credentials::CredentialController& controller)
    {
        m_connection =
            QObject::connect(&controller, &Credentials::CredentialController::stateChanged, &controller,
                             [this, &controller]() { m_states.append(ControllerState(controller.state())); });
    }

    /// Drops the connection, because the lambda captures THIS recorder and the
    /// controller normally outlives it (declared first in a test body, destroyed
    /// last). Without this, a transition emitted after the recorder went out of
    /// scope would write into freed memory.
    ~StateRecorder()
    {
        QObject::disconnect(m_connection);
    }

    StateRecorder(const StateRecorder&) = delete;
    StateRecorder& operator=(const StateRecorder&) = delete;

    [[nodiscard]] QList<ControllerState> states() const
    {
        return m_states;
    }

    /// The recorded sequence rendered for a failure message ("Working -> Result").
    [[nodiscard]] std::string trace() const
    {
        std::string out;
        for (const ControllerState s : m_states) {
            if (!out.empty()) {
                out += " -> ";
            }
            out += std::to_string(int(s));
        }
        return out.empty() ? std::string("<no transition>") : out;
    }

private:
    QList<ControllerState> m_states;
    QMetaObject::Connection m_connection;
};

// The controller binds a reader by its opaque agent-side id (the `--reader` arg),
// so the state must classify off the card behind that reader.
TEST(CredentialController, NoCardWhenReaderEmpty)
{
    FakeAgent::Config cfg;
    cfg.hasCard = false; // reader present, but empty
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(Credentials::CredentialController::State::NoCard); }));
    EXPECT_EQ(ctl.state(), int(Credentials::CredentialController::State::NoCard));
}

// plainDisplay neutralizes rich-text promotion for agent/hardware-derived values
// (reader names, agent guidance, budget lines) rendered by AutoText sinks that
// expose no textFormat knob (Kirigami PlaceholderMessage / InlineMessage /
// PromptDialog subtitle): tag-looking values are HTML-escaped — and the escaped
// form must itself be rich-detected, so the AutoText sink renders the ORIGINAL
// literal characters — while every legitimate value passes through
// byte-identical (presentation-layer neutralization only, never data mutation).
// Mirrors the plasmoid's SmartCardHandler::plainDisplay.
TEST(CredentialController, PlainDisplayNeutralizesMarkupAndPreservesPlainText)
{
    using Credentials::CredentialController;
    // Hostile: markup is escaped...
    EXPECT_EQ(CredentialController::plainDisplay(QStringLiteral("<b>Reader</b>")),
              QStringLiteral("&lt;b&gt;Reader&lt;/b&gt;"));
    const QString link = QStringLiteral("<a href='http://evil'>reader</a>");
    EXPECT_FALSE(CredentialController::plainDisplay(link).contains(QLatin1Char('<')));
    // ...and the escaped form is itself promoted by Qt::mightBeRichText, so an
    // AutoText sink renders it as the original literal characters.
    EXPECT_TRUE(Qt::mightBeRichText(CredentialController::plainDisplay(QStringLiteral("<b>x</b>"))));

    // Legitimate values are byte-identical — including ones with '&'.
    EXPECT_EQ(CredentialController::plainDisplay(QStringLiteral("Gemalto USB Reader (2)")),
              QStringLiteral("Gemalto USB Reader (2)"));
    EXPECT_EQ(CredentialController::plainDisplay(QStringLiteral("Nova & Stara uprava")),
              QStringLiteral("Nova & Stara uprava"));
    EXPECT_EQ(CredentialController::plainDisplay(QString()), QString());
}

// A card that carries no PinManagement bit has no surface in this window.
TEST(CredentialController, NotManageableWithoutPinMgmtBit)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData; // no PinManagement
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(Credentials::CredentialController::State::NotManageable); }));
    EXPECT_EQ(ctl.state(), int(Credentials::CredentialController::State::NotManageable));
    EXPECT_EQ(ctl.readerName(), QStringLiteral("Fake"));
}

// One scripted ListCredentials record — a plain operational User PIN.
QVariantMap userPinRecord()
{
    return QVariantMap{{QStringLiteral("id"), QStringLiteral("user:0x86")},
                       {QStringLiteral("kind"), QStringLiteral("user")},
                       {QStringLiteral("state"), QStringLiteral("operational")},
                       {QStringLiteral("can_change"), true},
                       {QStringLiteral("retries_left"), 3},
                       {QStringLiteral("retries_max"), 3}};
}

// A blocked, unblockable User PIN — the record the "Unblock…" verb targets.
QVariantMap unblockablePinRecord()
{
    return QVariantMap{{QStringLiteral("id"), QStringLiteral("user:0x86")},
                       {QStringLiteral("kind"), QStringLiteral("user")},
                       {QStringLiteral("state"), QStringLiteral("blocked")},
                       {QStringLiteral("unblockable"), true},
                       {QStringLiteral("retries_left"), 0}};
}

// The card's PUK, carrying a remaining unblock budget — the record the unblock
// pre-flight reads (and shows) before launching the agent's PUK prompter. Its
// usage budget (uses_left/uses_max, "a PUK good for N unblocks") is what the
// confirm sheet surfaces; unblocks_left (the DOCP reset counter) is carried but
// not shown.
QVariantMap pukRecord()
{
    return QVariantMap{{QStringLiteral("id"), QStringLiteral("puk:0x88")},
                       {QStringLiteral("kind"), QStringLiteral("puk")},
                       {QStringLiteral("state"), QStringLiteral("operational")},
                       {QStringLiteral("uses_left"), 16},
                       {QStringLiteral("uses_max"), 20},
                       {QStringLiteral("unblocks_left"), 8}};
}

// An operational Signing PIN whose on-card key still awaits bring-up — the
// record the standalone "Activate signing key" recovery button targets.
QVariantMap keyPendingSignRecord()
{
    return QVariantMap{{QStringLiteral("id"), QStringLiteral("sign:0x81")},
                       {QStringLiteral("kind"), QStringLiteral("sign")},
                       {QStringLiteral("state"), QStringLiteral("operational")},
                       {QStringLiteral("key_activatable"), true},
                       {QStringLiteral("key_activation_pending"), true}};
}

// A transport User PIN whose on-card signing key still needs bringing up — the
// record the "Activate…" verb targets with `activateKey=true`.
QVariantMap transportPinPendingKeyRecord()
{
    return QVariantMap{{QStringLiteral("id"), QStringLiteral("user:0x86")},
                       {QStringLiteral("kind"), QStringLiteral("user")},
                       {QStringLiteral("state"), QStringLiteral("transport")},
                       {QStringLiteral("activatable"), true},
                       {QStringLiteral("key_activation_pending"), true}};
}

// A manageable card kicks ListCredentials on entering Loading; a non-empty result
// lands in Ready with the model populated.
TEST(CredentialController, ReadyWhenCredentialsListed)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(Credentials::CredentialController::State::Ready); }));
    EXPECT_EQ(ctl.state(), int(Credentials::CredentialController::State::Ready));
    ASSERT_NE(ctl.credentials(), nullptr);
    EXPECT_EQ(ctl.credentials()->rowCount(), 1);
    EXPECT_EQ(ctl.credentials()->data(ctl.credentials()->index(0, 0), Credentials::CredentialModel::IdRole).toString(),
              QStringLiteral("user:0x86"));
}

// A manageable card whose ListCredentials returns zero records lands in Empty —
// distinct from NotManageable (a card outside this window's scope).
TEST(CredentialController, EmptyWhenNoCredentials)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {}; // empty — a manageable card with nothing to act on
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(Credentials::CredentialController::State::Empty); }));
    EXPECT_EQ(ctl.state(), int(Credentials::CredentialController::State::Empty));
    EXPECT_EQ(ctl.credentials()->rowCount(), 0);
}

// While the fetch is in flight the classifier rests in Loading, then advances to
// Ready when the record arrives. A generous op delay keeps Loading observable.
TEST(CredentialController, LoadingWhileFetchingThenReady)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 300; // Loading persists long enough for waitFor to catch it
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(Credentials::CredentialController::State::Loading); }));
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(Credentials::CredentialController::State::Ready); }));
    EXPECT_EQ(ctl.credentials()->rowCount(), 1);
}

// When the agent's bus name vanishes mid-session, the client reports
// availabilityChanged(false) and the controller leaves any card state for
// AgentUnavailable — a client-level state, not a NoCard. Uses the harness's real
// service-drop (no mock): first reach a live state, then unregister.
TEST(CredentialController, AgentUnavailableWhenServiceVanishes)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    // Reach a deterministic live state (the dashboard) before dropping the daemon.
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(Credentials::CredentialController::State::Ready); }));

    // The daemon vanishes off the bus.
    h.unregisterService();

    ASSERT_TRUE(
        waitFor([&]() { return ctl.state() == int(Credentials::CredentialController::State::AgentUnavailable); }));
    EXPECT_EQ(ctl.state(), int(Credentials::CredentialController::State::AgentUnavailable));
    EXPECT_TRUE(ctl.readerName().isEmpty());
}

// An unbound controller with a live agent and NO card anywhere rests in NoCard,
// the window's "insert the card" empty state (nothing to fall back to).
TEST(CredentialController, NoCardWhenUnboundAndNoCardAnywhere)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement;
    cfg.hasCard = false; // no reader holds a card -> no fallback target
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    Credentials::CredentialController ctl(client); // never bindReader()
    EXPECT_EQ(ctl.state(), int(Credentials::CredentialController::State::NoCard));
}

// A MISSING --reader falls back to firstReaderWithCard() — launched
// with no reader argument, the window manages the obvious card instead of
// dead-ending on NoCard.
TEST(CredentialController, UnboundFallsBackToFirstReaderWithCard)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    Credentials::CredentialController ctl(client); // never bindReader()
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }))
        << "with no --reader bound, the window must fall back to the first reader with a card";
    EXPECT_EQ(ctl.readerName(), QStringLiteral("Fake"));
}

// A STALE --reader (a path that doesn't resolve) falls back to
// firstReaderWithCard() too.
TEST(CredentialController, StaleReaderPathFallsBackToFirstReaderWithCard)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    Credentials::CredentialController ctl(client);
    ctl.bindReader(QStringLiteral("/org/librescrs/Agent/reader/99")); // nonexistent
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }))
        << "a stale --reader path must fall back to the first reader with a card";
    EXPECT_EQ(ctl.readerName(), QStringLiteral("Fake"));
}

// A transient read failure (the list op finishes Error) lands on ReadFailed — NOT
// the misleading NotManageable, and NOT a permanent latch: the op-latch releases,
// so once the agent recovers, an explicit refresh() re-fetches and reaches Ready.
TEST(CredentialController, TransientReadErrorRecoversOnRefetch)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    // The LISTING's own terminal status, scripted apart from the mutation's:
    // the shared double separates them because the agent caches a snapshot only
    // for a list that succeeded, so a listing dragged into a mutation's Error
    // would leave every id unresolvable for reasons this case is not about.
    cfg.listingFinalStatus = 2; // Error — a transient read failure
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::ReadFailed); }));
    EXPECT_EQ(ctl.state(), int(State::ReadFailed)); // read failure, not "unsupported"

    // The agent recovers: the next ListCredentials returns Ok with a record. A
    // transient error does not latch, so an explicit refresh() re-fetches.
    h.mutateConfig([](FakeAgent::Config& c) {
        c.listingFinalStatus = 0;
        c.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
        c.credRecords = {userPinRecord()};
    });
    ctl.refresh();

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }))
        << "a transient read error must stay re-fetchable, not permanently latch the window";
    EXPECT_EQ(ctl.credentials()->rowCount(), 1);
}

// A user-cancelled read lands on ReadFailed AND latches: an incidental re-classify
// (a card capability change) must NOT auto-re-fetch — re-listing after a cancelled
// CAN would re-prompt. Distinct from the transient case above.
TEST(CredentialController, UserCancelledReadLatchesWithoutReprompt)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 400; // keep the fetch in flight so cancel() has a live op
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Loading); }));

    ctl.cancel(); // the user cancels the in-flight read
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::ReadFailed); }));

    const int opsAfterCancel = h.operationCount();
    // A same-card capability change drives classify(); the cancel latch must hold —
    // no fresh ListCredentials op is minted (no CAN re-prompt).
    h.emitCardCapabilitiesChanged(Client::Cap::PinManagement); // still manageable, different value
    EXPECT_FALSE(waitFor([&]() { return h.operationCount() > opsAfterCancel; }, 300))
        << "a cancelled read must not auto-re-prompt on an incidental re-classify";
    EXPECT_EQ(ctl.state(), int(State::ReadFailed));
}

// The no-Result half of the list-cancel latch: an aborted ListCredentials the
// agent did not answer with a Result finishes client-side as
// Error/CommunicationError (finalizeTerminal rewrites the Cancelled terminal
// when nothing is recoverable) — the user's cancel must STILL latch. Keying the
// latch solely off a preserved Cancelled status would auto-re-fetch here and
// re-raise the CAN prompt against the user's explicit cancel. Mirrors the
// mutation path's cancel-request flag (suppressResult reproduces the abort).
TEST(CredentialController, CancelledReadWithNoResultStillLatches)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 400; // keep the fetch in flight so cancel() has a live op
    cfg.suppressResult = true;  // the abort delivers NO Result and retains nothing
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Loading); }));

    ctl.cancel(); // the user cancels the in-flight read
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::ReadFailed); }));

    const int opsAfterCancel = h.operationCount();
    // A same-card capability change drives classify(); the cancel latch must hold
    // even though the terminal arrived as Error/CommunicationError, not Cancelled.
    h.emitCardCapabilitiesChanged(Client::Cap::PinManagement); // still manageable, different value
    EXPECT_FALSE(waitFor([&]() { return h.operationCount() > opsAfterCancel; }, 300))
        << "a cancelled read whose abort delivered no Result must still latch (no CAN re-prompt)";
    EXPECT_EQ(ctl.state(), int(State::ReadFailed));
}

// --- Verb flow: invoke → Working → result → mandatory re-list -----------------

// A successful Change PIN: the verb runs (Working), lands a non-error result
// (Result), then MUST re-list — the agent invalidates its listing
// cache on any mutation that reached the card. Assert BOTH ops mint: the manage
// op AND the re-list, so operationCount grows by two past the initial list.
TEST(CredentialController, ChangePinOkResultThenMandatoryRelist)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 60; // keep Working/Result observable
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    const int opsBefore = h.operationCount(); // the initial ListCredentials
    ctl.changePin(QStringLiteral("user:0x86"));
    EXPECT_EQ(ctl.state(), int(State::Working)) << "the verb enters Working synchronously";

    // manage op + mandatory re-list op both mint → +2.
    ASSERT_TRUE(waitFor([&]() { return h.operationCount() == opsBefore + 2; }))
        << "a mutation that reached the card must re-list before offering another action";
    EXPECT_FALSE(ctl.resultIsError());
    EXPECT_FALSE(ctl.resultMessage().isEmpty()); // the neutral "Done." banner
    // The fresh list settles the dashboard back to Ready.
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
}

// A wrong PIN is a soft-fail: the op may finish with an Ok terminal yet the
// uniform a{sv} result carries outcome=invalidPin. The controller MUST drive off
// pinResult() (not the terminal status) and render an error, attributed to the
// presented credential (the record's kind → "User PIN").
TEST(CredentialController, ChangePinInvalidPinRendersAttributedError)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 60;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    // The change presents a wrong PIN. finalStatus stays 0 (Ok terminal) so the
    // re-list still succeeds AND the test proves the controller reads pinResult(),
    // never the terminal status.
    h.mutateConfig([](FakeAgent::Config& c) {
        c.credResult =
            QVariantMap{{QStringLiteral("outcome"), QStringLiteral("invalidPin")}, {QStringLiteral("retries_left"), 2}};
    });
    ctl.changePin(QStringLiteral("user:0x86"));

    ASSERT_TRUE(waitFor([&]() { return ctl.resultIsError(); }))
        << "a soft-fail invalidPin (Ok terminal) must still surface as an error result";
    EXPECT_TRUE(ctl.resultMessage().contains(QStringLiteral("User PIN")))
        << "attribution: the counter is labelled with the presented credential — " << ctl.resultMessage().toStdString();
    EXPECT_TRUE(ctl.resultMessage().contains(QStringLiteral("not correct"))) << ctl.resultMessage().toStdString();
    // Result rendering: the banner carries the attribution counter when
    // the result delivered one ("… — 2 attempts left").
    EXPECT_TRUE(ctl.resultMessage().contains(QStringLiteral("2 attempts left")))
        << "the result's retries_left must be surfaced in the banner — " << ctl.resultMessage().toStdString();
}

// A user-cancelled change is neutral — never a red error banner. The controller
// reaches Result (proving the mutation completed) with resultIsError=false and an
// empty (neutral) message.
TEST(CredentialController, ChangePinUserCancelledIsNeutral)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 60;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    // Cancel at the prompter. finalStatus stays 0 so the re-list still succeeds;
    // the outcome alone marks it neutral.
    h.mutateConfig([](FakeAgent::Config& c) {
        c.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("userCancelled")}};
    });
    ctl.changePin(QStringLiteral("user:0x86"));

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Result); }))
        << "a completed mutation always reaches Result (banner state)";
    EXPECT_FALSE(ctl.resultIsError()) << "userCancelled is neutral, never an error";
    EXPECT_TRUE(ctl.resultMessage().isEmpty()) << "a cancel shows no banner text";
}

// A stale credential id: the agent dropped its listing cache, so it refuses
// ManagePin at method entry, by name (UnknownCredential). The controller must NOT
// show a red error — it auto-re-lists (assert a ListCredentials op mints) to
// recover the fresh ids, and surfaces a neutral notice.
//
// The refusal now travels on the verb's own terminal, which the library queues to
// the event loop, so the window passes through Working on the way to Result —
// asserted as a SEQUENCE, since Working is too brief to poll for reliably.
TEST(CredentialController, ManageEntryUnknownCredentialAutoRelistsNeutrally)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    // ManagePin/ActivateSigningKey throw UnknownCredential at entry; ListCredentials
    // is id-less and stays exempt, so the re-list recovers.
    cfg.credEntryError = true;
    cfg.credEntryErrorName = QStringLiteral("org.librescrs.Agent.Error.UnknownCredential");
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    const int opsBefore = h.operationCount();
    StateRecorder seq(ctl);
    ctl.changePin(QStringLiteral("user:stale"));
    EXPECT_EQ(ctl.state(), int(State::Working)) << "the verb enters Working before its refusal lands";

    // No manage op mints (the refusal beat it); the auto-re-list mints exactly one op.
    ASSERT_TRUE(waitFor([&]() { return h.operationCount() == opsBefore + 1; }))
        << "an UnknownCredential refusal must trigger a recovery re-list";
    EXPECT_FALSE(ctl.resultIsError()) << "a stale id is a neutral refresh, not a red error";
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
    // Working, then the notice, then the recovered dashboard — the re-listing arm.
    EXPECT_EQ(seq.states(),
              (QList<ControllerState>{ControllerState::Working, ControllerState::Result, ControllerState::Ready}))
        << seq.trace();
}

// A RateLimited entry refusal: the request never reached the card, so the
// listing is still valid — NO re-list may mint (re-fetching would burn the
// user's budget for nothing). A neutral "please wait" notice renders over the
// still-usable dashboard: Result state, never an error banner.
TEST(CredentialController, ManageEntryRateLimitedNeutralNoticeWithoutRelist)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    cfg.credEntryError = true;
    cfg.credEntryErrorName = QStringLiteral("org.librescrs.Agent.Error.RateLimited");
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    const int opsBefore = h.operationCount(); // the initial ListCredentials
    StateRecorder seq(ctl);
    ctl.changePin(QStringLiteral("user:0x86"));
    EXPECT_EQ(ctl.state(), int(State::Working)) << "the verb enters Working before its refusal lands";

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Result); }));
    EXPECT_FALSE(ctl.resultIsError()) << "rate limiting is a neutral notice, never a red banner";
    EXPECT_TRUE(ctl.resultMessage().contains(QStringLiteral("wait"))) << ctl.resultMessage().toStdString();
    // The listing is still valid: no op may mint (neither a manage nor a re-list).
    EXPECT_FALSE(waitFor([&]() { return h.operationCount() > opsBefore; }, 300))
        << "a RateLimited refusal must not re-list — the request never reached the card, so the listing still holds";
    EXPECT_EQ(ctl.state(), int(State::Result)) << "the notice stays up until the next event";
    // Working, then the notice, and NOTHING further — the non-re-listing arm. A
    // trailing Ready here would mean this arm had re-listed after all, which is
    // precisely the collapse into the UnknownCredential arm that must not happen.
    EXPECT_EQ(seq.states(), (QList<ControllerState>{ControllerState::Working, ControllerState::Result})) << seq.trace();
}

// An InvalidRequest entry refusal is USER-REACHABLE: the agent maps a real card
// condition — two records with identical labels (AmbiguousCredential) — onto
// this wire error, so a user clicking Change on such a card must see feedback,
// not silence. The handler logs for diagnostics and surfaces a NEUTRAL notice
// (never a red banner) with NO re-list — the condition is persistent for the
// card, so a re-fetch cannot clear it — and no state regression: the dashboard
// rows survive under the notice. (A dedicated wire error for ambiguous
// credentials is the planned follow-up once all four mirrors can be extended
// together; the client keeps handling InvalidRequest gracefully regardless.)
TEST(CredentialController, ManageEntryInvalidRequestShowsNeutralNotice)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    cfg.credEntryError = true;
    cfg.credEntryErrorName = QStringLiteral("org.librescrs.Agent.Error.InvalidRequest");
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    const int opsBefore = h.operationCount();
    StateRecorder seq(ctl);
    ctl.changePin(QStringLiteral("user:0x86"));
    EXPECT_EQ(ctl.state(), int(State::Working)) << "the verb enters Working before its refusal lands";

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Result); }))
        << "the refusal must be visible, never silent";
    EXPECT_FALSE(ctl.resultIsError()) << "named refusals are never red banners";
    EXPECT_TRUE(ctl.resultMessage().contains(QStringLiteral("refused"))) << ctl.resultMessage().toStdString();
    EXPECT_FALSE(waitFor([&]() { return h.operationCount() > opsBefore; }, 300))
        << "no re-list for an InvalidRequest refusal — the condition is persistent for the card";
    // No state regression: the rows survive, so the dashboard stays actionable.
    EXPECT_EQ(ctl.credentials()->rowCount(), 1);
    // Working, then the notice, and NOTHING further — the other non-re-listing arm.
    EXPECT_EQ(seq.states(), (QList<ControllerState>{ControllerState::Working, ControllerState::Result})) << seq.trace();
}

// Any OTHER entry refusal (e.g. NotAuthorized) takes the generic branch: a
// neutral generic notice plus a DEFENSIVE re-list, so the window recovers to a
// fresh, actionable dashboard rather than a red banner.
TEST(CredentialController, ManageEntryGenericRefusalNeutralNoticeThenDefensiveRelist)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    cfg.credEntryError = true;
    cfg.credEntryErrorName = QStringLiteral("org.librescrs.Agent.Error.NotAuthorized");
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    const int opsBefore = h.operationCount();
    StateRecorder seq(ctl);
    ctl.changePin(QStringLiteral("user:0x86"));
    EXPECT_EQ(ctl.state(), int(State::Working)) << "the verb enters Working before its refusal lands";

    // No manage op mints (the refusal beat it); the DEFENSIVE re-list mints exactly one.
    ASSERT_TRUE(waitFor([&]() { return h.operationCount() == opsBefore + 1; }))
        << "a refusal this window has no specific recovery for must trigger the defensive re-list";
    EXPECT_FALSE(ctl.resultIsError()) << "named refusals are never red banners";
    EXPECT_TRUE(ctl.resultMessage().contains(QStringLiteral("could not be started")))
        << ctl.resultMessage().toStdString();
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }))
        << "the recovery re-list settles the dashboard back to Ready";
    EXPECT_EQ(h.operationCount(), opsBefore + 1) << "exactly one recovery op — no manage op";
    // Working, then the generic notice, then the recovered dashboard — the second
    // re-listing arm, and the copy is what separates it from the first.
    EXPECT_EQ(seq.states(),
              (QList<ControllerState>{ControllerState::Working, ControllerState::Result, ControllerState::Ready}))
        << seq.trace();
}

// The contrast case, and the one behaviour this relocation genuinely changed: a
// verb whose call fails with an error the AGENT never named — a bus-daemon error,
// here a no-reply timeout. The named-refusal axis stays disengaged for anything
// outside the agent's own error namespace, so this is NOT one of the four refusal
// arms; it takes the outcome path, where the shared rule composes the failure from
// both of the operation's failure axes.
//
// Two things must hold, and they used to be one thing. The BANNER changed: this
// once showed the same neutral generic refusal notice as the arm above, and now
// shows red, localized transport copy naming what actually went wrong. The RE-LIST
// did NOT change: the outcome path's mandatory re-list still runs, so the dashboard
// is refreshed exactly as it was before, and there is no stale-list consequence to
// this at all. Pinned here because the distinction is easy to assert wrongly.
TEST(CredentialController, ManageCallFailureUnnamedByAgentShowsTransportCopyAndStillRelists)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    cfg.credEntryError = true;
    // Outside `org.librescrs.Agent.Error.*`, so the client classifies the call
    // without borrowing the agent's named-error vocabulary: the name axis stays
    // disengaged and only the coarse call classification carries the reason.
    cfg.credEntryErrorName = QStringLiteral("org.freedesktop.DBus.Error.NoReply");
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    const int opsBefore = h.operationCount();
    StateRecorder seq(ctl);
    ctl.changePin(QStringLiteral("user:0x86"));
    EXPECT_EQ(ctl.state(), int(State::Working));

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Result); }));
    // Tone: red, unlike every named refusal, because the attempt reported no
    // outcome and a transport failure is a real error.
    EXPECT_TRUE(ctl.resultIsError()) << "a call the agent never answered is an error, not a neutral notice";
    // Copy: the composed transport text, NOT the generic refusal sentence and not
    // the credential vocabulary's one-size-fits-all "did not complete".
    EXPECT_FALSE(ctl.resultMessage().isEmpty());
    EXPECT_FALSE(ctl.resultMessage().contains(QStringLiteral("could not be started")))
        << "this path must not borrow the named-refusal copy — " << ctl.resultMessage().toStdString();
    EXPECT_TRUE(ctl.resultMessage().contains(QStringLiteral("did not answer in time")))
        << "the shared rule must name the transport failure it actually was — " << ctl.resultMessage().toStdString();

    // And the re-list is UNCHANGED: exactly one recovery op, settling back to Ready.
    ASSERT_TRUE(waitFor([&]() { return h.operationCount() == opsBefore + 1; }))
        << "the outcome path's mandatory re-list still runs for a failure the agent did not name";
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
    EXPECT_EQ(h.operationCount(), opsBefore + 1) << "exactly one recovery op — no manage op reached the agent";
    EXPECT_EQ(seq.states(),
              (QList<ControllerState>{ControllerState::Working, ControllerState::Result, ControllerState::Ready}))
        << seq.trace();
}

// The unblock pre-flight: requestUnblock formats the PUK's remaining budget from
// the current model and emits unblockConfirmRequested(id, budgetText); the QML
// sheet then confirms → confirmUnblock mints the ManagePin(unblock) op. The PUK
// entry itself happens in the agent's prompter — the window only surfaces the
// budget and launches.
TEST(CredentialController, RequestUnblockEmitsBudgetThenConfirmMintsOp)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 60;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {unblockablePinRecord(), pukRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
    ASSERT_EQ(ctl.credentials()->rowCount(), 2);

    QSignalSpy spy(&ctl, &Credentials::CredentialController::unblockConfirmRequested);
    ctl.requestUnblock(QStringLiteral("user:0x86"));
    ASSERT_EQ(spy.count(), 1) << "requestUnblock surfaces the confirm sheet synchronously";
    EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("user:0x86"));
    EXPECT_FALSE(spy.at(0).at(1).toString().isEmpty()) << "the PUK budget text must be shown";
    EXPECT_TRUE(spy.at(0).at(1).toString().contains(QStringLiteral("PUK")))
        << "the budget is attributed to the PUK — " << spy.at(0).at(1).toString().toStdString();
    EXPECT_TRUE(spy.at(0).at(1).toString().contains(QStringLiteral("16")))
        << "the budget shows the PUK's remaining unblock (usage) count — " << spy.at(0).at(1).toString().toStdString();
    // No card op is minted merely by asking (the pre-flight is client-side only).
    EXPECT_EQ(ctl.state(), int(State::Ready));

    const int opsBefore = h.operationCount();
    ctl.confirmUnblock(QStringLiteral("user:0x86"));
    EXPECT_EQ(ctl.state(), int(State::Working)) << "confirming launches the unblock verb";
    // ManagePin(unblock) op + its mandatory re-list → +2.
    ASSERT_TRUE(waitFor([&]() { return h.operationCount() == opsBefore + 2; }))
        << "confirmUnblock mints the unblock op, then re-lists";
}

// Partial bring-up: activating a transport PIN sets the PIN but its signing-key
// step fails (outcome=keyActivationFailed, pinActivated=true, keyActivated=false).
// The controller renders the key-failure message (an error, not neutral) and then
// re-lists — the fresh list is what re-offers the standalone "Activate signing
// key" affordance; we never re-request the spent transport value.
TEST(CredentialController, ActivatePartialBringUpRendersKeyFailure)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 60;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {transportPinPendingKeyRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    h.mutateConfig([](FakeAgent::Config& c) {
        c.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("keyActivationFailed")},
                                   {QStringLiteral("pin_activated"), true},
                                   {QStringLiteral("key_activated"), false}};
    });
    ctl.activate(QStringLiteral("user:0x86"));

    ASSERT_TRUE(waitFor([&]() { return ctl.resultIsError(); }))
        << "a failed key activation is an error result, not neutral";
    EXPECT_TRUE(ctl.resultMessage().contains(QStringLiteral("signing key"))) << ctl.resultMessage().toStdString();
}

// The standalone ActivateSigningKey recovery affordance: the verb
// mints on the wire (Working), attributes its result to the SIGNING PIN (the
// presented credential), and runs the mandatory post-mutation re-list — +2 ops.
TEST(CredentialController, ActivateSigningKeyAttributedResultThenMandatoryRelist)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 60;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {keyPendingSignRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    // A wrong SIGN PIN at the prompter — the attribution must name it.
    h.mutateConfig([](FakeAgent::Config& c) {
        c.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("invalidPin")}};
    });
    const int opsBefore = h.operationCount();
    ctl.activateSigningKey(QStringLiteral("sign:0x81"));
    EXPECT_EQ(ctl.state(), int(State::Working)) << "the verb enters Working synchronously";

    // ActivateSigningKey op + mandatory re-list -> +2.
    ASSERT_TRUE(waitFor([&]() { return h.operationCount() == opsBefore + 2; }))
        << "a key activation is a mutation: it must re-list";
    ASSERT_TRUE(waitFor([&]() { return ctl.resultIsError(); }));
    EXPECT_TRUE(ctl.resultMessage().contains(QStringLiteral("Signing PIN")))
        << "attribution: the standalone key activation presents the Signing PIN — "
        << ctl.resultMessage().toStdString();
}

// A capability desync — the client's cached card capabilities still advertise
// PinManagement while the agent refuses ListCredentials at entry
// (UnsupportedOnThisCard) — must land on ReadFailed, exercised against a real
// wire-shaped error reply. The refusal now arrives as a non-Ok terminal on the
// operation the call still hands back, not as a null operation, so this pins that
// the read's failure route reaches the same state it always did.
//
// It also pins the ONE visible difference that relocation brought to the read
// path, so it cannot drift unnoticed: the refused read passes through Loading
// first. Before, the null-operation branch returned ahead of the Loading
// transition and the window reached ReadFailed with no spinner at all. This is the
// list-path sibling of the mutation path's extra Working state.
TEST(CredentialController, ListRefusedAtEntryLandsReadFailed)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    // The client snapshots the caps (with PinManagement) at discovery…
    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    // …then the agent-side caps drop WITHOUT a PropertiesChanged (the desync).
    h.setCardCapabilitiesSilently(Client::Cap::Pki);

    Credentials::CredentialController ctl(client);
    StateRecorder seq(ctl);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::ReadFailed); }))
        << "a refused ListCredentials must land ReadFailed (a read failure, not NotManageable)";
    // The spinner turn, then the read-failure surface — and nothing else. Recorded
    // rather than polled: Loading lasts one event-loop turn here.
    EXPECT_EQ(seq.states(), (QList<ControllerState>{ControllerState::Loading, ControllerState::ReadFailed}))
        << seq.trace();
}

// Re-entrancy guard: while a verb is in flight (Working), further verb clicks must
// be ignored — never mint a second overlapping mutation on the card.
TEST(CredentialController, VerbIgnoredWhileMutationInFlight)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 400; // hold the mutation in flight so re-entry is observable
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    ctl.changePin(QStringLiteral("user:0x86"));
    ASSERT_EQ(ctl.state(), int(State::Working));
    const int opsDuring = h.operationCount(); // one manage op, still in flight

    // Re-entrant clicks while Working must not mint anything.
    ctl.changePin(QStringLiteral("user:0x86"));
    ctl.activate(QStringLiteral("user:0x86"));
    ctl.confirmUnblock(QStringLiteral("user:0x86"));
    EXPECT_FALSE(waitFor([&]() { return h.operationCount() > opsDuring; }, 200))
        << "a second verb must be ignored while one is already running";
    EXPECT_EQ(ctl.state(), int(State::Working));
}

// A user's own Cancel must render NEUTRAL, never a red error banner — even in the
// worst case where the agent aborts WITHOUT delivering a userCancelled Result: the
// op then finishes Error/CommunicationError with pinResult at default Unspecified
// (finalizeTerminal rewrites the terminal), so only the cancel-request flag knows
// this was a cancel. (Reproduced with suppressResult: Cancel → status=1, no Result.)
TEST(CredentialController, CancelledMutationRendersNeutralNotError)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 400; // hold the mutation in flight so cancel() has a live op
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    // From now on an aborted op yields NO Result and recovers nothing → the client
    // terminal is Error/CommunicationError, pinResult=Unspecified: the exact case a
    // naive pinResult-only classifier would paint red.
    h.mutateConfig([](FakeAgent::Config& c) { c.suppressResult = true; });

    ctl.changePin(QStringLiteral("user:0x86"));
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Working); }));
    ctl.cancel();

    // The cancel resolves to the Result banner state, but NEUTRAL — no red banner.
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Result); }));
    EXPECT_FALSE(ctl.resultIsError()) << "a user's own cancel must never render as a red error";
    EXPECT_TRUE(ctl.resultMessage().isEmpty()) << "a cancel shows no banner text";
}

// Launch order: main.cpp constructs the controller (the QML load)
// and applies the explicit --reader target only AFTERWARDS. On a multi-reader
// machine the first-reader-with-card fallback must NOT fire in that window: constructing the
// controller must mint ZERO ops (no wrong-card ListCredentials, whose CAN
// prompt the window could never dismiss), and after the bind exactly ONE list
// runs — the target card's.
TEST(CredentialController, CtorDoesNotProbeBeforeExplicitBindTarget)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    // A second reader arrives holding its own manageable card — the launch
    // targets THIS one; the first-by-path reader/0 card is the wrong-card trap.
    // (Live InterfacesAdded sequence: the client must process each step.)
    h.emitReaderArrivesEmpty();
    const QString reader2 = QStringLiteral("/org/librescrs/Agent/reader/1");
    ASSERT_TRUE(waitFor([&]() { return client->reader(reader2) != nullptr; }));
    const QString card2 = h.emitArrivedReaderCardAdded(Client::Cap::PinManagement | Client::Cap::Pki);
    ASSERT_TRUE(waitFor([&]() { return client->card(card2) != nullptr; }));
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return client->reader(reader2)->cardId() == card2; }));

    // main.cpp order: construction first…
    Credentials::CredentialController ctl(client);
    EXPECT_EQ(h.operationCount(), 0)
        << "construction must not probe ANY card before the explicit --reader target is applied";

    // …then the explicit target is bound.
    ctl.bindReader(reader2);
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
    EXPECT_EQ(ctl.readerName(), QStringLiteral("Fake2")) << "the explicit target wins, not the fallback";
    EXPECT_EQ(h.operationCount(), 1) << "exactly ONE ListCredentials — the target card's; zero on the non-target card "
                                        "(the deferred first classify must not double-list either)";
}

// Cancel-on-retarget: a re-target that abandons an in-flight list
// must cancel it AGENT-SIDE (fire-and-forget Cancel), so a secure prompt raised
// by the abandoned read is dismissed instead of orphaned. Client-side reaping
// alone (disconnect + deleteLater) leaves the agent op running.
TEST(CredentialController, RetargetMidListCancelsAbandonedAgentSideOp)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 400; // hold the first list in flight across the re-target
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    h.emitReaderArrivesEmpty();
    const QString reader2 = QStringLiteral("/org/librescrs/Agent/reader/1");
    ASSERT_TRUE(waitFor([&]() { return client->reader(reader2) != nullptr; }));
    const QString card2 = h.emitArrivedReaderCardAdded(Client::Cap::PinManagement | Client::Cap::Pki);
    ASSERT_TRUE(waitFor([&]() { return client->card(card2) != nullptr; }));
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return client->reader(reader2)->cardId() == card2; }));

    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath()); // reader/0 — its list is now in flight
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Loading); }));
    ASSERT_EQ(h.cancelledOperationCount(), 0);

    ctl.bindReader(reader2); // re-target abandons the reader/0 list
    ASSERT_TRUE(waitFor([&]() { return h.cancelledOperationCount() >= 1; }))
        << "the abandoned in-flight list must be cancelled agent-side (prompt dismissal)";
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
    EXPECT_EQ(ctl.readerName(), QStringLiteral("Fake2"));
}

// Re-targeting the window to an EMPTY reader while a mutation is in flight must
// detach (and cancel) that mutation: its terminal must neither crash the
// mandatory re-list path (a null bound-card deref) nor fire the OLD card's
// result banner / re-list over the new target. The old behavior left the
// mutation attached across bindCard(nullptr) and dereferenced the nulled card
// QPointer in startListCredentials().
TEST(CredentialController, RetargetToEmptyReaderMidMutationDetachesCleanly)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 400; // hold the mutation in flight across the re-target
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    // A second reader arrives holding NO card (its Card path is "/").
    h.emitReaderArrivesEmpty();
    const QString reader2 = QStringLiteral("/org/librescrs/Agent/reader/1");
    ASSERT_TRUE(waitFor([&]() { return client->reader(reader2) != nullptr; }));

    ctl.changePin(QStringLiteral("user:0x86"));
    ASSERT_EQ(ctl.state(), int(State::Working));

    // A second launch re-targets the Unique window to the empty reader.
    ctl.bindReader(reader2);
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::NoCard); }));

    // The detached mutation's terminal (due in ~400 ms) must not clobber the
    // re-targeted window's state, must not re-list, and above all must not crash
    // on the nulled card binding.
    EXPECT_FALSE(waitFor([&]() { return ctl.state() != int(State::NoCard); }, 900))
        << "a detached mutation's terminal must not clobber the re-targeted window";
    EXPECT_TRUE(ctl.resultMessage().isEmpty())
        << "no stale cross-card result banner: " << ctl.resultMessage().toStdString();
}

// Pulling the card while a mutation is in flight must land the window in NoCard
// (never a stuck Working scrim), and the outcome must be attributed truthfully
// as a card removal — the client-side sweep terminalizes the op with
// (Cancelled, CardRemoved), which must NOT be classified as the user's own
// cancel (a silent no-banner result). The Result banner never renders in
// NoCard, so the USER-VISIBLE surface is the dedicated removalNotice property
// (the NoCard placeholder's explanation): it must carry the card-removed copy,
// no re-list may be attempted (there is no card to list), and the notice must
// clear once a card arrives again.
TEST(CredentialController, CardRemovedMidMutationLandsNoCardWithRemovalNotice)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 400; // hold the mutation in flight so the pull races it
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    ctl.changePin(QStringLiteral("user:0x86"));
    ASSERT_EQ(ctl.state(), int(State::Working));
    const int opsBefore = h.operationCount(); // the initial list + the manage op

    h.setCardPresent(false);

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::NoCard); }))
        << "a card pull mid-mutation must land NoCard, not strand the Working scrim";
    EXPECT_TRUE(ctl.removalNotice().contains(QStringLiteral("removed")))
        << "the NoCard surface must carry the truthful card-removed copy, not a silent cancel — got: "
        << ctl.removalNotice().toStdString();
    // The removal path suppresses the mandatory re-list: there is no card to
    // list, so no op may mint until a card is present again.
    EXPECT_FALSE(waitFor([&]() { return h.operationCount() > opsBefore; }, 300))
        << "a mutation terminated by a card pull must not re-list an absent card";

    // The next card (re)bind clears the notice and lists the fresh card.
    h.setCardPresent(true);
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
    EXPECT_TRUE(ctl.removalNotice().isEmpty()) << "a live card supersedes the removal notice";
}

// A mutation the AGENT ITSELF reports as cardRemoved — while the registry still
// lists the card, so the card object outlives the pull — parks its truthful
// outcome on removalNotice. A LATER recovery verb must clear that stale notice
// even when the agent refuses it at method entry, and must clear it as the verb
// STARTS (not once its refusal lands), or the obsolete removal copy is on screen
// for the whole round trip.
TEST(CredentialController, RefusedVerbClearsStaleRemovalNotice)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    // The verb's own Result reports cardRemoved while the card object stays in
    // the registry: the outcome is parked on removalNotice (state = Result).
    h.mutateConfig([](FakeAgent::Config& c) {
        c.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("cardRemoved")}};
    });
    ctl.changePin(QStringLiteral("user:0x86"));
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Result); }));
    ASSERT_FALSE(ctl.removalNotice().isEmpty()) << "the premise: the card-removed outcome parked a notice";

    // The recovery verb refuses at method entry (RateLimited): the stale notice
    // must be cleared regardless — a fresh verb obsoletes it.
    h.mutateConfig([](FakeAgent::Config& c) {
        c.credEntryError = true;
        c.credEntryErrorName = QStringLiteral("org.librescrs.Agent.Error.RateLimited");
    });
    ctl.changePin(QStringLiteral("user:0x86"));
    // Synchronous on purpose: the notice must be gone the moment the verb starts,
    // BEFORE its refusal comes back — which is now a whole round trip later.
    EXPECT_TRUE(ctl.removalNotice().isEmpty())
        << "a stale removal notice must not survive a recovery verb the agent refuses";
    EXPECT_EQ(ctl.state(), int(State::Working));
    // …and it is still gone once the refusal's own neutral notice replaces it.
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Result); }));
    EXPECT_TRUE(ctl.removalNotice().isEmpty());
    EXPECT_FALSE(ctl.resultIsError());
}

// A registry event while a verb is in flight must NOT re-resolve the binding:
// with a fallback binding, a card inserted in ANOTHER reader would re-target
// bindCard() mid-mutation, whose detach silently cancels the user's in-flight
// PIN change and dismisses the secure prompt. The binding stays pinned until
// the mutation lands; the post-mutation re-list runs on the SAME card.
TEST(CredentialController, RegistryEventDuringMutationDoesNotRetargetBinding)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.hasCard = false; // reader/0 (first by path — the fallback trap) starts EMPTY
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());

    // A second reader arrives holding a manageable card; the UNBOUND controller
    // fallback-binds to it (the only reader with a card).
    h.emitReaderArrivesEmpty();
    const QString reader2 = QStringLiteral("/org/librescrs/Agent/reader/1");
    ASSERT_TRUE(waitFor([&]() { return client->reader(reader2) != nullptr; }));
    const QString card2 = h.emitArrivedReaderCardAdded(Client::Cap::PinManagement | Client::Cap::Pki);
    ASSERT_TRUE(waitFor([&]() { return client->card(card2) != nullptr; }));
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return client->reader(reader2)->cardId() == card2; }));

    Credentials::CredentialController ctl(client); // never bindReader() — fallback binding
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
    ASSERT_EQ(ctl.readerName(), QStringLiteral("Fake2"));

    // Launch a verb, held in flight long enough for the registry event to race it.
    h.mutateConfig([](FakeAgent::Config& c) { c.operationDelayMs = 400; });
    ctl.changePin(QStringLiteral("user:0x86"));
    ASSERT_EQ(ctl.state(), int(State::Working));
    ASSERT_EQ(h.cancelledOperationCount(), 0);

    // A card lands in reader/0 — the FIRST reader by path, exactly what the
    // fallback would re-resolve to. The in-flight mutation must survive.
    h.setCardPresent(true);
    EXPECT_FALSE(waitFor([&]() { return h.cancelledOperationCount() > 0; }, 300))
        << "a registry event mid-mutation must not cancel the user's in-flight verb";
    EXPECT_EQ(ctl.state(), int(State::Working)) << "the Working scrim survives the registry event";
    EXPECT_EQ(ctl.readerName(), QStringLiteral("Fake2")) << "the binding stays pinned to the mutating card";

    // The mutation lands normally; its mandatory re-list runs on the SAME card.
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));
    EXPECT_EQ(ctl.readerName(), QStringLiteral("Fake2"));
    EXPECT_EQ(h.cancelledOperationCount(), 0);
    EXPECT_FALSE(ctl.resultMessage().isEmpty()) << "the verb's own result banner rendered (not a cancel)";
}

// An EXPLICIT re-bind to a stale/unresolvable reader path while a mutation is
// in flight detaches the verb — and then refresh()'s fallback may re-resolve to
// the SAME card. bindCard() short-circuits on the same live card (no
// resetListState), classify() reaches startListCredentials, and the settled
// latch (m_listSettled) early-returns: without forcing re-classification past
// the latch, NO transition ever fires — the window strands in Working forever,
// with a focus-trapped scrim and a DEAD Cancel (cancel() finds nothing in
// flight once the verb detached). The contract: Working must exit, and Cancel
// must never be dead while the scrim shows.
TEST(CredentialController, StaleRebindDuringMutationExitsWorking)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    // Launch a verb, held in flight so the re-bind lands during Working.
    h.mutateConfig([](FakeAgent::Config& c) { c.operationDelayMs = 400; });
    ctl.changePin(QStringLiteral("user:0x86"));
    ASSERT_EQ(ctl.state(), int(State::Working));
    ASSERT_EQ(h.cancelledOperationCount(), 0);

    // A second launch re-binds to a reader path that never existed: the verb
    // detaches, and the fallback re-resolves to the SAME (only) card.
    ctl.bindReader(QStringLiteral("/org/librescrs/Agent/reader/99"));

    // Working must exit — a modal scrim whose Cancel can no longer reach
    // anything in flight must never stay up.
    EXPECT_NE(ctl.state(), int(State::Working))
        << "an explicit re-bind that detaches the verb must exit Working, not strand the scrim";
    // The detached verb was cancelled agent-side (its secure prompt dismissed).
    ASSERT_TRUE(waitFor([&]() { return h.cancelledOperationCount() == 1; }))
        << "the detached mutation must be cancelled agent-side";
    // The fallback re-classifies the same card past the settled latch and the
    // window converges on a live, actionable dashboard.
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }))
        << "the stale re-bind's fallback re-classification must settle back to Ready";
    EXPECT_EQ(ctl.readerName(), QStringLiteral("Fake")) << "the fallback re-resolved to the same reader";
}

// The mid-LIST variant: pulling the card while the credential list is being read
// must also converge on NoCard (the sweep cancels the list op; the ensuing
// cardChanged unbinds and re-classifies).
TEST(CredentialController, CardRemovedMidListLandsNoCard)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 400; // hold the list in flight so the pull races it
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Loading); }));

    h.setCardPresent(false);

    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::NoCard); }))
        << "a card pull mid-list must land NoCard, not strand Loading";
}

// The subtler re-entrancy path: a verb launched from the Result state, while the
// previous mutation's mandatory re-list is STILL in flight, must detach that
// re-list (beginMutation's resetListState) so its terminal cannot flip the
// window back to Ready underneath the new verb.
//
// What the verb itself does in that window is the AGENT's business, and this
// case used to assert the wrong answer to it. The agent resolves a pinId only
// against a COMPLETED listing, and a mutation drops the cached one; a verb
// issued before the mandatory re-list has finished therefore draws
// UnknownCredential, and the controller shows that refusal. The copy of the
// agent double this repository used to keep marked its listing cache current at
// ListCredentials METHOD ENTRY rather than at completion, so the second verb was
// accepted here and this case asserted a Working window that no real agent
// produces. The shared double marks it on completion, which is why the
// expectation below is a refusal rather than sustained Working.
//
// The property the case exists for is unchanged and is the assertion that
// matters: the detached re-list terminates during the window watched below and
// must not move the controller to Ready.
TEST(CredentialController, VerbFromResultDetachesInflightRelist)
{
    using State = Credentials::CredentialController::State;
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::PinManagement | Client::Cap::Pki;
    cfg.operationDelayMs = 200; // first mutation + its re-list run at this pace
    cfg.credResult = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("ok")}};
    cfg.credRecords = {userPinRecord()};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    ASSERT_TRUE(client->isAvailable());
    Credentials::CredentialController ctl(client);
    ctl.bindReader(h.readerPath());
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Ready); }));

    // First mutation → Result, with its re-list (delay 200) now in flight (the
    // controller holds Result across the re-list).
    ctl.changePin(QStringLiteral("user:0x86"));
    ASSERT_TRUE(waitFor([&]() { return ctl.state() == int(State::Result); }));

    // Slow subsequent ops so the SECOND mutation's Working window comfortably spans
    // the first re-list's (~200 ms) completion — that's exactly when a stale
    // onListFinished would clobber Working → Ready.
    h.mutateConfig([](FakeAgent::Config& c) { c.operationDelayMs = 2000; });

    // Launch a verb from Result while the first re-list is still in flight.
    ctl.changePin(QStringLiteral("user:0x86"));
    EXPECT_EQ(ctl.state(), int(State::Working)) << "the new verb adopts cleanly (Working)";

    // With the detach, the first re-list is disconnected + reaped, so it can NEVER
    // move us to Ready. Watch across a window that covers its ~200 ms completion.
    EXPECT_FALSE(waitFor([&]() { return ctl.state() == int(State::Ready); }, 700))
        << "a detached in-flight re-list must not clobber the new verb's window";

    // And the verb's own outcome, which is a refusal: the agent cannot resolve a
    // pinId while the mandatory re-list is still running, so it answers
    // UnknownCredential and the controller shows the result banner. Asserted so
    // the case says out loud which of the two windows it ends in.
    EXPECT_EQ(ctl.state(), int(State::Result));
}

} // namespace
