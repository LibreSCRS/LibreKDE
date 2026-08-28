// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "IdentityRows.h"

#include <KLocalizedString>

#include <QDate>
#include <QHash>

namespace LibreKDE {

namespace {

// Frozen labelKey -> translatable label, in the `librekde` gettext domain.
//
// The msgid is a compile-time English literal so Messages.sh (xgettext C++
// pass over shared/) extracts it into librekde.pot and English/untranslated
// locales render it verbatim. The KLocalizedString is NOT resolved here — it is
// resolved (toString) per lookup, so it honours the caller's current language
// (plasmoid process OR the card:/ KIO worker, both loading the `librekde`
// catalog by explicit domain, exactly as ErrorText.cpp already does).
//
// Keyed on the FROZEN labelKey the agent ships (LmSeams.cpp: "field." + key),
// NOT the plugin's English label, so a plugin re-wording its fallback never
// silently drops a translation. Coverage: the personal-identity, address,
// document, health-insurance and token fields of the eID / eMRTD / health /
// PKCS#15 identity plugins. Vehicle-registration (eu-vrc) fields carry
// EU-standard letter codes in their labels ("A: …", "D.1: …") and intentionally
// stay on their English fallback; dynamic keys (eMRTD DG16 contacts, per-DG
// security checks, PKCS#15 cert_<label>) likewise fall through to the fallback.
const QHash<QString, KLocalizedString>& labelTable()
{
    static const auto* const table = new QHash<QString, KLocalizedString>{
        // --- Names / person -------------------------------------------------
        {QStringLiteral("field.full_name"), ki18ndc("librekde", "@item:intable identity field", "Full Name")},
        {QStringLiteral("field.surname"), ki18ndc("librekde", "@item:intable identity field", "Surname")},
        {QStringLiteral("field.family_name"), ki18ndc("librekde", "@item:intable identity field", "Family Name")},
        {QStringLiteral("field.family_name_latin"),
         ki18ndc("librekde", "@item:intable identity field", "Family Name (Latin)")},
        {QStringLiteral("field.given_name"), ki18ndc("librekde", "@item:intable identity field", "Given Name")},
        {QStringLiteral("field.given_names"), ki18ndc("librekde", "@item:intable identity field", "Given Names")},
        {QStringLiteral("field.given_name_latin"),
         ki18ndc("librekde", "@item:intable identity field", "Given Name (Latin)")},
        {QStringLiteral("field.other_names"), ki18ndc("librekde", "@item:intable identity field", "Other Names")},
        {QStringLiteral("field.parent_given_name"),
         ki18ndc("librekde", "@item:intable identity field", "Parent Given Name")},
        {QStringLiteral("field.parent_name"), ki18ndc("librekde", "@item:intable identity field", "Parent Name")},
        {QStringLiteral("field.parent_name_latin"),
         ki18ndc("librekde", "@item:intable identity field", "Parent Name (Latin)")},
        {QStringLiteral("field.sex"), ki18ndc("librekde", "@item:intable identity field", "Sex")},
        {QStringLiteral("field.gender"), ki18ndc("librekde", "@item:intable identity field", "Gender")},
        {QStringLiteral("field.title"), ki18ndc("librekde", "@item:intable identity field", "Title")},
        {QStringLiteral("field.profession"), ki18ndc("librekde", "@item:intable identity field", "Profession")},
        {QStringLiteral("field.nationality"), ki18ndc("librekde", "@item:intable identity field", "Nationality")},
        {QStringLiteral("field.status_of_foreigner"),
         ki18ndc("librekde", "@item:intable identity field", "Status of Foreigner")},

        // --- Birth ----------------------------------------------------------
        {QStringLiteral("field.date_of_birth"), ki18ndc("librekde", "@item:intable identity field", "Date of Birth")},
        {QStringLiteral("field.place_of_birth"), ki18ndc("librekde", "@item:intable identity field", "Place of Birth")},
        {QStringLiteral("field.community_of_birth"),
         ki18ndc("librekde", "@item:intable identity field", "Community of Birth")},
        {QStringLiteral("field.state_of_birth"), ki18ndc("librekde", "@item:intable identity field", "State of Birth")},

        // --- Identifiers ----------------------------------------------------
        {QStringLiteral("field.personal_number"),
         ki18ndc("librekde", "@item:intable identity field", "Personal Number")},
        {QStringLiteral("field.insurant_number"),
         ki18ndc("librekde", "@item:intable identity field (health insurant number)", "Insurant Number")},

        // --- Address --------------------------------------------------------
        {QStringLiteral("field.address"), ki18ndc("librekde", "@item:intable identity field", "Address")},
        {QStringLiteral("field.state"), ki18ndc("librekde", "@item:intable identity field (address)", "State")},
        {QStringLiteral("field.country"), ki18ndc("librekde", "@item:intable identity field", "Country")},
        {QStringLiteral("field.community"), ki18ndc("librekde", "@item:intable identity field (address)", "Community")},
        {QStringLiteral("field.municipality"), ki18ndc("librekde", "@item:intable identity field", "Municipality")},
        {QStringLiteral("field.place"), ki18ndc("librekde", "@item:intable identity field (address)", "Place")},
        {QStringLiteral("field.street"), ki18ndc("librekde", "@item:intable identity field", "Street")},
        {QStringLiteral("field.house_number"), ki18ndc("librekde", "@item:intable identity field", "House Number")},
        {QStringLiteral("field.house_letter"), ki18ndc("librekde", "@item:intable identity field", "House Letter")},
        {QStringLiteral("field.address_number"),
         ki18ndc("librekde", "@item:intable identity field (house number)", "Number")},
        {QStringLiteral("field.entrance"), ki18ndc("librekde", "@item:intable identity field", "Entrance")},
        {QStringLiteral("field.floor"), ki18ndc("librekde", "@item:intable identity field", "Floor")},
        {QStringLiteral("field.apartment"), ki18ndc("librekde", "@item:intable identity field", "Apartment")},
        {QStringLiteral("field.apartment_number"),
         ki18ndc("librekde", "@item:intable identity field", "Apartment Number")},
        {QStringLiteral("field.telephone"), ki18ndc("librekde", "@item:intable identity field", "Telephone")},
        {QStringLiteral("field.address_date"),
         ki18ndc("librekde", "@item:intable identity field (when the address last changed)", "Address Date")},
        {QStringLiteral("field.address_label"),
         ki18ndc("librekde", "@item:intable identity field (the address as one line)", "Address")},

        // --- Document -------------------------------------------------------
        {QStringLiteral("field.document_number"),
         ki18ndc("librekde", "@item:intable identity field", "Document Number")},
        {QStringLiteral("field.document_code"), ki18ndc("librekde", "@item:intable identity field", "Document Code")},
        {QStringLiteral("field.document_type"), ki18ndc("librekde", "@item:intable identity field", "Document Type")},
        {QStringLiteral("field.document_serial_number"),
         ki18ndc("librekde", "@item:intable identity field", "Serial Number")},
        // NOT an alias of document_serial_number, though the two look alike.
        // That key is the eID plugin's and is one of the curated SUMMARY keys,
        // so folding this one into it would promote an annex row into the
        // popup's headline; and the two carry different labels besides.
        {QStringLiteral("field.document_serial"),
         ki18ndc("librekde", "@item:intable identity field (annex document number)", "Document Number")},
        {QStringLiteral("field.doc_reg_no"),
         ki18ndc("librekde", "@item:intable identity field", "Registration Number")},
        {QStringLiteral("field.issuing_state"), ki18ndc("librekde", "@item:intable identity field", "Issuing State")},
        {QStringLiteral("field.issuing_authority"),
         ki18ndc("librekde", "@item:intable identity field", "Issuing Authority")},
        {QStringLiteral("field.issuing_date"), ki18ndc("librekde", "@item:intable identity field", "Issuing Date")},
        {QStringLiteral("field.date_of_issue"), ki18ndc("librekde", "@item:intable identity field", "Date of Issue")},
        {QStringLiteral("field.date_of_expiry"), ki18ndc("librekde", "@item:intable identity field", "Date of Expiry")},
        {QStringLiteral("field.expiry_date"), ki18ndc("librekde", "@item:intable identity field", "Expiry Date")},
        {QStringLiteral("field.valid_until"), ki18ndc("librekde", "@item:intable identity field", "Valid Until")},
        {QStringLiteral("field.endorsements"), ki18ndc("librekde", "@item:intable identity field", "Endorsements")},
        {QStringLiteral("field.custody_info"),
         ki18ndc("librekde", "@item:intable identity field", "Custody Information")},

        // --- Health insurance ----------------------------------------------
        {QStringLiteral("field.insurer_name"), ki18ndc("librekde", "@item:intable identity field", "Insurer")},
        {QStringLiteral("field.insurer_id"), ki18ndc("librekde", "@item:intable identity field", "Insurer ID")},
        {QStringLiteral("field.card_id"), ki18ndc("librekde", "@item:intable identity field", "Card ID")},
        {QStringLiteral("field.insurance_basis_rzzo"),
         ki18ndc("librekde", "@item:intable identity field (insurance basis)", "Basis")},
        {QStringLiteral("field.insurance_description"),
         ki18ndc("librekde", "@item:intable identity field", "Description")},
        {QStringLiteral("field.insurance_start_date"),
         ki18ndc("librekde", "@item:intable identity field", "Start Date")},

        // --- eMRTD presence / security summary ------------------------------
        {QStringLiteral("field.data_groups"), ki18ndc("librekde", "@item:intable identity field", "Data Groups")},
        {QStringLiteral("field.auth_method"),
         ki18ndc("librekde", "@item:intable identity field", "Authentication Method")},
        {QStringLiteral("field.overall_integrity"),
         ki18ndc("librekde", "@item:intable identity field", "Data Integrity")},
        {QStringLiteral("field.overall_authenticity"),
         ki18ndc("librekde", "@item:intable identity field", "Data Authenticity")},
        {QStringLiteral("field.overall_genuineness"),
         ki18ndc("librekde", "@item:intable identity field", "Chip Genuineness")},

        // --- annex verification ---------------------------------------------
        // Deliberately NOT added to isHiddenIdentityRow, unlike the three
        // *_verification rows. Those are raw traces of a machine check; these
        // two are the only statement of how far the annex's guarantee reaches,
        // and hiding them would show personal detail with nothing said about
        // what was actually proven.
        {QStringLiteral("field.annex_integrity"),
         ki18ndc("librekde", "@item:intable identity field (annex)", "Data Integrity")},
        {QStringLiteral("field.annex_authenticity"),
         ki18ndc("librekde", "@item:intable identity field (annex)", "Data Authenticity")},

        // --- Card metadata --------------------------------------------------
        {QStringLiteral("field.card_type"),
         ki18ndc("librekde", "@item:intable identity field (card generation)", "Card Type")},

        // --- PKCS#15 / OpenSC token info ------------------------------------
        {QStringLiteral("field.label"), ki18ndc("librekde", "@item:intable identity field (token label)", "Label")},
        {QStringLiteral("field.serial_number"), ki18ndc("librekde", "@item:intable identity field", "Serial Number")},
        {QStringLiteral("field.manufacturer"), ki18ndc("librekde", "@item:intable identity field", "Manufacturer")},
    };
    return *table;
}

// Frozen groupKey -> translatable heading, same domain and same resolution
// timing as labelTable(). Only groups a heading actually helps: the summary
// above the expander is a curated cross-group extract and carries none.
const QHash<QString, KLocalizedString>& groupLabelTable()
{
    static const auto* const table = new QHash<QString, KLocalizedString>{
        {QStringLiteral("personal"), ki18ndc("librekde", "@title:group identity fields", "Personal Data")},
        {QStringLiteral("document"), ki18ndc("librekde", "@title:group identity fields", "Document")},
        {QStringLiteral("document_extra"), ki18ndc("librekde", "@title:group identity fields", "Issuing Information")},
        {QStringLiteral("additional"), ki18ndc("librekde", "@title:group identity fields", "Additional Data")},
        {QStringLiteral("national"), ki18ndc("librekde", "@title:group identity fields", "National Data")},
        {QStringLiteral("contacts"), ki18ndc("librekde", "@title:group identity fields", "Contacts")},
        {QStringLiteral("security_status"),
         ki18ndc("librekde", "@title:group identity fields", "Travel Document Verification")},
        // The desktop client titles the same wire group "Authentication";
        // one vocabulary, one heading.
        {QStringLiteral("presence"), ki18ndc("librekde", "@title:group identity fields", "Authentication")},
    };
    return *table;
}

/// Headings for the two annex groups, matched on prefix rather than on a full
/// key: the id between `annex.` and the suffix comes from the reader, and this
/// issuer has already moved its applet identifier once.
QString annexGroupHeading(const QString& groupKey)
{
    if (!groupKey.startsWith(QLatin1String("annex."))) {
        return {};
    }
    if (groupKey.endsWith(QLatin1String(".personal"))) {
        // NOT "Additional Data": the eMRTD DG11 group already carries that
        // heading, and an identity card ships BOTH — so the popup showed two
        // identically named blocks and a reader could not tell which fields
        // came from the passport data groups and which from the signed annex.
        return ki18ndc("librekde", "@title:group identity fields", "Additional Personal Data").toString();
    }
    if (groupKey.endsWith(QLatin1String(".security"))) {
        return ki18ndc("librekde", "@title:group identity fields", "Additional Data Verification").toString();
    }
    return {};
}

} // namespace

QString localizedFieldLabel(const LibreSCRS::AgentClient::IdentityRow& row)
{
    if (const auto it = labelTable().constFind(row.labelKey); it != labelTable().constEnd()) {
        return it->toString();
    }
    if (!row.labelFallback.isEmpty()) {
        return row.labelFallback;
    }
    return row.fieldKey;
}

bool isHiddenIdentityRow(const LibreSCRS::AgentClient::IdentityRow& row)
{
    return row.labelKey == QLatin1String("field.card_verification") ||
           row.labelKey == QLatin1String("field.fixed_verification") ||
           row.labelKey == QLatin1String("field.variable_verification");
}

namespace {

/// Groups whose VALUES are the wire's closed status vocabulary rather than card
/// data. Scoped deliberately: a personal field whose value happened to read
/// "PASSED" must not be rewritten into a verdict.
bool isVerdictGroup(const QString& groupKey)
{
    return groupKey == QLatin1String("security_status") ||
           (groupKey.startsWith(QLatin1String("annex.")) && groupKey.endsWith(QLatin1String(".security")));
}

/// The status token at the head of @p value, localized; empty when @p value does
/// not start with one this build names.
///
/// A verdict may carry a parenthetical the plugin authored — "NOT_PERFORMED (No
/// CSCA trust store configured)". The token is translated and the remainder is
/// kept verbatim: it is the only specific record of WHY, and no catalog can
/// hold it.
QString localizedStatusToken(const QString& value)
{
    static const QHash<QString, KLocalizedString> tokens{
        {QStringLiteral("PASSED"), ki18ndc("librekde", "@item:intable security check outcome", "Passed")},
        {QStringLiteral("FAILED"), ki18ndc("librekde", "@item:intable security check outcome", "Failed")},
        {QStringLiteral("NOT_PERFORMED"), ki18ndc("librekde", "@item:intable security check outcome", "Not performed")},
        {QStringLiteral("NOT_SUPPORTED"), ki18ndc("librekde", "@item:intable security check outcome", "Not supported")},
        {QStringLiteral("SKIPPED"), ki18ndc("librekde", "@item:intable security check outcome", "Skipped")},
    };
    const qsizetype split = value.indexOf(u' ');
    const QString head = split < 0 ? value : value.left(split);
    const auto it = tokens.constFind(head);
    if (it == tokens.constEnd()) {
        // Wire-frozen append-only: a token this build has never heard of passes
        // through verbatim rather than becoming "unknown", which would erase a
        // verdict a newer agent is reporting correctly.
        return {};
    }
    return split < 0 ? it->toString() : it->toString() + value.mid(split);
}

/// The access-control methods the eMRTD plugin names in the presence group,
/// localized by KEY scope (field.auth_method) — never by widening the
/// verdict-group dictionary, which would drag data_groups (a machine list
/// riding the same group) along with it. The protocol names stay themselves
/// in Serbian; the entries exist so a language CAN adapt them. Empty when the
/// value is not one this build names: append-only wire, verbatim passthrough.
QString localizedAuthMethod(const QString& value)
{
    static const QHash<QString, KLocalizedString> methods{
        {QStringLiteral("BAC"), ki18ndc("librekde", "@item:intable eMRTD access-control method", "BAC")},
        {QStringLiteral("PACE (CAN)"), ki18ndc("librekde", "@item:intable eMRTD access-control method", "PACE (CAN)")},
        {QStringLiteral("PACE (MRZ)"), ki18ndc("librekde", "@item:intable eMRTD access-control method", "PACE (MRZ)")},
        {QStringLiteral("Chip Authentication"),
         ki18ndc("librekde", "@item:intable eMRTD access-control method", "Chip Authentication")},
        {QStringLiteral("None (plain read)"),
         ki18ndc("librekde", "@item:intable eMRTD access-control method", "None (plain read)")},
    };
    const auto it = methods.constFind(value);
    return it == methods.constEnd() ? QString() : it->toString();
}

} // namespace

QString localizedFieldValue(const LibreSCRS::AgentClient::IdentityRow& row)
{
    if (isVerdictGroup(row.groupKey)) {
        if (const QString named = localizedStatusToken(row.value); !named.isEmpty()) {
            return named;
        }
    }

    // The address-change date reaches display in one of two shapes: the eMRTD
    // plugins render a real date as dd.MM.yyyy, while the annex reader ships the
    // card's raw ddMMyyyy digits (e.g. "06082016") untouched — the middleware
    // never reformats signed card bytes, so the presentation layer is where the
    // date becomes readable. Accept the formatted shape as-is; normalise the
    // raw shape to dd.MM.yyyy; treat anything that parses as neither (an
    // impossible day/month, or a placeholder like "00001") as the card's
    // no-date marker. Parsing rather than pattern-matching rejects impossible
    // dates.
    // The auth_method value is English prose from a closed five-token set;
    // key-scoped so data_groups in the same group stays the machine list it is.
    if (row.labelKey == QLatin1String("field.auth_method") && !row.value.isEmpty()) {
        if (const QString named = localizedAuthMethod(row.value); !named.isEmpty()) {
            return named;
        }
        return row.value;
    }

    if (row.labelKey == QLatin1String("field.address_date") && !row.value.isEmpty()) {
        if (QDate::fromString(row.value, QStringLiteral("dd.MM.yyyy")).isValid()) {
            return row.value;
        }
        if (const QDate d = QDate::fromString(row.value, QStringLiteral("ddMMyyyy")); d.isValid()) {
            return d.toString(QStringLiteral("dd.MM.yyyy"));
        }
        return ki18ndc("librekde", "@item:intable identity field value (card carries no date)", "Unknown").toString();
    }
    return row.value;
}

QString localizedGroupLabel(const QString& groupKey)
{
    if (const QString annex = annexGroupHeading(groupKey); !annex.isEmpty()) {
        return annex;
    }
    if (const auto it = groupLabelTable().constFind(groupKey); it != groupLabelTable().constEnd()) {
        return it->toString();
    }
    // Empty means "no heading". Inventing one from the raw key would put a
    // machine identifier on screen as if it were a title.
    return {};
}

QStringList mappedGroupKeys()
{
    QStringList keys = groupLabelTable().keys();
    // The annex pair is prefix-matched rather than tabulated; name the two
    // canonical spellings so the pin covers them too.
    keys << QStringLiteral("annex.<id>.personal") << QStringLiteral("annex.<id>.security");
    return keys;
}

QStringList fieldOrderForGroup(const QString& groupKey)
{
    // The annex's substance is an address, and the wire delivers it sorted by
    // key. Same order the desktop client reads it in — byte-identical twin in
    // LibreCelik, plugins/emrtd/emrtdwidget.cpp (annexFieldOrder()); each
    // repository pins its copy with a test, change both together.
    if (groupKey.startsWith(QLatin1String("annex.")) && groupKey.endsWith(QLatin1String(".personal"))) {
        return {
            QStringLiteral("address_label"),     QStringLiteral("street"),
            QStringLiteral("house_number"),      QStringLiteral("house_letter"),
            QStringLiteral("entrance"),          QStringLiteral("floor"),
            QStringLiteral("apartment_number"),  QStringLiteral("place"),
            QStringLiteral("community"),         QStringLiteral("state"),
            QStringLiteral("parent_given_name"), QStringLiteral("community_of_birth"),
            QStringLiteral("state_of_birth"),    QStringLiteral("document_serial"),
            QStringLiteral("address_date"),
        };
    }
    // The annex's verdict pair reads integrity-then-authenticity, matching the
    // desktop client's pane; the key-sorted wire would put authenticity first.
    // Fields outside the pair keep delivery order, after the pinned two.
    if (groupKey.startsWith(QLatin1String("annex.")) && groupKey.endsWith(QLatin1String(".security"))) {
        return {QStringLiteral("annex_integrity"), QStringLiteral("annex_authenticity")};
    }
    return {};
}

QStringList mappedLabelKeys()
{
    return labelTable().keys();
}

} // namespace LibreKDE
