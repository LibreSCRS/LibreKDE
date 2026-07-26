// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Exercises the shared identity-field label resolver (localizedFieldLabel):
//  - a KNOWN frozen labelKey resolves to the real Serbian translation, proving
//    the ki18ndc("librekde", …) table + the sr catalog line up end-to-end;
//  - an UNKNOWN key falls back to the agent-authored English label;
//  - an unknown key with no fallback degrades to the raw fieldKey.
//
// The sr catalog is compiled into a private XDG data tree by CMake and exposed
// via XDG_DATA_DIRS, so the translation resolves through KLocalizedString's
// standard catalog lookup (stable across ki18n versions) without a system-wide
// install — the runtime proof that the labelKey→Serbian wiring works.

#include "IdentityRows.h"

#include <KLocalizedString>

#include <QCoreApplication>

#include <gtest/gtest.h>

using namespace LibreKDE;

namespace {

IdentityRow makeRow(const QString& labelKey, const QString& labelFallback, const QString& fieldKey)
{
    IdentityRow row;
    row.labelKey = labelKey;
    row.labelFallback = labelFallback;
    row.fieldKey = fieldKey;
    return row;
}

} // namespace

TEST(IdentityLabel, KnownKeyResolvesToSerbian)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    // field.surname → "Surname" (msgid) → "Презиме" in the sr catalog.
    const QString label = localizedFieldLabel(
        makeRow(QStringLiteral("field.surname"), QStringLiteral("Surname"), QStringLiteral("surname")));
    EXPECT_EQ(label, QString::fromUtf8("Презиме"));

    // A second, distinct key resolves independently (not a single hard-coded hit).
    const QString num = localizedFieldLabel(makeRow(
        QStringLiteral("field.personal_number"), QStringLiteral("Personal Number"), QStringLiteral("personal_number")));
    EXPECT_EQ(num, QString::fromUtf8("Лични број"));
    KLocalizedString::clearLanguages();
}

TEST(IdentityLabel, UnknownKeyFallsBackToLabelFallback)
{
    const QString label = localizedFieldLabel(
        makeRow(QStringLiteral("field.no_such_key"), QStringLiteral("Agent Label"), QStringLiteral("no_such_key")));
    EXPECT_EQ(label, QStringLiteral("Agent Label"));
}

TEST(IdentityLabel, UnknownKeyWithNoFallbackUsesFieldKey)
{
    const QString label =
        localizedFieldLabel(makeRow(QStringLiteral("field.no_such_key"), QString(), QStringLiteral("raw_field_key")));
    EXPECT_EQ(label, QStringLiteral("raw_field_key"));
}

int main(int argc, char** argv)
{
    // KLocalizedString catalog loading resolves paths against the running
    // QCoreApplication; without one the sr catalog is never consulted.
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    // The CMake-compiled sr catalog lives in a private XDG data tree that the
    // test's XDG_DATA_DIRS entry exposes, so KLocalizedString resolves it via
    // the standard GenericDataLocation lookup — no system-wide install and no
    // version-fragile addDomainLocaleDir() registration.
    return RUN_ALL_TESTS();
}
