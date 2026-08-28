// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// SmartCardHandler driven by a real FakeAgent over a private D-Bus session:
// NoCard → Hybrid/PkiOnly/IdentityOnly per capabilities, PreAuthRequired when
// the card demands a pre-read unlock (and identity has not been read), and
// Error when an agent operation finishes non-Ok. No LibreMiddleware, no card.

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include <LibreSCRS/AgentClient/AgentCard.h>
#include <LibreSCRS/AgentClient/AgentClient.h>
#include <LibreSCRS/AgentClient/AgentReader.h>
#include <LibreSCRS/AgentClient/OperationPhase.h>
#include <LibreSCRS/AgentClient/Types.h>

#include "CardPhotoProvider.h"
#include "CardPhotoStore.h"
#include "CardStateModel.h"
#include "CardSelection.h" // LibreKDE::Signing::chooseSigningCard — the resolution the Purpose plugin calls
#include "SignJob.h"
#include "SmartCardHandler.h"

#include "IdentityRows.h"
#include "TestBus.h"

#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextDocument> // Qt::mightBeRichText (plainDisplay round-trip assertion)
#include <QUrl>
#include <gtest/gtest.h>
#include <memory>
#include <optional>

using namespace LibreKDE;
using namespace LibreKDETest;
using State = LibreKDE::Plasmoid::CardStateModel::State;
using LibreKDE::Plasmoid::SmartCardHandler;

// The agent client library, spelled through an alias rather than pulled in
// wholesale with a using-directive. NOT a collision fix — the host's own
// ErrorCode mirror, which the superseded rationale here named as the colliding
// spelling, no longer exists; ErrorText consumes the library's enum directly.
// The measurement also has to be the discriminating one: a using-directive added
// while every name here stays `Client::`-qualified proves nothing, because
// ambiguity between using-directives is diagnosed only at UNQUALIFIED lookup.
// What was actually run is the alias deleted, the directive put in its place,
// and all 102 `Client::` qualifications stripped — this file then compiles
// clean, so no name here collides with the host's. The alias stays for
// readability: it keeps each name below visibly the LIBRARY's rather than the
// host's, in a file that draws value types from both.
namespace Client = LibreSCRS::AgentClient;

namespace {

// Build a handler over a client of its own. The client is co-owned
// (shared_ptr), mirroring the production sharedAgentClient() shape — several
// handlers may share one client (see the two-instance test).
//
// It is the PRODUCTION constructor: the client binds itself to the agent's real
// well-known bus name, with no service-name hook to point elsewhere. Which is
// why every Harness here is built with BusNames::UniqueAndWellKnown — the fake
// has to answer to that name for the handler to see it at all.
std::unique_ptr<SmartCardHandler> makeHandler(Harness& h)
{
    EXPECT_TRUE(h.claimsWellKnownService())
        << "the handler's client binds the agent's well-known name; a Harness that does not claim it "
           "leaves every assertion below measuring an absent agent";
    return std::make_unique<SmartCardHandler>(std::make_shared<Client::AgentClient>());
}

// Encode a tiny 2x2 image to PNG so the FakeAgent's Photo1.Result memfd carries
// bytes QImage::fromData can actually decode (the handler stores a QImage, not
// raw bytes). Returns the PNG byte stream.
QByteArray tinyPngBytes()
{
    QImage img(2, 2, QImage::Format_RGB32);
    img.fill(Qt::red);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

} // namespace

// readerSerialKey: the stable bound-reader identity (the parenthesized unit
// serial) survives the volatile enumeration index AND the dual-interface model
// flip; an absent, too-short, or digitless token yields an empty key so it
// never becomes a spurious cross-reader match. Pure static — no D-Bus.
TEST(SmartCardHandler, ReaderSerialKeyIsStableAcrossReEnumeration)
{
    // Same physical OMNIKEY, model string flips 5422 <-> 5422CL, same serial.
    EXPECT_EQ(
        SmartCardHandler::readerSerialKey(QStringLiteral("HID Global OMNIKEY 5422 Smartcard Reader "
                                                         "[OMNIKEY 5422 Smartcard Reader] (IM0O2C00NF10456904) 01 00")),
        QStringLiteral("IM0O2C00NF10456904"));
    EXPECT_EQ(SmartCardHandler::readerSerialKey(
                  QStringLiteral("HID Global OMNIKEY 5422 Smartcard Reader "
                                 "[OMNIKEY 5422CL Smartcard Reader] (IM0O2C00NF10456904) 01 00")),
              QStringLiteral("IM0O2C00NF10456904"));
    // Same Gemalto, trailing enumeration index shifts 02 00 -> 00 00, same serial.
    EXPECT_EQ(SmartCardHandler::readerSerialKey(QStringLiteral("Gemalto PC Twin Reader (69988A87) 02 00")),
              QStringLiteral("69988A87"));
    EXPECT_EQ(SmartCardHandler::readerSerialKey(QStringLiteral("Gemalto PC Twin Reader (69988A87) 00 00")),
              QStringLiteral("69988A87"));
    // No serial, or a too-short incidental token -> empty (exact match only).
    EXPECT_TRUE(SmartCardHandler::readerSerialKey(QStringLiteral("Yubico YubiKey CCID 00 00")).isEmpty());
    EXPECT_TRUE(SmartCardHandler::readerSerialKey(QStringLiteral("Some Reader (1) 00 00")).isEmpty());
    EXPECT_TRUE(SmartCardHandler::readerSerialKey(QStringLiteral("Fake")).isEmpty());
}

// Driver-DB model-static parenthesized tokens ("(ICCD)", "(CCID)", "(Liteon)")
// ship verbatim in ifdFriendlyName entries and are IDENTICAL for every unit of
// those models: a digitless token must never qualify as a unit serial, or it
// would equate distinct physical readers. Pure static — no D-Bus.
TEST(SmartCardHandler, ReaderSerialKeyRejectsDigitlessModelTokens)
{
    EXPECT_TRUE(SmartCardHandler::readerSerialKey(
                    QStringLiteral("Giesecke & Devrient GmbH Star Sign Card Token 350 (ICCD) 00 00"))
                    .isEmpty());
    EXPECT_TRUE(SmartCardHandler::readerSerialKey(QStringLiteral("ubisys 13.56MHz RFID (CCID) 00 00")).isEmpty());
    EXPECT_TRUE(
        SmartCardHandler::readerSerialKey(QStringLiteral("Liteon HP SC Keyboard - Apollo (Liteon) 00 00")).isEmpty());
    // A digit-bearing unit serial keeps qualifying.
    EXPECT_EQ(SmartCardHandler::readerSerialKey(QStringLiteral("Gemalto PC Twin Reader (69988A87) 00 00")),
              QStringLiteral("69988A87"));
}

// sameReaderUnit: a shared serial key equates two names ONLY when the base
// (model part, minus the serial group and trailing indices) also agrees. Pure
// static — no D-Bus.
TEST(SmartCardHandler, SameReaderUnitRequiresMatchingBaseAndSerial)
{
    // The unit the fallback exists for: identical base, identical serial, only
    // the trailing enumeration index shifted.
    EXPECT_TRUE(SmartCardHandler::sameReaderUnit(QStringLiteral("Gemalto PC Twin Reader (69988A87) 02 00"),
                                                 QStringLiteral("Gemalto PC Twin Reader (69988A87) 00 00")));
    // Dual-interface siblings share one USB serial but differ in the bracketed
    // model string: NOT the same bind target (a widget binds per interface).
    EXPECT_FALSE(SmartCardHandler::sameReaderUnit(
        QStringLiteral(
            "HID Global OMNIKEY 5422 Smartcard Reader [OMNIKEY 5422 Smartcard Reader] (IM0O2C00NF10456904) 00 00"),
        QStringLiteral(
            "HID Global OMNIKEY 5422 Smartcard Reader [OMNIKEY 5422CL Smartcard Reader] (IM0O2C00NF10456904) 01 00")));
    // Different models sharing a digit-bearing token: the base gate rejects.
    EXPECT_FALSE(SmartCardHandler::sameReaderUnit(QStringLiteral("Vendor Alpha Model A (1234) 00 00"),
                                                  QStringLiteral("Vendor Alpha Model B (1234) 00 00")));
    // Digitless tokens never key at all, so they never equate anything.
    EXPECT_FALSE(SmartCardHandler::sameReaderUnit(
        QStringLiteral("Giesecke & Devrient GmbH Star Sign Card Token 350 (ICCD) 00 00"),
        QStringLiteral("Giesecke & Devrient GmbH Star Sign Card Token 550 (ICCD) 00 00")));
    // Distinct units of one model (different serials): not the same unit.
    EXPECT_FALSE(SmartCardHandler::sameReaderUnit(QStringLiteral("Gemalto PC Twin Reader (AAAA1111) 00 00"),
                                                  QStringLiteral("Gemalto PC Twin Reader (BBBB2222) 01 00")));
    // Residual risk, accepted: a model-static digit-bearing token equates two
    // same-model units — reachable only once the bound unit left the roster.
    EXPECT_TRUE(SmartCardHandler::sameReaderUnit(QStringLiteral("Example Token (0013) 00 00"),
                                                 QStringLiteral("Example Token (0013) 01 00")));
}

// readerDisplayLabels: friendly, distinguishable labels from raw PC/SC names.
// Pure static — no D-Bus. Runs in the test's C locale, so i18nc yields the
// English "%1 — contact(less)" msgids.
TEST(SmartCardHandler, ReaderDisplayLabelsShortensAndDisambiguates)
{
    const QStringList raw = {
        QStringLiteral(
            "HID Global OMNIKEY 5422 Smartcard Reader [OMNIKEY 5422 Smartcard Reader] (IM0O2C00NF10456904) 01 00"),
        QStringLiteral(
            "HID Global OMNIKEY 5422 Smartcard Reader [OMNIKEY 5422CL Smartcard Reader] (IM0O2C00NF10456904) 00 00"),
        QStringLiteral("Gemalto PC Twin Reader (69988A87) 02 00"),
    };
    const QStringList out = SmartCardHandler::readerDisplayLabels(raw);
    ASSERT_EQ(out.size(), 3);
    // The dual-interface OMNIKEY collapses to one model, disambiguated by interface.
    EXPECT_EQ(out.at(0), QStringLiteral("OMNIKEY 5422 — contact"));
    EXPECT_EQ(out.at(1), QStringLiteral("OMNIKEY 5422 — contactless"));
    // A single-interface reader keeps no interface suffix.
    EXPECT_EQ(out.at(2), QStringLiteral("Gemalto PC Twin"));
}

// A lone reader with no contactless sibling gets no "— contact" suffix; a lone
// contactless reader is still marked (a meaningful attribute).
TEST(SmartCardHandler, ReaderDisplayLabelsLoneReaders)
{
    const QStringList out = SmartCardHandler::readerDisplayLabels(
        {QStringLiteral("HID Global OMNIKEY 5422 Smartcard Reader [OMNIKEY 5422 Smartcard Reader] (SER) 01 00")});
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out.at(0), QStringLiteral("OMNIKEY 5422"));

    const QStringList cl = SmartCardHandler::readerDisplayLabels(
        {QStringLiteral("HID Global OMNIKEY 5422 Smartcard Reader [OMNIKEY 5422CL Smartcard Reader] (SER) 00 00")});
    ASSERT_EQ(cl.size(), 1);
    EXPECT_EQ(cl.at(0), QStringLiteral("OMNIKEY 5422 — contactless"));
}

// Never empty, never colliding: two identical devices get disambiguated, and an
// unparseable name falls back to itself.
TEST(SmartCardHandler, ReaderDisplayLabelsUniqueAndSafe)
{
    // Two identical models on different physical readers (distinct serials): the
    // labels must not collide.
    const QStringList twins = SmartCardHandler::readerDisplayLabels({
        QStringLiteral("Gemalto PC Twin Reader (AAAA1111) 00 00"),
        QStringLiteral("Gemalto PC Twin Reader (BBBB2222) 01 00"),
    });
    ASSERT_EQ(twins.size(), 2);
    EXPECT_NE(twins.at(0), twins.at(1)) << "identical models must be disambiguated";
    EXPECT_TRUE(twins.at(0).startsWith(QStringLiteral("Gemalto PC Twin")));

    // Two same-model units REPORTING THE SAME serial: the serial-tail form
    // collides for the second unit, and with that shared tail being "2" the
    // 1-based index fallback reproduces the very label it flees — the guard
    // must keep re-validating until a genuinely unique label comes out.
    const QStringList sameSerial = SmartCardHandler::readerDisplayLabels({
        QStringLiteral("Alpha (2) 00 00"),
        QStringLiteral("Alpha (2) 01 00"),
    });
    ASSERT_EQ(sameSerial.size(), 2);
    EXPECT_NE(sameSerial.at(0), sameSerial.at(1))
        << "the disambiguated fallback must itself be re-checked for uniqueness";

    // A name that yields nothing after cleaning falls back to the raw string.
    const QStringList odd = SmartCardHandler::readerDisplayLabels({QStringLiteral("Reader")});
    ASSERT_EQ(odd.size(), 1);
    EXPECT_FALSE(odd.at(0).isEmpty());

    EXPECT_TRUE(SmartCardHandler::readerDisplayLabels({}).isEmpty());
}

TEST(SmartCardHandler, HybridCardClassifiesHybrid)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData | Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::Hybrid); }));
    EXPECT_EQ(handler->readerName(), QStringLiteral("Fake"));
}

TEST(SmartCardHandler, PkiOnlyCard)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));
}

TEST(SmartCardHandler, IdentityOnlyCard)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));
}

TEST(SmartCardHandler, NoCardWhenReaderEmpty)
{
    FakeAgent::Config cfg;
    cfg.hasCard = false;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
}

// An empty reader (no card at all) is NoCard AND NOT cardDetected: the QML
// must say "insert a card", not "detecting a card".
TEST(SmartCardHandler, EmptyReaderIsNotCardDetected)
{
    FakeAgent::Config cfg;
    cfg.hasCard = false;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
    EXPECT_FALSE(handler->cardDetected());
}

// The deferred-publish window / dropped InterfacesAdded: the reader reports a
// card but no resolvable Card1 exists yet. The handler stays NoCard but flags
// cardDetected so the QML says "detecting a card" instead of "insert a card".
// A manual requestRefresh() then re-runs discovery and resolves the card.
TEST(SmartCardHandler, CardDetectedDuringDeferredPublishThenRefreshResolves)
{
    FakeAgent::Config cfg;
    cfg.hasCard = false; // discovered empty
    cfg.capabilities = Client::Cap::IdentityData | Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
    ASSERT_FALSE(handler->cardDetected());

    QSignalSpy detectedSpy(handler.get(), &SmartCardHandler::cardDetectedChanged);

    // The agent claims a card on the reader but never announces the Card1.
    h.exportCardSilently();
    ASSERT_TRUE(waitFor([&]() { return handler->cardDetected(); }))
        << "a seated-but-unresolved card must flip cardDetected, not leave it 'no card'";
    // Still NoCard (nothing resolvable to classify), but now "detecting".
    EXPECT_EQ(handler->state(), static_cast<int>(State::NoCard));
    EXPECT_GE(detectedSpy.count(), 1);

    // The refresh affordance re-runs discovery, which resolves the card.
    handler->requestRefresh();
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::Hybrid); }))
        << "requestRefresh must recover the card via GetManagedObjects";
    EXPECT_FALSE(handler->cardDetected());
}

// The reported field scenario: a second reader is hot-plugged but the client's
// live roster misses its InterfacesAdded, so the config chooser
// (availableReaderNames) never lists it. requestRefresh() re-runs discovery and
// the reader appears — no re-seat, no plasmashell restart.
TEST(SmartCardHandler, RefreshRecoversHotPluggedReaderMissedByLiveRoster)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData | Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::Hybrid); }));
    ASSERT_EQ(handler->availableReaderNames().size(), 1);
    ASSERT_EQ(handler->availableReaderNames().constFirst(), QStringLiteral("Fake"));

    // A second reader is registered on the agent, but its InterfacesAdded is
    // dropped: the client never hears about it.
    h.registerSecondReaderSilently();
    // Give any (absent) signal a chance — the roster must STILL be just "Fake".
    QSignalSpy rosterSpy(handler.get(), &SmartCardHandler::availableReaderNamesChanged);
    EXPECT_EQ(handler->availableReaderNames().size(), 1) << "a dropped InterfacesAdded must not silently appear";

    // The refresh affordance re-runs GetManagedObjects, which enumerates BOTH
    // readers → the second reader becomes selectable in the config chooser.
    handler->requestRefresh();
    ASSERT_TRUE(waitFor([&]() { return handler->availableReaderNames().size() == 2; }))
        << "requestRefresh must recover the reader the live roster missed";
    EXPECT_TRUE(handler->availableReaderNames().contains(QStringLiteral("Fake2")));
    EXPECT_GE(rosterSpy.count(), 1);
}

TEST(SmartCardHandler, NoCardToHybridOnLiveInsert)
{
    FakeAgent::Config cfg;
    cfg.hasCard = false;
    cfg.capabilities = Client::Cap::IdentityData | Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));

    h.setCardPresent(true);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::Hybrid); }));
}

TEST(SmartCardHandler, PreAuthRequiredWhenPreReadAuthNotNone)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    // Identity has not been read yet, so a pre-read-locked card surfaces the
    // "authenticate at the secure prompt" state, not its capability surface.
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));
}

TEST(SmartCardHandler, PreAuthRequiredForMrz)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Mrz");
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    // A BAC-MRZ pre-read card (e.g. an ICAO eMRTD) gates on the secure prompt
    // just like Can does — any non-None PreReadAuthMethod surfaces
    // PreAuthRequired until the identity has been read.
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));
}

// A card announcing an unlock method this build has no name for must read as
// "an unlock is required", never as "no unlock needed". The decoder in the
// client library maps every unrecognised token onto None, so deciding the
// requirement off the decoded method would classify such a card by its
// capabilities alone and then AUTO-READ it — unlocking nothing, prompting for
// nothing, and handing the holder's identity to a widget that never asked the
// card to be unlocked. Forward-compatibility only: no shipping agent emits such
// a token.
//
// The two preconditions are what make this measure the new branch rather than
// pass by accident:
//   - Cap::IdentityData, so the fallback classification is IdentityOnly — a
//     state ensureFreeRead() will actually act on (its own gate admits only
//     IdentityOnly / Hybrid);
//   - setViewActive(true), so the free read is genuinely armed.
// Without either, "no read happened" is true no matter which branch runs.
TEST(SmartCardHandler, UnrecognisedPreReadAuthTokenRequiresUnlockAndBlocksTheFreeRead)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData; // precondition: a free-read-eligible surface
    // A non-empty token outside this build's vocabulary ("None"/"Can"/"Mrz").
    cfg.preReadAuth = QStringLiteral("FutureUnlockMethod");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok — so a read, if one were issued, would SUCCEED and be visible
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    // Deliberately EXPECT, not ASSERT: if the classification regresses, the
    // free-read assertions below are the ones that show what it COSTS (the card
    // gets read with no unlock), and a fatal assertion here would hide them.
    EXPECT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }))
        << "an unlock method this build cannot name must still gate the card behind the unlock affordance";

    // Precondition: the free read is armed. With the requirement read off the
    // decoded method instead, this card would classify IdentityOnly and the
    // popup-open below would read it without any unlock.
    handler->setViewActive(true);
    handler->ensureFreeRead();
    waitFor([]() { return false; }, 80);

    EXPECT_EQ(handler->state(), static_cast<int>(State::PreAuthRequired));
    EXPECT_EQ(h.operationCount(), 0) << "no card read may be issued for a card whose unlock this build cannot name";
    EXPECT_FALSE(handler->hasIdentity());
    EXPECT_FALSE(handler->hasCardPhoto());
}

TEST(SmartCardHandler, HybridToNoCardOnLiveRemove)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData | Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::Hybrid); }));

    h.setCardPresent(false);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
}

// --- operation phase surface --------------------------------------

TEST(SmartCardHandler, OperationPhaseTracksActiveReadOp)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.announceConsentPhase = true; // the read op emits AwaitingConsent(2) at 50 ms
    cfg.operationDelayMs = 120;      // complete AFTER the phase so it is observable
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));

    handler->readIdentity();
    ASSERT_TRUE(waitFor(
        [&]() { return handler->operationPhase() == static_cast<int>(Client::OperationPhase::AwaitingConsent); }));
}

TEST(SmartCardHandler, OperationPhaseLabelNonEmptyForEveryPhase)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    auto handler = makeHandler(h);
    for (int p = 0; p <= 7; ++p) { // Created..Done
        EXPECT_FALSE(handler->operationPhaseLabel(p).isEmpty()) << "phase " << p;
    }
}

TEST(SmartCardHandler, PreAuthCardAdvancesAfterSuccessfulRead)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));

    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));
}

// A reader can appear on the bus ALREADY holding a seated card. The real agent
// announces this as a three-step sequence (PresenceModel): the reader arrives
// empty (HasCard=false / Card="/"), then the card's InterfacesAdded, then a
// Reader1 PropertiesChanged flipping HasCard=true AND Card=<cardPath> together.
// The handler must converge to the card's capability surface, not stick at
// NoCard. (Reader0 is configured empty so the arrived card is the active one.)
TEST(SmartCardHandler, ReaderArrivesAlreadyHoldingCardConverges)
{
    FakeAgent::Config cfg;
    cfg.hasCard = false; // reader/0 stays empty; the active card arrives on reader/1
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));

    // (a) empty reader arrives — still NoCard (no reader holds a card yet).
    h.emitReaderArrivesEmpty();
    waitFor([]() { return false; }, 50); // let the client register reader/1's match rule
    EXPECT_EQ(handler->state(), static_cast<int>(State::NoCard));

    // (b) the card's interface appears (reader still reports Card="/").
    h.emitArrivedReaderCardAdded(Client::Cap::IdentityData | Client::Cap::Pki);
    waitFor([]() { return false; }, 50); // let the client register the card match rule

    // (c) the reader flips HasCard=true / Card=<cardPath>: now it converges to the
    //     card's capability surface (Hybrid), not stuck at NoCard.
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::Hybrid); }));
    EXPECT_EQ(handler->readerName(), QStringLiteral("Fake2"));
}

// a readIdentity() op whose Finished fires BEFORE SmartCardHandler
// connects (raceResultBeforeReturn — recovered in the AgentOperation ctor). The
// terminal emit is queued, so onOperationFinished still runs and the handler
// transitions OUT of PreAuthRequired instead of hanging on a missed signal.
// Identity1 now exposes GetResult, so the raced/lost Identity Result is
// RECOVERED via the late-subscriber pull: the handler receives finished(Ok)
// with the field map and converges to the card's IdentityOnly surface (no loud
// CommunicationError). The load-bearing point remains that the handler RECEIVES
// finished and transitions; the recovery makes that terminal a success.
TEST(SmartCardHandler, OperationFinishedBeforeSubscribeStillTransitions)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.raceResultBeforeReturn = true; // finish before readIdentity() returns the path
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));

    handler->readIdentity();
    // Without the queued emit the handler would never see finished() (it fired in
    // the op ctor before the connect) and would stay stuck at PreAuthRequired.
    ASSERT_TRUE(waitFor([&]() { return handler->state() != static_cast<int>(State::PreAuthRequired); }))
        << "a Finished that fired before the handler subscribed must still reach it (queued emit)";
    EXPECT_EQ(handler->state(), static_cast<int>(State::IdentityOnly))
        << "the raced Identity Result is recovered via Identity1.GetResult, so the read succeeds";
    EXPECT_TRUE(handler->errorMessage().isEmpty());
}

TEST(SmartCardHandler, ErrorOnOperationFinishedNonOk)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 2;                                                   // Error
    cfg.finalErrorCode = static_cast<uint>(Client::ErrorCode::AuthFailed); // wrong CAN
    cfg.suppressResult = true;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));

    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::Error); }));
    EXPECT_FALSE(handler->errorMessage().isEmpty());
}

// The composed copy rule's FLOOR: a read that finishes non-Ok must never leave
// the banner blank, whatever its outcome carried. This fixture is the worst case
// the rule exists for — a wire cancel, whose terminal carries no error code, no
// call classification and no agent message at all (the fake sends msgKey /
// msgFallback only for an Error terminal) — so every axis the copy could be
// drawn from is empty and the rule's own localized floor is the only thing left.
//
// Resolving this through the error code alone renders NOTHING, and ErrorState's
// banner comes up empty in a state whose whole job is to say what went wrong.
// That is what this pins.
TEST(SmartCardHandler, CancelledReadStillCarriesNonEmptyErrorCopy)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can"); // an EXPLICIT read — no free read to race
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 1; // Cancelled; errorCode stays None and no message is sent
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));

    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::Error); }));
    EXPECT_FALSE(handler->errorMessage().isEmpty())
        << "a cancelled read must still draw copy from the shared rule's floor, never a blank banner";
    EXPECT_FALSE(handler->busy()) << "the terminal must release the busy latch on a cancel too";
}

// On a successful identity read the handler ALSO fires a best-effort GetPhoto;
// when the card yields a photo the handler decodes it into its CardPhotoStore
// and flips hasCardPhoto true (driving the image:// URL the QML Image binds to).
// Async/signal-driven: no nested event loop on the GUI thread.
TEST(SmartCardHandler, ReadIdentityCapturesCardPhoto)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok
    cfg.photoBytes = tinyPngBytes();
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));

    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));

    // The photo arrives on a SECOND agent op after the identity op finishes, so
    // wait on the flag rather than coupling it to the identity transition.
    ASSERT_TRUE(waitFor([&]() { return handler->hasCardPhoto(); }))
        << "a successful identity read must drive a best-effort GetPhoto and store the decoded image";
    EXPECT_FALSE(handler->cardPhotoUrl().isEmpty());

    // The decoded image carries the scripted bytes: a valid 2x2 image, stored
    // under THIS handler's own slot (per-widget isolation).
    const QImage stored = handler->photoStore()->image(handler->photoSlot());
    EXPECT_FALSE(stored.isNull());
    EXPECT_EQ(stored.size(), QSize(2, 2));
}

// Security-relevant PII invariant: the holder photo is scrubbed from the
// CardPhotoStore the instant the card is removed. Script a photo, read it, then
// pull the card and assert the store is cleared and the QML URL emptied.
TEST(SmartCardHandler, CardRemovalClearsCapturedPhoto)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok
    cfg.photoBytes = tinyPngBytes();
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));

    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));
    ASSERT_TRUE(waitFor([&]() { return handler->hasCardPhoto(); }));
    ASSERT_TRUE(handler->photoStore()->hasImage(handler->photoSlot()));

    // Pull the card: refresh() → bindCard(nullptr) → clearPhoto() must scrub the PII.
    h.setCardPresent(false);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
    EXPECT_FALSE(handler->hasCardPhoto());
    EXPECT_TRUE(handler->cardPhotoUrl().isEmpty());
    EXPECT_FALSE(handler->photoStore()->hasImage(handler->photoSlot()))
        << "card removal must scrub the holder photo from the store";
}

// A card with no photo is NOT an error: the read still succeeds, hasCardPhoto
// stays false, no diagnostic is surfaced, and the QML keeps its graceful
// placeholder. There are THREE distinct no-photo shapes the best-effort GetPhoto
// must absorb silently; the cases below exercise each:
//   - empty-MAP:    Photo1.Result is a genuinely empty a{sh} (no entries);
//   - empty-FD:     Result carries a "personal:photo" entry whose memfd is empty;
//   - lost-Result:  the op finishes Ok but no typed Result ever arrives (Photo
//                   has no GetResult, so AgentOperation::finalizeTerminal converts
//                   the silent-empty Ok into Error/CommunicationError).
// All three must leave hasCardPhoto=false with NO error surfaced (best-effort).

// (1) empty-MAP — the agent reports a present card with no photo field at all.
TEST(SmartCardHandler, ReadIdentityWithEmptyPhotoMapLeavesNoPhotoAndNoError)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0;      // Ok
    cfg.photoEmptyMap = true; // GetPhoto emits a genuinely empty PhotoMap

    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));

    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));

    // Let the best-effort GetPhoto round-trip complete (it finishes with an empty
    // PhotoMap → the empty-map guard), then assert it left no photo and no error.
    waitFor([]() { return false; }, 60);
    EXPECT_FALSE(handler->hasCardPhoto());
    EXPECT_TRUE(handler->cardPhotoUrl().isEmpty());
    EXPECT_TRUE(handler->errorMessage().isEmpty());
    EXPECT_EQ(handler->state(), static_cast<int>(State::IdentityOnly));
}

// (2) empty-FD — the Result carries a "personal:photo" entry, but its sealed
// memfd is empty (readBoundedPayload yields no bytes → the empty-fd guard).
TEST(SmartCardHandler, ReadIdentityWithEmptyPhotoFdLeavesNoPhotoAndNoError)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0;           // Ok
    cfg.photoBytes = QByteArray(); // entry present, but its memfd is empty
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));

    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));

    // Let the best-effort GetPhoto round-trip complete (Result carries an empty
    // memfd → the empty-fd guard), then assert it left no photo and no error.
    waitFor([]() { return false; }, 60);
    EXPECT_FALSE(handler->hasCardPhoto());
    EXPECT_TRUE(handler->cardPhotoUrl().isEmpty());
    EXPECT_TRUE(handler->errorMessage().isEmpty());
    EXPECT_EQ(handler->state(), static_cast<int>(State::IdentityOnly));
}

// (3) lost-Result — the GetPhoto op finishes Ok but its typed Result never
// arrives. Photo has no GetResult, so AgentOperation::finalizeTerminal converts
// the silent-empty Ok into Error/CommunicationError on the op; the handler's
// onPhotoFinished treats any non-Ok terminal as best-effort (no photo, no error
// surfaced) — the production lost-Result path, NOT just the empty-payload guards.
TEST(SmartCardHandler, ReadIdentityWithLostPhotoResultLeavesNoPhotoAndNoError)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0;            // Ok terminal...
    cfg.photoSuppressResult = true; // ...but the GetPhoto typed Result is never emitted
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));

    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));

    // Let the best-effort GetPhoto op terminate (Ok-without-Result →
    // CommunicationError), then assert the handler absorbed it: no photo, no error.
    waitFor([]() { return false; }, 60);
    EXPECT_FALSE(handler->hasCardPhoto());
    EXPECT_TRUE(handler->cardPhotoUrl().isEmpty());
    EXPECT_TRUE(handler->errorMessage().isEmpty());
    EXPECT_EQ(handler->state(), static_cast<int>(State::IdentityOnly));
}

// The agent is not on the bus: the plasmoid must reach AgentUnavailable
// (client-availability state), never hang, never NoCard-masquerade.
//
// The client binds itself to the agent's real well-known name, so "absent" is
// modelled by NOT claiming that name: this Harness deliberately keeps only its
// per-test unique name (BusNames::UniqueOnly, the default), so a real fake is
// running on the bus and the name the handler looks for is owned by nobody.
TEST(SmartCardHandler, AgentUnavailableWhenServiceAbsent)
{
    FakeAgent::Config cfg;
    Harness unclaimed(cfg, BusNames::UniqueOnly);

    auto handler = std::make_unique<SmartCardHandler>(std::make_shared<Client::AgentClient>());
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::AgentUnavailable); }));
}

// Pure install probe: a data dir carrying the agent's D-Bus activation file (or
// its systemd user unit) reads as "installed"; an empty dir reads as "not
// installed" — the two cases the AgentUnavailable state must distinguish.
TEST(SmartCardHandler, AgentServiceInstalledInDetectsActivationFileAndUnit)
{
    QTemporaryDir dbusDir;
    ASSERT_TRUE(dbusDir.isValid());
    ASSERT_TRUE(QDir(dbusDir.path()).mkpath(QStringLiteral("dbus-1/services")));
    {
        QFile svc(dbusDir.path() + QStringLiteral("/dbus-1/services/org.librescrs.Agent.service"));
        ASSERT_TRUE(svc.open(QIODevice::WriteOnly));
        svc.write("[D-BUS Service]\nName=org.librescrs.Agent\n");
    }
    EXPECT_TRUE(SmartCardHandler::agentServiceInstalledIn({dbusDir.path()}));

    QTemporaryDir unitDir;
    ASSERT_TRUE(QDir(unitDir.path()).mkpath(QStringLiteral("systemd/user")));
    {
        QFile unit(unitDir.path() + QStringLiteral("/systemd/user/librescrs-agent.service"));
        ASSERT_TRUE(unit.open(QIODevice::WriteOnly));
        unit.write("[Unit]\n");
    }
    EXPECT_TRUE(SmartCardHandler::agentServiceInstalledIn({unitDir.path()}));

    QTemporaryDir emptyDir;
    EXPECT_FALSE(SmartCardHandler::agentServiceInstalledIn({emptyDir.path()}));
}

// A present card whose Capabilities are empty (no plugin matched) resolves to the
// calm UnknownCard state through the existing classifyActiveCard() path.
TEST(SmartCardHandler, UnknownCardWhenCapabilitiesEmpty)
{
    FakeAgent::Config cfg;
    cfg.hasCard = true;
    cfg.capabilities = Client::Cap::None; // no plugin matched
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::UnknownCard); }));
    EXPECT_EQ(handler->readerName(), QStringLiteral("Fake"));
}

// A successful identity read is exposed to QML as a flat field model plus a
// curated summary, reusing AgentOperation::identityResult() + the shared
// flattenIdentityFields helper. The FakeAgent emits one field:
// personal/given_name = "Ana" (labelFallback "Given name").
TEST(SmartCardHandler, ReadIdentityPopulatesQmlModel)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));
    EXPECT_FALSE(handler->hasIdentity());
    EXPECT_TRUE(handler->identityFields().isEmpty());

    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->hasIdentity(); }));

    const QVariantList fields = handler->identityFields();
    ASSERT_EQ(fields.size(), 1);
    const QVariantMap row = fields.first().toMap();
    EXPECT_EQ(row.value(QStringLiteral("fieldKey")).toString(), QStringLiteral("given_name"));
    EXPECT_EQ(row.value(QStringLiteral("value")).toString(), QStringLiteral("Ana"));
    EXPECT_EQ(row.value(QStringLiteral("label")).toString(), QStringLiteral("Given name"));

    const QVariantList summary = handler->identitySummary();
    ASSERT_FALSE(summary.isEmpty());
    EXPECT_EQ(summary.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("Ana"));
}

namespace {
QVariant summaryRow(const char* group, const char* key, const char* value)
{
    QVariantMap m;
    m.insert(QStringLiteral("groupKey"), QString::fromLatin1(group));
    m.insert(QStringLiteral("fieldKey"), QString::fromLatin1(key));
    m.insert(QStringLiteral("label"), QString::fromLatin1(key));
    m.insert(QStringLiteral("value"), QString::fromUtf8(value));
    return QVariant(m);
}
} // namespace

// DG1 (the machine-verified MRZ name components) is the primary identity
// source: a supplementary full_name row (eMRTD DG11 — free-form, often
// national-script, on some documents not a person name at all) must never
// precede or displace the DG1 surname/given-names rows in the curated
// summary. It participates only when no MRZ-derived name component exists.
TEST(SmartCardHandlerSummary, PrefersDg1NameOverDg11FullName)
{
    const QVariantList fields = {
        summaryRow("additional", "full_name", "NOT-A-NAME"),
        summaryRow("personal", "surname", "SPECIMENSURNAME"),
        summaryRow("personal", "given_names", "SPECIMEN"),
        summaryRow("document", "document_number", "T00000000"),
    };
    const QVariantList summary = SmartCardHandler::curateIdentitySummary(fields);
    ASSERT_FALSE(summary.isEmpty());
    EXPECT_EQ(summary.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("SPECIMENSURNAME"))
        << "the DG1 surname must lead the summary";
    for (const QVariant& entry : summary) {
        EXPECT_NE(entry.toMap().value(QStringLiteral("value")).toString(), QStringLiteral("NOT-A-NAME"))
            << "DG11 full_name must not appear while DG1 name components exist";
    }
}

// Cards that expose ONLY a combined full name (no MRZ name split) keep it as
// the summary headline — the fallback role of full_name is preserved.
TEST(SmartCardHandlerSummary, FallsBackToFullNameWithoutDg1Name)
{
    const QVariantList fields = {
        summaryRow("additional", "full_name", "SPECIMEN FULLNAME"),
        summaryRow("document", "document_number", "T00000000"),
    };
    const QVariantList summary = SmartCardHandler::curateIdentitySummary(fields);
    ASSERT_FALSE(summary.isEmpty());
    EXPECT_EQ(summary.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("SPECIMEN FULLNAME"));
}

// Every MRZ name-component key suppresses the full_name headline on its own —
// pins the complete suppressor set, not just the eMRTD surname/given pair.
TEST(SmartCardHandlerSummary, EachNameComponentSuppressesFullName)
{
    const char* nameKeys[] = {"surname", "family_name", "given_name", "given_names"};
    for (const char* key : nameKeys) {
        const QVariantList fields = {
            summaryRow("additional", "full_name", "NOT-A-NAME"),
            summaryRow("personal", key, "SPECIMEN"),
        };
        const QVariantList summary = SmartCardHandler::curateIdentitySummary(fields);
        ASSERT_FALSE(summary.isEmpty()) << key;
        for (const QVariant& entry : summary) {
            EXPECT_NE(entry.toMap().value(QStringLiteral("value")).toString(), QStringLiteral("NOT-A-NAME")) << key;
        }
    }
}

// An empty-valued row never headlines the summary either: with one empty and
// one populated MRZ name component, the populated one leads (the empty row is
// skipped by selection, full_name stays suppressed by the populated one).
TEST(SmartCardHandlerSummary, EmptyValuedRowNeverHeadlines)
{
    const QVariantList fields = {
        summaryRow("additional", "full_name", "NOT-A-NAME"),
        summaryRow("personal", "surname", ""),
        summaryRow("personal", "given_names", "SPECIMEN"),
    };
    const QVariantList summary = SmartCardHandler::curateIdentitySummary(fields);
    ASSERT_FALSE(summary.isEmpty());
    EXPECT_EQ(summary.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("SPECIMEN"));
    for (const QVariant& entry : summary) {
        EXPECT_NE(entry.toMap().value(QStringLiteral("value")).toString(), QStringLiteral("NOT-A-NAME"));
    }
}

// An EMPTY-valued name component is not a suppressor: consumers that feed
// unfiltered flattened rows (empty values retained) still get the full_name
// headline when the MRZ name fields carry no text.
TEST(SmartCardHandlerSummary, EmptyNameComponentDoesNotSuppressFullName)
{
    const QVariantList fields = {
        summaryRow("additional", "full_name", "SPECIMEN FULLNAME"),
        summaryRow("personal", "surname", ""),
        summaryRow("document", "document_number", "T00000000"),
    };
    const QVariantList summary = SmartCardHandler::curateIdentitySummary(fields);
    ASSERT_FALSE(summary.isEmpty());
    EXPECT_EQ(summary.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("SPECIMEN FULLNAME"));
}

// The summary and the detail list PARTITION the flattened model. The popup
// renders both — the summary always, the detail list once the expander is
// checked — so a detail list that is the whole model prints every summarised
// row a SECOND time the moment the user expands ("Card Type" twice, and the
// name, and the document number). Together they must contain every row exactly
// once.
TEST(SmartCardHandlerSummary, SummaryAndDetailsPartitionTheFields)
{
    const QVariantList fields = {
        summaryRow("personal", "surname", "SPECIMENSURNAME"),
        summaryRow("personal", "given_names", "SPECIMEN"),
        summaryRow("personal", "address", "Neka ulica 1"),
        summaryRow("document", "document_number", "T00000000"),
        summaryRow("meta", "card_type", "eID"),
        summaryRow("meta", "issuing_authority", "MUP"),
    };
    const QVariantList summary = SmartCardHandler::curateIdentitySummary(fields);
    const QVariantList details = SmartCardHandler::curateIdentityDetails(fields, summary);

    // Exhaustive, not spot-checked: every row lands in exactly one of the two.
    ASSERT_EQ(summary.size() + details.size(), fields.size())
        << "summary " << summary.size() << " + details " << details.size() << " != fields " << fields.size();
    const auto identityOf = [](const QVariant& entry) {
        const QVariantMap row = entry.toMap();
        return row.value(QStringLiteral("groupKey")).toString() + QLatin1Char('/') +
               row.value(QStringLiteral("fieldKey")).toString();
    };
    QStringList seen;
    for (const QVariant& entry : summary) {
        seen << identityOf(entry);
    }
    for (const QVariant& entry : details) {
        seen << identityOf(entry);
    }
    QStringList expected;
    for (const QVariant& entry : fields) {
        expected << identityOf(entry);
    }
    seen.sort();
    expected.sort();
    EXPECT_EQ(seen, expected);

    // The concrete symptom, named: the card type is summarised, so it must NOT
    // also be in the rows the expander reveals.
    for (const QVariant& entry : details) {
        EXPECT_NE(identityOf(entry), QStringLiteral("meta/card_type"))
            << "card_type is in the summary; the expander must not repeat it";
    }
    EXPECT_FALSE(details.isEmpty()) << "the un-summarised rows (address, issuing authority) must survive";
}

// A field key that appears under TWO groups is a real shape (the identity
// plugins emit one), and the summary takes only ONE of the two. Matching rows
// by their rendered text would drop both; matching by (groupKey, fieldKey)
// keeps the copy the summary did not take.
TEST(SmartCardHandlerSummary, DetailsKeepTheSecondGroupsCopyOfASummarisedKey)
{
    const QVariantList fields = {
        summaryRow("meta", "card_type", "eID"),
        summaryRow("extra", "card_type", "eID"),
        summaryRow("personal", "surname", "SPECIMENSURNAME"),
    };
    const QVariantList summary = SmartCardHandler::curateIdentitySummary(fields);
    const QVariantList details = SmartCardHandler::curateIdentityDetails(fields, summary);
    ASSERT_EQ(summary.size() + details.size(), fields.size());

    int cardTypeInDetails = 0;
    for (const QVariant& entry : details) {
        if (entry.toMap().value(QStringLiteral("fieldKey")).toString() == QStringLiteral("card_type")) {
            ++cardTypeInDetails;
        }
    }
    EXPECT_EQ(cardTypeInDetails, 1) << "exactly the group copy the summary did not take";
}

// The lazy card-I/O invariant: a Can card issues ZERO agent operations on
// popup-open / classification. Only the explicit readIdentity() reads.
TEST(SmartCardHandler, CanCardIssuesNoImplicitCardIo)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData | Client::Cap::Pki;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));

    // Pump the loop as a popup-open + expand would — INCLUDING the popup-open
    // hook itself (main.qml binds viewActive to the popup's expanded, which
    // drives ensureFreeRead internally): for a Can card it must remain a
    // no-op, never a surprise CAN prompt.
    handler->setViewActive(true);
    handler->ensureFreeRead();
    waitFor([]() { return false; }, 80);
    EXPECT_EQ(h.operationCount(), 0) << "classification / popup-open must issue zero agent operations";
    EXPECT_FALSE(handler->hasIdentity());
    EXPECT_FALSE(handler->hasCardPhoto());

    // Only the explicit Unlock/Read action reads.
    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->hasIdentity(); }));
    EXPECT_GE(h.operationCount(), 1);
}

// The identity model is scrubbed the instant the card is removed (PII invariant).
TEST(SmartCardHandler, CardRemovalClearsIdentityModel)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));
    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->hasIdentity(); }));

    h.setCardPresent(false);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
    EXPECT_FALSE(handler->hasIdentity());
    EXPECT_TRUE(handler->identityFields().isEmpty());
    EXPECT_TRUE(handler->identitySummary().isEmpty());
}

// A None-preauth identity card (the contact PKS / RS-eID demo card: no CAN, no
// MRZ) has NO gating secret, so its summary populates via the "free
// read" — but ON FIRST VIEW (ensureFreeRead, driven by popup-open in QML), NOT
// at insertion: classification alone must issue ZERO agent operations so
// identity PII never sits in plasmashell for a popup nobody opened.
// Repeat views are no-ops.
TEST(SmartCardHandler, NoneIdentityCardFreeReadsOnFirstViewOnly)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData; // preReadAuth defaults to "None"
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));

    // Insertion/classification alone reads NOTHING.
    waitFor([]() { return false; }, 60);
    EXPECT_EQ(h.operationCount(), 0) << "a free-read card must not be read merely on insertion";
    EXPECT_FALSE(handler->hasIdentity());

    // First view: the popup opens -> QML calls ensureFreeRead() -> summary
    // populates with zero clicks and zero prompts.
    handler->ensureFreeRead();
    ASSERT_TRUE(waitFor([&]() { return handler->hasIdentity(); }));
    EXPECT_EQ(handler->state(), static_cast<int>(State::IdentityOnly));
    const int opsAfterFirstView = h.operationCount();
    EXPECT_GE(opsAfterFirstView, 1);
    EXPECT_FALSE(handler->identitySummary().isEmpty());

    // Repeat views are no-ops (the identity is already in hand).
    handler->ensureFreeRead();
    waitFor([]() { return false; }, 60);
    EXPECT_EQ(h.operationCount(), opsAfterFirstView);
}

// Regression (a): with the popup OPEN (viewActive), switching the detail
// between two SAME-classification cards via the master-detail chips fires NO
// stateChanged (IdentityOnly -> IdentityOnly) — yet the rebind discarded the
// shown identity, so the free read must re-run for the newly selected card or
// the open popup shows a permanently blank identity body (never-blank on
// the headline interaction). The trigger therefore hangs off the card
// (re)classification itself, not off state edges.
TEST(SmartCardHandler, FreeReadFollowsChipSwitchBetweenSameStateCards)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData; // reader/0 "Fake": IdentityOnly, no pre-auth
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));

    // A second reader "Fake2" holding a card of the SAME classification.
    h.emitReaderArrivesEmpty();
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderCardAdded(Client::Cap::IdentityData);
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return handler->readersWithCards().size() == 2; }));

    // The popup opens: the free read populates card A ("Fake").
    handler->setViewActive(true);
    ASSERT_TRUE(waitFor([&]() { return handler->hasIdentity(); }));
    EXPECT_EQ(handler->readerName(), QStringLiteral("Fake"));
    waitFor([]() { return false; }, 60); // let the best-effort GetPhoto settle
    const int opsAfterA = h.operationCount();

    // Chip click on "Fake2" while the popup stays open: same classification, no
    // stateChanged — the identity body must still re-populate for card B.
    handler->selectReader(QStringLiteral("Fake2"));
    ASSERT_TRUE(waitFor([&]() { return handler->readerName() == QStringLiteral("Fake2") && handler->hasIdentity(); }))
        << "switching between two same-classification cards must re-issue the free read";
    EXPECT_GT(h.operationCount(), opsAfterA);
    EXPECT_EQ(handler->state(), static_cast<int>(State::IdentityOnly));
}

// Regression (b): a chip switch that abandons an in-flight identity read must
// cancel it AGENT-SIDE (fire-and-forget Cancel), matching the credentials
// window's re-target rule — a secure prompt raised by the abandoned read must
// be dismissed, never orphaned. Client-side disconnect alone leaves the agent
// op running.
TEST(SmartCardHandler, ChipSwitchMidIdentityReadCancelsAbandonedAgentSideOp)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData; // reader/0 "Fake": IdentityOnly, no pre-auth
    cfg.operationDelayMs = 400;                   // hold the identity read in flight across the switch
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));

    // A second reader "Fake2" holding a card of the SAME classification.
    h.emitReaderArrivesEmpty();
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderCardAdded(Client::Cap::IdentityData);
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return handler->readersWithCards().size() == 2; }));

    // The popup opens: card A's free read is now in flight (held by the delay).
    handler->setViewActive(true);
    ASSERT_TRUE(waitFor([&]() { return handler->busy(); }));
    ASSERT_EQ(h.cancelledOperationCount(), 0);

    // Chip click on "Fake2" mid-read: the rebind abandons card A's identity op.
    handler->selectReader(QStringLiteral("Fake2"));
    ASSERT_TRUE(waitFor([&]() { return h.cancelledOperationCount() >= 1; }))
        << "the abandoned in-flight identity read must be cancelled agent-side (prompt dismissal)";
    // The switch target's own read still completes.
    ASSERT_TRUE(waitFor([&]() { return handler->readerName() == QStringLiteral("Fake2") && handler->hasIdentity(); }));
}

// boundReaderPresent distinguishes a bound reader that is physically on the bus
// (present, perhaps card-less) from one that is absent from the roster (pulled,
// or missed) — the seam behind NoCardState's honest "Insert a card" vs
// "Reader not connected" messaging. Auto mode (no binding) is never "present".
TEST(SmartCardHandler, BoundReaderPresentReflectsRoster)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->availableReaderNames().size() == 1; }));

    // Auto mode: no bound reader -> not "present".
    EXPECT_FALSE(handler->boundReaderPresent());

    // Bind to the reader the agent actually reports -> present.
    handler->setBoundReaderName(QStringLiteral("Fake"));
    EXPECT_TRUE(handler->boundReaderPresent());

    // Bind to a reader that is not in the roster (pulled / never there) -> absent.
    handler->setBoundReaderName(QStringLiteral("Ghost Reader"));
    EXPECT_FALSE(handler->boundReaderPresent());
}

// A bound reader that is PRESENT in the roster but card-less keeps the widget
// reader-scoped. On a dual-interface reader the contact and contactless PC/SC
// entries carry the SAME parenthesized group (one shared USB iSerial), so a
// card laid on the contactless pad must NOT capture a widget bound to the
// contact interface: the serial fallback exists only for re-enumeration, i.e.
// when the bound NAME has vanished from the roster.
TEST(SmartCardHandler, BoundEmptyContactReaderIgnoresCardOnContactlessSibling)
{
    const QString contactName = QStringLiteral(
        "HID Global OMNIKEY 5422 Smartcard Reader [OMNIKEY 5422 Smartcard Reader] (IM0O2C00NF10456904) 00 00");
    const QString contactlessName = QStringLiteral(
        "HID Global OMNIKEY 5422 Smartcard Reader [OMNIKEY 5422CL Smartcard Reader] (IM0O2C00NF10456904) 01 00");

    FakeAgent::Config cfg;
    cfg.hasCard = false; // the bound contact slot stays EMPTY throughout
    cfg.readerName = contactName;
    cfg.reader2Name = contactlessName;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->availableReaderNames().size() == 1; }));
    handler->setBoundReaderName(contactName);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
    EXPECT_TRUE(handler->boundReaderPresent());

    // The contactless sibling arrives and resolves a card.
    h.emitReaderArrivesEmpty();
    waitFor([]() { return false; }, 50); // let the client register reader/1's match rule
    h.emitArrivedReaderCardAdded(Client::Cap::Pki);
    waitFor([]() { return false; }, 50); // let the client register the card match rule
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return handler->readersWithCards().size() == 1; }));
    waitFor([]() { return false; }, 80); // let any (wrong) rebind settle

    // Honest reader-scoped waiting — the widget must not flip to the sibling.
    EXPECT_EQ(handler->state(), static_cast<int>(State::NoCard))
        << "a card on the contactless pad must not capture a contact-bound widget";
    EXPECT_EQ(handler->readerName(), contactName);
    EXPECT_TRUE(handler->boundReaderPresent());
    EXPECT_FALSE(handler->cardDetected()) << "the bound contact slot is physically empty";
}

// A widget bound to an ABSENT reader must not jump to a DIFFERENT model that
// shares a generic parenthesized token: with the G&D Star Sign 350 unplugged
// and a 550 (both named "… (ICCD)") present and holding a card, the widget
// shows reader-scoped waiting — never the 550's card.
TEST(SmartCardHandler, AbsentBoundReaderNeverMatchesDifferentModelSharingGenericToken)
{
    FakeAgent::Config cfg;
    cfg.hasCard = true;
    cfg.capabilities = Client::Cap::Pki;
    cfg.readerName = QStringLiteral("Giesecke & Devrient GmbH Star Sign Card Token 550 (ICCD) 00 00");
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));

    handler->setBoundReaderName(QStringLiteral("Giesecke & Devrient GmbH Star Sign Card Token 350 (ICCD) 00 00"));
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }))
        << "a generic '(ICCD)' token must never equate two different models";
    EXPECT_EQ(handler->readerName(), QStringLiteral("Giesecke & Devrient GmbH Star Sign Card Token 350 (ICCD) 00 00"));
    EXPECT_FALSE(handler->boundReaderPresent());
    EXPECT_FALSE(handler->cardDetected());
}

// The case the serial fallback exists for: the SAME physical unit re-enumerated
// under a shifted trailing index (" 02 00" → " 00 00"). The bound (old) name is
// gone from the roster and the re-minted entry carries the same unit serial on
// the same base name — the widget follows it.
TEST(SmartCardHandler, BoundReaderFollowsReEnumeratedIndexShift)
{
    FakeAgent::Config cfg;
    cfg.hasCard = true;
    cfg.capabilities = Client::Cap::Pki;
    cfg.readerName = QStringLiteral("Gemalto PC Twin Reader (69988A87) 00 00");
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));

    // The binding predates the re-enumeration: it remembers the OLD index.
    handler->setBoundReaderName(QStringLiteral("Gemalto PC Twin Reader (69988A87) 02 00"));
    waitFor([]() { return false; }, 80); // let the rebind settle
    EXPECT_EQ(handler->state(), static_cast<int>(State::PkiOnly))
        << "an index-only shift must keep the widget on its physical unit";
    EXPECT_EQ(handler->readerName(), QStringLiteral("Gemalto PC Twin Reader (69988A87) 00 00"));
    EXPECT_TRUE(handler->boundReaderPresent());
}

// cardDetected must answer for the same candidate pickActiveReader() would
// choose. Transient double-enumeration: a STALE, empty twin of the bound unit
// sorts first while the re-enumerated entry physically reports a card that is
// not yet resolvable (the agent's deferred-publish window). The stale twin's
// empty slot must not hide the seated card behind "insert a card".
TEST(SmartCardHandler, CardDetectedFollowsTheCandidateEntryNotTheFirstSerialMatch)
{
    FakeAgent::Config cfg;
    cfg.hasCard = false;
    cfg.readerName = QStringLiteral("Gemalto PC Twin Reader (69988A87) 00 00");  // stale empty twin
    cfg.reader2Name = QStringLiteral("Gemalto PC Twin Reader (69988A87) 01 00"); // re-enumerated unit
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->availableReaderNames().size() == 1; }));
    // Bound under an index no longer present: fallback scope = the unit's entries.
    handler->setBoundReaderName(QStringLiteral("Gemalto PC Twin Reader (69988A87) 02 00"));
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));

    h.emitReaderArrivesEmpty();
    waitFor([]() { return false; }, 50); // let the client register reader/1's match rule
    // The re-enumerated entry reports a seated card with NO resolvable Card1
    // yet (the agent's deferred-publish window).
    h.emitArrivedReaderHasCard();
    EXPECT_TRUE(waitFor([&]() { return handler->cardDetected(); }))
        << "a card seated on the re-enumerated entry must read as detecting, not 'insert a card'";
    EXPECT_EQ(handler->state(), static_cast<int>(State::NoCard));
}

// Regression (b): a same-READER card change while the popup is open (pull
// eID A, insert eID B — reader name AND classification both unchanged
// end-to-end) must re-populate the identity body. onReaderNameChanged could
// never cover this shape; the classification-driven trigger does.
TEST(SmartCardHandler, FreeReadFollowsSameReaderCardSwapWhileViewActive)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData; // no pre-auth: a free-read card
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));

    // Popup open: card A free-reads.
    handler->setViewActive(true);
    ASSERT_TRUE(waitFor([&]() { return handler->hasIdentity(); }));
    waitFor([]() { return false; }, 60); // let the best-effort GetPhoto settle
    const int opsAfterA = h.operationCount();

    // Swap the card in the SAME reader while the popup stays open.
    h.setCardPresent(false);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
    EXPECT_FALSE(handler->hasIdentity()); // the old card's PII was scrubbed
    h.setCardPresent(true);

    // The swapped-in card must free-read without collapse+reopen.
    ASSERT_TRUE(waitFor([&]() { return handler->hasIdentity(); }))
        << "a card swapped into the same reader must re-populate the open popup";
    EXPECT_EQ(handler->state(), static_cast<int>(State::IdentityOnly));
    EXPECT_GT(h.operationCount(), opsAfterA);

    // And a card landing while the popup is CLOSED still reads nothing until
    // the next view (the lazy invariant, now against viewActive).
    handler->setViewActive(false);
    h.setCardPresent(false);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
    waitFor([]() { return false; }, 60);
    const int opsWhileClosed = h.operationCount();
    h.setCardPresent(true);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::IdentityOnly); }));
    waitFor([]() { return false; }, 60);
    EXPECT_EQ(h.operationCount(), opsWhileClosed) << "no view, no read";
    EXPECT_FALSE(handler->hasIdentity());

    // Re-opening the popup is the first view of the swapped-in card.
    handler->setViewActive(true);
    ASSERT_TRUE(waitFor([&]() { return handler->hasIdentity(); }));
}

// curateIdentitySummary is a pure static (no D-Bus, no handler): a card whose
// field keys are OUTSIDE the curated identifying set still gets a non-empty
// summary (the first rows) — the headline is NEVER blank, even on an
// unrecognised key set.
TEST(SmartCardHandlerSummary, FallsBackWhenNoCuratedKeyMatches)
{
    const auto makeRow = [](const QString& key, const QString& label, const QString& value) {
        QVariantMap row;
        row.insert(QStringLiteral("groupKey"), QStringLiteral("insurance"));
        row.insert(QStringLiteral("fieldKey"), key);
        row.insert(QStringLiteral("label"), label);
        row.insert(QStringLiteral("value"), value);
        return QVariant(row);
    };
    QVariantList flat;
    flat << makeRow(QStringLiteral("insurer_name"), QStringLiteral("Insurer"), QStringLiteral("RFZO"));
    flat << makeRow(QStringLiteral("insurer_id"), QStringLiteral("Insurer ID"), QStringLiteral("12345"));

    const QVariantList summary = SmartCardHandler::curateIdentitySummary(flat);
    ASSERT_FALSE(summary.isEmpty()) << "summary must never be empty on an unrecognised key set";
    EXPECT_EQ(summary.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("RFZO"));
}

// The curated branch (above) can never repeat an identity into `summary`: it
// walks a fixed, distinct key list and takes at most one row per key. The
// "never empty" FALLBACK a few lines below `curateIdentitySummary` walks
// `fields` in raw delivery order instead, so it needs its OWN guard against
// repeating an identity — without one, a fallback summary that happened to
// copy two rows sharing a (groupKey, fieldKey) would render the second one
// twice: once verbatim in the summary it was copied into, once again in
// `details` (identity-based subtraction can only erase ONE claim per
// identity, so the second row is never removed from `fields`). This is the
// fallback counterpart of `SummaryAndDetailsPartitionTheFields` above; on the
// shipped path the wire cannot deliver a repeated identity in the first
// place (identity crosses it as a map of maps), so this exercises the
// function directly rather than a scenario a real card can produce.
TEST(SmartCardHandlerSummary, SummaryAndDetailsPartitionTheFallbackRows)
{
    const QVariantList fields = {
        summaryRow("subject", "note", "first"),
        summaryRow("subject", "note", "second"),
        summaryRow("subject", "colour", "blue"),
    };
    const QVariantList summary = SmartCardHandler::curateIdentitySummary(fields);
    const QVariantList details = SmartCardHandler::curateIdentityDetails(fields, summary);

    ASSERT_EQ(summary.size() + details.size(), fields.size())
        << "summary " << summary.size() << " + details " << details.size() << " != fields " << fields.size()
        << " -- a fallback row rendered twice";

    const auto identityOf = [](const QVariant& entry) {
        const QVariantMap row = entry.toMap();
        return row.value(QStringLiteral("groupKey")).toString() + QLatin1Char('/') +
               row.value(QStringLiteral("fieldKey")).toString();
    };
    QStringList seen;
    for (const QVariant& entry : summary) {
        seen << identityOf(entry);
    }
    for (const QVariant& entry : details) {
        seen << identityOf(entry);
    }
    QStringList expected;
    for (const QVariant& entry : fields) {
        expected << identityOf(entry);
    }
    seen.sort();
    expected.sort();
    EXPECT_EQ(seen, expected);
}

// The curated identifying keys are picked in preferred order, ahead of both the
// fallback and non-identifying rows. Uses the REAL RS-eID field keys (surname /
// expiry_date), NOT the guessed generic ones — issuing_authority is not an
// identifying key and must be excluded from the curated summary.
TEST(SmartCardHandlerSummary, PrefersCuratedIdentifyingKeys)
{
    const auto makeRow = [](const QString& key, const QString& value) {
        QVariantMap row;
        row.insert(QStringLiteral("groupKey"), QStringLiteral("personal"));
        row.insert(QStringLiteral("fieldKey"), key);
        row.insert(QStringLiteral("label"), key);
        row.insert(QStringLiteral("value"), value);
        return QVariant(row);
    };
    QVariantList flat;
    flat << makeRow(QStringLiteral("issuing_authority"), QStringLiteral("MUP"));
    flat << makeRow(QStringLiteral("surname"), QStringLiteral("Peric"));
    flat << makeRow(QStringLiteral("expiry_date"), QStringLiteral("2030-01-01"));

    const QVariantList summary = SmartCardHandler::curateIdentitySummary(flat);
    ASSERT_GE(summary.size(), 2); // surname + expiry_date; issuing_authority excluded
    EXPECT_EQ(summary.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("Peric"));
}

// plainDisplay neutralizes rich-text promotion for untrusted (card / agent /
// filename-derived) values rendered by AutoText sinks that expose no
// textFormat (Kirigami InlineMessage/PlaceholderMessage, PC3 button labels):
// tag-looking values are HTML-escaped — and the escaped form must itself be
// rich-detected so the AutoText sink renders the ORIGINAL literal characters —
// while every legitimate value passes through byte-identical (the raw-card-data
// rule: presentation-layer neutralization only, never data mutation).
TEST(SmartCardHandlerDisplay, PlainDisplayNeutralizesMarkupAndPreservesPlainText)
{
    // Hostile: markup is escaped...
    EXPECT_EQ(SmartCardHandler::plainDisplay(QStringLiteral("<b>PERIĆ</b>")),
              QStringLiteral("&lt;b&gt;PERIĆ&lt;/b&gt;"));
    const QString link = QStringLiteral("<a href='http://evil'>doc</a>.pdf");
    const QString escapedLink = SmartCardHandler::plainDisplay(link);
    EXPECT_FALSE(escapedLink.contains(QLatin1Char('<')));
    // ...and the escaped form is itself promoted by Qt::mightBeRichText, so an
    // AutoText sink renders it as the original literal characters.
    EXPECT_TRUE(Qt::mightBeRichText(SmartCardHandler::plainDisplay(QStringLiteral("<b>x</b>"))));

    // Legitimate values are byte-identical — including ones with '&'.
    EXPECT_EQ(SmartCardHandler::plainDisplay(QStringLiteral("Gemalto USB Reader (2)")),
              QStringLiteral("Gemalto USB Reader (2)"));
    EXPECT_EQ(SmartCardHandler::plainDisplay(QStringLiteral("Nova & Stara uprava")),
              QStringLiteral("Nova & Stara uprava"));
    EXPECT_EQ(SmartCardHandler::plainDisplay(QString()), QString());
}

// --- Manage credentials launch affordance ---------------------------

// pinManagementAvailable mirrors the Client::Cap::PinManagement bit on the active
// card — gates the plasmoid's "Manage credentials…" action. Purely
// capability-driven: no card read is issued to determine it.
TEST(SmartCardHandler, PinManagementAvailableTrueWhenCapabilityBitSet)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki | Client::Cap::PinManagement;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));
    EXPECT_TRUE(handler->pinManagementAvailable());
}

TEST(SmartCardHandler, PinManagementAvailableFalseWithoutCapabilityBit)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki; // no PinManagement
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));
    EXPECT_FALSE(handler->pinManagementAvailable());
}

// No card at all: the property is false, and — as with every other
// card-derived flag — never itself issues a read.
TEST(SmartCardHandler, PinManagementAvailableFalseWhenNoCard)
{
    FakeAgent::Config cfg;
    cfg.hasCard = false;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
    EXPECT_FALSE(handler->pinManagementAvailable());
}

// A live capability flip that KEEPS the coarse state (Hybrid stays Hybrid)
// must still update pinManagementAvailable and fire its OWN change signal —
// transitionTo() dedupes stateChanged, so a NOTIFY riding stateChanged would
// leave the QML launcher affordance stale.
TEST(SmartCardHandler, PinManagementAvailableTracksLiveCapabilityFlip)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData | Client::Cap::Pki; // Hybrid, no PinManagement
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::Hybrid); }));
    ASSERT_FALSE(handler->pinManagementAvailable());

    QSignalSpy spy(handler.get(), &SmartCardHandler::pinManagementAvailableChanged);

    // The agent surfaces PinManagement live; the coarse state does not change,
    // so no stateChanged fires — the property's own signal must.
    h.emitCardCapabilitiesChanged(Client::Cap::IdentityData | Client::Cap::Pki | Client::Cap::PinManagement);
    ASSERT_TRUE(waitFor([&]() { return handler->pinManagementAvailable(); }))
        << "a live capability flip that keeps the coarse state must still flip the launcher gate";
    EXPECT_EQ(handler->state(), static_cast<int>(State::Hybrid));
    EXPECT_GE(spy.count(), 1);

    // And back off — the affordance must retract too.
    h.emitCardCapabilitiesChanged(Client::Cap::IdentityData | Client::Cap::Pki);
    ASSERT_TRUE(waitFor([&]() { return !handler->pinManagementAvailable(); }))
        << "dropping the capability must retract the launcher affordance";
    EXPECT_EQ(handler->state(), static_cast<int>(State::Hybrid));
    EXPECT_GE(spy.count(), 2);
}

// credentialsLaunchArgs is the pure, unit-testable seam behind
// manageCredentials(): the exact argv the standalone credentials window's
// `--reader` option expects. No D-Bus, no handler instance.
TEST(SmartCardHandler, CredentialsLaunchArgsBuildsReaderOption)
{
    const QStringList args = SmartCardHandler::credentialsLaunchArgs(QStringLiteral("/org/librescrs/Agent/reader/0"));
    EXPECT_EQ(args, QStringList({QStringLiteral("--reader"), QStringLiteral("/org/librescrs/Agent/reader/0")}));
}

// The card:/ URL for "Open in Files" is built from the reader friendly name.
TEST(SmartCardHandler, CardUrlForReaderBuildsCardScheme)
{
    const QUrl url = SmartCardHandler::cardUrlForReader(QStringLiteral("My eID Reader"));
    EXPECT_EQ(url.scheme(), QStringLiteral("card"));
    EXPECT_EQ(url.path(), QStringLiteral("/My eID Reader"));
}

// Copy field puts the value on the clipboard (needs the QGuiApplication main).
// Stock Qt6's offscreen QPA clipboard DOES round-trip setText/text in-process,
// so the assertion holds on the target Qt; if a stripped Qt build ships a no-op
// offscreen clipboard we still exercise the copyField() code path and SKIP the
// round-trip assertion rather than hard-failing the whole suite.
TEST(SmartCardHandler, CopyFieldSetsClipboard)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    auto handler = makeHandler(h);

    QClipboard* clipboard = QGuiApplication::clipboard();
    ASSERT_NE(clipboard, nullptr);
    handler->copyField(QStringLiteral("012345678"));
    if (clipboard->text() != QStringLiteral("012345678")) {
        GTEST_SKIP() << "offscreen QPA clipboard does not round-trip on this Qt build; "
                        "copyField() path exercised without the round-trip assertion";
    }
    EXPECT_EQ(clipboard->text(), QStringLiteral("012345678"));
}

// Save photo writes the RAW agent bytes (original format) to the chosen file.
TEST(SmartCardHandler, SavePhotoWritesRawBytes)
{
    const QByteArray png = tinyPngBytes();
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok
    cfg.photoBytes = png;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));
    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->hasCardPhoto(); }));

    QTemporaryDir out;
    ASSERT_TRUE(out.isValid());
    const QString path = out.path() + QStringLiteral("/photo.png");
    EXPECT_TRUE(handler->savePhoto(QUrl::fromLocalFile(path)));

    QFile written(path);
    ASSERT_TRUE(written.open(QIODevice::ReadOnly));
    EXPECT_EQ(written.readAll(), png);
}

// The Save-photo suggested file name derives its extension from the RAW agent
// bytes' ACTUAL format (savePhoto writes those bytes verbatim; jp2 bytes under
// a ".png" name would lie about the content) — pure helper + live property.
TEST(SmartCardHandler, SuggestedPhotoFileNameMatchesActualFormat)
{
    // PNG bytes -> .png
    EXPECT_EQ(SmartCardHandler::suggestedPhotoFileName(tinyPngBytes()), QStringLiteral("card-photo.png"));

    // JPEG bytes -> the conventional .jpg
    QImage img(2, 2, QImage::Format_RGB32);
    img.fill(Qt::green);
    QByteArray jpegBytes;
    QBuffer buf(&jpegBytes);
    buf.open(QIODevice::WriteOnly);
    ASSERT_TRUE(img.save(&buf, "JPEG"));
    EXPECT_EQ(SmartCardHandler::suggestedPhotoFileName(jpegBytes), QStringLiteral("card-photo.jpg"));

    // Unrecognizable bytes -> bare base name (no lying extension).
    EXPECT_EQ(SmartCardHandler::suggestedPhotoFileName(QByteArrayLiteral("not an image")),
              QStringLiteral("card-photo"));
}

// After a successful photo read the property carries the sniffed name; card
// removal clears it with the rest of the photo state.
TEST(SmartCardHandler, PhotoSuggestedFileNameFollowsPhotoLifecycle)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok
    cfg.photoBytes = tinyPngBytes();
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));
    EXPECT_TRUE(handler->photoSuggestedFileName().isEmpty());
    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->hasCardPhoto(); }));
    EXPECT_EQ(handler->photoSuggestedFileName(), QStringLiteral("card-photo.png"));

    h.setCardPresent(false);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
    EXPECT_TRUE(handler->photoSuggestedFileName().isEmpty());
}

// LibreCelik detection reflects PATH: hidden when the binary is absent.
TEST(SmartCardHandler, LibreCelikDetectionReflectsPath)
{
    const QByteArray savedPath = qgetenv("PATH");

    QTemporaryDir withCelik;
    const QString exe = withCelik.path() + QStringLiteral("/librecelik");
    {
        QFile f(exe);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n");
        f.close();
        QFile::setPermissions(exe, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    qputenv("PATH", withCelik.path().toLocal8Bit());
    auto present = makeHandler(h);
    EXPECT_TRUE(present->libreCelikAvailable());

    QTemporaryDir noCelik;
    qputenv("PATH", noCelik.path().toLocal8Bit());
    auto absent = makeHandler(h);
    EXPECT_FALSE(absent->libreCelikAvailable());

    qputenv("PATH", savedPath);
}

// busy latches true from readIdentity() until the read finishes.
TEST(SmartCardHandler, BusyLatchesDuringRead)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 20;
    cfg.finalStatus = 0; // Ok
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));
    EXPECT_FALSE(handler->busy());

    handler->readIdentity();
    EXPECT_TRUE(handler->busy()) << "busy must be set synchronously when a read starts";

    ASSERT_TRUE(waitFor([&]() { return handler->hasIdentity(); }));
    EXPECT_FALSE(handler->busy());
}

// Regression (a): pulling the card while an identity read is in flight
// (the realistic shape: the agent's CAN prompt is open and the user pulls the
// card instead of answering). This path heals through the CLIENT: the
// InterfacesRemoved death-sweep terminalizes the op (Cancelled/CardRemoved)
// synchronously BEFORE cardChanged drives refresh()/bindCard(nullptr), so
// onOperationFinished still runs and releases busy. Pinned here so a future
// re-ordering (sweep after the signal) or a sweep removal re-opens the latch.
TEST(SmartCardHandler, BusyClearsWhenCardRemovedMidRead)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 400; // long enough to pull the card mid-flight
    cfg.finalStatus = 0;        // Ok (never delivered — the op is discarded)
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));

    handler->readIdentity();
    ASSERT_TRUE(handler->busy());

    // Pull the card mid-read: the in-flight op is discarded (disconnected), so
    // onOperationFinished can never fire for it — busy must be reset HERE.
    h.setCardPresent(false);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
    EXPECT_FALSE(handler->busy()) << "discarding an in-flight read must release the busy latch";

    // Re-insert: PreAuthRequired again, and a NEW explicit read must run to
    // completion (the button is enabled again and actually works).
    h.setCardPresent(true);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));
    handler->readIdentity();
    EXPECT_TRUE(handler->busy()) << "a fresh read must be startable after the mid-read removal";
    ASSERT_TRUE(waitFor([&]() { return handler->hasIdentity(); }));
    EXPECT_FALSE(handler->busy());
    EXPECT_EQ(handler->state(), static_cast<int>(State::IdentityOnly));
}

// Regression (b), the path with NO client death-sweep: switching the
// active card while a read is in flight (master-detail chip click / a config
// rebind — the discarded op's card is still alive, so nothing terminalizes
// it). bindCard() disconnects the op; without releasing the busy latch there,
// busy stays true FOREVER: PreAuthState's "Read Card…" is `enabled:
// !smartCard.busy`, so a Can/Mrz card is permanently bricked
// (ensureFreeRead deliberately skips pre-auth cards — no other path reads).
TEST(SmartCardHandler, BusyClearsWhenActiveCardSwitchedMidRead)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can"); // reader/0 "Fake": pre-auth card
    cfg.operationDelayMs = 400;              // long enough to switch mid-flight
    cfg.finalStatus = 0;                     // Ok (never delivered — op discarded)
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));

    // A second reader "Fake2" holding a card (so the master-detail exists).
    h.emitReaderArrivesEmpty();
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderCardAdded(Client::Cap::IdentityData | Client::Cap::Pki);
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return handler->readersWithCards().size() == 2; }));

    // Start the pre-auth read on "Fake" (the agent's CAN prompt would be up
    // now), then click the OTHER chip while it is in flight.
    handler->readIdentity();
    ASSERT_TRUE(handler->busy());
    handler->selectReader(QStringLiteral("Fake2"));
    ASSERT_TRUE(waitFor([&]() { return handler->readerName() == QStringLiteral("Fake2"); }));
    EXPECT_FALSE(handler->busy()) << "discarding an in-flight read on a card switch must release the busy latch";

    // Switch back: the pre-auth card must be readable again (button enabled,
    // and an explicit read runs to completion).
    handler->selectReader(QStringLiteral("Fake"));
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));
    EXPECT_FALSE(handler->busy());
    handler->readIdentity();
    EXPECT_TRUE(handler->busy());
    ASSERT_TRUE(waitFor([&]() { return handler->hasIdentity(); }));
    EXPECT_FALSE(handler->busy());
}

// The plasmoid "Sign a file…" entry: signFile() drives the shared SignJob over
// the handler's bound card, forwarding an input fd + the MIME-derived
// {format,packaging} to Card1.Sign. The plasmoid injects its OWN non-QtWidgets
// seams; SignJob auto-picks the lone signing cert (chooser never invoked) and the
// temp output is fresh (overwrite confirmer never invoked), so no seam fires here.
// Assert the wire Sign carries the right cert, options, and input bytes.
TEST(SmartCardHandler, SignFileDrivesWireSign)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-sign"), true, QStringLiteral("Signer")}};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("doc.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\nplasmoid\n");
    f.close();

    handler->signFile(QUrl::fromLocalFile(input).toString());

    ASSERT_TRUE(waitFor([&]() { return !h.lastSignCertId().isEmpty(); }));
    EXPECT_EQ(h.lastSignCertId(), QStringLiteral("cert-sign"));
    const QVariantMap opts = h.lastSignOptions();
    EXPECT_EQ(opts.value(QStringLiteral("format")).toString(), QStringLiteral("pades"));
    EXPECT_EQ(opts.value(QStringLiteral("packaging")).toString(), QStringLiteral("enveloped"));
    EXPECT_EQ(h.lastSignInputBytes(), QByteArray("%PDF-1.4\nplasmoid\n"));
}

// A multi-signing-cert card signs with the deterministic FIRST cert — but the
// implicit pick must never stay silent: the reader's sign result carries the
// picked cert's display name (subjectCn) so the success banner can say which
// cert signed. A lone auto-selected cert (SignJob never invokes the chooser)
// carries an EMPTY label — no callout for the unambiguous common case.
TEST(SmartCardHandler, SignSuccessNamesImplicitlyPickedCertOnlyForMultiCert)
{
    // Multi-cert: label = first cert's subjectCn.
    {
        FakeAgent::Config cfg;
        cfg.capabilities = Client::Cap::Pki;
        cfg.certScript = {{QStringLiteral("cert-a"), true, QStringLiteral("Alpha")},
                          {QStringLiteral("cert-b"), true, QStringLiteral("Beta")}};
        Harness h(cfg, BusNames::UniqueAndWellKnown);
        auto handler = makeHandler(h);
        ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));

        QTemporaryDir dir;
        const QString input = dir.filePath(QStringLiteral("doc.pdf"));
        QFile f(input);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("%PDF-1.4\nmulticert\n");
        f.close();
        handler->signFile(QUrl::fromLocalFile(input).toString());
        ASSERT_TRUE(waitFor([&]() {
            return handler->signResult().value(QStringLiteral("outcome")).toInt() ==
                   static_cast<int>(SmartCardHandler::SignOutcome::Succeeded);
        }));
        EXPECT_EQ(h.lastSignCertId(), QStringLiteral("cert-a"));
        EXPECT_EQ(handler->signResult().value(QStringLiteral("certLabel")).toString(), QStringLiteral("Alpha"))
            << "the implicit first-cert pick must be surfaced by name";
    }

    // Lone cert: auto-selected without the chooser -> empty label.
    {
        FakeAgent::Config cfg;
        cfg.capabilities = Client::Cap::Pki;
        cfg.certScript = {{QStringLiteral("cert-sign"), true, QStringLiteral("Signer")}};
        Harness h(cfg, BusNames::UniqueAndWellKnown);
        auto handler = makeHandler(h);
        ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));

        QTemporaryDir dir;
        const QString input = dir.filePath(QStringLiteral("doc.pdf"));
        QFile f(input);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("%PDF-1.4\nlonecert\n");
        f.close();
        handler->signFile(QUrl::fromLocalFile(input).toString());
        ASSERT_TRUE(waitFor([&]() {
            return handler->signResult().value(QStringLiteral("outcome")).toInt() ==
                   static_cast<int>(SmartCardHandler::SignOutcome::Succeeded);
        }));
        EXPECT_TRUE(handler->signResult().value(QStringLiteral("certLabel")).toString().isEmpty())
            << "a lone auto-selected cert needs no callout";
    }
}

// The banner says which conformance level came out, which is not necessarily
// one this client asked for — it does not ask. Scripted to a level the request
// could not have produced by accident, so the assertion proves the value came
// from the agent's own metadata.
TEST(SmartCardHandler, SignResultCarriesTheResolvedLevel)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-sign"), true, QStringLiteral("Signer")}};
    cfg.signMeta = QVariantMap{{QStringLiteral("format"), QStringLiteral("pades")},
                               {QStringLiteral("level"), QStringLiteral("b-lt")},
                               {QStringLiteral("tsaUsed"), true},
                               {QStringLiteral("chainComplete"), true}};
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));

    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("doc.pdf"));
    QFile f(input);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("%PDF-1.4\nlevel\n");
    f.close();
    handler->signFile(QUrl::fromLocalFile(input).toString());
    ASSERT_TRUE(waitFor([&]() {
        return handler->signResult().value(QStringLiteral("outcome")).toInt() ==
               static_cast<int>(SmartCardHandler::SignOutcome::Succeeded);
    }));
    EXPECT_EQ(handler->signResult().value(QStringLiteral("level")).toString(), QStringLiteral("b-lt"));
    // And nothing on the way out asked for it.
    EXPECT_FALSE(h.lastSignOptions().contains(QStringLiteral("level")));
}

// --- Sign state is per READER, not per widget -------------------------------
//
// In Auto mode the widget renders a master-detail chooser over every reader
// holding a card (MultiCardState.qml), so the chips PROMISE per-reader state.
// A sign, unlike an identity read, is deliberately NOT cancelled when the chip
// moves (bindCard cancels the read op only) — it keeps running on the card it
// was started on. Its UI surfaces must therefore be scoped to the reader that
// is actually signing, or the spinner and the disabled "Sign a file…" button
// land on whatever card happens to be displayed.

namespace {

// Build two readers, each holding a signing-capable card, and leave the handler
// in Auto mode. Returns with reader/0 "Fake" selected (the deterministic first).
void arrangeTwoSigningCards(Harness& h, SmartCardHandler& handler)
{
    ASSERT_TRUE(waitFor([&]() { return handler.state() == static_cast<int>(State::PkiOnly); }));
    h.emitReaderArrivesEmpty();
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderCardAdded(Client::Cap::Pki);
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return handler.readersWithCards().size() == 2; }));
    ASSERT_EQ(handler.readerName(), QStringLiteral("Fake"));
}

// Write @p contents to a fresh file in @p dir and return its file:// URL.
QString signableFile(const QTemporaryDir& dir, const QString& name, const QByteArray& contents)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(contents);
    f.close();
    return QUrl::fromLocalFile(path).toString();
}

} // namespace

// The spinner and the busy-disabled button follow the SIGNING reader. Start a
// sign on "Fake", move the master-detail pick to "Fake2" while it is still in
// flight: the second card is idle and must say so — its "Sign a file…" stays
// live. Coming back to "Fake" shows the running sign again.
TEST(SmartCardHandler, SigningBusyFollowsTheSigningReaderAcrossAChipSwitch)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-sign"), true, QStringLiteral("Signer")}};
    // Long enough that the sign is unambiguously still in flight while the
    // assertions below run (the flow is two ops: certs, then Sign).
    cfg.operationDelayMs = 1000;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    arrangeTwoSigningCards(h, *handler);

    QTemporaryDir dir;
    handler->signFile(signableFile(dir, QStringLiteral("alpha.pdf"), QByteArray("%PDF-1.4\nalpha\n")));
    ASSERT_TRUE(handler->signingBusy()) << "the sign starts on the displayed reader";

    handler->selectReader(QStringLiteral("Fake2"));
    ASSERT_EQ(handler->readerName(), QStringLiteral("Fake2"));
    EXPECT_FALSE(handler->signingBusy())
        << "the second card is not signing — its spinner must stay dark and its Sign button live";

    handler->selectReader(QStringLiteral("Fake"));
    ASSERT_EQ(handler->readerName(), QStringLiteral("Fake"));
    EXPECT_TRUE(handler->signingBusy()) << "the signing reader still shows its own in-flight sign";
}

// The half that is not cosmetic: with one global sign slot the widget refuses to
// start a second sign on ANY reader while one is in flight, so the concurrent
// credential prompts the agent supports cannot be raised from this client at
// all. Two readers must be able to sign at the same time — assert the agent
// really received BOTH Sign calls, not that a job object was merely created.
TEST(SmartCardHandler, ASecondReaderCanSignWhileTheFirstSignIsInFlight)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-sign"), true, QStringLiteral("Signer")}};
    cfg.operationDelayMs = 1000;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    arrangeTwoSigningCards(h, *handler);

    QTemporaryDir dir;
    handler->signFile(signableFile(dir, QStringLiteral("alpha.pdf"), QByteArray("%PDF-1.4\nalpha\n")));
    ASSERT_TRUE(handler->signingBusy());

    handler->selectReader(QStringLiteral("Fake2"));
    handler->signFile(signableFile(dir, QStringLiteral("beta.pdf"), QByteArray("%PDF-1.4\nbeta\n")));
    EXPECT_TRUE(handler->signingBusy()) << "the second reader's own sign is in flight";

    // Both reached the wire: the count is the only witness that the second sign
    // was issued rather than refused client-side (lastSign* describes one call).
    ASSERT_TRUE(waitFor([&]() { return h.signCallCount() == 2; }))
        << "a sign in flight on one reader must not block a sign on another";

    // …and they overlap: the first reader's sign is still running now.
    handler->selectReader(QStringLiteral("Fake"));
    EXPECT_TRUE(handler->signingBusy()) << "both signs are in flight at the same moment";
}

// The result banner is STATE, not an event. A sign that finishes while another
// chip is displayed must not paint its outcome over a foreign card — and the
// card that actually signed must still be able to show it when the user comes
// back. Completion is observed off the artifact on disk, so the assertion does
// not depend on which signal the handler happens to emit.
TEST(SmartCardHandler, SignResultBelongsToTheReaderThatSigned)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-sign"), true, QStringLiteral("Signer")}};
    cfg.signMeta = QVariantMap{{QStringLiteral("level"), QStringLiteral("b-lt")}};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    arrangeTwoSigningCards(h, *handler);

    QTemporaryDir dir;
    handler->signFile(signableFile(dir, QStringLiteral("alpha.pdf"), QByteArray("%PDF-1.4\nalpha\n")));

    // Move to the other card and let the first reader's sign finish there.
    handler->selectReader(QStringLiteral("Fake2"));
    ASSERT_TRUE(waitFor([&]() { return QFile::exists(dir.filePath(QStringLiteral("alpha-signed.pdf"))); }))
        << "the sign must keep running on the card it was started on after a chip switch";

    EXPECT_EQ(handler->signResult().value(QStringLiteral("outcome")).toInt(),
              static_cast<int>(SmartCardHandler::SignOutcome::None))
        << "the displayed card never signed — another reader's outcome must not land on it";

    handler->selectReader(QStringLiteral("Fake"));
    ASSERT_TRUE(waitFor([&]() {
        return handler->signResult().value(QStringLiteral("outcome")).toInt() ==
               static_cast<int>(SmartCardHandler::SignOutcome::Succeeded);
    })) << "coming back to the card that signed must still show its result";
    const QVariantMap res = handler->signResult();
    EXPECT_TRUE(res.value(QStringLiteral("outputPath")).toString().endsWith(QStringLiteral("alpha-signed.pdf")));
    EXPECT_EQ(res.value(QStringLiteral("level")).toString(), QStringLiteral("b-lt"));

    // The close button is reader-scoped too.
    handler->dismissSignResult();
    EXPECT_EQ(handler->signResult().value(QStringLiteral("outcome")).toInt(),
              static_cast<int>(SmartCardHandler::SignOutcome::None));
}

// A result belongs to the CARD, not merely to the reader's name: swap the card
// in the same slot and the previous card's "Signed …" must not greet the new
// one. (The QML used to clear the banner on stateChanged/readerNameChanged;
// with the outcome held per reader, the card identity is what settles it.)
TEST(SmartCardHandler, ASwappedCardDoesNotInheritThePreviousCardsSignResult)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-sign"), true, QStringLiteral("Signer")}};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));

    QTemporaryDir dir;
    handler->signFile(signableFile(dir, QStringLiteral("alpha.pdf"), QByteArray("%PDF-1.4\nalpha\n")));
    ASSERT_TRUE(waitFor([&]() {
        return handler->signResult().value(QStringLiteral("outcome")).toInt() ==
               static_cast<int>(SmartCardHandler::SignOutcome::Succeeded);
    }));

    // Same reader, different card.
    h.setCardPresent(false);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
    h.setCardPresent(true);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));

    EXPECT_EQ(handler->signResult().value(QStringLiteral("outcome")).toInt(),
              static_cast<int>(SmartCardHandler::SignOutcome::None))
        << "a freshly inserted card has signed nothing";
}

// Spec test (b): the plasmoid "Sign a file…" and the Purpose path produce a
// BYTE-IDENTICAL agent Sign request for the same input + cert. Run the plasmoid
// path (SmartCardHandler::signFile), capture the wire args, then run the Purpose
// core (a SignJob built as SignPurposeJob builds it) over the same card and the
// same input content, and compare. The parity is about the REQUEST shape (certId,
// format/packaging options, input bytes), which is independent of the injected
// dialog seams: this %PDF input resolves to pades/enveloped whether the MIME is
// sniffed (plasmoid, empty mimeType) or caller-supplied (Purpose,
// "application/pdf"), and neither seam fires for a single-cert / fresh-output run.
TEST(SignParity, PlasmoidAndPurposeProduceIdenticalWireSign)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    cfg.certScript = {{QStringLiteral("cert-sign"), true, QStringLiteral("Signer")}};
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    const QByteArray content("%PDF-1.4\nparity\n");

    // --- plasmoid path: SmartCardHandler::signFile (input in dirA) ---
    QTemporaryDir dirA;
    const QString inputA = dirA.filePath(QStringLiteral("doc.pdf"));
    {
        QFile fa(inputA);
        ASSERT_TRUE(fa.open(QIODevice::WriteOnly));
        fa.write(content);
    }

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));
    handler->signFile(QUrl::fromLocalFile(inputA).toString());
    ASSERT_TRUE(waitFor([&]() { return !h.lastSignCertId().isEmpty(); }));
    const QString certA = h.lastSignCertId();
    const QVariantMap optA = h.lastSignOptions();
    const QByteArray inA = h.lastSignInputBytes();

    // --- Purpose path: a SignJob over the same card, as SignPurposeJob builds it ---
    // Through the SAME card resolution the Purpose plugin calls, not a copy of
    // it: a parity assertion that re-implements the primitive it is comparing
    // across cannot catch the primitive drifting. One card is present, so the
    // chooser below is never invoked — asserted, so this stays a single-card
    // parity case and not an accidental multi-card one.
    auto* client2 = new Client::AgentClient();
    int cardChooserCalls = 0;
    LibreKDE::CardChooser countingChooser = [&cardChooserCalls](const QList<LibreKDE::CardChoice>&) {
        ++cardChooserCalls;
        return std::optional<QString>();
    };
    ASSERT_TRUE(
        waitFor([&]() { return LibreKDE::Signing::chooseSigningCard(*client2, countingChooser).card != nullptr; }));
    Client::AgentCard* card2 = LibreKDE::Signing::chooseSigningCard(*client2, countingChooser).card;
    ASSERT_NE(card2, nullptr);
    EXPECT_EQ(cardChooserCalls, 0) << "a single-card fixture must never raise the card chooser";

    QTemporaryDir dirB; // different dir -> the second run never hits the overwrite seam
    const QString inputB = dirB.filePath(QStringLiteral("doc.pdf"));
    {
        QFile fb(inputB);
        ASSERT_TRUE(fb.open(QIODevice::WriteOnly));
        fb.write(content);
    }

    CertChooser pickFirst = [](const QList<Client::CertificateInfo>& c) -> std::optional<QString> {
        return c.isEmpty() ? std::nullopt : std::optional<QString>(c.first().id);
    };
    OverwriteConfirmer alwaysOverwrite = [](const QString&) { return true; };
    // Same shape as SignPurposeJob::start(): mimeType from Purpose data() (here
    // "application/pdf"), empty formatOverride.
    auto* job =
        new SignJob(card2, inputB, QStringLiteral("application/pdf"), QString(), pickFirst, alwaysOverwrite, client2);
    bool done = false;
    QObject::connect(job, &SignJob::succeeded, client2, [&](const QString&) { done = true; });
    QObject::connect(job, &SignJob::failed, client2, [&](const QString&) { done = true; });
    job->start();
    ASSERT_TRUE(waitFor([&]() { return done; }));

    const QString certB = h.lastSignCertId();
    const QVariantMap optB = h.lastSignOptions();
    const QByteArray inB = h.lastSignInputBytes();

    EXPECT_EQ(certA, certB);
    EXPECT_EQ(optA, optB);
    EXPECT_EQ(inA, inB);

    delete client2; // deletes the child job too
}

// A widget bound to a reader friendly Name reflects ONLY that reader's card —
// never falling back to another present reader. reader/0 "Fake" is
// PkiOnly; reader/1 "Fake2" is Hybrid; binding "Fake2" must surface Hybrid.
TEST(SmartCardHandler, BoundReaderSelectsOnlyThatReader)
{
    FakeAgent::Config cfg;
    cfg.hasCard = true;
    cfg.capabilities = Client::Cap::Pki; // reader/0 "Fake" -> PkiOnly
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));
    EXPECT_EQ(handler->readerName(), QStringLiteral("Fake"));

    // Bring up a SECOND reader "Fake2" holding a Hybrid card (reader/1 / card/1).
    h.emitReaderArrivesEmpty();
    waitFor([]() { return false; }, 50); // let the client register reader/1's match rule
    h.emitArrivedReaderCardAdded(Client::Cap::IdentityData | Client::Cap::Pki);
    waitFor([]() { return false; }, 50); // let the client register the card match rule
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return handler->readersWithCards().size() == 2; }));

    // Auto still picks the deterministic first (reader/0 "Fake").
    EXPECT_EQ(handler->readerName(), QStringLiteral("Fake"));

    // Bind to the SECOND reader by friendly name: the widget now reflects ONLY
    // that reader's card (Hybrid), never reader/0's PkiOnly.
    handler->setBoundReaderName(QStringLiteral("Fake2"));
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::Hybrid); }));
    EXPECT_EQ(handler->readerName(), QStringLiteral("Fake2"));
}

// A widget bound to an ABSENT reader shows a reader-scoped waiting/NoCard state
// and IGNORES any other present card. The bound name stays visible
// so the QML can render "Waiting for reader <name>".
TEST(SmartCardHandler, AbsentBoundReaderShowsWaitingState)
{
    FakeAgent::Config cfg;
    cfg.hasCard = true;
    cfg.capabilities = Client::Cap::IdentityData | Client::Cap::Pki; // reader/0 present + Hybrid
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::Hybrid); }));

    handler->setBoundReaderName(QStringLiteral("NoSuchReader"));
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));
    EXPECT_EQ(handler->readerName(), QStringLiteral("NoSuchReader"));
    EXPECT_EQ(handler->boundReaderName(), QStringLiteral("NoSuchReader"));
}

// Auto mode with two cards: the active/detail card is the DETERMINISTIC first by
// sorted reader path (mirrors firstReaderWithCard), and the master list is in
// the same sorted-path order. selectReader() re-binds the detail deterministically.
TEST(SmartCardHandler, AutoMultiCardSelectionIsDeterministicAndSelectable)
{
    FakeAgent::Config cfg;
    cfg.hasCard = true;
    cfg.capabilities = Client::Cap::Pki; // reader/0 "Fake" -> PkiOnly
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));

    h.emitReaderArrivesEmpty();
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderCardAdded(Client::Cap::IdentityData | Client::Cap::Pki); // reader/1 "Fake2" -> Hybrid
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return handler->readersWithCards().size() == 2; }));

    // Auto (no bound reader): deterministic first == reader/0 "Fake".
    EXPECT_EQ(handler->boundReaderName(), QString());
    EXPECT_EQ(handler->readerName(), QStringLiteral("Fake"));
    EXPECT_EQ(handler->state(), static_cast<int>(State::PkiOnly));
    const QStringList expected{QStringLiteral("Fake"), QStringLiteral("Fake2")};
    EXPECT_EQ(handler->readersWithCards(), expected);

    // Master-detail pick: selecting the second reader re-binds the detail to it.
    handler->selectReader(QStringLiteral("Fake2"));
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::Hybrid); }));
    EXPECT_EQ(handler->readerName(), QStringLiteral("Fake2"));

    // Selecting back returns to the first card deterministically.
    handler->selectReader(QStringLiteral("Fake"));
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));
    EXPECT_EQ(handler->readerName(), QStringLiteral("Fake"));
}

// availableReaderNames feeds the config chooser: it lists EVERY present reader,
// including card-less ones (so a widget can be bound before the card is seated).
TEST(SmartCardHandler, AvailableReaderNamesListsEmptyReaders)
{
    FakeAgent::Config cfg;
    cfg.hasCard = false; // reader/0 "Fake" present but empty
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::NoCard); }));

    // A second, also-empty reader arrives.
    h.emitReaderArrivesEmpty();
    ASSERT_TRUE(waitFor([&]() { return handler->availableReaderNames().size() == 2; }));

    const QStringList expected{QStringLiteral("Fake"), QStringLiteral("Fake2")};
    EXPECT_EQ(handler->availableReaderNames(), expected);
    // Neither reader holds a card, so the master list is empty.
    EXPECT_TRUE(handler->readersWithCards().isEmpty());
}

// selectReader() is an Auto-mode affordance only: while a widget is bound it is
// pinned to its reader and a stray selectReader() must NOT move the detail.
TEST(SmartCardHandler, SelectReaderIsNoOpWhileBound)
{
    FakeAgent::Config cfg;
    cfg.hasCard = true;
    cfg.capabilities = Client::Cap::Pki; // reader/0 "Fake" -> PkiOnly
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));

    h.emitReaderArrivesEmpty();
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderCardAdded(Client::Cap::IdentityData | Client::Cap::Pki);
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return handler->readersWithCards().size() == 2; }));

    handler->setBoundReaderName(QStringLiteral("Fake"));
    ASSERT_TRUE(waitFor([&]() { return handler->readerName() == QStringLiteral("Fake"); }));
    EXPECT_EQ(handler->state(), static_cast<int>(State::PkiOnly));

    // Bound to "Fake": trying to select "Fake2" is ignored.
    handler->selectReader(QStringLiteral("Fake2"));
    waitFor([]() { return false; }, 40);
    EXPECT_EQ(handler->readerName(), QStringLiteral("Fake"));
    EXPECT_EQ(handler->state(), static_cast<int>(State::PkiOnly));
}

// Availability-gate composition: when the agent vanishes the
// handler must BOTH reach AgentUnavailable (the gate runs FIRST) and clear the
// reader rosters — a stale readersWithCards would keep the QML master-detail (and
// its dead reader chips) on screen around the AgentUnavailable detail.
//
// The vanish is modelled by tearing the whole peer down (the harness leaves
// scope), not by the harness's drop-one-name helper: that helper releases only
// the per-test unique name, while the handler's client is bound to the agent's
// well-known name, so dropping the unique name alone leaves the handler still
// looking at a live agent and the vanish never happens. Teardown releases BOTH
// names and destroys the peer, which is what a daemon exiting actually does.
TEST(SmartCardHandler, AgentVanishClearsReaderRosters)
{
    std::unique_ptr<SmartCardHandler> handler;
    {
        FakeAgent::Config cfg;
        cfg.hasCard = true;
        cfg.capabilities = Client::Cap::Pki;
        Harness h(cfg, BusNames::UniqueAndWellKnown);

        handler = makeHandler(h);
        ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PkiOnly); }));
        ASSERT_TRUE(waitFor([&]() { return handler->readersWithCards().size() == 1; }));
        ASSERT_EQ(handler->availableReaderNames().size(), 1);
    }

    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::AgentUnavailable); }));
    EXPECT_TRUE(handler->readersWithCards().isEmpty());
    EXPECT_TRUE(handler->availableReaderNames().isEmpty());
}

// C1 regression (multi-instance): Plasma 6 hosts EVERY applet instance in ONE
// shared QQmlEngine, so the handler must be a per-instance type — an
// engine-scoped QML singleton aliased all widgets onto one handler ("last
// config write wins"). Production shape: TWO handlers co-owning the SAME
// AgentClient (sharedAgentClient()), bound to DIFFERENT readers, must reflect
// different cards CONCURRENTLY, and one widget's binding change must never
// disturb the other.
TEST(SmartCardHandler, TwoInstancesWithDifferentBindingsAreIndependent)
{
    FakeAgent::Config cfg;
    cfg.hasCard = true;
    cfg.capabilities = Client::Cap::Pki; // reader/0 "Fake" -> PkiOnly
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = std::make_shared<Client::AgentClient>();
    SmartCardHandler widgetA(client);
    SmartCardHandler widgetB(client);
    ASSERT_TRUE(waitFor([&]() {
        return widgetA.state() == static_cast<int>(State::PkiOnly) &&
               widgetB.state() == static_cast<int>(State::PkiOnly);
    }));

    // Bring up a SECOND reader "Fake2" holding a Hybrid card (reader/1 / card/1).
    h.emitReaderArrivesEmpty();
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderCardAdded(Client::Cap::IdentityData | Client::Cap::Pki);
    waitFor([]() { return false; }, 50);
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(
        waitFor([&]() { return widgetA.readersWithCards().size() == 2 && widgetB.readersWithCards().size() == 2; }));

    // Pin the two widgets to DIFFERENT readers: both bindings must hold at once.
    widgetA.setBoundReaderName(QStringLiteral("Fake"));
    widgetB.setBoundReaderName(QStringLiteral("Fake2"));
    ASSERT_TRUE(waitFor([&]() {
        return widgetA.state() == static_cast<int>(State::PkiOnly) &&
               widgetB.state() == static_cast<int>(State::Hybrid);
    })) << "two widgets bound to different readers must render different cards concurrently";
    EXPECT_EQ(widgetA.readerName(), QStringLiteral("Fake"));
    EXPECT_EQ(widgetB.readerName(), QStringLiteral("Fake2"));
    EXPECT_EQ(widgetA.boundReaderName(), QStringLiteral("Fake"));
    EXPECT_EQ(widgetB.boundReaderName(), QStringLiteral("Fake2"));

    // Rebinding widget A to Auto must NOT disturb widget B (the singleton bug:
    // A's config write used to clobber B's binding).
    widgetA.setBoundReaderName(QString());
    ASSERT_TRUE(waitFor([&]() { return widgetA.boundReaderName().isEmpty(); }));
    EXPECT_EQ(widgetA.readerName(), QStringLiteral("Fake")); // Auto: deterministic first
    EXPECT_EQ(widgetB.boundReaderName(), QStringLiteral("Fake2"));
    EXPECT_EQ(widgetB.readerName(), QStringLiteral("Fake2"));
    EXPECT_EQ(widgetB.state(), static_cast<int>(State::Hybrid));

    // The Auto-mode transient master-detail pick is per-widget too: A's pick of
    // "Fake2" must leave B's pinned view untouched.
    widgetA.selectReader(QStringLiteral("Fake2"));
    ASSERT_TRUE(waitFor([&]() { return widgetA.state() == static_cast<int>(State::Hybrid); }));
    EXPECT_EQ(widgetA.readerName(), QStringLiteral("Fake2"));
    EXPECT_EQ(widgetB.readerName(), QStringLiteral("Fake2"));
    EXPECT_EQ(widgetB.boundReaderName(), QStringLiteral("Fake2"));
}

// C1 regression (photo isolation): the shared CardPhotoStore is keyed by a
// process-unique per-handler slot, so two widgets showing two cards never alias
// each other's photo. Pure store semantics + slot uniqueness + URL addressing.
TEST(CardPhotoStore, SlotsAreIsolatedPerHandler)
{
    // Distinct slots per handler instance.
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    Harness h(cfg, BusNames::UniqueAndWellKnown);
    auto client = std::make_shared<Client::AgentClient>();
    SmartCardHandler widgetA(client);
    SmartCardHandler widgetB(client);
    EXPECT_NE(widgetA.photoSlot(), widgetB.photoSlot());

    // Keyed isolation: writing/clearing one slot leaves the other intact.
    LibreKDE::Plasmoid::CardPhotoStore store;
    QImage red(2, 2, QImage::Format_RGB32);
    red.fill(Qt::red);
    QImage blue(3, 3, QImage::Format_RGB32);
    blue.fill(Qt::blue);
    store.setImage(1, red);
    store.setImage(2, blue);
    EXPECT_EQ(store.image(1).size(), QSize(2, 2));
    EXPECT_EQ(store.image(2).size(), QSize(3, 3));
    store.clear(1);
    EXPECT_FALSE(store.hasImage(1));
    EXPECT_TRUE(store.hasImage(2)) << "clearing one widget's slot must not scrub another widget's photo";
    EXPECT_TRUE(store.image(3).isNull()) << "an unknown slot resolves to a null image";

    // URL addressing: the provider parses the handler slot out of the image id.
    using LibreKDE::Plasmoid::CardPhotoProvider;
    EXPECT_EQ(CardPhotoProvider::slotFromImageId(QStringLiteral("cardphoto/7?3")), 7u);
    EXPECT_EQ(CardPhotoProvider::slotFromImageId(QStringLiteral("cardphoto/12")), 12u);
    EXPECT_EQ(CardPhotoProvider::slotFromImageId(QStringLiteral("cardphoto")), 0u);
    EXPECT_EQ(CardPhotoProvider::slotFromImageId(QStringLiteral("cardphoto/junk?1")), 0u);
}

// C1 regression (photo URL wiring): a handler's cardPhotoUrl addresses ITS OWN
// slot — the provider-side parse of the URL id yields exactly photoSlot(), and
// that slot resolves to the decoded image in the shared store.
TEST(SmartCardHandler, CardPhotoUrlAddressesOwnStoreSlot)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    cfg.preReadAuth = QStringLiteral("Can");
    cfg.operationDelayMs = 5;
    cfg.finalStatus = 0; // Ok
    cfg.photoBytes = tinyPngBytes();
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto handler = makeHandler(h);
    ASSERT_TRUE(waitFor([&]() { return handler->state() == static_cast<int>(State::PreAuthRequired); }));
    handler->readIdentity();
    ASSERT_TRUE(waitFor([&]() { return handler->hasCardPhoto(); }));

    const QString url = handler->cardPhotoUrl();
    const QString prefix = QStringLiteral("image://librekde/");
    ASSERT_TRUE(url.startsWith(prefix)) << url.toStdString();
    const QString imageId = url.mid(prefix.size());
    EXPECT_EQ(LibreKDE::Plasmoid::CardPhotoProvider::slotFromImageId(imageId), handler->photoSlot());
    EXPECT_FALSE(handler->photoStore()->image(handler->photoSlot()).isNull());
}

// ---- group headings -------------------------------------------------------
//
// The popup used to render every detail row in one flat list. That is fine for
// name and address rows and wrong for a verdict: a card can report the travel
// document's passive authentication (against a known issuer) and an annex's
// weaker integrity-only result (no trust anchor exists for an annex yet) in the
// same read. Side by side with nothing naming what either covers, a reader
// credits the annex with a check nobody ran.
//
// Headings are computed here rather than in the delegate so this rule is
// testable at all: QML in the plasmoid package is lint-checked, never
// instantiated.

TEST(SmartCardHandlerHeadings, GroupHeadingsMarkOnlyTheFirstRowOfEachGroup)
{
    // THREE groups, and the assertions target the MIDDLE one: with two, "head
    // the first" and "head the last" both pass by accident.
    const QVariantList details = {
        summaryRow("personal", "surname", "A"),
        summaryRow("personal", "given_names", "B"),
        summaryRow("annex.rs.personal", "street", "C"),
        summaryRow("annex.rs.personal", "place", "D"),
        summaryRow("annex.rs.security", "annex_integrity", "PASSED"),
    };
    const QVariantList out = SmartCardHandler::applyGroupHeadings(details);
    ASSERT_EQ(out.size(), details.size()) << "heading pass must not add or drop rows";

    QStringList headings;
    for (const QVariant& entry : out) {
        headings << entry.toMap().value(QStringLiteral("groupHeading")).toString();
    }
    // Exactly one non-empty heading per group, on its first row.
    EXPECT_FALSE(headings.at(0).isEmpty());
    EXPECT_TRUE(headings.at(1).isEmpty());
    EXPECT_FALSE(headings.at(2).isEmpty()) << "the middle group lost its heading";
    EXPECT_TRUE(headings.at(3).isEmpty());
    EXPECT_FALSE(headings.at(4).isEmpty());
    // And the middle one says what it is, rather than merely being non-empty.
    EXPECT_EQ(headings.at(2), LibreKDE::localizedGroupLabel(QStringLiteral("annex.rs.personal")));
}

// Invariant 2 of the identity-render contract: group by KEY, never by
// adjacency. A model that revisits a group must not print its heading twice.
TEST(SmartCardHandlerHeadings, ARevisitedGroupIsHeadedOnceAndKeptTogether)
{
    const QVariantList details = {
        summaryRow("personal", "surname", "A"),
        summaryRow("annex.rs.personal", "street", "B"),
        summaryRow("personal", "given_names", "C"), // the group comes back
    };
    const QVariantList out = SmartCardHandler::applyGroupHeadings(details);
    ASSERT_EQ(out.size(), 3);

    int headed = 0;
    for (const QVariant& entry : out) {
        if (!entry.toMap().value(QStringLiteral("groupHeading")).toString().isEmpty()) {
            ++headed;
        }
    }
    EXPECT_EQ(headed, 2) << "one heading per GROUP, not per run";

    // Regrouped so each group's rows are contiguous, in first-appearance order —
    // the same output shape renderIdentityTxt chose, and for the same reason: a
    // row printed under a heading it does not belong to is worse than a reorder.
    QStringList groups;
    for (const QVariant& entry : out) {
        groups << entry.toMap().value(QStringLiteral("groupKey")).toString();
    }
    EXPECT_EQ(groups, (QStringList{QStringLiteral("personal"), QStringLiteral("personal"),
                                   QStringLiteral("annex.rs.personal")}));
}

// A group this build has no name for is not given an invented one: an empty
// heading means "no heading", and the rows still render.
TEST(SmartCardHandlerHeadings, UnknownGroupCarriesNoHeading)
{
    const QVariantList details = {summaryRow("no_such_group", "whatever", "V")};
    const QVariantList out = SmartCardHandler::applyGroupHeadings(details);
    ASSERT_EQ(out.size(), 1);
    EXPECT_TRUE(out.first().toMap().value(QStringLiteral("groupHeading")).toString().isEmpty());
    EXPECT_EQ(out.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("V"));
}

// The summary is a curated subset taken from the full model, so it can carry
// off a group's FIRST row. The heading has to survive that and move to the row
// which actually remains — computing headings before the subtraction leaves the
// group headless in the only list that renders it.
TEST(SmartCardHandlerHeadings, GroupHeadingSurvivesASummarisedFirstRow)
{
    // `surname` is a curated key, so the summary lifts it out of the personal
    // group — which is that group's FIRST row. `given_names` is curated too but
    // the annex rows are not, so the model keeps enough behind for a details
    // list. (An all-uncurated model is not usable here: the curation falls back
    // to the leading rows when nothing matches, and the details come out empty.)
    const QVariantList fields = {
        summaryRow("personal", "surname", "SPECIMENSURNAME"),
        summaryRow("personal", "sex", "F"),
        summaryRow("annex.rs.personal", "street", "STREET"),
    };
    const QVariantList summary = SmartCardHandler::curateIdentitySummary(fields);
    ASSERT_FALSE(summary.isEmpty());
    const QVariantList details =
        SmartCardHandler::applyGroupHeadings(SmartCardHandler::curateIdentityDetails(fields, summary));
    ASSERT_FALSE(details.isEmpty()) << "nothing left to head";

    // The personal group lost the row it opened with; the one still on screen
    // has to carry the heading. Computing headings BEFORE the subtraction
    // leaves this group headless in the only list that renders it.
    bool personalIsHeaded = false;
    for (const QVariant& entry : details) {
        const QVariantMap row = entry.toMap();
        if (row.value(QStringLiteral("groupKey")).toString() != QStringLiteral("personal")) {
            continue;
        }
        if (!row.value(QStringLiteral("groupHeading")).toString().isEmpty()) {
            personalIsHeaded = true;
        }
        break; // only the group's first surviving row may carry it
    }
    EXPECT_TRUE(personalIsHeaded) << "the surviving first row of the group lost its heading";
}

// ---- reading order --------------------------------------------------------

// The wire delivers a group's fields sorted by KEY, so an address arrives with
// its street last and its apartment third. Built here in exactly that
// alphabetical order on purpose: built in address order the case would pass
// with no implementation at all.
TEST(SmartCardHandlerOrder, AnnexFieldsReadInAddressOrder)
{
    const QVariantList details = {
        summaryRow("annex.rs.personal", "address_date", "D"),
        summaryRow("annex.rs.personal", "apartment_number", "12"),
        summaryRow("annex.rs.personal", "house_number", "118"),
        summaryRow("annex.rs.personal", "street", "BULEVAR"),
    };
    const QVariantList out = SmartCardHandler::applyFieldOrder(details);
    QStringList keys;
    for (const QVariant& e : out) {
        keys << e.toMap().value(QStringLiteral("fieldKey")).toString();
    }
    // street is the discriminating target: last alphabetically, first here.
    EXPECT_EQ(keys, (QStringList{QStringLiteral("street"), QStringLiteral("house_number"),
                                 QStringLiteral("apartment_number"), QStringLiteral("address_date")}));
}

// Rows of other groups must not move, and must not be dragged between groups.
TEST(SmartCardHandlerOrder, OtherGroupsKeepTheirDeliveryOrder)
{
    const QVariantList details = {
        summaryRow("personal", "surname", "S"),         summaryRow("annex.rs.personal", "address_date", "D"),
        summaryRow("document", "document_number", "N"), summaryRow("annex.rs.personal", "street", "BULEVAR"),
        summaryRow("personal", "given_names", "G"),
    };
    const QVariantList out = SmartCardHandler::applyFieldOrder(details);
    ASSERT_EQ(out.size(), details.size());

    QStringList groups;
    for (const QVariant& e : out) {
        groups << e.toMap().value(QStringLiteral("groupKey")).toString();
    }
    // The group at each position is unchanged: only WITHIN-group permutation.
    EXPECT_EQ(groups,
              (QStringList{QStringLiteral("personal"), QStringLiteral("annex.rs.personal"), QStringLiteral("document"),
                           QStringLiteral("annex.rs.personal"), QStringLiteral("personal")}));
    // And the annex's two rows swapped, street first.
    EXPECT_EQ(out.at(1).toMap().value(QStringLiteral("fieldKey")).toString(), QStringLiteral("street"));
    EXPECT_EQ(out.at(3).toMap().value(QStringLiteral("fieldKey")).toString(), QStringLiteral("address_date"));
}

// A key the order does not name keeps its position after the named ones rather
// than jumping to the front.
TEST(SmartCardHandlerOrder, UnnamedKeysFollowTheNamedOnes)
{
    const QVariantList details = {
        summaryRow("annex.rs.personal", "zz_unknown", "Z"),
        summaryRow("annex.rs.personal", "street", "BULEVAR"),
    };
    const QVariantList out = SmartCardHandler::applyFieldOrder(details);
    EXPECT_EQ(out.at(0).toMap().value(QStringLiteral("fieldKey")).toString(), QStringLiteral("street"));
    EXPECT_EQ(out.at(1).toMap().value(QStringLiteral("fieldKey")).toString(), QStringLiteral("zz_unknown"));
}
