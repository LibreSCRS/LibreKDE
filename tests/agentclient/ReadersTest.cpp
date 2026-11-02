// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// The shared reader label, the one every surface (plasmoid, credentials window,
// card:/ worker) shows a reader under. The pure case runs in the test's C
// locale, so the English msgids come back; the live cases go through a real
// AgentClient against the FakeAgent on a private session bus.

#include "Readers.h"
#include "TestBus.h"

#include <LibreSCRS/AgentClient/AgentClient.h>
#include <LibreSCRS/AgentClient/AgentReader.h>

#include <gtest/gtest.h>
#include <memory>

namespace Client = LibreSCRS::AgentClient;
using namespace LibreSCRS::AgentClient::Fakes;
using LibreKDE::Readers::readerLabel;

namespace {

const QString kOmnikeyRaw = QStringLiteral(
    "HID Global OMNIKEY 5422 Smartcard Reader [OMNIKEY 5422 Smartcard Reader] (IM0O2C00NF10456904) 00 00");

std::unique_ptr<Client::AgentClient> makeClient(Harness& h)
{
    EXPECT_TRUE(h.claimsWellKnownService())
        << "the client binds the agent's well-known name; a Harness that does not claim it leaves every "
           "assertion below measuring an absent agent";
    return std::make_unique<Client::AgentClient>();
}

} // namespace

// The agent's model, with the interface said in the holder's language.
TEST(Readers, LabelSaysTheInterfaceInTheHoldersLanguage)
{
    const QString model = QStringLiteral("OMNIKEY 5422");
    EXPECT_EQ(readerLabel(model, Client::ReaderInterface::Contact), QStringLiteral("OMNIKEY 5422 — contact"));
    EXPECT_EQ(readerLabel(model, Client::ReaderInterface::Contactless), QStringLiteral("OMNIKEY 5422 — contactless"));
    // A single-interface reader, or one the agent could not tell: the model alone.
    EXPECT_EQ(readerLabel(model, Client::ReaderInterface::Unknown), model);
}

// Over a live reader the label is what the agent says now, not the raw PC/SC
// name, and it follows a relabel while the name stays.
TEST(Readers, LabelIsTheAgentsAndFollowsARelabel)
{
    FakeAgent::Config cfg;
    cfg.hasCard = false;
    cfg.readerName = kOmnikeyRaw;
    cfg.readerModel = QStringLiteral("OMNIKEY 5422");
    cfg.readerInterface = QStringLiteral("contact");
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1; }));
    Client::AgentReader* reader = client->readers().first();
    EXPECT_EQ(readerLabel(*reader), QStringLiteral("OMNIKEY 5422 — contact"));

    h.emitReaderRelabelled(QStringLiteral("OMNIKEY 5422 (4569)"), QStringLiteral("contactless"));
    ASSERT_TRUE(waitFor([&]() { return readerLabel(*reader) == QStringLiteral("OMNIKEY 5422 (4569) — contactless"); }))
        << "label is still " << readerLabel(*reader).toStdString();
    EXPECT_EQ(reader->name(), kOmnikeyRaw);
}

// A reader the agent sends no label for is shown by its name as the agent has
// it; nothing here shortens it by a rule of its own.
TEST(Readers, AReaderTheAgentLeavesUnlabelledIsShownByItsName)
{
    const QString raw = QStringLiteral("Gemalto PC Twin Reader (69988A87) 00 00");
    FakeAgent::Config cfg;
    cfg.hasCard = false;
    cfg.readerName = raw;
    Harness h(cfg, BusNames::UniqueAndWellKnown);

    auto client = makeClient(h);
    ASSERT_TRUE(waitFor([&]() { return client->readers().size() == 1; }));
    EXPECT_EQ(readerLabel(*client->readers().first()), raw);
}
