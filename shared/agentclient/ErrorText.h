// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <LibreSCRS/AgentClient/CallError.h>
#include <LibreSCRS/AgentClient/ErrorCode.h>

#include <QString>

/// @file
/// @brief Client-side mapping of a finished operation's failure axes to a
///        localized, user-facing string.
///
/// The bulk of user-facing copy originates in the agent as a `LocalizedText`
/// `msgKey`/`msgFallback` pair on `Operation1.Finished` — the
/// client renders that, it does not invent copy. This maps the small set of
/// client-synchronous codes to a ki18n message, falling back to the
/// agent-provided `msgFallback` for anything not localized here.
///
/// A failure has TWO mutually exclusive classification axes, and rendering only
/// the first one leaves a hole. `ErrorCode` is the agent's wire answer; it is
/// `None` whenever the call never got a wire-level answer at all (no service
/// reachable, a permission refusal, a broken connection), and those failures
/// carry their reason on `AgentOperation::callError()` instead. Rendering only
/// `ErrorCode` therefore drops straight through to the raw transport string —
/// untranslated developer/bus text — or, when that string is empty, to nothing
/// at all. `forOutcome()` below consults both axes and can never return empty.

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
/// other (or `None`) returns @p msgFallback (the agent's authored fallback).
///
/// @warning May return an EMPTY string — `None` (or a code appended by an agent
///          newer than this build, which the client library decodes through
///          verbatim) with an empty @p msgFallback has nothing to return.
///          Callers rendering a banner want `forOutcome()` instead, which
///          cannot. This entry point stays for callers that hold a code and
///          nothing else.
[[nodiscard]] QString forCode(LibreSCRS::AgentClient::ErrorCode code, const QString& msgFallback);

/// @brief Localized message for a failed operation, composed from all three of
///        its outcome inputs. NEVER returns an empty string.
///
/// Resolution order, first match wins:
///   1. this client's mapping names @p code → that. The agent answered, and its
///      own taxonomy is the most specific axis there is. A code this client does
///      not name — `None`, or one an agent newer than this build appended — does
///      NOT match: it carries no information of its own, so resolution continues
///      rather than handing back @p msgFallback dressed as a mapping result.
///   2. @p call is not `None` → localized copy for that classification. This
///      is the transport-level failure axis: no service reachable, a permission
///      refusal, a timeout, a broken connection, a reply that did not decode.
///      Deliberately ahead of @p msgFallback: on these routes the fallback is
///      whatever raw string the bus or this client's own diagnostics produced,
///      which is untranslated and written for a developer.
///   3. non-blank @p msgFallback → the agent's authored English fallback. Only
///      reachable when neither axis classified the failure, which is to say
///      when the agent DID answer and the answer's message is the only thing
///      that carries meaning.
///   4. otherwise → a localized generic. The floor that makes a blank banner
///      impossible.
///
/// A whitespace-only @p msgFallback counts as no message throughout: it is
/// non-empty to `QString` and blank to a user, and it is the user's reading that
/// this function answers to.
///
/// `call` is the coarser axis by construction — several distinct wire errors
/// collapse onto one enumerator — which is exactly why it resolves after
/// @p code rather than before it.
///
/// The adoption surface: a caller passes `op->errorCode()`, `op->callError()`
/// and `op->messageFallback()` straight through, with no re-spelling step of its
/// own.
[[nodiscard]] QString forOutcome(LibreSCRS::AgentClient::ErrorCode code, LibreSCRS::AgentClient::CallError call,
                                 const QString& msgFallback);

} // namespace ErrorText

} // namespace LibreKDE
