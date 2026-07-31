// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once
#include <LibreSCRS/AgentClient/CredentialTypes.h>

#include <QString>
#include <optional>

/// @file
/// @brief Client-side, localized copy for the agent's credential vocabulary.
///        ki18n helpers only; no LibreMiddleware, no secrets. Guidance strings
///        still originate in the agent (see the `guidance()` helper) — this maps
///        only the fixed enum vocabulary.
///
/// The enums are the agent client library's own (`CredentialKind`,
/// `CredentialState`, `CredentialOutcome`), consumed directly. Translation is
/// deliberately NOT that library's job — it renders no text — so the copy lives
/// here while the vocabulary lives there, and there is nothing to keep in step.

namespace LibreKDE::CredentialText {

/// @brief User-facing message for a mutation @p outcome, phrased for the
///        @p presented credential (e.g. "The User PIN was not correct.").
///        When the result delivered @p retriesLeft, an InvalidPin outcome
///        carries the attribution counter ("The User PIN was not
///        correct — 2 attempts left."), pluralized per locale.
/// @return Empty for CredentialOutcome::UserCancelled (a neutral result that
///         must not surface as an error banner).
[[nodiscard]] QString outcomeMessage(LibreSCRS::AgentClient::CredentialOutcome outcome,
                                     LibreSCRS::AgentClient::CredentialKind presented,
                                     std::optional<int> retriesLeft = std::nullopt);

/// @brief Whether @p outcome is neutral (success or user cancel) and so must
///        not be rendered as an error.
[[nodiscard]] bool isNeutral(LibreSCRS::AgentClient::CredentialOutcome outcome);

/// @brief Localized display name for a credential @p kind (User PIN, Signing
///        PIN, PUK, CAN, or a generic fallback).
[[nodiscard]] QString kindName(LibreSCRS::AgentClient::CredentialKind kind);

/// @brief Localized display name for a credential @p state (Transport,
///        Operational, Change required, Blocked, Unknown).
[[nodiscard]] QString stateName(LibreSCRS::AgentClient::CredentialState state);

/// @brief Resolve agent-supplied guidance to display text: translate @p key via
///        ki18n when a catalog entry exists, otherwise return the agent's
///        English @p fallback. Copy is authored agent-side; the client never
///        invents guidance.
[[nodiscard]] QString guidance(const std::optional<QString>& key, const std::optional<QString>& fallback);

} // namespace LibreKDE::CredentialText
