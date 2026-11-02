// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// The credential window's "Start service" action against a session bus that has
// the agent installed but not running (librekde_activatable_agent_bus): the
// agent is not on the bus until something starts it, and it claims its name a
// moment after launch.

#include "CredentialController.h"
#include "TestBus.h" // waitFor

#include <LibreSCRS/AgentClient/AgentClient.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QSignalSpy>
#include <QTimer>

#include <gtest/gtest.h>

#include <memory>

using LibreKDE::Credentials::CredentialController;
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

// The start runs from the event loop, not under the trigger: what it delivers
// re-binds the view the action lives in, so the trigger returns before the
// agent is waited for. A second trigger while that start waits -- delivered
// inside its local event loop -- asks for nothing more: one start, entered and
// left once, and the agent the first trigger started arrives.
TEST(CredentialAgentStart, ATriggerWhileTheStartWaitsDoesNothing)
{
    CredentialController ctl(std::make_shared<Client::AgentClient>());
    // The first classify runs on the event loop, not in the constructor.
    ASSERT_TRUE(waitFor([&] { return ctl.state() == int(CredentialController::State::AgentUnavailable); }));
    ASSERT_FALSE(agentOnBus());
    ASSERT_TRUE(ctl.agentInstalled()) << "an activatable agent is installed, merely stopped";
    QSignalSpy startingChanges(&ctl, &CredentialController::agentStartingChanged);

    ctl.startAgent();
    EXPECT_TRUE(ctl.agentStarting());
    EXPECT_FALSE(agentOnBus()) << "the trigger waited for the agent itself instead of leaving it to the event loop";

    bool secondTriggerLanded = false;
    bool startingAtSecondTrigger = false;
    QTimer::singleShot(0, &ctl, [&] {
        secondTriggerLanded = true;
        startingAtSecondTrigger = ctl.agentStarting();
        ctl.startAgent();
    });

    EXPECT_TRUE(waitFor([&] { return !ctl.agentStarting(); }, 8000));
    ASSERT_TRUE(secondTriggerLanded) << "the second trigger never arrived while the first start waited";
    EXPECT_TRUE(startingAtSecondTrigger);
    EXPECT_EQ(startingChanges.count(), 2) << "one start, entered and left once";
    EXPECT_TRUE(waitFor([] { return agentOnBus(); }));
    EXPECT_TRUE(waitFor([&] { return ctl.state() != int(CredentialController::State::AgentUnavailable); }));
}

// A start asked for while a refresh of the same client is already starting the
// agent sends nothing of its own: the client answers Requested from inside the
// refresh's wait, and the refresh reports the agent's arrival. That answer ends
// this start -- the action is usable again at once, not held until an arrival
// that a failed start would never bring.
TEST(CredentialAgentStart, AStartQueuedBehindARefreshEndsWithItsAnswer)
{
    auto client = std::make_shared<Client::AgentClient>();
    CredentialController ctl(client);
    ASSERT_TRUE(waitFor([&] { return ctl.state() == int(CredentialController::State::AgentUnavailable); }));
    ASSERT_FALSE(agentOnBus());
    QSignalSpy startingChanges(&ctl, &CredentialController::agentStartingChanged);

    // Connected after the controller's own handler, so this sees what it left behind.
    int answers = 0;
    bool requested = false;
    bool startingAfterAnswer = true;
    QObject::connect(client.get(), &Client::AgentClient::agentStartFinished, &ctl,
                     [&](Client::AgentStartResult result) {
                         ++answers;
                         requested = result == Client::AgentStartResult::Requested;
                         startingAfterAnswer = ctl.agentStarting();
                     });

    // Both from the event loop, in this order: the refresh's start waits in a
    // local event loop, and the start queued behind it runs from there.
    client->refreshDiscoveryLater();
    ctl.startAgent();
    EXPECT_TRUE(ctl.agentStarting());

    EXPECT_TRUE(waitFor([&] { return answers == 1; }, 8000));
    EXPECT_TRUE(requested) << "the start did not run inside the refresh's start";
    EXPECT_FALSE(startingAfterAnswer) << "a Requested answer left the action waiting";
    EXPECT_TRUE(waitFor([] { return agentOnBus(); }));
    EXPECT_TRUE(waitFor([&] { return ctl.state() != int(CredentialController::State::AgentUnavailable); }));
    EXPECT_FALSE(ctl.agentStarting());
    EXPECT_EQ(startingChanges.count(), 2) << "one start, entered and left once";
    EXPECT_EQ(answers, 1);
}
