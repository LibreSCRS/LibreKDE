// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Total-coverage guard for the CredentialOutcome enum <-> CredentialText copy
// mapping. Mirrors ErrorTextCoverageTest's expectedKindFor()/assertion-loop
// idiom.
//
// Two distinct failure modes are guarded, and it takes BOTH halves to close
// the drift the earlier revision left open:
//
//  1. Adding a CredentialOutcome enumerator. expectedKindFor() switches over
//     EVERY enumerator with no default, and this file compiles with
//     -Werror=switch — so a new value that nobody classified here is a compile
//     error naming the enumerator, never silence.
//
//  2. Forgetting the new outcome in PRODUCTION CredentialText::outcomeMessage()
//     (whose switch is NOT built with -Werror=switch — the library carries no
//     such flag, so a missed arm falls through to `return {}` and compiles
//     clean). The assertion loop below drives the *production*
//     outcomeMessage()/isNeutral() over the full enumerated set and demands the
//     classified contract, so a silently-empty production result fails the test
//     even though it built without a warning. This is the property the previous
//     hand-maintained initializer list could not enforce.

#include "CredentialText.h"

#include <gtest/gtest.h>

#include <QString>
#include <cstdint>
#include <optional>

namespace {

using LibreKDE::CredentialKind;
using LibreKDE::CredentialOutcome;

// The contract each outcome must satisfy, split so both dimensions
// (isNeutral + whether outcomeMessage carries copy) are pinned. Ok and
// UserCancelled are BOTH neutral, but Ok shows an informational "Done." while
// UserCancelled shows nothing (the caller renders it as a neutral affordance,
// not a banner) — so a single Neutral bucket would under-specify.
enum class Kind {
    NeutralSilent, // isNeutral()==true  AND outcomeMessage() empty     (UserCancelled)
    NeutralInfo,   // isNeutral()==true  AND outcomeMessage() non-empty  (Ok: "Done.")
    Error,         // isNeutral()==false AND outcomeMessage() non-empty  (everything else)
};

// NO default case: with -Werror=switch a new enumerator is a compile error
// until it is classified here. Values past the tail return nullopt so the walk
// below derives the enumerated count from the switch itself.
constexpr std::optional<Kind> expectedKindFor(CredentialOutcome o) noexcept
{
    switch (o) {
    case CredentialOutcome::UserCancelled:
        return Kind::NeutralSilent;
    case CredentialOutcome::Ok:
        return Kind::NeutralInfo;
    case CredentialOutcome::Unspecified:
    case CredentialOutcome::MissingFields:
    case CredentialOutcome::InvalidPin:
    case CredentialOutcome::Blocked:
    case CredentialOutcome::PluginError:
    case CredentialOutcome::Unsupported:
    case CredentialOutcome::KeyActivationFailed:
    case CredentialOutcome::CardRemoved:
        return Kind::Error;
    }
    return std::nullopt; // not an enumerated value (probes past the end)
}

// Enumerated count derived from the classification switch itself (values
// outside it fall to nullopt), so the walk can never silently under-cover: the
// compiler forces the switch to track the enum, and the count tracks the switch.
constexpr std::uint32_t enumeratedCount() noexcept
{
    std::uint32_t count = 0;
    while (expectedKindFor(static_cast<CredentialOutcome>(count)).has_value()) {
        ++count;
    }
    return count;
}

constexpr std::uint32_t kOutcomeCount = enumeratedCount();

// Anchor on the LAST enumerator so an append/reorder that leaves the switch
// stale trips here too, in addition to -Werror=switch.
static_assert(kOutcomeCount == static_cast<std::uint32_t>(CredentialOutcome::CardRemoved) + 1u,
              "classification switch out of step with the CredentialOutcome tail; move this anchor to the new last "
              "enumerator and classify the new value in expectedKindFor() in the same change");

} // namespace

// Drives PRODUCTION outcomeMessage()/isNeutral() over every enumerated outcome
// and demands the classified contract. Forgetting an arm in the production
// outcomeMessage() switch (which returns {} on fall-through, and is NOT
// -Werror=switch-guarded) fails here.
TEST(CredentialTextCoverage, EveryOutcomeMatchesItsClassification)
{
    for (std::uint32_t value = 0; value < kOutcomeCount; ++value) {
        const auto o = static_cast<CredentialOutcome>(value);
        const auto kind = expectedKindFor(o);
        ASSERT_TRUE(kind.has_value()) << "outcome " << value << " lost its classification";

        const QString msg = LibreKDE::CredentialText::outcomeMessage(o, CredentialKind::User);
        const bool neutral = LibreKDE::CredentialText::isNeutral(o);
        switch (*kind) {
        case Kind::NeutralSilent:
            EXPECT_TRUE(neutral) << "outcome " << value << " is NeutralSilent but isNeutral() returned false";
            EXPECT_TRUE(msg.isEmpty()) << "outcome " << value << " is NeutralSilent but outcomeMessage() returned copy";
            break;
        case Kind::NeutralInfo:
            EXPECT_TRUE(neutral) << "outcome " << value << " is NeutralInfo but isNeutral() returned false";
            EXPECT_FALSE(msg.isEmpty()) << "outcome " << value << " is NeutralInfo but outcomeMessage() returned empty";
            break;
        case Kind::Error:
            EXPECT_FALSE(neutral) << "outcome " << value << " is Error but isNeutral() returned true";
            EXPECT_FALSE(msg.isEmpty())
                << "outcome " << value
                << " is Error but outcomeMessage() returned empty — add an arm to the CredentialText.cpp switch";
            break;
        }
    }
}

// Forward-compat: a value past the enumerated tail carries no classification
// (production outcomeMessage() falls through to {} for it), proving the count
// tripwire above is anchored to the real enum end.
TEST(CredentialTextCoverage, FirstValueBeyondTheEnumHasNoClassification)
{
    EXPECT_FALSE(expectedKindFor(static_cast<CredentialOutcome>(kOutcomeCount)).has_value());
}

TEST(CredentialTextCoverage, CancelIsNeutralAndEmpty)
{
    EXPECT_TRUE(LibreKDE::CredentialText::isNeutral(CredentialOutcome::UserCancelled));
    EXPECT_TRUE(LibreKDE::CredentialText::isNeutral(CredentialOutcome::Ok));
    EXPECT_TRUE(
        LibreKDE::CredentialText::outcomeMessage(CredentialOutcome::UserCancelled, CredentialKind::User).isEmpty());
}

// Result rendering: invalidPin carries the attribution counter when the
// result delivered retries_left — pluralized (Serbian needs three forms; under
// LANGUAGE=en the singular/plural pair must agree with the count) — and keeps
// the plain copy when the wire omitted the count.
TEST(CredentialTextCoverage, InvalidPinCarriesRetriesWhenPresent)
{
    const QString two =
        LibreKDE::CredentialText::outcomeMessage(CredentialOutcome::InvalidPin, CredentialKind::User, 2);
    EXPECT_TRUE(two.contains(QStringLiteral("2 attempts left"))) << two.toStdString();
    EXPECT_TRUE(two.contains(QStringLiteral("User PIN"))) << two.toStdString();

    const QString one =
        LibreKDE::CredentialText::outcomeMessage(CredentialOutcome::InvalidPin, CredentialKind::User, 1);
    EXPECT_TRUE(one.contains(QStringLiteral("1 attempt left"))) << one.toStdString();
    EXPECT_FALSE(one.contains(QStringLiteral("attempts"))) << one.toStdString();

    const QString none = LibreKDE::CredentialText::outcomeMessage(CredentialOutcome::InvalidPin, CredentialKind::User);
    EXPECT_TRUE(none.contains(QStringLiteral("not correct"))) << none.toStdString();
    EXPECT_FALSE(none.contains(QStringLiteral("left")))
        << "no counter clause without a delivered count: " << none.toStdString();
}

TEST(CredentialTextCoverage, GuidanceFallsBackWhenUntranslated)
{
    // An unknown key resolves to itself under ki18n -> fallback wins.
    EXPECT_EQ(LibreKDE::CredentialText::guidance(QStringLiteral("librescrs.pin.no.such.key"),
                                                 QStringLiteral("Contact the issuer.")),
              QStringLiteral("Contact the issuer."));
}
