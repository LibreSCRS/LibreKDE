// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#include "CredentialTypes.h"
#include <QVariantMap>
#include <gtest/gtest.h>
using namespace LibreKDE;

TEST(CredentialTypes, RecordParsesFullRecord)
{
    QVariantMap m;
    m[QStringLiteral("id")] = QStringLiteral("user:0x86");
    m[QStringLiteral("label")] = QStringLiteral("User PIN");
    m[QStringLiteral("kind")] = QStringLiteral("user");
    m[QStringLiteral("state")] = QStringLiteral("operational");
    m[QStringLiteral("retries_left")] = 3;
    m[QStringLiteral("retries_max")] = 3;
    m[QStringLiteral("can_change")] = true;
    m[QStringLiteral("unblockable")] = true;
    m[QStringLiteral("unblock_style")] = QStringLiteral("unblockAndChange");
    m[QStringLiteral("activatable")] = false;
    m[QStringLiteral("key_activation_pending")] = false;
    m[QStringLiteral("key_activatable")] = false;
    m[QStringLiteral("recovery")] = QStringLiteral("holderViaPuk");
    m[QStringLiteral("probe_safe")] = true;
    const CredentialRecord r = CredentialRecord::fromVariantMap(m);
    EXPECT_EQ(r.id, QStringLiteral("user:0x86"));
    EXPECT_EQ(r.kind, CredentialKind::User);
    EXPECT_EQ(r.state, CredentialState::Operational);
    ASSERT_TRUE(r.retriesLeft.has_value());
    EXPECT_EQ(*r.retriesLeft, 3);
    EXPECT_TRUE(r.canChange);
    EXPECT_TRUE(r.unblockable);
    EXPECT_EQ(r.unblockStyle, UnblockStyle::UnblockAndChange);
    EXPECT_EQ(r.recovery, RecoveryPath::HolderViaPuk);
}

TEST(CredentialTypes, OmittedIntsStayNullopt)
{
    QVariantMap m;
    m[QStringLiteral("id")] = QStringLiteral("puk:0x93");
    m[QStringLiteral("kind")] = QStringLiteral("puk");
    m[QStringLiteral("state")] = QStringLiteral("operational");
    m[QStringLiteral("can_change")] = false;
    m[QStringLiteral("unblockable")] = false;
    m[QStringLiteral("activatable")] = false;
    m[QStringLiteral("key_activation_pending")] = false;
    m[QStringLiteral("key_activatable")] = false;
    m[QStringLiteral("probe_safe")] = false;
    const CredentialRecord r = CredentialRecord::fromVariantMap(m);
    EXPECT_FALSE(r.retriesLeft.has_value());
    EXPECT_FALSE(r.usesLeft.has_value());
    EXPECT_FALSE(r.usesMax.has_value());
    EXPECT_EQ(r.kind, CredentialKind::Puk);
    EXPECT_EQ(r.unblockStyle, UnblockStyle::Unknown); // absent token → Unknown
}

TEST(CredentialTypes, UnknownTokensAreSafe)
{
    QVariantMap m;
    m[QStringLiteral("kind")] = QStringLiteral("wat");
    m[QStringLiteral("state")] = QStringLiteral("nope");
    m[QStringLiteral("recovery")] = QStringLiteral("??");
    const CredentialRecord r = CredentialRecord::fromVariantMap(m);
    EXPECT_EQ(r.kind, CredentialKind::Unknown);
    EXPECT_EQ(r.state, CredentialState::Unknown);
    EXPECT_EQ(r.recovery, RecoveryPath::Unknown);
}

TEST(CredentialTypes, PinResultParses)
{
    QVariantMap m;
    m[QStringLiteral("outcome")] = QStringLiteral("invalidPin");
    m[QStringLiteral("retries_left")] = 2;
    m[QStringLiteral("blocked")] = false;
    const PinResult r = PinResult::fromVariantMap(m);
    EXPECT_EQ(r.outcome, CredentialOutcome::InvalidPin);
    ASSERT_TRUE(r.retriesLeft.has_value());
    EXPECT_EQ(*r.retriesLeft, 2);
    EXPECT_FALSE(r.blocked);
    EXPECT_FALSE(r.pinActivated.has_value()); // omitted → nullopt
}

// Wire-key drift guard: the snake_case key strings below are the
// VERBATIM record/result key set of the frozen
// org.librescrs.Agent.Operation.Credentials1.xml (LibreLinux/agent/dbus/ —
// this repo's client mirrors it by hand, so both the production demarshaller
// AND the ordinary test fixtures could drift TOGETHER; this fixture freezes the
// XML spelling). Every key carries a non-default value and every demarshaled
// field must come back non-default — a client-side key rename (e.g. reading
// "retriesLeft" where the agent emits "retries_left") fails loudly here while
// passing every self-consistent test. Keep byte-identical to the XML.
TEST(CredentialTypes, XmlVerbatimRecordKeySetDemarshalsEveryFieldNonDefault)
{
    const QVariantMap wire{
        {QStringLiteral("id"), QStringLiteral("user:0x86")},
        {QStringLiteral("label"), QStringLiteral("User PIN")},
        {QStringLiteral("kind"), QStringLiteral("user")},
        {QStringLiteral("state"), QStringLiteral("operational")},
        {QStringLiteral("retries_left"), 3},
        {QStringLiteral("retries_max"), 5},
        {QStringLiteral("uses_left"), 28},
        {QStringLiteral("uses_max"), 40},
        {QStringLiteral("unblocks_left"), 19},
        {QStringLiteral("min_length"), 4},
        {QStringLiteral("max_length"), 8},
        {QStringLiteral("can_change"), true},
        {QStringLiteral("unblockable"), true},
        {QStringLiteral("unblock_style"), QStringLiteral("setsNewPin")},
        {QStringLiteral("activatable"), true},
        {QStringLiteral("key_activation_pending"), true},
        {QStringLiteral("key_activatable"), true},
        {QStringLiteral("recovery"), QStringLiteral("holderViaPuk")},
        {QStringLiteral("probe_safe"), true},
        {QStringLiteral("blocked_guidance_key"), QStringLiteral("librescrs.pin.blocked.issuer")},
        {QStringLiteral("blocked_guidance_fallback"), QStringLiteral("Contact the issuer.")},
        {QStringLiteral("key_activation_guidance_key"), QStringLiteral("librescrs.pin.keyActivation.issuer")},
        {QStringLiteral("key_activation_guidance_fallback"), QStringLiteral("The issuer activates the key.")},
    };
    const CredentialRecord r = CredentialRecord::fromVariantMap(wire);
    EXPECT_EQ(r.id, QStringLiteral("user:0x86"));
    EXPECT_EQ(r.label, QStringLiteral("User PIN"));
    EXPECT_EQ(r.kind, CredentialKind::User);
    EXPECT_EQ(r.state, CredentialState::Operational);
    ASSERT_TRUE(r.retriesLeft.has_value());
    EXPECT_EQ(*r.retriesLeft, 3);
    ASSERT_TRUE(r.retriesMax.has_value());
    EXPECT_EQ(*r.retriesMax, 5);
    ASSERT_TRUE(r.usesLeft.has_value());
    EXPECT_EQ(*r.usesLeft, 28);
    ASSERT_TRUE(r.usesMax.has_value());
    EXPECT_EQ(*r.usesMax, 40);
    ASSERT_TRUE(r.unblocksLeft.has_value());
    EXPECT_EQ(*r.unblocksLeft, 19);
    ASSERT_TRUE(r.minLength.has_value());
    EXPECT_EQ(*r.minLength, 4);
    ASSERT_TRUE(r.maxLength.has_value());
    EXPECT_EQ(*r.maxLength, 8);
    EXPECT_TRUE(r.canChange);
    EXPECT_TRUE(r.unblockable);
    EXPECT_EQ(r.unblockStyle, UnblockStyle::SetsNewPin);
    EXPECT_TRUE(r.activatable);
    EXPECT_TRUE(r.keyActivationPending);
    EXPECT_TRUE(r.keyActivatable);
    EXPECT_EQ(r.recovery, RecoveryPath::HolderViaPuk);
    EXPECT_TRUE(r.probeSafe);
    ASSERT_TRUE(r.blockedGuidanceKey.has_value());
    EXPECT_EQ(*r.blockedGuidanceKey, QStringLiteral("librescrs.pin.blocked.issuer"));
    ASSERT_TRUE(r.blockedGuidanceFallback.has_value());
    EXPECT_EQ(*r.blockedGuidanceFallback, QStringLiteral("Contact the issuer."));
    ASSERT_TRUE(r.keyActivationGuidanceKey.has_value());
    EXPECT_EQ(*r.keyActivationGuidanceKey, QStringLiteral("librescrs.pin.keyActivation.issuer"));
    ASSERT_TRUE(r.keyActivationGuidanceFallback.has_value());
    EXPECT_EQ(*r.keyActivationGuidanceFallback, QStringLiteral("The issuer activates the key."));
}

// The result-half of the same guard: the uniform a{sv} mutation-result key set,
// XML-verbatim, every field non-default.
TEST(CredentialTypes, XmlVerbatimResultKeySetDemarshalsEveryFieldNonDefault)
{
    const QVariantMap wire{
        {QStringLiteral("outcome"), QStringLiteral("invalidPin")},
        {QStringLiteral("retries_left"), 2},
        {QStringLiteral("blocked"), true},
        {QStringLiteral("pin_activated"), true},
        {QStringLiteral("key_activated"), true},
    };
    const PinResult r = PinResult::fromVariantMap(wire);
    EXPECT_EQ(r.outcome, CredentialOutcome::InvalidPin);
    ASSERT_TRUE(r.retriesLeft.has_value());
    EXPECT_EQ(*r.retriesLeft, 2);
    EXPECT_TRUE(r.blocked);
    ASSERT_TRUE(r.pinActivated.has_value());
    EXPECT_TRUE(*r.pinActivated);
    ASSERT_TRUE(r.keyActivated.has_value());
    EXPECT_TRUE(*r.keyActivated);
}

TEST(CredentialTypes, PinResultBringUpPartial)
{
    QVariantMap m;
    m[QStringLiteral("outcome")] = QStringLiteral("keyActivationFailed");
    m[QStringLiteral("blocked")] = false;
    m[QStringLiteral("pin_activated")] = true;
    m[QStringLiteral("key_activated")] = false;
    const PinResult r = PinResult::fromVariantMap(m);
    EXPECT_EQ(r.outcome, CredentialOutcome::KeyActivationFailed);
    ASSERT_TRUE(r.pinActivated.has_value());
    EXPECT_TRUE(*r.pinActivated);
    ASSERT_TRUE(r.keyActivated.has_value());
    EXPECT_FALSE(*r.keyActivated);
}
