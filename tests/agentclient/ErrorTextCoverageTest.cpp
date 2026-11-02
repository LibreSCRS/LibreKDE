// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Total-coverage guard for the ErrorCode -> ErrorText copy mapping.
//
// This file is now the only place in this repo where APPENDING a code fails the
// build, and that is deliberate. ErrorText no longer keeps a local copy of the
// enumeration: it switches on the agent client library's own ErrorCode, and the
// library decodes a value this build does not know through verbatim rather than
// rejecting it — so every switch over the type is obliged to carry a `default:`
// arm, which is exactly what stops -Wswitch from noticing an append. ErrorText
// and the card:/ worker's status mapper both carry one, on purpose: at RUN time an
// unknown code must render the agent's own message, not a diagnostic.
//
// expectedKindFor() below switches over EVERY enumerator with NO default, and
// this file compiles with -Werror=switch. Rebuilding against a client library
// that appended a code therefore fails HERE, naming the enumerator, and the fix
// is to decide its copy: phrase it in the ErrorText.cpp switch, or consciously
// leave it to the agent-authored fallback, and classify it below either way.
//
// What this cannot do is notice a code the AGENT has but this build's client
// library does not: there is no build edge from here to the agent. That gate
// lives agent-side, where the enum is pinned value-for-value against the
// published wire contract, and new codes land there first.

//
// The same guard runs over the SECOND failure axis, CallError — a purely
// client-local classification with no wire anchor, so there is no numbering to
// pin. What is pinned is that every enumerator has its own localized copy, and
// that the composed entry point never returns an empty string anywhere in the two
// enums' cross-product.
//
// The third axis is the agent's message keys. The client library names the keys
// a client phrases itself (`kLocalizedMessageKeys`) and obliges every client to
// carry a sentence for each; that list, not one kept here, is what is walked.

#include "ErrorText.h"

#include <LibreSCRS/AgentClient/FailureCopy.h>

#include <gtest/gtest.h>

#include <QLatin1StringView>
#include <QSet>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ostream>

// Readable QString diagnostics — see the identical note in ErrorTextTest.cpp.
// Without it a copy mismatch renders as pages of raw UTF-16 byte objects.
inline void PrintTo(const QString& value, std::ostream* os)
{
    *os << '"' << value.toStdString() << '"';
}

namespace {

using LibreSCRS::AgentClient::CallError;
using LibreSCRS::AgentClient::ErrorCode;

enum class Kind {
    Localized,     // ErrorText phrases this code itself (ki18n)
    AgentFallback, // ErrorText passes the agent-authored msgFallback through
};

// NO default case: with -Werror=switch an enumerator appended to the client
// library's ErrorCode is a compile error until it is classified here.
constexpr std::optional<Kind> expectedKindFor(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::None:
        // Not an error; the agent's own (typically empty) fallback rides along.
        return Kind::AgentFallback;
    case ErrorCode::CardRemoved:
    case ErrorCode::CredentialWrong:
    case ErrorCode::CredentialBlocked:
    case ErrorCode::CommunicationError:
    case ErrorCode::ParseError:
    case ErrorCode::UnsupportedCard:
    case ErrorCode::AuthFailed:
    case ErrorCode::PrompterError:
    case ErrorCode::CapabilityMissing:
    case ErrorCode::WatchdogTimeout:
    case ErrorCode::KeyNotFound:
    case ErrorCode::KeyAmbiguous:
    case ErrorCode::CertExpiredBlocked:
    case ErrorCode::ChainIncomplete:
    case ErrorCode::TsaUnreachable:
    case ErrorCode::RateLimited:
    case ErrorCode::EngineUnavailable:
    case ErrorCode::InvalidDocument:
    case ErrorCode::EntryExpired:
    case ErrorCode::SigningEngineError:
        return Kind::Localized;
    }
    return std::nullopt; // past the enumerated tail (used to probe beyond it)
}

// Enumerated size derived from the classification switch itself (values outside
// it fall through to nullopt), so the walk below can never silently under-cover —
// the compiler forces the switch to track the enum, and the count tracks the
// switch.
constexpr std::uint32_t classifiedCount() noexcept
{
    std::uint32_t count = 0;
    while (expectedKindFor(static_cast<ErrorCode>(count)).has_value()) {
        ++count;
    }
    return count;
}

constexpr std::uint32_t kClassifiedCount = classifiedCount();

// Keep this anchor on the LAST enumerator. It and -Werror=switch catch DIFFERENT
// halves of an append, and it takes both to force a deliberate decision:
//   - a code appended and NOT classified above trips -Werror=switch (the switch
//     stops being exhaustive) while this assert still holds;
//   - a code appended AND classified above, under an existing `case` group,
//     leaves the switch exhaustive — and then only this assert trips, because the
//     derived count outgrew the anchor.
// So neither check is redundant with the other, and the pair is what remains of
// the growth gate in this repo now that the taxonomy is not declared here.
static_assert(kClassifiedCount == static_cast<std::uint32_t>(ErrorCode::EntryExpired) + 1u,
              "classification switch out of step with the ErrorCode tail; move this anchor to the new last "
              "enumerator and classify the new code in expectedKindFor() in the same change");

// --- the second axis: CallError ---------------------------------------------

enum class CallKind {
    OwnCopy,         // forOutcome renders this classification's own localized copy
    AnsweredRefusal, // the agent refused the request: its message wins, own copy without one
    NotAFailure      // CallError::None — resolution continues past the call-error arm
};

// NO default case, same as expectedKindFor above: with -Werror=switch an
// enumerator appended to the agent client library's CallError is a compile
// error naming it, so new transport-failure classifications cannot land
// unphrased and silently collapse onto the generic.
constexpr std::optional<CallKind> expectedCallKindFor(CallError call) noexcept
{
    switch (call) {
    case CallError::None:
        return CallKind::NotAFailure;
    case CallError::AgentUnavailable:
    case CallError::Timeout:
    case CallError::TransportFailure:
        return CallKind::OwnCopy;
    case CallError::AccessDenied:
    case CallError::InvalidArguments:
    case CallError::ProtocolError:
        return CallKind::AnsweredRefusal;
    }
    return std::nullopt; // not a CallError value (used to probe past the end)
}

// Derived from the classification switch itself, exactly like kClassifiedCount.
constexpr std::uint8_t callErrorCount() noexcept
{
    std::uint8_t count = 0;
    while (expectedCallKindFor(static_cast<CallError>(count)).has_value()) {
        ++count;
    }
    return count;
}

constexpr std::uint8_t kCallErrorCount = callErrorCount();

static_assert(kCallErrorCount == static_cast<std::uint8_t>(CallError::ProtocolError) + 1u,
              "classification switch out of step with CallError's tail; move this anchor to the new last enumerator");

// --- the third axis: message keys ------------------------------------------

// The keys that name exactly the failure one code names, each with that code.
// Such a key shows the code's sentence, and only that code's: a key paired with
// the wrong code would read to a user as a different failure. Every key not
// listed here must have a sentence of its own.
std::optional<ErrorCode> codeSharedBy(const QString& key)
{
    if (key == QLatin1StringView("op.card_removed")) {
        return ErrorCode::CardRemoved;
    }
    if (key == QLatin1StringView("op.entry_expired")) {
        return ErrorCode::EntryExpired;
    }
    if (key == QLatin1StringView("op.watchdog_timeout")) {
        return ErrorCode::WatchdogTimeout;
    }
    return std::nullopt;
}

// Two values past the taxonomy's tail, standing in for a code an agent newer
// than this build can already send (the client library passes such a value
// through verbatim). Nothing here names them, so they are the shapes where the
// composed entry point's first arm declines and MUST keep resolving instead of
// handing the fallback back.
constexpr std::uint32_t kProbesBeyondTail = 2;

// The same probe on the other axis. ErrorText's call-error switch claims
// totality for a value from a newer build of the agent client library; nothing
// asserted that until now, so the two axes were guarded asymmetrically.
constexpr std::uint8_t kProbesBeyondCallTail = 2;

// Fallback shapes every walk crosses. The whitespace-only entry is not padding:
// it is non-empty to QString and blank to a user, and a banner is judged by the
// user's reading — so it must never be what gets rendered.
const QString kBlankish[] = {QString(), QStringLiteral("   "), QStringLiteral("\t \n"),
                             QStringLiteral("__agent_fallback_sentinel__")};

} // namespace

// Every enumerated value must land in exactly the classified set: Localized ->
// non-empty own copy (never the fallback), AgentFallback -> the agent's
// msgFallback verbatim. Appending a code without touching ErrorText's switch
// therefore fails loudly (at compile via -Werror=switch, then here if the
// classification and the implementation disagree).
TEST(ErrorTextCoverage, EveryEnumeratedCodeHasADeliberateOutcome)
{
    const QString sentinel = QStringLiteral("__agent_fallback_sentinel__");
    for (std::uint32_t value = 0; value < kClassifiedCount; ++value) {
        const auto code = static_cast<ErrorCode>(value);
        const auto kind = expectedKindFor(code);
        ASSERT_TRUE(kind.has_value()) << "code " << value << " lost its classification";

        const QString text = LibreKDE::ErrorText::forCode(code, sentinel);
        switch (*kind) {
        case Kind::Localized:
            EXPECT_FALSE(text.isEmpty()) << "code " << value << " is classified Localized but forCode returned empty";
            EXPECT_NE(text, sentinel) << "code " << value
                                      << " is classified Localized but forCode fell back — phrase it in the "
                                         "ErrorText.cpp switch or reclassify it AgentFallback here";
            break;
        case Kind::AgentFallback:
            EXPECT_EQ(text, sentinel) << "code " << value
                                      << " is classified AgentFallback but forCode returned its own copy — "
                                         "reclassify it Localized here";
            break;
        }
    }
}

// Forward compatibility: an agent newer than this client may already send the
// next code; until this build names it, it must surface the agent-authored
// fallback untouched. This is the behaviour ErrorText's `default:` arm exists
// for, asserted rather than assumed.
TEST(ErrorTextCoverage, FirstValueBeyondTheTailFallsBack)
{
    const QString sentinel = QStringLiteral("__agent_fallback_sentinel__");
    EXPECT_EQ(LibreKDE::ErrorText::forCode(static_cast<ErrorCode>(kClassifiedCount), sentinel), sentinel);
}

// Every call-failure classification must carry its OWN copy: non-empty and
// distinct from every sibling and from the generic. A call that never reached
// the agent prefers that copy over the string riding along (bus text, or one of
// the client's own English diagnostics); a refusal the agent answered keeps the
// agent's reason, which is the only record of why. Sharing copy between two
// classifications would be indistinguishable to a user, so it is a failure
// here, not a stylistic remark.
TEST(ErrorTextCoverage, EveryCallErrorHasItsOwnLocalizedCopy)
{
    const QString sentinel = QStringLiteral("__raw_transport_string_sentinel__");
    QSet<QString> seen;

    for (std::uint8_t value = 0; value < kCallErrorCount; ++value) {
        const auto call = static_cast<CallError>(value);
        const auto kind = expectedCallKindFor(call);
        ASSERT_TRUE(kind.has_value()) << "call error " << static_cast<int>(value) << " lost its classification";

        switch (*kind) {
        case CallKind::OwnCopy:
        case CallKind::AnsweredRefusal: {
            const QString text = LibreKDE::ErrorText::forOutcome(ErrorCode::None, call, QString(), QString());
            EXPECT_FALSE(text.isEmpty()) << "call error " << static_cast<int>(value) << " rendered nothing";
            EXPECT_FALSE(seen.contains(text))
                << "call error " << static_cast<int>(value)
                << " shares its copy with another classification; a user cannot tell the two apart";
            seen.insert(text);
            const QString withMessage = LibreKDE::ErrorText::forOutcome(ErrorCode::None, call, QString(), sentinel);
            if (*kind == CallKind::OwnCopy) {
                // The request never reached the agent: the string riding along
                // is bus or diagnostic text, never shown in place of the copy.
                EXPECT_EQ(withMessage, text) << "call error " << static_cast<int>(value)
                                             << " surfaced the raw transport string instead of localized copy";
            } else {
                // The agent answered and said why: its words are the record.
                EXPECT_EQ(withMessage, sentinel)
                    << "call error " << static_cast<int>(value) << " dropped the agent's own reason for refusing";
            }
            break;
        }
        case CallKind::NotAFailure:
            // None must not consume the fallback — resolution continues past it.
            EXPECT_EQ(LibreKDE::ErrorText::forOutcome(ErrorCode::None, call, QString(), sentinel), sentinel)
                << "CallError::None swallowed the agent-authored message";
            break;
        }
    }

    const QString generic = LibreKDE::ErrorText::forOutcome(ErrorCode::None, CallError::None, QString(), QString());
    EXPECT_FALSE(generic.isEmpty()) << "the generic floor rendered nothing";
    EXPECT_FALSE(seen.contains(generic)) << "the generic reuses a call error's copy, so an unclassified failure is "
                                            "indistinguishable from that classification";
}

// The never-blank invariant, asserted as an invariant: EVERY point in the
// cross-product of both enums — each probed past its own tail — crossed with
// every fallback shape. Exhaustive rather than sampled: the defect this
// function exists to prevent is a blank banner, and a blank banner at one
// unvisited point is the whole defect.
//
// "Blank", not "empty": a fallback of three spaces satisfies !isEmpty() and
// renders as an empty banner, so it is the invariant's real boundary and is
// walked alongside the empty and present cases.
TEST(ErrorTextCoverage, ForOutcomeIsNeverBlankAnywhereInTheCrossProduct)
{
    constexpr std::uint32_t kFallbackShapes = std::size(kBlankish);
    std::uint32_t points = 0;

    for (std::uint32_t codeValue = 0; codeValue < kClassifiedCount + kProbesBeyondTail; ++codeValue) {
        const auto code = static_cast<ErrorCode>(codeValue);
        for (std::uint8_t callValue = 0; callValue < kCallErrorCount + kProbesBeyondCallTail; ++callValue) {
            const auto call = static_cast<CallError>(callValue);
            for (const QString& fallback : kBlankish) {
                EXPECT_FALSE(LibreKDE::ErrorText::forOutcome(code, call, QString(), fallback).trimmed().isEmpty())
                    << "blank banner at code " << codeValue << ", call error " << static_cast<int>(callValue)
                    << ", fallback \"" << fallback.toStdString() << '"';
                ++points;
            }
        }
    }

    // Guards the walk itself: a loop bound silently collapsing to zero would
    // otherwise leave every assertion above unexecuted and the test green.
    EXPECT_EQ(points,
              (kClassifiedCount + kProbesBeyondTail) * (kCallErrorCount + kProbesBeyondCallTail) * kFallbackShapes);
    EXPECT_GT(points, 0u);
}

// A key the client library adds to `kLocalizedMessageKeys` is reported as that
// key alone: the agent's English line does not ride along. A key this client has
// no sentence for therefore reads as the generic failure with nothing below it,
// which is the whole report lost, so it fails here instead.
//
// Some keys name exactly the failure a code names and share that code's
// sentence (codeSharedBy). Every other key is a sentence of its own, neither a
// code's nor another key's: two alike would read to a user as one failure.
TEST(ErrorTextCoverage, EveryLocalizedMessageKeyHasItsOwnSentence)
{
    const auto& keys = LibreSCRS::AgentClient::kLocalizedMessageKeys;
    const QString generic = LibreKDE::ErrorText::forOutcome(ErrorCode::None, CallError::None, QString(), QString());

    QSet<QString> codeSentences;
    for (std::uint32_t value = 0; value < kClassifiedCount; ++value) {
        const QString text = LibreKDE::ErrorText::forCode(static_cast<ErrorCode>(value), QString());
        if (!text.isEmpty()) {
            codeSentences.insert(text);
        }
    }

    QSet<QString> ownSentences;
    std::size_t walked = 0;
    for (const QLatin1StringView key : keys) {
        const QString name(key);
        const QString text = LibreKDE::ErrorText::forOutcome(ErrorCode::None, CallError::None, name, QString());
        EXPECT_FALSE(text.trimmed().isEmpty()) << "message key " << name.toStdString() << " rendered nothing";
        EXPECT_NE(text, generic) << "message key " << name.toStdString()
                                 << " has no sentence in this client and reads as the generic failure";
        if (const auto code = codeSharedBy(name)) {
            EXPECT_EQ(text, LibreKDE::ErrorText::forCode(*code, QString()))
                << "message key " << name.toStdString() << " does not read as the code it names, code "
                << static_cast<std::uint32_t>(*code);
        } else {
            EXPECT_FALSE(codeSentences.contains(text))
                << "message key " << name.toStdString()
                << " reads as some code's sentence; pair it with that code in codeSharedBy or phrase it on its own";
            EXPECT_FALSE(ownSentences.contains(text))
                << "message key " << name.toStdString()
                << " shares its sentence with another key; a user cannot tell the two apart";
            ownSentences.insert(text);
        }
        ++walked;
    }

    // Guards the walk itself, as in the cross-product above.
    EXPECT_EQ(walked, keys.size());
    EXPECT_GT(walked, 0u);
}
