// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once
#include <LibreSCRS/AgentClient/AgentCard.h>
#include <LibreSCRS/AgentClient/AgentClient.h>

#include <cstdint>

/// @file
/// @brief Host-side card capability predicates over the agent client's
///        deterministic `readers()` view. Sibling of Readers.h: pure functions
///        of the client's own registry, which the client library deliberately
///        does not carry, so the hosts share ONE copy here instead of each
///        surface re-adding its own.
///
/// Deliberately NOT promoted into `LibreAgent::ClientQt`. Which card a surface
/// reaches for is host POLICY, and the client library's contract is to publish
/// the registry, not to pick from it — the same call this repo already made for
/// the plugin-directory reporting it kept host-side.

namespace LibreKDE::Cards {

/// @brief Does @p card advertise @p cap?
///
/// The client publishes a card's capabilities as stable TOKENS; the pure
/// resolvers consume a bitfield. `capabilityBits()` is the exact inverse of the
/// tokenizer — a bit this build has no name for round-trips as `bit<i>` rather
/// than being dropped — so the round-trip is lossless, which is why every
/// caller may ask this question in bit terms.
[[nodiscard]] bool hasCapability(const LibreSCRS::AgentClient::AgentCard& card, std::uint32_t cap);

/// @brief The first present card advertising @p cap, in the client's id-sorted
///        reader order, or `nullptr` when no present card does.
///
/// "First" is the client's deterministic order, not an arbitrary one, so two
/// surfaces asking the same question of the same registry reach the same card.
[[nodiscard]] LibreSCRS::AgentClient::AgentCard* firstWithCapability(LibreSCRS::AgentClient::AgentClient& client,
                                                                     std::uint32_t cap);

} // namespace LibreKDE::Cards
