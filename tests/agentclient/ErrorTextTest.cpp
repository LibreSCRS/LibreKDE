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

// --- forOutcome: rendering the client library's decision --------------------
//
// Which part of a failure a user reads is the agent client library's decision
// (`describeFailure()`), shared by every client. Each test below pins one
// branch of how this client renders it; ErrorTextCoverageTest owns the
// exhaustive walks.

namespace {
const QString kNoKey;
} // namespace

// The agent's code outranks the coarse transport classification.
TEST(ErrorText, ErrorCodeOutranksCallError)
{
    const QString composed = forOutcome(ErrorCode::CardRemoved, CallError::AgentUnavailable, kNoKey, QString());

    EXPECT_EQ(composed, forCode(ErrorCode::CardRemoved, QString()));
    EXPECT_NE(composed, forOutcome(ErrorCode::None, CallError::AgentUnavailable, kNoKey, QString()));
}

// A call that never reached the agent carries bus or diagnostic text; the
// localized copy wins over it.
TEST(ErrorText, CallErrorOutranksTheRawTransportString)
{
    const QString raw = QStringLiteral("org.freedesktop.DBus.Error.NoReply: did not receive a reply");
    const QString composed = forOutcome(ErrorCode::None, CallError::TransportFailure, kNoKey, raw);

    EXPECT_NE(composed, raw);
    EXPECT_EQ(composed, forOutcome(ErrorCode::None, CallError::TransportFailure, kNoKey, QString()));
}

// A refusal the agent answered keeps the agent's reason: it is the only record
// of why the request was refused.
TEST(ErrorText, AnAnsweredRefusalKeepsTheAgentsReason)
{
    const QString reason = QStringLiteral("unknown option 'level'");
    EXPECT_EQ(forOutcome(ErrorCode::None, CallError::InvalidArguments, kNoKey, reason), reason);
    // Without a reason, the classification's own copy.
    EXPECT_NE(forOutcome(ErrorCode::None, CallError::InvalidArguments, kNoKey, QString()), QString());
}

// Nothing classified, but the agent authored a message — that message is shown.
TEST(ErrorText, UnclassifiedFailureKeepsTheAgentMessage)
{
    const QString authored = QStringLiteral("agent-authored detail");

    EXPECT_EQ(forOutcome(ErrorCode::None, CallError::None, kNoKey, authored), authored);
}

// The blank-banner case: nothing classified and nothing said.
TEST(ErrorText, TotallyUninformativeFailureStillSaysSomething)
{
    const QString generic = forOutcome(ErrorCode::None, CallError::None, kNoKey, QString());

    EXPECT_FALSE(generic.isEmpty());
    EXPECT_NE(generic, forOutcome(ErrorCode::None, CallError::Timeout, kNoKey, QString()));
}

// A code this client names is the headline; what the agent said beyond it
// stays below, because it is often the only record of which failure the code
// covers.
TEST(ErrorText, ANamedCodeKeepsTheAgentsLineBelowItsHeadline)
{
    const QString detail = QStringLiteral("PAdES: the document has an unsigned incremental update");
    const QString headline = forCode(ErrorCode::SigningEngineError, QString());

    EXPECT_EQ(forOutcome(ErrorCode::SigningEngineError, CallError::None, kNoKey, detail),
              headline + QLatin1Char('\n') + detail);
    EXPECT_EQ(forOutcome(ErrorCode::SigningEngineError, CallError::None, kNoKey, QString()), headline);
}

// A code an agent newer than this build sends is still the agent's answer: the
// generic sentence heads it, and the agent's own line says what happened.
TEST(ErrorText, ACodeBeyondTheTaxonomyKeepsTheAgentsLine)
{
    const auto unnamed = static_cast<ErrorCode>(200);
    ASSERT_TRUE(forCode(unnamed, QString()).isEmpty()) << "premise broken: this value is now named, pick a higher one";
    const QString generic = forOutcome(ErrorCode::None, CallError::None, kNoKey, QString());
    const QString said = QStringLiteral("the agent's own account");

    EXPECT_EQ(forOutcome(unnamed, CallError::None, kNoKey, said), generic + QLatin1Char('\n') + said);
    EXPECT_EQ(forOutcome(unnamed, CallError::None, kNoKey, QString()), generic);
}

// A message key this client localizes is the most specific report there is,
// in the user's language, and it stands alone — the agent's English line for
// it says the same thing.
TEST(ErrorText, ALocalizedMessageKeyStandsAlone)
{
    const QString cancelled = forOutcome(ErrorCode::None, CallError::None, QStringLiteral("op.cancelled"),
                                         QStringLiteral("Operation cancelled"));
    EXPECT_EQ(cancelled, QStringLiteral("The operation was cancelled."));

    EXPECT_EQ(forOutcome(ErrorCode::PrompterError, CallError::None, QStringLiteral("op.helper_too_old"),
                         QStringLiteral("pinentry helper is older than the agent")),
              QStringLiteral("The secure entry prompt is out of date. Update the smart-card service."));

    // Keys that name exactly what a code names share that code's sentence.
    EXPECT_EQ(forOutcome(ErrorCode::None, CallError::None, QStringLiteral("op.card_removed"), QString()),
              forCode(ErrorCode::CardRemoved, QString()));
    EXPECT_EQ(forOutcome(ErrorCode::EntryExpired, CallError::None, QStringLiteral("op.entry_expired"),
                         QStringLiteral("The entry window closed")),
              forCode(ErrorCode::EntryExpired, QString()));
}

// A key this client does not localize is no reason to drop the other facts.
TEST(ErrorText, AnUnlocalizedMessageKeyFallsThroughToTheCode)
{
    EXPECT_EQ(forOutcome(ErrorCode::CredentialWrong, CallError::None, QStringLiteral("op.some_future_key"), QString()),
              forCode(ErrorCode::CredentialWrong, QString()));
}

// A message made only of whitespace counts as no message.
TEST(ErrorText, WhitespaceOnlyAgentMessageCountsAsNoMessage)
{
    for (const QString& blank : {QStringLiteral("   "), QStringLiteral("\t"), QStringLiteral("\n \n")}) {
        EXPECT_EQ(forOutcome(ErrorCode::None, CallError::None, kNoKey, blank),
                  forOutcome(ErrorCode::None, CallError::None, kNoKey, QString()))
            << "a whitespace-only message reached the banner instead of the generic";
        EXPECT_EQ(forOutcome(ErrorCode::SigningEngineError, CallError::None, kNoKey, blank),
                  forCode(ErrorCode::SigningEngineError, QString()));
    }

    // forCode is a verbatim pass-through for a code it does not name.
    EXPECT_EQ(forCode(ErrorCode::None, QStringLiteral("   ")), QStringLiteral("   "));
}

// forCode: the client's copy for a code it names, the caller's text otherwise.
TEST(ErrorText, ForCodeIsTheCodesOwnCopy)
{
    const QString fallback = QStringLiteral("agent-authored detail");

    EXPECT_EQ(forCode(ErrorCode::None, fallback), fallback);
    EXPECT_EQ(forCode(ErrorCode::None, QString()), QString());
    EXPECT_NE(forCode(ErrorCode::SigningEngineError, fallback), fallback);
}
