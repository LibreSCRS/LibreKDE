// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "ErrorText.h"

#include <gtest/gtest.h>

#include <ostream>

// Readable QString diagnostics. Without this gtest falls back to its raw-bytes
// printer and a copy mismatch renders as pages of `2-byte object <54-00>`,
// which is unusable for exactly the assertions below — every one of them
// compares user-visible sentences. Found by ADL (QString is in the global
// namespace); TU-local, so it changes no other test's output.
inline void PrintTo(const QString& value, std::ostream* os)
{
    *os << '"' << value.toStdString() << '"';
}

using LibreKDE::ErrorText::forCode;
using LibreKDE::ErrorText::forOutcome;
using LibreSCRS::AgentClient::CallError;
using LibreSCRS::AgentClient::ErrorCode;

TEST(ErrorText, KnownCodesAreNonEmptyAndDistinct)
{
    const QString cap = forCode(ErrorCode::CapabilityMissing, QStringLiteral("fallback"));
    const QString rate = forCode(ErrorCode::RateLimited, QStringLiteral("fallback"));
    const QString prompter = forCode(ErrorCode::PrompterError, QStringLiteral("fallback"));

    EXPECT_FALSE(cap.isEmpty());
    EXPECT_FALSE(rate.isEmpty());
    EXPECT_FALSE(prompter.isEmpty());

    // Each known code maps to its own copy, not the bare fallback.
    EXPECT_NE(cap, QStringLiteral("fallback"));
    EXPECT_NE(cap, rate);
    EXPECT_NE(cap, prompter);
    EXPECT_NE(rate, prompter);
}

TEST(ErrorText, UnknownCodeFallsBackToMsgFallback)
{
    const QString fallback = QStringLiteral("agent-authored detail");
    EXPECT_EQ(forCode(ErrorCode::None, fallback), fallback);
}

// --- forOutcome: the resolution order, arm by arm ---------------------------
//
// Each test below pins ONE adjacency in the order, phrased so that swapping the
// two arms it separates makes it fail. ErrorTextCoverageTest owns the
// exhaustive walks (every CallError enumerator, the whole cross-product's
// never-empty invariant); these are the ordering assertions.

// Arm 1 before arm 2: the agent's own taxonomy is more specific than the coarse
// transport classification, so a terminal carrying both must render the code.
TEST(ErrorText, ErrorCodeOutranksCallError)
{
    const QString composed = forOutcome(ErrorCode::CardRemoved, CallError::AgentUnavailable, QString());

    EXPECT_EQ(composed, forCode(ErrorCode::CardRemoved, QString()));
    // ...and is NOT the call error's copy — the assertion above alone would
    // also hold if both arms happened to produce the same string.
    EXPECT_NE(composed, forOutcome(ErrorCode::None, CallError::AgentUnavailable, QString()));
}

// Arm 2 before arm 3: on a transport failure the fallback string is raw bus or
// client-diagnostic text, so localized copy must win over it. The fallback used
// here is a real one the client can produce for this exact classification.
TEST(ErrorText, CallErrorOutranksTheRawTransportString)
{
    const QString raw = QStringLiteral("method entry returned no operation path");
    const QString composed = forOutcome(ErrorCode::None, CallError::ProtocolError, raw);

    EXPECT_NE(composed, raw);
    // The rendered copy is the call error's own, unaffected by the raw string.
    EXPECT_EQ(composed, forOutcome(ErrorCode::None, CallError::ProtocolError, QString()));
}

// Arm 3: nothing classified, but the agent authored a message — that message is
// user copy (the agent's own), so it is rendered rather than flattened into the
// generic.
TEST(ErrorText, UnclassifiedFailureKeepsTheAgentMessage)
{
    const QString authored = QStringLiteral("agent-authored detail");

    EXPECT_EQ(forOutcome(ErrorCode::None, CallError::None, authored), authored);
}

// Arm 4: the blank-banner case. A terminal that classified on neither axis and
// carried no message at all — reachable today, e.g. a Cancelled operation
// recovered from the agent's terminal-state snapshot, which carries no message
// by contract — must still say something.
TEST(ErrorText, TotallyUninformativeFailureStillSaysSomething)
{
    const QString generic = forOutcome(ErrorCode::None, CallError::None, QString());

    EXPECT_FALSE(generic.isEmpty());
    // The generic is its own copy, not one of the call errors' recycled.
    EXPECT_NE(generic, forOutcome(ErrorCode::None, CallError::Timeout, QString()));
}

// Arm 1 matches only when this client's mapping NAMES the code. A code an agent
// newer than this build appended is not named — the client library decodes it
// through verbatim — so it must not short-circuit the order, whether or not a
// message rode along with it. The version WITH a message is the one that matters:
// without this, arm 1 would hand back the raw transport string and arm 2's
// localized copy would never be reached.
TEST(ErrorText, CodeBeyondTheTaxonomyNeverShortCircuitsTheOrder)
{
    // Comfortably past the taxonomy's tail; it is append-only, so this stays
    // unnamed for a long time and the assertions do not depend on which value it
    // is.
    const auto unnamed = static_cast<ErrorCode>(200);
    ASSERT_TRUE(forCode(unnamed, QString()).isEmpty()) << "premise broken: this value is now named, pick a higher one";

    // With a transport diagnostic riding along, arm 2 must still win.
    const QString raw = QStringLiteral("method entry returned no operation path");
    EXPECT_NE(forOutcome(unnamed, CallError::ProtocolError, raw), raw);
    EXPECT_EQ(forOutcome(unnamed, CallError::ProtocolError, raw),
              forOutcome(ErrorCode::None, CallError::ProtocolError, QString()));

    // With nothing to classify it, the agent's own message still survives.
    EXPECT_EQ(forOutcome(unnamed, CallError::None, raw), raw);

    // And with neither, the order runs to its floor rather than to nothing.
    EXPECT_EQ(forOutcome(unnamed, CallError::Timeout, QString()),
              forOutcome(ErrorCode::None, CallError::Timeout, QString()));
    EXPECT_EQ(forOutcome(unnamed, CallError::None, QString()), forOutcome(ErrorCode::None, CallError::None, QString()));
    EXPECT_FALSE(forOutcome(unnamed, CallError::None, QString()).isEmpty());
}

// A message made only of whitespace is non-empty to QString and blank to the
// person reading the banner. The banner is what this function answers to, so
// such a message counts as no message on every arm that consults one.
TEST(ErrorText, WhitespaceOnlyAgentMessageCountsAsNoMessage)
{
    for (const QString& blank : {QStringLiteral("   "), QStringLiteral("\t"), QStringLiteral("\n \n")}) {
        EXPECT_EQ(forOutcome(ErrorCode::None, CallError::None, blank),
                  forOutcome(ErrorCode::None, CallError::None, QString()))
            << "a whitespace-only message reached the banner instead of the generic";
        // SigningEngineError is the one mapped code that defers to the agent's
        // message; it must not defer to whitespace either.
        EXPECT_FALSE(forOutcome(ErrorCode::SigningEngineError, CallError::None, blank).trimmed().isEmpty());
    }

    // forCode is deliberately NOT hardened: its documented contract is a
    // verbatim pass-through, and callers that have not migrated depend on it.
    EXPECT_EQ(forCode(ErrorCode::None, QStringLiteral("   ")), QStringLiteral("   "));
}

// forCode keeps its own, deliberately weaker contract alongside the composed
// entry point: a verbatim pass-through that MAY return empty. Callers that hold
// nothing but a code still depend on it, so adding forOutcome must not have
// quietly changed it.
TEST(ErrorText, ForCodeIsUnchangedByTheComposedEntryPoint)
{
    const QString fallback = QStringLiteral("agent-authored detail");

    EXPECT_EQ(forCode(ErrorCode::None, fallback), fallback);
    EXPECT_EQ(forCode(ErrorCode::None, QString()), QString());
    EXPECT_EQ(forCode(ErrorCode::SigningEngineError, fallback), fallback);
}
