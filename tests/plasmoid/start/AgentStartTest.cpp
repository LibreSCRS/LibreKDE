// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// The "Start service" control against a session bus that has the agent
// installed but not running: the state a fresh login leaves on a packaged
// system, where the agent is D-Bus activated and nothing has asked for it yet.
// This binary's bus is started with a configuration whose only service
// directory holds the agent's `.service` file, pointing at the FakeAgent as a
// separate program — so the agent is NOT on the bus until something starts it.

#include "CardStateModel.h"
#include "SmartCardHandler.h"
#include "TestBus.h"

#include <LibreSCRS/AgentClient/AgentClient.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QSignalSpy>
#include <QTimer>

#include <gtest/gtest.h>

#include <memory>

using State = LibreKDE::Plasmoid::CardStateModel::State;
using LibreKDE::Plasmoid::SmartCardHandler;
using LibreSCRS::AgentClient::Fakes::waitFor;
namespace Client = LibreSCRS::AgentClient;

namespace {

bool agentOnBus()
{
    return QDBusConnection::sessionBus()
        .interface()
        ->isServiceRegistered(QStringLiteral("org.librescrs.Agent"))
        .value();
}

} // namespace

TEST(AgentStart, StartServiceBringsUpAnInstalledAgent)
{
    SmartCardHandler handler(std::make_shared<Client::AgentClient>());
    ASSERT_EQ(handler.state(), static_cast<int>(State::AgentUnavailable));
    ASSERT_FALSE(agentOnBus()) << "the agent must not be running before the user asks for it";
    EXPECT_TRUE(handler.agentInstalled()) << "an activatable agent is installed, merely stopped";

    handler.startAgent();

    EXPECT_TRUE(waitFor([] { return agentOnBus(); }));
    EXPECT_TRUE(waitFor([&] { return handler.state() != static_cast<int>(State::AgentUnavailable); }));
    EXPECT_FALSE(handler.agentStartFailed());
}

// The start runs from the event loop, not under the click: the control that
// asked may be replaced by the agent's arrival, so the click returns before
// the agent is waited for. A second click while that start waits -- delivered
// inside its local event loop -- asks for nothing more: one start, entered and
// left once, and the agent the first click started arrives.
TEST(AgentStart, AClickWhileTheStartWaitsDoesNothing)
{
    SmartCardHandler handler(std::make_shared<Client::AgentClient>());
    ASSERT_FALSE(agentOnBus());
    ASSERT_FALSE(handler.agentStarting());
    QSignalSpy startingChanges(&handler, &SmartCardHandler::agentStartingChanged);

    handler.startAgent();
    EXPECT_TRUE(handler.agentStarting());
    EXPECT_FALSE(agentOnBus()) << "the click waited for the agent itself instead of leaving it to the event loop";

    bool secondClickLanded = false;
    bool startingAtSecondClick = false;
    QTimer::singleShot(0, &handler, [&] {
        secondClickLanded = true;
        startingAtSecondClick = handler.agentStarting();
        handler.startAgent();
    });

    EXPECT_TRUE(waitFor([&] { return !handler.agentStarting(); }, 8000));
    ASSERT_TRUE(secondClickLanded) << "the second click never arrived while the first start waited";
    EXPECT_TRUE(startingAtSecondClick);
    EXPECT_EQ(startingChanges.count(), 2) << "one start, entered and left once";
    EXPECT_TRUE(waitFor([] { return agentOnBus(); }));
    EXPECT_TRUE(waitFor([&] { return handler.state() != static_cast<int>(State::AgentUnavailable); }));
    EXPECT_FALSE(handler.agentStartFailed());
}

// A start asked for while a refresh is already starting the agent sends
// nothing of its own: the client answers Requested from inside the refresh's
// wait, and the refresh reports the agent's arrival. That answer ends this
// start -- the control is usable again at once, not held until an arrival that
// a failed start would never bring -- and it is not a failure.
TEST(AgentStart, AStartQueuedBehindARefreshEndsWithItsAnswer)
{
    auto client = std::make_shared<Client::AgentClient>();
    SmartCardHandler handler(client);
    ASSERT_FALSE(agentOnBus());
    QSignalSpy startingChanges(&handler, &SmartCardHandler::agentStartingChanged);

    // Connected after the handler's own slot, so this sees what it left behind.
    int answers = 0;
    bool requested = false;
    bool startingAfterAnswer = true;
    QObject::connect(client.get(), &Client::AgentClient::agentStartFinished, &handler,
                     [&](Client::AgentStartResult result) {
                         ++answers;
                         requested = result == Client::AgentStartResult::Requested;
                         startingAfterAnswer = handler.agentStarting();
                     });

    // Both from the event loop, in this order: the refresh's start waits in a
    // local event loop, and the start queued behind it runs from there.
    handler.requestRefresh();
    handler.startAgent();
    EXPECT_TRUE(handler.agentStarting());

    EXPECT_TRUE(waitFor([&] { return answers == 1; }, 8000));
    EXPECT_TRUE(requested) << "the start did not run inside the refresh's start";
    EXPECT_FALSE(startingAfterAnswer) << "a Requested answer left the control waiting";
    EXPECT_FALSE(handler.agentStartFailed());
    EXPECT_TRUE(waitFor([] { return agentOnBus(); }));
    EXPECT_TRUE(waitFor([&] { return handler.state() != static_cast<int>(State::AgentUnavailable); }));
    EXPECT_FALSE(handler.agentStarting());
    EXPECT_EQ(startingChanges.count(), 2) << "one start, entered and left once";
    EXPECT_EQ(answers, 1);
}
