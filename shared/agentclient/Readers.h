// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once
#include <LibreSCRS/AgentClient/AgentClient.h>
#include <LibreSCRS/AgentClient/AgentReader.h>

/// @file
/// @brief Host-side reader finders over the agent client's deterministic
///        `readers()` view. Pure functions of that list: selection policy is
///        deliberately NOT the client library's to carry, so the hosts share
///        one copy here instead of each component re-adding its own.

namespace LibreKDE::Readers {

/// @brief The first reader (in the client's id-sorted order) holding a
///        resolvable card — the "obvious card" every unbound surface manages.
[[nodiscard]] LibreSCRS::AgentClient::AgentReader* firstReaderWithCard(LibreSCRS::AgentClient::AgentClient& client);

} // namespace LibreKDE::Readers
