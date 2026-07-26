// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Total-coverage guard for the four closed Credentials1 record-token enums —
// CredentialKind / CredentialState / UnblockStyle / RecoveryPath — mirroring
// CredentialTextCoverageTest (which owns CredentialOutcome). Compiled with
// -Werror=switch, so extending any of these enums without a decision here is a
// compile error naming the enumerator, never silence.
//
// Guarded regressions:
//  1. A new enumerator whose wire token nobody mapped: wireTokenFor() switches
//     over EVERY enumerator with no default (compile error), and the tail
//     static_asserts trip on an append that leaves a classifier stale.
//  2. A parse-arm gap: the walk round-trips every token through the PRODUCTION
//     CredentialRecord::fromVariantMap (the kindFrom/stateFrom/styleFrom/
//     recoveryFrom chains are file-local, so the record demarshaller is the
//     public seam) and demands the exact enumerator back.
//  3. A display-arm gap: kindName()/stateName() fall through to `return {}` on
//     a missed switch arm (the production lib carries no -Werror=switch) — the
//     walk demands non-empty copy for EVERY enumerator, so a blank dashboard
//     cell fails here instead of shipping.
//
// Token vocabulary source of truth: the frozen
// org.librescrs.Agent.Operation.Credentials1.xml record keys
//   kind          "user" | "sign" | "puk" | "can" | "unknown"
//   state         "unknown" | "transport" | "operational" | "needsChange" | "blocked"
//   unblock_style "unknown" | "resetOnly" | "setsNewPin" | "unblockAndChange"
//   recovery      "unknown" | "holderViaPuk" | "issuerProcess" | "none"

#include "CredentialText.h"
#include "CredentialTypes.h"

#include <gtest/gtest.h>

#include <QString>
#include <QVariantMap>
#include <cstdint>
#include <optional>

namespace {

using LibreKDE::CredentialKind;
using LibreKDE::CredentialRecord;
using LibreKDE::CredentialState;
using LibreKDE::RecoveryPath;
using LibreKDE::UnblockStyle;

// NO default case in any classifier: with -Werror=switch a new enumerator is a
// compile error until its wire token is decided here. Values past the tail
// return nullopt so each walk derives the enumerated count from its switch.

constexpr std::optional<const char*> wireTokenForKind(CredentialKind k) noexcept
{
    switch (k) {
    case CredentialKind::User:
        return "user";
    case CredentialKind::Sign:
        return "sign";
    case CredentialKind::Puk:
        return "puk";
    case CredentialKind::Can:
        return "can";
    case CredentialKind::Unknown:
        return "unknown";
    }
    return std::nullopt;
}

constexpr std::optional<const char*> wireTokenForState(CredentialState s) noexcept
{
    switch (s) {
    case CredentialState::Unknown:
        return "unknown";
    case CredentialState::Transport:
        return "transport";
    case CredentialState::Operational:
        return "operational";
    case CredentialState::NeedsChange:
        return "needsChange";
    case CredentialState::Blocked:
        return "blocked";
    }
    return std::nullopt;
}

constexpr std::optional<const char*> wireTokenForStyle(UnblockStyle u) noexcept
{
    switch (u) {
    case UnblockStyle::Unknown:
        return "unknown";
    case UnblockStyle::ResetOnly:
        return "resetOnly";
    case UnblockStyle::SetsNewPin:
        return "setsNewPin";
    case UnblockStyle::UnblockAndChange:
        return "unblockAndChange";
    }
    return std::nullopt;
}

constexpr std::optional<const char*> wireTokenForRecovery(RecoveryPath r) noexcept
{
    switch (r) {
    case RecoveryPath::Unknown:
        return "unknown";
    case RecoveryPath::HolderViaPuk:
        return "holderViaPuk";
    case RecoveryPath::IssuerProcess:
        return "issuerProcess";
    case RecoveryPath::None:
        return "none";
    }
    return std::nullopt;
}

template <typename E, typename Classifier>
constexpr std::uint32_t enumeratedCount(Classifier classify) noexcept
{
    std::uint32_t count = 0;
    while (classify(static_cast<E>(count)).has_value()) {
        ++count;
    }
    return count;
}

constexpr std::uint32_t kKindCount = enumeratedCount<CredentialKind>(wireTokenForKind);
constexpr std::uint32_t kStateCount = enumeratedCount<CredentialState>(wireTokenForState);
constexpr std::uint32_t kStyleCount = enumeratedCount<UnblockStyle>(wireTokenForStyle);
constexpr std::uint32_t kRecoveryCount = enumeratedCount<RecoveryPath>(wireTokenForRecovery);

// Anchor each count on the LAST enumerator so an append that leaves a
// classifier stale trips here too, in addition to -Werror=switch.
static_assert(kKindCount == static_cast<std::uint32_t>(CredentialKind::Unknown) + 1u,
              "wireTokenForKind out of step with the CredentialKind tail");
static_assert(kStateCount == static_cast<std::uint32_t>(CredentialState::Blocked) + 1u,
              "wireTokenForState out of step with the CredentialState tail");
static_assert(kStyleCount == static_cast<std::uint32_t>(UnblockStyle::UnblockAndChange) + 1u,
              "wireTokenForStyle out of step with the UnblockStyle tail");
static_assert(kRecoveryCount == static_cast<std::uint32_t>(RecoveryPath::None) + 1u,
              "wireTokenForRecovery out of step with the RecoveryPath tail");

// Round-trip @p token through the PRODUCTION record demarshaller into the
// field selected by @p key.
CredentialRecord recordWith(const char* key, const char* token)
{
    return CredentialRecord::fromVariantMap(QVariantMap{{QLatin1String(key), QLatin1String(token)}});
}

} // namespace

// Every enumerated token parses back to its exact enumerator through the
// production demarshaller, and every Kind/State enumerator carries non-empty
// localized display copy.
TEST(CredentialVocabularyCoverage, EveryKindRoundTripsAndHasCopy)
{
    for (std::uint32_t v = 0; v < kKindCount; ++v) {
        const auto k = static_cast<CredentialKind>(v);
        const auto token = wireTokenForKind(k);
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(recordWith("kind", *token).kind, k) << "token '" << *token << "' failed the parse round-trip";
        EXPECT_FALSE(LibreKDE::CredentialText::kindName(k).isEmpty())
            << "kindName() returned empty for enumerator " << v << " — add the display arm";
    }
}

TEST(CredentialVocabularyCoverage, EveryStateRoundTripsAndHasCopy)
{
    for (std::uint32_t v = 0; v < kStateCount; ++v) {
        const auto s = static_cast<CredentialState>(v);
        const auto token = wireTokenForState(s);
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(recordWith("state", *token).state, s) << "token '" << *token << "' failed the parse round-trip";
        EXPECT_FALSE(LibreKDE::CredentialText::stateName(s).isEmpty())
            << "stateName() returned empty for enumerator " << v << " — add the display arm";
    }
}

TEST(CredentialVocabularyCoverage, EveryUnblockStyleRoundTrips)
{
    for (std::uint32_t v = 0; v < kStyleCount; ++v) {
        const auto u = static_cast<UnblockStyle>(v);
        const auto token = wireTokenForStyle(u);
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(recordWith("unblock_style", *token).unblockStyle, u)
            << "token '" << *token << "' failed the parse round-trip";
    }
}

TEST(CredentialVocabularyCoverage, EveryRecoveryPathRoundTrips)
{
    for (std::uint32_t v = 0; v < kRecoveryCount; ++v) {
        const auto r = static_cast<RecoveryPath>(v);
        const auto token = wireTokenForRecovery(r);
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(recordWith("recovery", *token).recovery, r) << "token '" << *token << "' failed the parse round-trip";
    }
}

// Fail-safe: a token outside the closed sets must map to Unknown, never crash
// or alias a real value (forward compatibility with future agent vocabulary).
TEST(CredentialVocabularyCoverage, UnknownTokensFailSafeToUnknown)
{
    EXPECT_EQ(recordWith("kind", "no-such-token").kind, CredentialKind::Unknown);
    EXPECT_EQ(recordWith("state", "no-such-token").state, CredentialState::Unknown);
    EXPECT_EQ(recordWith("unblock_style", "no-such-token").unblockStyle, UnblockStyle::Unknown);
    EXPECT_EQ(recordWith("recovery", "no-such-token").recovery, RecoveryPath::Unknown);
}
