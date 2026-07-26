// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "ErrorText.h"

#include <gtest/gtest.h>

using namespace LibreKDE;

TEST(ErrorText, KnownCodesAreNonEmptyAndDistinct)
{
    const QString cap = ErrorText::forCode(ErrorCode::CapabilityMissing, QStringLiteral("fallback"));
    const QString rate = ErrorText::forCode(ErrorCode::RateLimited, QStringLiteral("fallback"));
    const QString prompter = ErrorText::forCode(ErrorCode::PrompterError, QStringLiteral("fallback"));

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
    EXPECT_EQ(ErrorText::forCode(ErrorCode::None, fallback), fallback);
}

TEST(ErrorText, EnumValuesMirrorTaxonomy)
{
    EXPECT_EQ(static_cast<std::uint32_t>(ErrorCode::PrompterError), 8u);
    EXPECT_EQ(static_cast<std::uint32_t>(ErrorCode::CapabilityMissing), 9u);
    EXPECT_EQ(static_cast<std::uint32_t>(ErrorCode::RateLimited), 17u);
}
