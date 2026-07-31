// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "ErrorText.h"

#include <KLocalizedString>

#include <optional>

namespace LibreKDE::ErrorText {

namespace {

using LibreSCRS::AgentClient::CallError;
using LibreSCRS::AgentClient::ErrorCode;

/// @brief The floor of the resolution order: what a failure that classified on
///        neither axis and carried no message can still honestly be told to a
///        user. Non-empty by construction (see `localizedCopyFor`'s ki18nd note).
[[nodiscard]] QString genericFailure()
{
    return ki18ndc("librekde", "@info:status the operation failed without reporting any reason",
                   "The operation did not finish, and no reason was reported.")
        .toString();
}

/// @brief Localized copy for a transport-level call failure.
///
/// These are the failures that never got a wire-level answer, so they carry no
/// `ErrorCode` and the only string the transport has is raw bus text or one of
/// this client's own diagnostics — neither of which is user copy. The wording
/// follows what each enumerator MEANS (see `CallError`'s own documentation),
/// not how it is spelled: `AccessDenied` is the local IPC access-control layer
/// refusing, not a card authentication failure; `Timeout` is the client's own
/// reply budget elapsing, not the agent's watchdog (`ErrorCode::WatchdogTimeout`,
/// which keeps its own separate copy below).
///
/// The vocabulary is coarse on purpose — several distinct wire errors share one
/// enumerator — so the copy stays at the granularity the enumerator actually
/// supports rather than naming a specific cause it cannot know.
///
/// Exhaustive switch with no `default`, so an enumerator appended upstream is a
/// `-Wswitch` diagnostic here rather than a silent fall to the generic. A
/// diagnostic, not a build failure: this library carries no `-Werror`, and the
/// gate that does fail a build is `-Werror=switch` on ErrorTextCoverageTest,
/// which walks the same enumeration. The fall-through return still keeps the
/// function total (and non-empty) for a value from a newer client library than
/// this file was compiled against.
[[nodiscard]] QString forCallError(CallError call)
{
    switch (call) {
    case CallError::AgentUnavailable:
        return ki18ndc("librekde", "@info:status the smart-card service could not be reached at all",
                       "Could not reach the smart-card service. Check that it is installed and running.")
            .toString();
    case CallError::Timeout:
        return ki18ndc("librekde", "@info:status the smart-card service did not answer in time",
                       "The smart-card service did not answer in time. Try again.")
            .toString();
    case CallError::AccessDenied:
        // States the fact and stops there, deliberately. The same bucket also
        // carries the caller simply not being logged in, and that user is not
        // lacking a permission — pointing them at an administrator sends them
        // the wrong way. Nothing on this axis can tell the two apart; only the
        // wire's own named-error axis can, so precise help belongs there.
        return ki18ndc("librekde", "@info:status the system denied this application access to the smart-card service",
                       "Permission to use the smart-card service was denied.")
            .toString();
    case CallError::InvalidArguments:
        // The widest bucket, and the one where naming either an actor or a
        // recovery would be a lie. The rejector varies: this application
        // refusing its own caller's arguments before anything is sent, the
        // agent refusing the request at method entry, or the bus refusing to
        // dispatch it at all. And the members' recoveries diverge — a stale
        // identifier wants a re-read, a version skew wants an update, a bad
        // argument is a defect in the calling code — with only the wire's own
        // named-error axis able to tell them apart, which this axis is not.
        // So the copy states the one thing true of every member: it was
        // rejected, and nothing was done about it.
        return ki18ndc("librekde",
                       "@info:status the request was rejected before any work on it started; do not name who "
                       "rejected it, it may be this application, the smart-card service, or the message bus",
                       "This request was refused before any work on it started.")
            .toString();
    case CallError::TransportFailure:
        return ki18ndc("librekde", "@info:status the connection to the smart-card service broke during the request",
                       "The connection to the smart-card service was lost. Try again.")
            .toString();
    case CallError::ProtocolError:
        return ki18ndc("librekde", "@info:status the smart-card service's reply could not be understood",
                       "The smart-card service replied in a way this application does not understand. The two may be "
                       "different versions.")
            .toString();
    case CallError::None:
        break;
    }
    return genericFailure();
}

/// @brief This client's own decision for @p code, or `std::nullopt` when it does
///        not name the code at all.
///
/// Engaged means "this client decided what to say about this code". Disengaged
/// means the opposite — `None`, or a code an agent newer than this build appended
/// — and it is a genuinely different fact from "decided to echo the agent's
/// message", which is what `SigningEngineError` deliberately does.
///
/// Collapsing those two into one string is what let an unnamed code hand a
/// caller its own @p msgFallback back while looking like a mapping result, so
/// the distinction is carried in the type rather than reconstructed downstream
/// by testing the returned string for emptiness.
///
/// Every engaged value is non-blank, provided @p msgFallback is itself either
/// empty or non-blank: `ki18nd("librekde", msgid).toString()` yields the
/// translation or the English msgid on a catalog miss, never empty.
[[nodiscard]] std::optional<QString> localizedCopyFor(ErrorCode code, const QString& msgFallback)
{
    // Only the codes a client can meaningfully phrase synchronously are
    // localized here; everything else (and None) is not this file's to name.
    switch (code) {
    case ErrorCode::CardRemoved:
        return ki18nd("librekde", "The card was removed before the operation finished.").toString();
    case ErrorCode::CredentialWrong:
        return ki18nd("librekde", "The PIN or access code entered was incorrect.").toString();
    case ErrorCode::CredentialBlocked:
        return ki18nd("librekde", "The card credential is blocked. Unblock it before retrying.").toString();
    case ErrorCode::CommunicationError:
        return ki18nd("librekde", "Communication with the card reader failed.").toString();
    case ErrorCode::ParseError:
        return ki18nd("librekde", "The data read from the card could not be interpreted.").toString();
    case ErrorCode::UnsupportedCard:
        return ki18nd("librekde", "This card is not supported.").toString();
    case ErrorCode::AuthFailed:
        return ki18nd("librekde", "Authentication with the card failed.").toString();
    case ErrorCode::PrompterError:
        return ki18nd("librekde", "The secure entry prompt could not be shown.").toString();
    case ErrorCode::CapabilityMissing:
        return ki18nd("librekde", "This card does not support the requested operation.").toString();
    case ErrorCode::WatchdogTimeout:
        return ki18nd("librekde", "The operation timed out.").toString();
    case ErrorCode::KeyNotFound:
        return ki18nd("librekde", "The selected certificate could not be found on the card.").toString();
    case ErrorCode::KeyAmbiguous:
        return ki18nd("librekde", "More than one key matched the selection.").toString();
    case ErrorCode::CertExpiredBlocked:
        return ki18nd("librekde", "The signing certificate has expired.").toString();
    case ErrorCode::ChainIncomplete:
        return ki18nd("librekde", "The certificate chain could not be completed.").toString();
    case ErrorCode::TsaUnreachable:
        return ki18nd("librekde", "The timestamp authority is unreachable.").toString();
    case ErrorCode::SigningEngineError:
        // The catch-all engine code defers to the agent's own message when it
        // carried one, matching the None-defers-to-msgFallback rule below.
        // Today the agent's msgFallback for this code is usually the LM's own
        // (near-identical, English) generic, so the practical win is small — the
        // common deployment failure is separated out to its own LOCALIZED
        // EngineUnavailable code. This branch keeps the door open for
        // an agent that sends a genuinely specific message on this code without
        // the client having to hardcode-flatten it.
        return msgFallback.isEmpty() ? ki18nd("librekde", "The signing engine reported an error.").toString()
                                     : msgFallback;
    case ErrorCode::InvalidDocument:
        return ki18nd("librekde", "The document you tried to sign is invalid or could not be read. Check the file.")
            .toString();
    case ErrorCode::EngineUnavailable:
        return ki18nd("librekde",
                      "The signing service is not set up correctly — its security module could not be loaded. "
                      "Check the installation.")
            .toString();
    case ErrorCode::RateLimited:
        return ki18nd("librekde", "Too many signing requests. Try again shortly.").toString();
    case ErrorCode::None:
        break;
    default:
        // REQUIRED, not stylistic: the client library decodes an `ErrorCode`
        // numeric value this build does not know through VERBATIM rather than
        // rejecting it, and its header therefore obliges every switch over the
        // type to carry a `default:` arm and treat an unrecognised value as
        // opaque display/log data. Falling here is the disengaged answer — this
        // build has no copy for that code — which is exactly how the resolution
        // order in `forOutcome` wants an unnamed code handled.
        //
        // The cost of this arm is that a code APPENDED to the client library is
        // no longer a `-Wswitch` diagnostic here. That guard moves wholesale to
        // ErrorTextCoverageTest, whose classification switch over the same
        // enumeration carries no `default:` and is compiled `-Werror=switch`.
        break;
    }
    return std::nullopt;
}

} // namespace

QString forCode(ErrorCode code, const QString& msgFallback)
{
    return localizedCopyFor(code, msgFallback).value_or(msgFallback);
}

QString forOutcome(ErrorCode code, CallError call, const QString& msgFallback)
{
    // A whitespace-only agent message is not a message. It is non-empty by
    // QString's reckoning and blank by the user's, which is the outcome this
    // function exists to prevent — so it is treated as ABSENT here, once, and
    // every arm below sees either a real message or none at all. Normalizing at
    // the top rather than per-arm also keeps `localizedCopyFor`'s
    // SigningEngineError decision ("defer to the agent's message when it sent
    // one") from deferring to whitespace.
    const QString message = msgFallback.trimmed().isEmpty() ? QString() : msgFallback;

    // Arm 1 — the agent's own taxonomy, when this client names the code.
    // `localizedCopyFor` is disengaged (rather than echoing @p message) for a
    // code it does not name, so an unnamed code cannot short-circuit the order
    // and hand a user the raw transport string that arm 2 exists to replace.
    // Every engaged value is non-blank, so no emptiness re-check is needed here
    // — the type carries what a string comparison used to guess.
    if (const std::optional<QString> named = localizedCopyFor(code, message)) {
        return *named;
    }
    // Arm 2 — the transport axis, ahead of the fallback string. Every route
    // that classifies here reaches this function with a fallback that is either
    // empty or a developer/bus diagnostic, so preferring the fallback would be
    // preferring untranslated developer text over localized copy.
    if (call != CallError::None) {
        return forCallError(call);
    }
    // Arm 3 — the agent's authored message. Reached only when the agent
    // answered and neither classification axis named the failure, so this
    // string is agent-authored user copy rather than a transport diagnostic.
    if (!message.isEmpty()) {
        return message;
    }
    // Arm 4 — nothing classified and nothing was said.
    return genericFailure();
}

} // namespace LibreKDE::ErrorText
