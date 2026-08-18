// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// The shared card capability predicates, over a real AgentClient talking to the
// FakeAgent on a private session bus. This exercises the SAME definitions the
// Purpose plugin, the signing core, the plasmoid and the credentials window
// link — the point of moving them here. A suite carrying its own copy of the
// primitive can only ever measure the copy.

#include "Cards.h"
#include "TestBus.h"

#include <LibreSCRS/AgentClient/AgentCapabilities.h>
#include <LibreSCRS/AgentClient/AgentCard.h>
#include <LibreSCRS/AgentClient/AgentClient.h>
#include <LibreSCRS/AgentClient/AgentReader.h>

#include <gtest/gtest.h>
#include <memory>

namespace Client = LibreSCRS::AgentClient;
using namespace LibreKDETest;
using LibreKDE::Cards::cardsWithCapability;
using LibreKDE::Cards::hasCapability;

namespace {

// The client binds the agent's real well-known name, so the fake has to answer
// to it — the same reason every SmartCardHandler case builds its Harness this
// way.
std::unique_ptr<Client::AgentClient> makeClient(Harness& h)
{
    EXPECT_TRUE(h.claimsWellKnownService())
        << "the client binds the agent's well-known name; a Harness that does not claim it leaves every "
           "assertion below measuring an absent agent";
    return std::make_unique<Client::AgentClient>();
}

// reader/1 arrives already holding a card of the given capabilities — the
// faithful three-step sequence (reader, then card, then the HasCard flip), with
// the client loop pumped between the steps.
void addSecondReaderWithCard(Harness& h, Client::AgentClient& client, unsigned capabilities)
{
    h.emitReaderArrivesEmpty();
    ASSERT_TRUE(waitFor([&]() { return client.readers().size() == 2; }));
    h.emitArrivedReaderCardAdded(capabilities);
    h.emitArrivedReaderHasCard();
    ASSERT_TRUE(waitFor([&]() { return client.readers().at(1)->card() != nullptr; }));
}

} // namespace

// No agent on the bus at all: the registry is empty and the walk answers an
// empty list rather than dereferencing its way through it.
TEST(Cards, NoCandidatesWithoutAnAgent)
{
    auto client = std::make_unique<Client::AgentClient>();
    EXPECT_TRUE(cardsWithCapability(*client, Client::Cap::Pki).isEmpty());
    EXPECT_TRUE(client->readers().isEmpty());
}

// A reader is present but holds no card.
TEST(Cards, NoCandidatesWhenNoReaderHoldsACard)
{
    FakeAgent::Config cfg;
    cfg.hasCard = false;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return !client->readers().isEmpty(); }));
    EXPECT_TRUE(cardsWithCapability(*client, Client::Cap::Pki).isEmpty());
}

// A card is present but advertises a DIFFERENT capability. This is a capability
// question, not a presence question.
TEST(Cards, NoCandidatesWhenNoCardAdvertisesTheCapability)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1 && client->readers().at(0)->card() != nullptr; }));
    Client::AgentCard* card = client->readers().at(0)->card();
    ASSERT_NE(card, nullptr);

    EXPECT_TRUE(hasCapability(*card, Client::Cap::IdentityData));
    EXPECT_FALSE(hasCapability(*card, Client::Cap::Pki));
    EXPECT_TRUE(cardsWithCapability(*client, Client::Cap::Pki).isEmpty());
}

// Two cards present, only the SECOND capable: the walk passes over the first
// rather than stopping at "a card exists".
TEST(Cards, IncapableCardIsNotACandidate)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::IdentityData; // reader/0: identity only
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1; }));
    addSecondReaderWithCard(h, *client, Client::Cap::Pki);

    const QList<Client::AgentCard*> pki = cardsWithCapability(*client, Client::Cap::Pki);
    ASSERT_EQ(pki.size(), 1);
    EXPECT_EQ(pki.at(0), client->readers().at(1)->card());
    EXPECT_NE(pki.at(0), client->readers().at(0)->card()) << "the identity-only card must not be a candidate";
}

// Two capable cards: both are candidates, in the client's own id-sorted reader
// order, so two surfaces asking of the same registry see the same list.
TEST(Cards, TwoCapableCardsAreBothCandidatesInReaderOrder)
{
    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1; }));
    addSecondReaderWithCard(h, *client, Client::Cap::Pki);

    const QList<Client::AgentCard*> pki = cardsWithCapability(*client, Client::Cap::Pki);
    ASSERT_EQ(pki.size(), 2);
    EXPECT_EQ(pki.at(0), client->readers().at(0)->card());
    EXPECT_EQ(pki.at(1), client->readers().at(1)->card());
}

// A capability bit this build has NO NAME for. The wire carries capabilities as
// tokens, and the client round-trips an unnamed bit as "bit<i>" rather than
// dropping it — so the predicate answers correctly for a capability added by a
// newer agent, which is the whole reason it asks in bit terms.
TEST(Cards, UnnamedCapabilityBitSurvivesTheTokenRoundTrip)
{
    constexpr std::uint32_t kUnnamedBit = 1U << 20;
    static_assert((kUnnamedBit & (Client::Cap::Pki | Client::Cap::IdentityData | Client::Cap::EmrtdCrypto |
                                  Client::Cap::PinManagement)) == 0U,
                  "the probe bit must be one this build does not name");

    FakeAgent::Config cfg;
    cfg.capabilities = Client::Cap::Pki | kUnnamedBit;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1 && client->readers().at(0)->card() != nullptr; }));
    Client::AgentCard* card = client->readers().at(0)->card();
    ASSERT_NE(card, nullptr);

    EXPECT_TRUE(hasCapability(*card, kUnnamedBit));
    EXPECT_TRUE(hasCapability(*card, Client::Cap::Pki));
    EXPECT_EQ(cardsWithCapability(*client, kUnnamedBit), QList<Client::AgentCard*>{card});
}
