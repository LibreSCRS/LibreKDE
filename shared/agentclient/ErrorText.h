// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <LibreSCRS/AgentClient/CallError.h>
#include <LibreSCRS/AgentClient/ErrorCode.h>

#include <QString>

/// @file
/// @brief This client's localized sentences for a finished operation's failure.
///
/// A failure arrives as four facts — error code, call error, the agent's message
/// key and its English message — and which of them a user reads is decided by
/// the agent client library (`describeFailure()`), the same way for every
/// client of the agent. What stays here is only this client's catalog: the
/// sentence for each code, each call class and each message key the library
/// says a client localizes.

namespace LibreKDE {

/// @brief Single source of client-side error copy.
///
/// Both entry points take the agent client library's own `ErrorCode` — the
/// wire-frozen, append-only taxonomy — directly. There is no local re-declaration
/// of it to keep in step: the client library owns the enumeration, and the only
/// thing this file decides is which codes get copy of their own.
namespace ErrorText {

/// @brief Localized message for @p code.
///
/// Returns a ki18n-translated string for codes the client localizes; for any
/// other (or `None`) returns @p msgFallback.
///
/// @warning May return an EMPTY string — `None` (or a code appended by an agent
///          newer than this build, which the client library decodes through
///          verbatim) with an empty @p msgFallback has nothing to return.
///          Callers rendering a finished operation want `forOutcome()`. This
///          entry point is for callers that hold a code and nothing else.
[[nodiscard]] QString forCode(LibreSCRS::AgentClient::ErrorCode code, const QString& msgFallback);

/// @brief Localized message for a failed operation. NEVER returns an empty
///        string.
///
/// The client library's `describeFailure()` picks the part of the report a user
/// reads; this renders it: a localized message key alone; a code's localized
/// headline with the agent's own line below it when the agent said more (the
/// generic sentence for a code this build does not name); the agent's message
/// for a refusal it answered or a failure nothing else classifies; the
/// localized sentence for a call that never reached the agent; the generic
/// sentence when nothing usable was reported.
///
/// The adoption surface: a caller passes `op->errorCode()`, `op->callError()`,
/// `op->messageKey()` and `op->messageFallback()` straight through.
[[nodiscard]] QString forOutcome(LibreSCRS::AgentClient::ErrorCode code, LibreSCRS::AgentClient::CallError call,
                                 const QString& messageKey, const QString& msgFallback);

} // namespace ErrorText

} // namespace LibreKDE
