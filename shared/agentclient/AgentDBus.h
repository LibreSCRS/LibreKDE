// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

/// @file
/// @brief Shared D-Bus wire constants for `librekde-agentclient`.
///
/// The single source of truth for the agent's bus name, the manager object
/// path, and every interface name the client touches. These were previously
/// hand-spelled as file-scope constants in each translation unit (the same
/// `org.freedesktop.DBus.Properties` and `org.librescrs.Agent.Operation.*`
/// strings re-typed across AgentClient/AgentCard/AgentReader/AgentOperation),
/// so a typo in any one copy was a silent routing failure with no compile
/// error. Declaring them once here keeps every call site referencing the same
/// literal. (The agent XML in `LibreLinux/agent/dbus/*.xml` remains the
/// cross-stack contract; this header is the client-side mirror of those names.)

namespace LibreKDE {

/// @brief Cap (ms) on every D-Bus call the client issues — the synchronous
///        calls on the (often main/GUI) thread (method entry, GetManagedObjects,
///        GetResult recovery) and the asynchronous property-refresh GetAll
///        alike. A wedged agent must never stall the caller for the
///        multi-second default D-Bus timeout (~25 s); this caps the worst-case
///        synchronous freeze at a few seconds and bounds how long an async
///        refresh can stay in flight before its error reply is delivered.
inline constexpr int kPropTimeoutMs = 3000;

/// @brief Cap (ms) on the client's DISCOVERY calls: `NameHasOwner` (is the
///        agent on the bus?) and the initial `GetManagedObjects`. Snappier than
///        kPropTimeoutMs because a registry read against a healthy agent is
///        instant, so a wedged agent must not make `ls card:/` sit for the full
///        kPropTimeoutMs. Driven through cappedCall() so the wait is bounded AND
///        keeps a running outer event loop responsive (the plasmoid/Purpose GUI
///        constructs AgentClient with a live loop), rather than freezing it for
///        the whole timeout as a bare QDBus::Block would.
inline constexpr int kDiscoveryTimeoutMs = 1000;

/// @brief Backstop (ms) for a card-read Operation that never advances while the
///        agent stays alive AND the card stays seated (an orphaned op the
///        agent's own WatchdogTimeout somehow never terminates). Sized ABOVE the
///        agent watchdog so the agent's clean Finished normally wins; the client
///        backstop only catches a truly stuck MACHINE phase. It is NEVER applied
///        during AwaitingConsent/Authenticating — a human at the CAN/PIN prompter
///        legitimately takes their time. driveToFinished() stops the
///        timer for those phases and resets it on every machine-phase tick.
inline constexpr int kOpStallTimeoutMs = 35000;

// Bus name + manager object path.
inline constexpr const char* kService = "org.librescrs.Agent";
inline constexpr const char* kRootPath = "/org/librescrs/Agent";

// Standard freedesktop interfaces.
inline constexpr const char* kObjectManagerIface = "org.freedesktop.DBus.ObjectManager";
inline constexpr const char* kPropertiesIface = "org.freedesktop.DBus.Properties";

// Agent object interfaces.
inline constexpr const char* kReaderIface = "org.librescrs.Agent.Reader1";
inline constexpr const char* kCardIface = "org.librescrs.Agent.Card1";
inline constexpr const char* kPkcs11Iface = "org.librescrs.Agent.Pkcs11_1";

// Operation1 + its typed result interfaces.
inline constexpr const char* kOperationIface = "org.librescrs.Agent.Operation1";
inline constexpr const char* kSignIface = "org.librescrs.Agent.Operation.Sign1";
inline constexpr const char* kIdentityIface = "org.librescrs.Agent.Operation.Identity1";
inline constexpr const char* kCertificatesIface = "org.librescrs.Agent.Operation.Certificates1";
inline constexpr const char* kPhotoIface = "org.librescrs.Agent.Operation.Photo1";
inline constexpr const char* kCredentialsIface = "org.librescrs.Agent.Credentials1";
inline constexpr const char* kOperationCredentialsIface = "org.librescrs.Agent.Operation.Credentials1";

} // namespace LibreKDE
