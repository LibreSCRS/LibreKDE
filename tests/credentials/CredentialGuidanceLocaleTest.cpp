// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Proves the credential copy resolves end-to-end through the real compiled
// Serbian catalog under a genuine UTF-8 locale — the runtime counterpart to the
// CredentialTextCoverageTest compile-time guard:
//  - a credential STATE label (CredentialText::stateName) resolves to its
//    Serbian translation, so the ki18nd("librekde", …) table and the sr catalog
//    line up;
//  - the two guidance KEYS the agent emits (librescrs.pin.blocked.issuer,
//    librescrs.pin.keyActivation.issuer — defined LM-side in pin_family_quirks)
//    resolve to their Serbian strings via CredentialText::guidance;
//  - an UNKNOWN guidance key (no catalog entry) degrades to the agent-authored
//    English fallback, never leaking the raw key.
//
// Mirrors tests/agentclient/IdentityLabelTest: CMake compiles po/sr/librekde.po
// into a private XDG data tree exposed via XDG_DATA_DIRS, so the translation
// resolves through KLocalizedString's standard catalog lookup (stable across
// ki18n versions) without a system-wide install.

#include "CredentialText.h"
#include "CredentialTypes.h"

#include <KLocalizedString>

#include <QCoreApplication>

#include <gtest/gtest.h>

using namespace LibreKDE;

TEST(CredentialGuidanceLocale, StateNameResolvesToSerbian)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    const QString blocked = CredentialText::stateName(CredentialState::Blocked);
    // "Blocked" (msgid) → "Блокиран" in the sr catalog — proves a real
    // translation, not the English source, comes back.
    EXPECT_EQ(blocked, QString::fromUtf8("Блокиран"));
    EXPECT_NE(blocked, QStringLiteral("Blocked"));
    KLocalizedString::clearLanguages();
}

TEST(CredentialGuidanceLocale, GuidanceKeysResolveToSerbian)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    // The exact keys the agent emits (LM pin_family_quirks). The English
    // fallbacks are the agent-authored defaultText; a translation MUST win.
    const QString blocked = CredentialText::guidance(QStringLiteral("librescrs.pin.blocked.issuer"),
                                                     QStringLiteral("Unblocking is done by the issuer."));
    EXPECT_EQ(blocked, QString::fromUtf8("Деблокаду обавља издавалац картице."));

    const QString keyAct = CredentialText::guidance(QStringLiteral("librescrs.pin.keyActivation.issuer"),
                                                    QStringLiteral("The signing key is activated by the issuer."));
    EXPECT_EQ(keyAct, QString::fromUtf8("Активацију кључа за потписивање обавља издавалац."));
    KLocalizedString::clearLanguages();
}

TEST(CredentialGuidanceLocale, UnknownGuidanceKeyFallsBackToAgentText)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    // No catalog entry → ki18nd returns the key unchanged → guidance() must
    // hand back the agent's English fallback, never the raw key.
    const QString text = CredentialText::guidance(QStringLiteral("librescrs.pin.no.such.key"),
                                                  QStringLiteral("Agent-authored fallback."));
    EXPECT_EQ(text, QStringLiteral("Agent-authored fallback."));
    KLocalizedString::clearLanguages();
}

int main(int argc, char** argv)
{
    // KLocalizedString catalog loading resolves paths against the running
    // QCoreApplication; without one the sr catalog is never consulted.
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
