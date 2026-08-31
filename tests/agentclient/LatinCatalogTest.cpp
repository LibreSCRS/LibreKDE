// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// LibreKDE ships Serbian in two scripts: po/sr (Cyrillic, primary) and
// po/sr@latin. tools/check-catalogs.py (the CatalogReconcile ctest case)
// proves both carry the SAME msgid set, but that is an inventory check — it
// never opens po/sr@latin for its CONTENT, so a wrong or missing
// transliteration in that file passes every other gate. This is the runtime
// counterpart, mirroring tests/agentclient/IdentityLabelTest: the sr@latin
// catalog is compiled into the same private XDG data tree and resolved
// through KLocalizedString's standard catalog lookup, so a caller who asks
// for "sr@latin" gets what a real user's desktop would show.
//
// Two properties, because each fails differently:
//
//  - GENERIC, whole-catalog: a Latin rendering that still contains Cyrillic
//    characters is a transliteration that never happened. This is checked
//    for EVERY entry in both shipped sr@latin catalogs by reading the
//    compiled msgstr text directly — no call site, no app-level key table,
//    so an entry nobody remembered to exercise through IdentityRows.h is
//    still covered.
//  - SPECIFIC, by value: a handful of entries — spanning field labels, group
//    headings, a check-reason sentence and a verdict token, so no single
//    resolver function is the only thing proven — asserted against their
//    exact Latin string AND against the Cyrillic string they must differ
//    from. A sr@latin file that merely copied po/sr's msgstrs verbatim would
//    reconcile (same msgids) and even pass the generic scan IF the copied
//    text happened to already be Latin, but it cannot pass THESE: the
//    expected value is the transliteration, not the source.

#include "IdentityRows.h"

#include <LibreSCRS/AgentClient/IdentityRows.h>

#include <KLocalizedString>

#include <QCoreApplication>
#include <QFile>
#include <QStringConverter>
#include <QTextStream>

#include <gtest/gtest.h>

using LibreKDE::localizedCheckReason;
using LibreKDE::localizedFieldLabel;
using LibreKDE::localizedFieldValue;
using LibreKDE::localizedGroupLabel;
using LibreKDE::mappedCheckReasonKeys;
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

// ---- whole-catalog content scan --------------------------------------------

/// Which `.po` grammar field a continuation `"..."` line belongs to. Only
/// `Msgid` and `Msgstr` content is kept — a `msgctxt`/`msgid_plural`
/// continuation is read (to stay in step with the file) but its text plays no
/// part in this check.
enum class PoField { None, Msgctxt, Msgid, MsgidPlural, Msgstr };

/// The quoted payload of one `.po` line, with the handful of C escapes real
/// catalogs use resolved. Mirrors tools/check-catalogs.py's `unquote()`: take
/// the run between the FIRST and LAST `"` on the line, since an escaped `\"`
/// inside the string would otherwise break a naive split.
QString unquotePoLine(const QString& line)
{
    const qsizetype start = line.indexOf(QLatin1Char('"'));
    const qsizetype end = line.lastIndexOf(QLatin1Char('"'));
    if (start < 0 || end <= start) {
        return {};
    }
    const QString body = line.mid(start + 1, end - start - 1);
    QString out;
    out.reserve(body.size());
    for (qsizetype i = 0; i < body.size(); ++i) {
        const QChar c = body.at(i);
        if (c == QLatin1Char('\\') && i + 1 < body.size()) {
            const QChar next = body.at(++i);
            switch (next.unicode()) {
            case 'n':
                out += QLatin1Char('\n');
                break;
            case 't':
                out += QLatin1Char('\t');
                break;
            case 'r':
                out += QLatin1Char('\r');
                break;
            default:
                out += next; // covers \" and \\ as well as anything unexpected
                break;
            }
        } else {
            out += c;
        }
    }
    return out;
}

/// Every rendered `msgstr` a shipped `.po` file carries — including each
/// plural form (`msgstr[0]`, `msgstr[1]`, …) — with the header entry (the
/// empty-msgid, no-msgctxt record `msgid ""` opens every catalog with)
/// excluded, since that is metadata (Project-Id-Version, …), not a
/// translated UI string.
QStringList catalogRenderedStrings(const QString& poPath)
{
    QFile file(poPath);
    const bool opened = file.open(QIODevice::ReadOnly | QIODevice::Text);
    EXPECT_TRUE(opened) << "cannot open catalog: " << qPrintable(poPath);
    if (!opened) {
        return {};
    }

    QStringList out;
    bool haveMsgid = false;
    bool haveMsgctxt = false;
    QString msgid;
    QStringList msgstrs;
    PoField field = PoField::None;

    auto flush = [&]() {
        const bool isHeader = haveMsgid && msgid.isEmpty() && !haveMsgctxt;
        if (haveMsgid && !isHeader) {
            for (const QString& text : std::as_const(msgstrs)) {
                if (!text.isEmpty()) {
                    out << text;
                }
            }
        }
        haveMsgid = false;
        haveMsgctxt = false;
        msgid.clear();
        msgstrs.clear();
        field = PoField::None;
    };

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty()) {
            flush();
            continue;
        }
        if (line.startsWith(QLatin1Char('#'))) {
            continue; // comments, including `#~` obsolete entries
        }
        if (line.startsWith(QLatin1String("msgctxt "))) {
            haveMsgctxt = true;
            field = PoField::Msgctxt;
            continue;
        }
        if (line.startsWith(QLatin1String("msgid_plural "))) {
            field = PoField::MsgidPlural;
            continue;
        }
        if (line.startsWith(QLatin1String("msgid "))) {
            haveMsgid = true;
            msgid = unquotePoLine(line);
            field = PoField::Msgid;
            continue;
        }
        if (line.startsWith(QLatin1String("msgstr[")) || line.startsWith(QLatin1String("msgstr "))) {
            msgstrs << unquotePoLine(line);
            field = PoField::Msgstr;
            continue;
        }
        if (line.startsWith(QLatin1Char('"'))) {
            const QString cont = unquotePoLine(line);
            if (field == PoField::Msgid) {
                msgid += cont;
            } else if (field == PoField::Msgstr && !msgstrs.isEmpty()) {
                msgstrs.last() += cont;
            }
            continue;
        }
    }
    flush();
    return out;
}

/// True when @p text carries a Cyrillic code point (U+0400-U+04FF) — the
/// whole Cyrillic Unicode block, a superset of what Serbian Ćirilica actually
/// uses. A `sr@latin` string never legitimately needs one: this is the
/// generic, single mechanism that proves a transliteration happened, without
/// naming what any individual entry says.
bool containsCyrillic(const QString& text)
{
    for (const QChar& ch : text) {
        const ushort u = ch.unicode();
        if (u >= 0x0400 && u <= 0x04FF) {
            return true;
        }
    }
    return false;
}

} // namespace

// Reads both shipped sr@latin catalogs directly — the ACTUAL files this
// build ships, not a copy or a subset reachable through some app-level key
// table — and asserts none of their rendered strings regressed to Cyrillic.
// Covers every entry in both files, so a mistranslation anywhere in the
// catalog fails here even if no test happens to exercise that entry's call
// site.
TEST(LatinCatalog, NoEntryRendersInCyrillic)
{
    const QString root = QStringLiteral(LIBREKDE_SOURCE_DIR);
    const QStringList catalogs{
        root + QStringLiteral("/po/sr@latin/librekde.po"),
        root + QStringLiteral("/po/sr@latin/plasma_applet_org.librescrs.smartcard.po"),
    };

    int checked = 0;
    for (const QString& path : catalogs) {
        const QStringList strings = catalogRenderedStrings(path);
        for (const QString& text : strings) {
            ++checked;
            EXPECT_FALSE(containsCyrillic(text))
                << "Cyrillic survived transliteration in " << qPrintable(path) << ": " << qPrintable(text);
        }
    }
    // A parser bug that silently found zero strings would make every
    // EXPECT_FALSE above vacuously true. Pin a floor well under the current
    // count (310 across both files) so an empty or truncated scan fails
    // loudly instead of passing by accident.
    EXPECT_GT(checked, 300) << "expected the scan to cover both catalogs' entries; only found " << checked;
}

// The four keys IdentityLabelTest's CardTypeAndAddressDateResolveToSerbian
// pins for Cyrillic, resolved here under "sr@latin" instead of "sr". Each
// assertion names the exact transliterated string, not just its absence of
// Cyrillic — so a Latin file that dropped an entry back to English, or to
// the wrong translation, fails exactly as loudly as one that stayed
// Cyrillic.
TEST(IdentityLabelLatin, KnownKeysResolveToLatinTransliteration)
{
    KLocalizedString::setLanguages({QStringLiteral("sr@latin")});

    const QString surname = localizedFieldLabel(
        makeRow(QStringLiteral("field.surname"), QStringLiteral("Surname"), QStringLiteral("surname")));
    EXPECT_EQ(surname, QStringLiteral("Prezime"));
    EXPECT_NE(surname, QString::fromUtf8("Презиме")) << "still the Cyrillic catalog's answer";

    const QString num = localizedFieldLabel(makeRow(
        QStringLiteral("field.personal_number"), QStringLiteral("Personal Number"), QStringLiteral("personal_number")));
    EXPECT_EQ(num, QStringLiteral("Lični broj"));
    EXPECT_NE(num, QString::fromUtf8("Лични број")) << "still the Cyrillic catalog's answer";

    const QString cardType = localizedFieldLabel(
        makeRow(QStringLiteral("field.card_type"), QStringLiteral("Card Type"), QStringLiteral("card_type")));
    EXPECT_EQ(cardType, QStringLiteral("Tip kartice"));
    EXPECT_NE(cardType, QString::fromUtf8("Тип картице")) << "still the Cyrillic catalog's answer";

    const QString addressDate = localizedFieldLabel(
        makeRow(QStringLiteral("field.address_date"), QStringLiteral("Address Date"), QStringLiteral("address_date")));
    EXPECT_EQ(addressDate, QStringLiteral("Datum promene adrese"));
    EXPECT_NE(addressDate, QString::fromUtf8("Датум промене адресе")) << "still the Cyrillic catalog's answer";

    KLocalizedString::clearLanguages();
}

// Group headings go through a SEPARATE table from field labels
// (groupLabelTable(), not labelTable()); pinning one here proves the Latin
// catalog is wired for both, not just the table the field-label test above
// happens to touch.
TEST(IdentityGroupLabelLatin, KnownGroupsResolveToLatinTransliteration)
{
    KLocalizedString::setLanguages({QStringLiteral("sr@latin")});

    const QString personal = localizedGroupLabel(QStringLiteral("personal"));
    EXPECT_EQ(personal, QStringLiteral("Lični podaci"));
    EXPECT_NE(personal, QString::fromUtf8("Лични подаци")) << "still the Cyrillic catalog's answer";

    const QString security = localizedGroupLabel(QStringLiteral("security_status"));
    EXPECT_EQ(security, QStringLiteral("Provera podataka putne isprave"));
    EXPECT_NE(security, QString::fromUtf8("Провера података путне исправе")) << "still the Cyrillic catalog's answer";

    KLocalizedString::clearLanguages();
}

// The CSCA chain-check reason sentences are the longest strings this repo
// ships through the catalog and the ones a reader most needs to actually
// read; a third table (checkReasonTable()) from either test above.
TEST(IdentityCheckReasonLatin, ChainFailedResolvesToLatinTransliteration)
{
    KLocalizedString::setLanguages({QStringLiteral("sr@latin")});

    const QString reason = localizedCheckReason(QStringLiteral("csca.chain-failed"));
    EXPECT_EQ(reason, QStringLiteral("Potpisnik ovog dokumenta se ne povezuje ni sa jednim uvezenim CSCA "
                                     "sertifikatom. Ne oslanjajte se na ovaj dokument; proverite ga kod izdavaoca."));
    EXPECT_NE(reason, QString::fromUtf8("Потписник овог документа се не повезује ни са једним увезеним CSCA "
                                        "сертификатом. Не ослањајте се на овај документ; проверите га код издаваоца."))
        << "still the Cyrillic catalog's answer";

    KLocalizedString::clearLanguages();
}

// The sr@latin half of IdentityLabelTest's EveryImportRemedyNamesWhereToImport,
// and the one gate that can see this file lose the location clause. Neither of
// the two checks that already cover sr@latin would: the whole-catalog scan only
// asks whether a string transliterated, and CatalogReconcile compares msgid sets,
// so a Latin msgstr left behind at an older English source reconciles perfectly
// while telling its reader to import a master list and never saying where.
TEST(IdentityCheckReasonLatin, ImportRemedyStillNamesWhereToImport)
{
    const QString application = QStringLiteral("LibreCelik");
    const QString menuPath = QString::fromUtf8("→");

    // Which reasons owe a location is read off the English source, exactly as the
    // Cyrillic test reads it — never from a list of keys kept in a test file.
    QStringList askForAnImport;
    KLocalizedString::setLanguages({QStringLiteral("en")});
    for (const QString& key : mappedCheckReasonKeys()) {
        if (localizedCheckReason(key).contains(QStringLiteral("master list"))) {
            askForAnImport << key;
        }
    }
    EXPECT_EQ(askForAnImport.size(), 3);

    KLocalizedString::setLanguages({QStringLiteral("sr@latin")});
    for (const QString& key : std::as_const(askForAnImport)) {
        const QString latin = localizedCheckReason(key);
        EXPECT_TRUE(latin.contains(application))
            << qPrintable(key) << " lost the location in transliteration: " << qPrintable(latin);
        EXPECT_TRUE(latin.contains(menuPath))
            << qPrintable(key) << " lost the location in transliteration: " << qPrintable(latin);
        EXPECT_FALSE(containsCyrillic(latin)) << qPrintable(key) << ": " << qPrintable(latin);
    }
    KLocalizedString::clearLanguages();
}

// localizedFieldValue's status-token vocabulary is a fourth table
// (localizedStatusToken()'s `tokens`), keyed by the wire's UPPERCASE token
// rather than by a labelKey — the last of the resolver's four independent
// lookup tables, each pinned once here under sr@latin.
TEST(IdentityValueLatin, VerdictTokenRendersAsLatinSentence)
{
    KLocalizedString::setLanguages({QStringLiteral("sr@latin")});

    IdentityRow row;
    row.groupKey = QStringLiteral("security_status");
    row.value = QStringLiteral("PASSED");
    const QString rendered = localizedFieldValue(row);
    EXPECT_EQ(rendered, QStringLiteral("Uspešno"));
    EXPECT_NE(rendered, QString::fromUtf8("Успешно")) << "still the Cyrillic catalog's answer";

    KLocalizedString::clearLanguages();
}

int main(int argc, char** argv)
{
    // KLocalizedString catalog loading resolves paths against the running
    // QCoreApplication; without one the sr@latin catalog is never consulted.
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
