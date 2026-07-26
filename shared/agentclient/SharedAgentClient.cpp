// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "SharedAgentClient.h"

#include "AgentClient.h"

#include <QGlobalStatic>

#include <memory>

namespace LibreKDE {

// The one process-wide AgentClient, using the same Q_GLOBAL_STATIC shared-Qt-state
// idiom as CardPhotoStore (the LibreKDE shared-Qt-state pattern). Constructed on
// first access via the AgentClient(QObject*) session-bus ctor; a shared_ptr owns
// it (no QObject parent), destroyed at process exit with the global static.
//
// NOTE (differs from CardPhotoStore): CardPhotoStore is a plain non-QObject value
// holder, but AgentClient is a QObject owning a QDBusServiceWatcher + a session-bus
// connection. Destroying a QObject/QDBusConnection at static teardown — possibly
// after QCoreApplication is gone, and (in the Purpose case) inside a dlopened
// plugin whose host owns the QApplication — can emit Qt teardown warnings or touch
// an already-torn-down connection. This is behaviourally acceptable
// (no functional effect; the agent connection is process-scoped anyway). If
// exit-time warnings ever appear, guard teardown by tying the lifetime to
// QCoreApplication::aboutToQuit or by intentionally leaking on exit.
Q_GLOBAL_STATIC_WITH_ARGS(std::shared_ptr<AgentClient>, g_sharedAgentClient, (std::make_shared<AgentClient>()))

std::shared_ptr<AgentClient> sharedAgentClient()
{
    return *g_sharedAgentClient;
}

} // namespace LibreKDE
