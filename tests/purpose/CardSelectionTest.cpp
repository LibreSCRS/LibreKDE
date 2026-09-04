// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Which card signs, against a real AgentClient talking to the FakeAgent on a
// private session bus. The chooser here is a recording fake, so every case can
// assert not only WHICH card came back but whether the user was asked at all —
// the half of the contract a return value cannot express.

#include "CardSelection.h"
#include "TestBus.h"

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include <LibreSCRS/AgentClient/AgentCard.h>
#include <LibreSCRS/AgentClient/AgentClient.h>
#include <LibreSCRS/AgentClient/AgentReader.h>

#include <QList>
#include <QString>
#include <gtest/gtest.h>
#include <memory>
#include <optional>

namespace Client = LibreSCRS::AgentClient;
using namespace LibreSCRS::AgentClient::Fakes;
using LibreKDE::Signing::chooseSigningCard;
using LibreKDE::Signing::SigningCardSelection;

namespace {

/// A CardChooser that records every call and answers by a scripted rule.
struct RecordingChooser
{
    int calls = 0;
    QList<LibreKDE::CardChoice> lastCandidates;
    /// Index into the candidate list to pick; a negative value cancels.
    int pick = 0;
    /// Answer with this id instead of a candidate's, when non-empty (the
    /// "chooser returned an id naming no candidate" case).
    QString forcedId;

    LibreKDE::CardChooser seam()
    {
        return [this](const QList<LibreKDE::CardChoice>& cands) -> std::optional<QString> {
            ++calls;
            lastCandidates = cands;
            if (!forcedId.isEmpty()) {
                return forcedId;
            }
            if (pick < 0 || pick >= cands.size()) {
                return std::nullopt;
            }
            return cands.at(pick).cardId;
        };
    }
};

std::unique_ptr<Client::AgentClient> makeClient(Harness& h)
{
    EXPECT_TRUE(h.claimsWellKnownService())
        << "the client binds the agent's well-known name; a Harness that does not claim it leaves every "
           "assertion below measuring an absent agent";
    return std::make_unique<Client::AgentClient>();
}

void addSecondReaderWithCard(Harness& h, Client::AgentClient& client, unsigned capabilities)
{
    h.emitReaderArrivesEmpty();
    ASSERT_TRUE(waitFor([&]() { return client.readers().size() == 2; }));
    h.emitArrivedReaderCardAdded(capabilities);
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return client.readers().at(1)->card() != nullptr; }));
}

} // namespace

// ZERO candidates: nothing to choose between, so nothing is asked. The
// never-prompt half of the contract, pinned so a later change cannot start
// raising a dialog on a desk with no signing card at all.
TEST(CardSelection, NoSigningCardAsksNothing)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData; // present, but cannot sign
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1 && client->readers().at(0)->card() != nullptr; }));

    RecordingChooser chooser;
    const SigningCardSelection selection = chooseSigningCard(*client, chooser.seam());
    EXPECT_EQ(selection.card, nullptr);
    EXPECT_FALSE(selection.cancelled) << "nobody was asked, so nobody declined";
    EXPECT_EQ(chooser.calls, 0) << "a chooser must not be raised when there is nothing to choose between";
}

// ONE candidate: the single-card desk — nearly every desk — keeps the behaviour
// it has always had. The card is taken silently; a dialog whose only answer is
// the card already in the reader is never raised.
TEST(CardSelection, SingleSigningCardIsTakenWithoutAsking)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1 && client->readers().at(0)->card() != nullptr; }));

    RecordingChooser chooser;
    const SigningCardSelection selection = chooseSigningCard(*client, chooser.seam());
    EXPECT_EQ(selection.card, client->readers().at(0)->card());
    EXPECT_FALSE(selection.cancelled);
    EXPECT_EQ(chooser.calls, 0) << "one candidate is not a choice";
}

// A second card that CANNOT sign does not make the choice ambiguous: the
// capable one is still taken silently.
TEST(CardSelection, ASecondIncapableCardDoesNotMakeItAmbiguous)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1; }));
    addSecondReaderWithCard(h, *client, Client::Cap::IdentityData);

    RecordingChooser chooser;
    const SigningCardSelection selection = chooseSigningCard(*client, chooser.seam());
    EXPECT_EQ(selection.card, client->readers().at(0)->card());
    EXPECT_EQ(chooser.calls, 0);
}

// TWO candidates: the user is asked, and the card they name is the one that
// comes back — NOT the first one, which is what an auto-select would return.
TEST(CardSelection, TwoSigningCardsRaiseTheChooserAndHonourTheChoice)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1; }));
    addSecondReaderWithCard(h, *client, Client::Cap::Pki);

    Client::AgentCard* first = client->readers().at(0)->card();
    Client::AgentCard* second = client->readers().at(1)->card();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    RecordingChooser chooser;
    chooser.pick = 1; // deliberately NOT the one an auto-select would take
    const SigningCardSelection selection = chooseSigningCard(*client, chooser.seam());

    EXPECT_EQ(chooser.calls, 1);
    ASSERT_EQ(selection.card, second) << "the chosen card must win over the first-in-order one";
    EXPECT_NE(selection.card, first);
    EXPECT_FALSE(selection.cancelled);

    // Both candidates are offered, in reader order, each labelled by the reader
    // holding it — the only thing that tells two cards apart on a desk.
    ASSERT_EQ(chooser.lastCandidates.size(), 2);
    EXPECT_EQ(chooser.lastCandidates.at(0).cardId, first->id());
    EXPECT_EQ(chooser.lastCandidates.at(1).cardId, second->id());
    EXPECT_EQ(chooser.lastCandidates.at(0).readerName, client->readers().at(0)->name());
    EXPECT_EQ(chooser.lastCandidates.at(1).readerName, client->readers().at(1)->name());
    EXPECT_FALSE(chooser.lastCandidates.at(0).readerName.isEmpty());
}

// A declined choice is reported as a cancellation, not as "no card": the caller
// has to be able to tell the user which of the two actually happened.
TEST(CardSelection, DecliningTheChooserCancelsRatherThanFallingBack)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1; }));
    addSecondReaderWithCard(h, *client, Client::Cap::Pki);

    RecordingChooser chooser;
    chooser.pick = -1; // cancel
    const SigningCardSelection selection = chooseSigningCard(*client, chooser.seam());

    EXPECT_EQ(chooser.calls, 1);
    EXPECT_EQ(selection.card, nullptr) << "cancelling must not fall back to signing with some card";
    EXPECT_TRUE(selection.cancelled);
}

// A caller with NO way to ask must not sign with a card the user never picked.
TEST(CardSelection, AmbiguityWithoutAChooserSelectsNothing)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1; }));
    addSecondReaderWithCard(h, *client, Client::Cap::Pki);

    const SigningCardSelection selection = chooseSigningCard(*client, LibreKDE::CardChooser());
    EXPECT_EQ(selection.card, nullptr);
    EXPECT_TRUE(selection.cancelled);
}

// The production chooser is a MODAL dialog: QInputDialog::getItem() runs a
// nested event loop, and everything the agent announces while it is up is
// delivered inside that loop — a card removal included, which the client
// services by DELETING the AgentCard. So a chooser that holds card pointers
// across the call hands back freed memory. These two pin the only safe shape:
// nothing but VALUES may cross the chooser, and the answer is re-resolved
// against the registry as it stands when the dialog closes.
//
// Removal is driven through the real wire (InterfacesRemoved) and the loop is
// pumped until the client has actually dropped the card, so the delete really
// has happened by the time the chooser answers — a synchronous fake that never
// spins a loop cannot reproduce this at all.
TEST(CardSelection, AChosenCardRemovedInsideTheChooserSelectsNothing)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1; }));
    addSecondReaderWithCard(h, *client, Client::Cap::Pki);

    Client::AgentCard* doomed = client->readers().at(0)->card();
    ASSERT_NE(doomed, nullptr);
    const QString doomedId = doomed->id();

    int calls = 0;
    const LibreKDE::CardChooser chooser = [&](const QList<LibreKDE::CardChoice>& cands) -> std::optional<QString> {
        ++calls;
        EXPECT_EQ(cands.size(), 2);
        // The card the user is about to name is pulled out of the reader while
        // the dialog is still open, and the client deletes it before we answer.
        h.setCardPresent(false);
        EXPECT_TRUE(waitFor([&]() { return client->card(doomedId) == nullptr; }))
            << "the removal never reached the client, so this test would not be exercising a freed card at all";
        return doomedId;
    };

    const SigningCardSelection selection = chooseSigningCard(*client, chooser);

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(selection.card, nullptr) << "the chosen card was deleted while the dialog was open — resolving it from a "
                                          "list captured BEFORE the dialog returns a dangling AgentCard*";
    EXPECT_FALSE(selection.cancelled) << "the user did not decline; the card they named stopped existing";
}

// The complementary half: a removal during the dialog must not poison a choice
// that is still valid. The OTHER card goes away, and the one the user actually
// named still comes back live and signable.
TEST(CardSelection, RemovingAnotherCardInsideTheChooserStillHonoursTheChoice)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1; }));
    addSecondReaderWithCard(h, *client, Client::Cap::Pki);

    Client::AgentCard* doomed = client->readers().at(0)->card();
    Client::AgentCard* survivor = client->readers().at(1)->card();
    ASSERT_NE(doomed, nullptr);
    ASSERT_NE(survivor, nullptr);
    const QString doomedId = doomed->id();
    const QString survivorId = survivor->id();

    const LibreKDE::CardChooser chooser = [&](const QList<LibreKDE::CardChoice>&) -> std::optional<QString> {
        h.setCardPresent(false); // the card the user did NOT pick
        EXPECT_TRUE(waitFor([&]() { return client->card(doomedId) == nullptr; }));
        return survivorId;
    };

    const SigningCardSelection selection = chooseSigningCard(*client, chooser);

    ASSERT_NE(selection.card, nullptr) << "the chosen card is still in the reader; only its neighbour left";
    EXPECT_EQ(selection.card, client->card(survivorId));
    EXPECT_EQ(selection.card->id(), survivorId);
    EXPECT_FALSE(selection.cancelled);
}

// A chooser answering with an id that names no candidate is a bug in the
// chooser; substituting some other card would sign with one the user did not
// pick, so nothing is selected.
TEST(CardSelection, AnUnknownChosenIdSelectsNothing)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1; }));
    addSecondReaderWithCard(h, *client, Client::Cap::Pki);

    RecordingChooser chooser;
    chooser.forcedId = QStringLiteral("no-such-card");
    const SigningCardSelection selection = chooseSigningCard(*client, chooser.seam());

    EXPECT_EQ(chooser.calls, 1);
    EXPECT_EQ(selection.card, nullptr);
    EXPECT_FALSE(selection.cancelled) << "the user did not decline; the chooser answered nonsense";
}

// An agent restart while the dialog is open re-mints object-path ids from a
// fresh per-process counter, so the id the user chose can come back ALIVE —
// naming a card in a different reader. The reader name in the choice list is
// what the person actually picked by ("the one on the left"); resolving the
// id alone would sign with an identity they did not pick.
TEST(CardSelection, AnAgentRestartRemintingIdsAcrossReadersSelectsNothing)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1; }));
    addSecondReaderWithCard(h, *client, Client::Cap::Pki);

    Client::AgentCard* second = client->readers().at(1)->card();
    ASSERT_NE(second, nullptr);
    const QString chosenId = second->id();

    int calls = 0;
    const LibreKDE::CardChooser chooser = [&](const QList<LibreKDE::CardChoice>& cands) -> std::optional<QString> {
        ++calls;
        EXPECT_EQ(cands.size(), 2);
        // The restart, under the open dialog: vanish, re-mint so each reader's
        // card now carries the OTHER card's path, reappear, re-discover. The
        // chosen id resolves a live card again — in the other reader.
        h.unregisterService();
        EXPECT_TRUE(waitFor([&]() { return client->readers().isEmpty(); }))
            << "the vanish never cleared the registry, so this test would not be exercising a restart at all";
        h.remintCardPathsSwapped();
        h.registerService();
        client->refreshDiscovery();
        EXPECT_TRUE(waitFor([&]() { return client->readers().size() == 2 && client->card(chosenId) != nullptr; }))
            << "the re-minted id never came back alive, so the wrong-reader resolution cannot be exercised";
        return chosenId;
    };

    const SigningCardSelection selection = chooseSigningCard(*client, chooser);

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(selection.card, nullptr)
        << "the chosen id now names a live card in a DIFFERENT reader; signing with it would use an "
           "identity the user did not pick";
    EXPECT_FALSE(selection.cancelled) << "the user did not decline; the id stopped meaning what they chose";
}
