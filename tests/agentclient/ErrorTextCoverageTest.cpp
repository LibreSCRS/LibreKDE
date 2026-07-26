// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Total-coverage guard for the ErrorCode mirror <-> ErrorText mapping.
//
// Honest limitation: this repo has no build edge to the agent, so a code
// appended upstream cannot auto-fail anything here. The machine gate for
// taxonomy growth lives agent-side, where the agent enum is pinned
// value-for-value against the published org.librescrs.Agent.Operation1.xml
// errorCode enumeration — the canonical wire contract; new codes land there
// first, and this repo reacts to that XML's diffs. What THIS test
// machine-enforces is the mirror's internal consistency: every mirrored code
// must have a DELIBERATE ErrorText outcome.
//
// expectedKindFor() switches over EVERY mirrored enumerator with no default,
// and this file compiles with -Werror=switch — so appending a value to the
// LibreKDE::ErrorCode mirror without classifying it here (phrase it in the
// ErrorText.cpp switch, or consciously leave it to the agent-authored
// fallback) is a compile error naming the enumerator, never silence.

#include "ErrorText.h"

#include <gtest/gtest.h>

#include <QString>
#include <cstdint>
#include <optional>

namespace {

using LibreKDE::ErrorCode;

enum class Kind {
    Localized,     // ErrorText phrases this code itself (ki18n)
    AgentFallback, // ErrorText passes the agent-authored msgFallback through
    PreferFallback // ErrorText returns the agent msgFallback when present, else its own generic
};

// NO default case: with -Werror=switch a new mirror enumerator is a compile
// error until it is classified here.
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
        return Kind::Localized;
    case ErrorCode::SigningEngineError:
        // The catch-all engine code defers to the agent's message when present
        // (a specific cause beats a wrong generic), else its own generic.
        return Kind::PreferFallback;
    }
    return std::nullopt; // not a mirrored value (used to probe past the end)
}

// Mirror size derived from the classification switch itself (values outside it
// fall through to nullopt), so the walk below can never silently under-cover —
// the compiler forces the switch to track the enum, and the count tracks the
// switch.
constexpr std::uint32_t mirroredCount() noexcept
{
    std::uint32_t count = 0;
    while (expectedKindFor(static_cast<ErrorCode>(count)).has_value()) {
        ++count;
    }
    return count;
}

constexpr std::uint32_t kMirroredCount = mirroredCount();

// Keep this anchor on the LAST enumerator — it ties the classification switch
// to the same count tripwire WireContractGuardTest pins, so the two guards
// cannot disagree about the mirror's size.
static_assert(kMirroredCount == static_cast<std::uint32_t>(ErrorCode::InvalidDocument) + 1u,
              "classification switch out of step with the mirror enum tail; move this anchor to the new last "
              "enumerator and update the WireContractGuardTest count tripwire in the same change");

} // namespace

// Every mirrored value must land in exactly the classified set: Localized ->
// non-empty own copy (never the fallback), AgentFallback -> the agent's
// msgFallback verbatim. Extending the mirror without touching ErrorText's
// switch therefore fails loudly (at compile via -Werror=switch, then here if
// the classification and the implementation disagree).
TEST(ErrorTextCoverage, EveryMirroredCodeHasADeliberateOutcome)
{
    const QString sentinel = QStringLiteral("__agent_fallback_sentinel__");
    for (std::uint32_t value = 0; value < kMirroredCount; ++value) {
        const auto code = static_cast<ErrorCode>(value);
        const auto kind = expectedKindFor(code);
        ASSERT_TRUE(kind.has_value()) << "mirror value " << value << " lost its classification";

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
        case Kind::PreferFallback:
            // A present agent message wins...
            EXPECT_EQ(text, sentinel) << "code " << value
                                      << " is classified PreferFallback but forCode ignored the agent message";
            // ...but an EMPTY fallback must still yield a non-empty own generic.
            EXPECT_FALSE(LibreKDE::ErrorText::forCode(code, QString()).isEmpty())
                << "code " << value << " is PreferFallback but has no generic for the empty-fallback case";
            break;
        }
    }
}

// Forward compatibility: an agent newer than this client may already send the
// next code; until the mirror catches up it must surface the agent-authored
// fallback untouched.
TEST(ErrorTextCoverage, FirstValueBeyondTheMirrorFallsBack)
{
    const QString sentinel = QStringLiteral("__agent_fallback_sentinel__");
    EXPECT_EQ(LibreKDE::ErrorText::forCode(static_cast<ErrorCode>(kMirroredCount), sentinel), sentinel);
}
