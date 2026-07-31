// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "IdentityRows.h"

#include <KLocalizedString>

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

        // --- Document -------------------------------------------------------
        {QStringLiteral("field.document_number"),
         ki18ndc("librekde", "@item:intable identity field", "Document Number")},
        {QStringLiteral("field.document_code"), ki18ndc("librekde", "@item:intable identity field", "Document Code")},
        {QStringLiteral("field.document_type"), ki18ndc("librekde", "@item:intable identity field", "Document Type")},
        {QStringLiteral("field.document_serial_number"),
         ki18ndc("librekde", "@item:intable identity field", "Serial Number")},
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

        // --- PKCS#15 / OpenSC token info ------------------------------------
        {QStringLiteral("field.label"), ki18ndc("librekde", "@item:intable identity field (token label)", "Label")},
        {QStringLiteral("field.serial_number"), ki18ndc("librekde", "@item:intable identity field", "Serial Number")},
        {QStringLiteral("field.manufacturer"), ki18ndc("librekde", "@item:intable identity field", "Manufacturer")},
    };
    return *table;
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

} // namespace LibreKDE
