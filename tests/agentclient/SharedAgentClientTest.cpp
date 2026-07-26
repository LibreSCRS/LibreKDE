// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// The shared, process-wide AgentClient (the Purpose fresh-client residual fix).
// Each Purpose Share action calls sharedAgentClient(); they must all receive the
// SAME instance, so the agent connection + ObjectManager discovery run once, not
// per action. Runs under dbus-run-session (the AgentClient ctor touches the
// session bus); no agent need be present — construction alone is exercised.

#include "AgentClient.h"
#include "SharedAgentClient.h"

#include <gtest/gtest.h>

#include <memory>

using namespace LibreKDE;

TEST(SharedAgentClient, ReturnsOneInstanceAcrossShareActions)
{
    std::shared_ptr<AgentClient> first = sharedAgentClient();
    std::shared_ptr<AgentClient> second = sharedAgentClient();

    ASSERT_NE(first.get(), nullptr);
    // Same object -> a second Share action reuses the first's connection/discovery.
    EXPECT_EQ(first.get(), second.get());
}
