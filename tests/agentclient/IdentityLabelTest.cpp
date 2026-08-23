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
using LibreKDE::localizedGroupLabel;
using LibreKDE::mappedGroupKeys;
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
    EXPECT_EQ(keys.size(), 72);
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
    // The annex reader ships the card's raw ddMMyyyy digits; the display
    // normalises them to dd.MM.yyyy (06.08.2016), not "Unknown".
    EXPECT_EQ(localizedFieldValue(makeValueRow(QStringLiteral("field.address_date"), QStringLiteral("06082016"))),
              QStringLiteral("06.08.2016"));
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

// The four keys the annex needs and the table did not have. Each names its
// exact Serbian string: a presence check would pass on a mis-wired entry.
TEST(IdentityLabel, AnnexKeysResolveToSerbian)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    EXPECT_EQ(localizedFieldLabel(makeRow(QStringLiteral("field.address_label"), QStringLiteral("Address"),
                                          QStringLiteral("address_label"))),
              QString::fromUtf8("Адреса"));
    EXPECT_EQ(localizedFieldLabel(makeRow(QStringLiteral("field.document_serial"), QStringLiteral("Document Number"),
                                          QStringLiteral("document_serial"))),
              QString::fromUtf8("Број документа"));
    EXPECT_EQ(localizedFieldLabel(makeRow(QStringLiteral("field.annex_integrity"), QStringLiteral("Data Integrity"),
                                          QStringLiteral("annex_integrity"))),
              QString::fromUtf8("Интегритет података"));
    EXPECT_EQ(localizedFieldLabel(makeRow(QStringLiteral("field.annex_authenticity"),
                                          QStringLiteral("Data Authenticity"), QStringLiteral("annex_authenticity"))),
              QString::fromUtf8("Аутентичност података"));
    KLocalizedString::clearLanguages();
}

// document_serial is NOT folded into document_serial_number. That key is the
// eID plugin's and is one of the curated summary keys, so aliasing would
// promote an annex row into the popup headline; the labels differ besides.
TEST(IdentityLabel, AnnexDocumentSerialIsNotTheEidSerialNumber)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    const QString annex = localizedFieldLabel(
        makeRow(QStringLiteral("field.document_serial"), QString(), QStringLiteral("document_serial")));
    const QString eid = localizedFieldLabel(
        makeRow(QStringLiteral("field.document_serial_number"), QString(), QStringLiteral("document_serial_number")));
    EXPECT_NE(annex, eid);
    KLocalizedString::clearLanguages();
}

// The verdict rows are the only statement of how far the annex's guarantee
// reaches. Unlike the three raw *_verification traces, they must NOT be hidden.
TEST(IdentityRowFilter, AnnexVerdictRowsAreNotHidden)
{
    for (const QString& key : {QStringLiteral("field.annex_integrity"), QStringLiteral("field.annex_authenticity")}) {
        EXPECT_FALSE(isHiddenIdentityRow(makeRow(key, key, key))) << qPrintable(key);
    }
}

// ---- group headings --------------------------------------------------------

TEST(IdentityGroupLabel, KnownGroupResolvesToSerbian)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    EXPECT_EQ(localizedGroupLabel(QStringLiteral("personal")), QString::fromUtf8("Лични подаци"));
    EXPECT_EQ(localizedGroupLabel(QStringLiteral("security_status")),
              QString::fromUtf8("Провера података путне исправе"));
    KLocalizedString::clearLanguages();
}

// The id in the middle comes from the reader, so matching is on the prefix.
// Two different ids must resolve to the SAME heading, or the next annex shows
// up headless exactly as this one did.
TEST(IdentityGroupLabel, AnnexGroupsMatchOnPrefixNotOnId)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    const QString rs = localizedGroupLabel(QStringLiteral("annex.rs.personal"));
    const QString zz = localizedGroupLabel(QStringLiteral("annex.zz.personal"));
    EXPECT_FALSE(rs.isEmpty());
    EXPECT_EQ(rs, zz);
    EXPECT_EQ(rs, QString::fromUtf8("Додатни лични подаци"));

    // The annex block must NOT read as the passport-supplementary one. A card
    // ships BOTH, and while they shared a heading the popup showed two
    // identically named blocks — the reader could not tell which fields came
    // from the passport data groups and which from the signed annex.
    EXPECT_NE(rs, localizedGroupLabel(QStringLiteral("additional")))
        << "annex and DG11 sections share a heading: " << qPrintable(rs);

    const QString verdict = localizedGroupLabel(QStringLiteral("annex.rs.security"));
    EXPECT_EQ(verdict, QString::fromUtf8("Провера додатних података"));
    EXPECT_NE(verdict, rs) << "the verdict heading must not read as the data's";
    KLocalizedString::clearLanguages();
}

// Empty means "render no heading", never "render an empty heading" — and never
// the raw key, which would put a machine identifier on screen as a title.
TEST(IdentityGroupLabel, UnknownGroupResolvesToEmpty)
{
    EXPECT_TRUE(localizedGroupLabel(QStringLiteral("no_such_group")).isEmpty());
    EXPECT_TRUE(localizedGroupLabel(QStringLiteral("annex.rs.unknown_suffix")).isEmpty());
    EXPECT_TRUE(localizedGroupLabel(QString()).isEmpty());
}

TEST(IdentityGroupLabel, MappedGroupKeyCoverageIsPinned)
{
    const QStringList keys = mappedGroupKeys();
    EXPECT_EQ(keys.size(), 9);
    EXPECT_TRUE(keys.contains(QStringLiteral("security_status")));
    EXPECT_TRUE(keys.contains(QStringLiteral("annex.<id>.personal")));
    EXPECT_TRUE(keys.contains(QStringLiteral("annex.<id>.security")));
}

// One heading per group, and no two groups sharing one. The collision this
// pins was found on a live card, not here: the annex block and the
// passport-supplementary block both read "Additional Data", and a Serbian
// identity card carries both.
TEST(IdentityGroupLabel, NoTwoGroupsShareAHeading)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    QStringList headings;
    for (const QString& key : mappedGroupKeys()) {
        // The two annex entries are spelled with a placeholder id in the pinned
        // list; resolve them through a real key.
        QString probe = key;
        probe.replace(QStringLiteral("<id>"), QStringLiteral("rs"));
        const QString heading = localizedGroupLabel(probe);
        EXPECT_FALSE(heading.isEmpty()) << "unmapped: " << qPrintable(probe);
        headings << heading;
    }
    QStringList unique = headings;
    unique.removeDuplicates();
    EXPECT_EQ(headings.size(), unique.size()) << "two groups share a heading: " << qPrintable(headings.join(u" | "));
    KLocalizedString::clearLanguages();
}

// The popup was printing the wire's own tokens at a reader: "PASSED",
// "NOT_PERFORMED". Those are machine vocabulary sitting beside Cyrillic labels.
TEST(IdentityValue, VerdictTokensRenderAsSentences)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    IdentityRow row;
    row.groupKey = QStringLiteral("security_status");
    row.value = QStringLiteral("PASSED");
    EXPECT_EQ(localizedFieldValue(row), QString::fromUtf8("Успешно"));

    row.value = QStringLiteral("NOT_PERFORMED");
    EXPECT_EQ(localizedFieldValue(row), QString::fromUtf8("Није извршено"));

    row.groupKey = QStringLiteral("annex.rs.security");
    row.value = QStringLiteral("PASSED");
    EXPECT_EQ(localizedFieldValue(row), QString::fromUtf8("Успешно"));
    KLocalizedString::clearLanguages();
}

// A verdict may carry a parenthetical the plugin authored. It is the only
// specific record of WHY, no catalog can hold it, and it must survive.
TEST(IdentityValue, VerdictDetailSurvivesTranslation)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    IdentityRow row;
    row.groupKey = QStringLiteral("security_status");
    row.value = QStringLiteral("NOT_PERFORMED (No CSCA trust store configured)");
    const QString out = localizedFieldValue(row);
    EXPECT_TRUE(out.startsWith(QString::fromUtf8("Није извршено"))) << qPrintable(out);
    EXPECT_TRUE(out.contains(QStringLiteral("(No CSCA trust store configured)"))) << qPrintable(out);
    KLocalizedString::clearLanguages();
}

// Scoped to verdict groups: a personal field whose value happens to read
// "PASSED" is card data and must not be rewritten into a verdict.
TEST(IdentityValue, OnlyVerdictGroupsGetTheStatusVocabulary)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    IdentityRow row;
    row.groupKey = QStringLiteral("personal");
    row.value = QStringLiteral("PASSED");
    EXPECT_EQ(localizedFieldValue(row), QStringLiteral("PASSED"));
    KLocalizedString::clearLanguages();
}

// The wire is append-only: a token this build has never heard of passes through
// verbatim rather than being erased into "unknown".
TEST(IdentityValue, UnknownVerdictTokenPassesThrough)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    IdentityRow row;
    row.groupKey = QStringLiteral("security_status");
    row.value = QStringLiteral("SOMETHING_NEW");
    EXPECT_EQ(localizedFieldValue(row), QStringLiteral("SOMETHING_NEW"));
    KLocalizedString::clearLanguages();
}

// The annex's fields have a reading order; other groups keep delivery order.
TEST(IdentityGroupLabel, AnnexHasAReadingOrderAndOtherGroupsDoNot)
{
    const QStringList annex = LibreKDE::fieldOrderForGroup(QStringLiteral("annex.rs.personal"));
    EXPECT_EQ(annex.size(), 15);
    EXPECT_EQ(annex.at(1), QStringLiteral("street")) << "the street leads the address";
    EXPECT_TRUE(LibreKDE::fieldOrderForGroup(QStringLiteral("personal")).isEmpty());
    const QStringList security = LibreKDE::fieldOrderForGroup(QStringLiteral("annex.rs.security"));
    EXPECT_EQ(security, (QStringList{QStringLiteral("annex_integrity"), QStringLiteral("annex_authenticity")}))
        << "integrity leads the verdict pair, matching the desktop client";
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
