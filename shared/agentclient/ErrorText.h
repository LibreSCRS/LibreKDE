// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <QString>
#include <cstdint>

/// @file
/// @brief Client-side mapping of the agent's stable `ErrorCode` taxonomy to a
///        localized, user-facing string.
///
/// The bulk of user-facing copy originates in the agent as a `LocalizedText`
/// `msgKey`/`msgFallback` pair on `Operation1.Finished` — the
/// client renders that, it does not invent copy. This maps the small set of
/// client-synchronous codes to a ki18n message, falling back to the
/// agent-provided `msgFallback` for anything not localized here.

namespace LibreKDE {

/// @brief Hand-maintained mirror of the agent's stable error taxonomy.
///        Append-only; never renumber.
///
/// The canonical wire contract is the errorCode enumeration documented in the
/// agent's published D-Bus interface description
/// `org.librescrs.Agent.Operation1.xml` — a new code lands there first. The
/// implementing enum is `LibreSCRS::Agent::ErrorCode`
/// (`include/LibreSCRS/Agent/value/ErrorTaxonomy.h` in the LibreAgent core),
/// machine-pinned to that XML agent-side. This client links no agent code, so
/// this mirror is updated on wire-contract (XML) diffs — extend the
/// `ErrorText.cpp` switch (or consciously leave the new code to the agent
/// fallback) and the guard tests in the same change.
enum class ErrorCode : std::uint32_t {
    None = 0,
    CardRemoved = 1,
    CredentialWrong = 2,
    CredentialBlocked = 3,
    CommunicationError = 4,
    ParseError = 5,
    UnsupportedCard = 6,
    AuthFailed = 7,
    PrompterError = 8,
    CapabilityMissing = 9,
    WatchdogTimeout = 10,
    KeyNotFound = 11,
    KeyAmbiguous = 12,
    CertExpiredBlocked = 13,
    ChainIncomplete = 14,
    TsaUnreachable = 15,
    SigningEngineError = 16,
    RateLimited = 17,
    EngineUnavailable = 18, // engine/security module could not load (deployment)
    InvalidDocument = 19,   // the document to sign is invalid/unreadable (client input)
};

/// @brief Single source of client-side error copy.
namespace ErrorText {

/// @brief Localized message for @p code.
///
/// Returns a ki18n-translated string for codes the client localizes; for any
/// other (or `None`) returns @p msgFallback (the agent's authored fallback).
[[nodiscard]] QString forCode(ErrorCode code, const QString& msgFallback);

} // namespace ErrorText

} // namespace LibreKDE
