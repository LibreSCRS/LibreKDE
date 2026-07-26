// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <memory>

/// @file
/// @brief The process-wide shared `AgentClient` accessor.

namespace LibreKDE {

class AgentClient;

/// @brief The one process-wide `AgentClient` (session bus), created lazily on
///        first call. Uses the same `Q_GLOBAL_STATIC` shared-Qt-state idiom as
///        `CardPhotoStore` (`sharedCardPhotoStore()`), but is NOT identical to it:
///        `CardPhotoStore` is a plain (non-`QObject`) value holder, whereas
///        `AgentClient` is a `QObject` owning a `QDBusServiceWatcher` + a
///        session-bus connection (see the teardown note in the `.cpp`). Every
///        caller — e.g. each Purpose Share action — gets the SAME instance, so the
///        agent connection + ObjectManager discovery are established once, not per
///        action. Co-owned via `shared_ptr` so a caller may hold it for an
///        operation's duration; its `QDBusServiceWatcher` keeps availability
///        live, so a long-lived shared client also SEES the D-Bus-activatable
///        agent appear after a cold start.
[[nodiscard]] std::shared_ptr<AgentClient> sharedAgentClient();

} // namespace LibreKDE
