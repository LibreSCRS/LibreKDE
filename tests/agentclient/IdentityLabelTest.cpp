// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Exercises the shared identity-field display rules: label resolution (known
// key → real Serbian string, unknown key → agent fallback → raw fieldKey), the
// card-verification row filter, and the address-date value fallback.
//
// Assertions check CONTENT, not presence: each names the exact Serbian string,
// and the coverage test pins the size of the mapped-key set, so an entry
// dropped from the table cannot pass silently.
//
// The sr catalog is compiled into a private XDG data tree by CMake and exposed
// via XDG_DATA_DIRS, so the translation resolves through KLocalizedString's
// standard catalog lookup (stable across ki18n versions) without a system-wide
// install — the runtime proof that the labelKey→Serbian wiring works.

#include "IdentityRows.h"

#include <LibreSCRS/AgentClient/IdentityRows.h>

#include <KLocalizedString>

#include <QCoreApplication>

#include <gtest/gtest.h>

using LibreKDE::isHiddenIdentityRow;
using LibreKDE::localizedFieldLabel;
using LibreKDE::localizedFieldValue;
using LibreKDE::mappedLabelKeys;
using LibreSCRS::AgentClient::IdentityRow;

namespace {

IdentityRow makeRow(const QString& labelKey, const QString& labelFallback, const QString& fieldKey)
{
    IdentityRow row;
    row.labelKey = labelKey;
    row.labelFallback = labelFallback;
    row.fieldKey = fieldKey;
    return row;
}

IdentityRow makeValueRow(const QString& labelKey, const QString& value)
{
    IdentityRow row;
    row.labelKey = labelKey;
    row.value = value;
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

// The two keys the hand test found rendering in English.
TEST(IdentityLabel, CardTypeAndAddressDateResolveToSerbian)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    EXPECT_EQ(localizedFieldLabel(
                  makeRow(QStringLiteral("field.card_type"), QStringLiteral("Card Type"), QStringLiteral("card_type"))),
              QString::fromUtf8("Тип картице"));
    EXPECT_EQ(localizedFieldLabel(makeRow(QStringLiteral("field.address_date"), QStringLiteral("Address Date"),
                                          QStringLiteral("address_date"))),
              QString::fromUtf8("Датум промене адресе"));
    KLocalizedString::clearLanguages();
}

// A dropped entry changes the count even when every assertion above still
// finds its own key.
TEST(IdentityLabel, MappedKeyCoverageIsPinned)
{
    const QStringList keys = mappedLabelKeys();
    EXPECT_EQ(keys.size(), 68);
    EXPECT_TRUE(keys.contains(QStringLiteral("field.card_type")));
    EXPECT_TRUE(keys.contains(QStringLiteral("field.address_date")));
    // Hidden rows are filtered, never labelled.
    EXPECT_FALSE(keys.contains(QStringLiteral("field.card_verification")));
    EXPECT_FALSE(keys.contains(QStringLiteral("field.fixed_verification")));
    EXPECT_FALSE(keys.contains(QStringLiteral("field.variable_verification")));
}

TEST(IdentityRowFilter, CardVerificationRowsAreHidden)
{
    for (const QString& key : {QStringLiteral("field.card_verification"), QStringLiteral("field.fixed_verification"),
                               QStringLiteral("field.variable_verification")}) {
        EXPECT_TRUE(isHiddenIdentityRow(makeRow(key, key, key))) << qPrintable(key);
    }
}

TEST(IdentityRowFilter, OrdinaryRowsSurvive)
{
    EXPECT_FALSE(isHiddenIdentityRow(
        makeRow(QStringLiteral("field.surname"), QStringLiteral("Surname"), QStringLiteral("surname"))));
    EXPECT_FALSE(isHiddenIdentityRow(
        makeRow(QStringLiteral("field.card_type"), QStringLiteral("Card Type"), QStringLiteral("card_type"))));
    // A key that merely CONTAINS "verification" is not one of the three.
    EXPECT_FALSE(
        isHiddenIdentityRow(makeRow(QStringLiteral("field.verification_note"), QStringLiteral("Verification Note"),
                                    QStringLiteral("verification_note"))));
}

// The card carries a placeholder where the address-change date belongs; the
// middleware ships it verbatim, so the display is where it becomes readable.
TEST(IdentityValue, AddressDateThatIsNotADateReadsAsUnknown)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    EXPECT_EQ(localizedFieldValue(makeValueRow(QStringLiteral("field.address_date"), QStringLiteral("00001"))),
              QString::fromUtf8("Непознато"));
    EXPECT_EQ(localizedFieldValue(makeValueRow(QStringLiteral("field.address_date"), QStringLiteral("15.03.2020"))),
              QStringLiteral("15.03.2020"));
    // The value read off a live card, which must survive untouched.
    EXPECT_EQ(localizedFieldValue(makeValueRow(QStringLiteral("field.address_date"), QStringLiteral("01.11.2019"))),
              QStringLiteral("01.11.2019"));
    // Date-SHAPED but impossible: rejected because the value is parsed as a
    // date rather than pattern-matched.
    EXPECT_EQ(localizedFieldValue(makeValueRow(QStringLiteral("field.address_date"), QStringLiteral("32.13.2020"))),
              QString::fromUtf8("Непознато"));
    KLocalizedString::clearLanguages();
}

// No blanket date policing — every other field is passed through byte for byte.
TEST(IdentityValue, OtherFieldsPassThroughUntouched)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    EXPECT_EQ(localizedFieldValue(makeValueRow(QStringLiteral("field.personal_number"), QStringLiteral("00001"))),
              QStringLiteral("00001"));
    EXPECT_EQ(localizedFieldValue(makeValueRow(QStringLiteral("field.date_of_birth"), QStringLiteral("00001"))),
              QStringLiteral("00001"));
    KLocalizedString::clearLanguages();
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
