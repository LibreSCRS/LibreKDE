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

#include <utility>

#include <gtest/gtest.h>

using LibreKDE::isHiddenIdentityRow;
using LibreKDE::localizedCheckReason;
using LibreKDE::localizedFieldLabel;
using LibreKDE::localizedFieldValue;
using LibreKDE::localizedGroupLabel;
using LibreKDE::mappedCheckReasonKeys;
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
    EXPECT_EQ(keys.size(), 10);
    EXPECT_TRUE(keys.contains(QStringLiteral("presence")));
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
// specific record of WHY those checks have, no catalog can hold it, and it must
// survive. The reason key does NOT retire this passthrough: it retires the
// English sentence at the producer, for the one check that now ships a key. The
// example here is deliberately a check that ships no reason — the CSCA sentence
// this used to name is gone from the producer, and a test that keeps asserting
// it would be pinning a string nobody sends.
TEST(IdentityValue, VerdictDetailSurvivesTranslation)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    IdentityRow row;
    row.groupKey = QStringLiteral("security_status");
    row.value = QStringLiteral("FAILED (DG2 hash mismatch)");
    const QString out = localizedFieldValue(row);
    EXPECT_TRUE(out.startsWith(QString::fromUtf8("Неуспешно"))) << qPrintable(out);
    EXPECT_TRUE(out.contains(QStringLiteral("(DG2 hash mismatch)"))) << qPrintable(out);
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

// The presence group's auth_method value is the eMRTD plugin's English prose
// ("Chip Authentication", "None (plain read)") sitting beside Cyrillic labels
// — a closed five-token set, localized by KEY scope so data_groups (a machine
// list riding the same group) stays verbatim.
TEST(IdentityValue, AuthMethodTokensRenderLocalized)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    IdentityRow row;
    row.groupKey = QStringLiteral("presence");
    row.labelKey = QStringLiteral("field.auth_method");
    row.value = QStringLiteral("Chip Authentication");
    EXPECT_EQ(localizedFieldValue(row), QString::fromUtf8("Аутентификација чипа"));

    row.value = QStringLiteral("None (plain read)");
    EXPECT_EQ(localizedFieldValue(row), QString::fromUtf8("Без заштите (отворено читање)"));

    // The protocol names stay themselves in Serbian; the catalog entries exist
    // so a language CAN adapt them, not because they must change.
    row.value = QStringLiteral("PACE (CAN)");
    EXPECT_EQ(localizedFieldValue(row), QStringLiteral("PACE (CAN)"));
    KLocalizedString::clearLanguages();
}

// Append-only wire: a method this build has never heard of passes through
// verbatim rather than being erased.
TEST(IdentityValue, UnknownAuthMethodPassesThrough)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    IdentityRow row;
    row.labelKey = QStringLiteral("field.auth_method");
    row.value = QStringLiteral("Terminal Authentication");
    EXPECT_EQ(localizedFieldValue(row), QStringLiteral("Terminal Authentication"));
    KLocalizedString::clearLanguages();
}

// data_groups rides the same presence group and must stay the machine list it
// is — the value dictionary is keyed by labelKey, not by group.
TEST(IdentityValue, DataGroupsListStaysVerbatim)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    IdentityRow row;
    row.groupKey = QStringLiteral("presence");
    row.labelKey = QStringLiteral("field.data_groups");
    row.value = QStringLiteral("DG1, DG2, DG14");
    EXPECT_EQ(localizedFieldValue(row), QStringLiteral("DG1, DG2, DG14"));
    KLocalizedString::clearLanguages();
}

// The presence group renders with no heading here while the desktop client
// titles the same wire "Authentication" — one vocabulary, two renderings.
TEST(IdentityGroupLabel, PresenceHasTheDesktopClientsHeading)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    EXPECT_EQ(localizedGroupLabel(QStringLiteral("presence")), QString::fromUtf8("Аутентификација"));
    KLocalizedString::clearLanguages();
}

// The annex's fields have a reading order; other groups keep delivery order.
// The full list is asserted, not sampled: it must stay byte-identical to the
// desktop client's copy (LibreCelik, plugins/emrtd/emrtdwidget.cpp,
// annexFieldOrder()), and no shared library links the two repositories, so
// each pins its own copy — change both together.
TEST(IdentityGroupLabel, AnnexHasAReadingOrderAndOtherGroupsDoNot)
{
    const QStringList expected{
        QStringLiteral("address_label"),     QStringLiteral("street"),
        QStringLiteral("house_number"),      QStringLiteral("house_letter"),
        QStringLiteral("entrance"),          QStringLiteral("floor"),
        QStringLiteral("apartment_number"),  QStringLiteral("place"),
        QStringLiteral("community"),         QStringLiteral("state"),
        QStringLiteral("parent_given_name"), QStringLiteral("community_of_birth"),
        QStringLiteral("state_of_birth"),    QStringLiteral("document_serial"),
        QStringLiteral("address_date"),
    };
    EXPECT_EQ(LibreKDE::fieldOrderForGroup(QStringLiteral("annex.rs.personal")), expected);
    EXPECT_TRUE(LibreKDE::fieldOrderForGroup(QStringLiteral("personal")).isEmpty());
    const QStringList security = LibreKDE::fieldOrderForGroup(QStringLiteral("annex.rs.security"));
    EXPECT_EQ(security, (QStringList{QStringLiteral("annex_integrity"), QStringLiteral("annex_authenticity")}))
        << "integrity leads the verdict pair, matching the desktop client";
}

// The five CSCA reason keys, in Serbian, asserted by CONTENT. These are the
// only place a reader is told what to DO about a chain check that did not run,
// so a key that resolves to nothing (or to itself) is the whole feature
// missing. Each names the remedy, not just the condition — "not configured" and
// "could not be read" want different instructions, which is why the wire
// carries a reason key at all rather than one status.
TEST(IdentityCheckReason, EveryReasonResolvesToSerbian)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    EXPECT_EQ(localizedCheckReason(QStringLiteral("csca.not-configured")),
              QString::fromUtf8("Ниједан CSCA сертификат није увезен. Увезите ICAO мастер-листу у "
                                "LibreCelik-у, у одељку Подешавања → Поверење, да би потписник овог "
                                "документа могао да се провери."));
    EXPECT_EQ(localizedCheckReason(QStringLiteral("csca.anchors-unreadable")),
              QString::fromUtf8("Складиште CSCA сертификата се не може прочитати. Проверите да ли његов "
                                "директоријум постоји и да ли дозволе допуштају читање."));
    EXPECT_EQ(localizedCheckReason(QStringLiteral("csca.anchors-undecodable")),
              QString::fromUtf8("Складиште CSCA сертификата не садржи ниједан употребљив сертификат. "
                                "Поново увезите ICAO мастер-листу у LibreCelik-у, у одељку "
                                "Подешавања → Поверење."));
    EXPECT_EQ(localizedCheckReason(QStringLiteral("csca.no-anchor-for-issuer")),
              QString::fromUtf8("Ниједан увезени CSCA сертификат не припада издаваоцу овог документа. У "
                                "LibreCelik-у, у одељку Подешавања → Поверење, увезите мастер-листу која "
                                "покрива државу издаваоца."));
    EXPECT_EQ(localizedCheckReason(QStringLiteral("csca.chain-failed")),
              QString::fromUtf8("Потписник овог документа се не повезује ни са једним увезеним CSCA "
                                "сертификатом. Не ослањајте се на овај документ; проверите га код издаваоца."));
    KLocalizedString::clearLanguages();
}

// Same three-step the field labels take: catalog hit, else the text the
// producer authored, else the raw key. Never blank, never the word "unknown" —
// a reason this build cannot name still has to leave the reader something to
// quote in a bug report.
TEST(IdentityCheckReason, UnknownKeyDegradesRatherThanErasing)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    EXPECT_EQ(localizedCheckReason(QStringLiteral("csca.invented-later"), QStringLiteral("agent sentence")),
              QStringLiteral("agent sentence"));
    EXPECT_EQ(localizedCheckReason(QStringLiteral("csca.invented-later")), QStringLiteral("csca.invented-later"));
    EXPECT_FALSE(localizedCheckReason(QStringLiteral("csca.invented-later")).isEmpty());
    KLocalizedString::clearLanguages();
}

TEST(IdentityCheckReason, MappedReasonKeyCoverageIsPinned)
{
    const QStringList keys = mappedCheckReasonKeys();
    EXPECT_EQ(keys.size(), 5);
    // Exactly one of the five accuses the document; the other four describe the
    // reader's own configuration and must never be reported as FAILED.
    EXPECT_TRUE(keys.contains(QStringLiteral("csca.not-configured")));
    EXPECT_TRUE(keys.contains(QStringLiteral("csca.anchors-unreadable")));
    EXPECT_TRUE(keys.contains(QStringLiteral("csca.anchors-undecodable")));
    EXPECT_TRUE(keys.contains(QStringLiteral("csca.no-anchor-for-issuer")));
    EXPECT_TRUE(keys.contains(QStringLiteral("csca.chain-failed")));
}

// No two reasons may share a string: the five exist precisely because one
// message for all of them sends the reader looking in the wrong place.
TEST(IdentityCheckReason, NoTwoReasonsShareAMessage)
{
    KLocalizedString::setLanguages({QStringLiteral("sr")});
    QStringList seen;
    for (const QString& key : mappedCheckReasonKeys()) {
        const QString text = localizedCheckReason(key);
        EXPECT_FALSE(text.isEmpty()) << qPrintable(key);
        EXPECT_NE(text, key) << qPrintable(key) << " has no translation of its own";
        EXPECT_FALSE(seen.contains(text)) << qPrintable(key) << " repeats another reason's message";
        seen << text;
    }
    KLocalizedString::clearLanguages();
}

// A remedy the reader cannot locate is a condition in the imperative mood: "import
// a master list" is an instruction only for someone who already knows where the
// import lives, and the average reader does not. So every reason that asks for an
// import has to name the place — in English AND in every translation of it.
//
// The translation half is the point. Changing the English source of a message that
// already has one leaves the OLD translation attached to the new msgid, so the
// Serbian reader keeps being shown the sentence that named nowhere while the
// English reader sees the fixed one. Nothing else in this suite would notice:
// msgfmt is happy, the catalogs still reconcile, and a by-value assertion only
// covers the strings someone remembered to write down.
//
// Deliberately a property rather than a transcript — the exact sentences are pinned
// by EveryReasonResolvesToSerbian above. Which reasons must carry a location is
// derived from the English text (it asks for an import), not from a list kept here,
// so a sixth reason key added later inherits the rule without anyone updating this.
TEST(IdentityCheckReason, EveryImportRemedyNamesWhereToImport)
{
    // The import lives in LibreCelik, under Settings → Trust; the KDE surfaces
    // launch that application rather than carrying an import of their own. What is
    // asserted is that an application and a menu path are named AT ALL — not their
    // wording, which a translation is free to change. No URL is asserted, and none
    // belongs here: that settings screen names the portal itself.
    const QString application = QStringLiteral("LibreCelik");
    const QString menuPath = QString::fromUtf8("→");

    QStringList askForAnImport;
    KLocalizedString::setLanguages({QStringLiteral("en")});
    for (const QString& key : mappedCheckReasonKeys()) {
        const QString english = localizedCheckReason(key);
        ASSERT_NE(english, key) << qPrintable(key) << " resolved to nothing but itself";
        if (!english.contains(QStringLiteral("master list"))) {
            EXPECT_FALSE(english.contains(application))
                << qPrintable(key) << " sends the reader to a screen it never asks them to use";
            continue;
        }
        askForAnImport << key;
        EXPECT_TRUE(english.contains(application)) << qPrintable(key) << ": " << qPrintable(english);
        EXPECT_TRUE(english.contains(menuPath)) << qPrintable(key) << ": " << qPrintable(english);
    }
    // Two of the five ask for something else entirely — a directory's permissions,
    // and the issuing authority — so a run that found every reason asking for an
    // import has stopped discriminating and proves nothing.
    EXPECT_EQ(askForAnImport.size(), 3);

    KLocalizedString::setLanguages({QStringLiteral("sr")});
    for (const QString& key : std::as_const(askForAnImport)) {
        const QString serbian = localizedCheckReason(key);
        EXPECT_TRUE(serbian.contains(application))
            << qPrintable(key) << " lost the location in translation: " << qPrintable(serbian);
        EXPECT_TRUE(serbian.contains(menuPath))
            << qPrintable(key) << " lost the location in translation: " << qPrintable(serbian);
    }
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
