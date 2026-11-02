// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// The shared FakeAgent as a program the session bus starts on demand, through
// an ordinary `.service` file — the way a login session on a packaged system
// starts the real agent. It claims the agent's well-known name on the bus that
// launched it and exits when that bus goes away, so it never outlives the test.

#include "FakeAgent.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QTimer>

#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::ActivationBus, QStringLiteral("starter"));
    if (!bus.isConnected()) {
        std::fprintf(stderr, "activatable agent: no bus started this process\n");
        return 1;
    }
    LibreSCRS::AgentClient::Fakes::FakeAgent::Config config;
    config.service = QStringLiteral("org.librescrs.Agent");
    LibreSCRS::AgentClient::Fakes::FakeAgent agent(bus, config);
    // The name is claimed a moment after launch, not at once, so a start
    // request is observably in flight for that long: a test can act while the
    // client still waits for the agent. The tests that act in that window are
    // only as sound as its width — a test process that is slow to get the CPU
    // back must still find the agent missing — so it is a full second, which a
    // real agent can take as well.
    constexpr int kClaimDelayMs = 1000;
    QTimer::singleShot(kClaimDelayMs, &app, [&bus, &config]() {
        if (!bus.registerService(config.service)) {
            std::fprintf(stderr, "activatable agent: could not claim the agent name\n");
            QCoreApplication::exit(1);
        }
    });
    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&bus]() {
        if (!bus.isConnected()) {
            QCoreApplication::quit();
        }
    });
    watchdog.start(100);
    QTimer::singleShot(120000, &app, &QCoreApplication::quit);
    return QCoreApplication::exec();
}
