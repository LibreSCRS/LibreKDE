// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Pins the ROWS a LibreKDE surface draws for a security-verdict group, against
// both shapes a plugin may ship a check as: the joined shape (one field per
// check, key = the check id, value "STATUS (detail)") and the structured shape
// (several `check_<N>_<suffix>` fields). One readable row per check either
// way — the structured shape must not explode into one row per wire field, and
// the joined shape must keep rendering exactly as it does today while the
// producer still sends it.
//
// The wire shape itself is read by the client library
// (`LibreSCRS::AgentClient::separateSecurityChecks`), which every client that
// builds it shares; what this file measures is what a READER ends up seeing
// after this repository's own label, value and reason catalogues have had
// their say. These are claims about the screen, not about which function does
// the separating.

#include "IdentityRows.h"

#include <LibreSCRS/AgentClient/IdentityRows.h>
#include <LibreSCRS/AgentClient/Types.h>

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <ostream>

using LibreSCRS::AgentClient::Field;
using LibreSCRS::AgentClient::FieldGroup;
using LibreSCRS::AgentClient::IdentityRow;

// Readable QString diagnostics on a mismatch — see the identical note in
// ErrorTextCoverageTest.cpp. Must live at namespace scope (not inside the
// anonymous namespace below) for gtest's lookup to find it.
inline void PrintTo(const QString& value, std::ostream* os)
{
    *os << '"' << value.toStdString() << '"';
}

namespace {

/// One rendered row: the label and value a view actually draws, produced the
/// same way SmartCardHandler and AgentCardDataSource produce them — assemble
/// the group's wire fields into rows, then resolve each row's label and value
/// through the SAME host-side functions every LibreKDE surface uses.
struct RenderedRow
{
    QString label;
    QString value;
};

/// Builds one wire group from (fieldKey, labelFallback, value) triples and runs
/// it through the production assembly + render path.
QList<RenderedRow> rowsFor(const char* groupKey, std::initializer_list<std::array<const char*, 3>> fields)
{
    FieldGroup group;
    group.key = QString::fromUtf8(groupKey);
    group.fields.reserve(static_cast<qsizetype>(fields.size()));
    for (const std::array<const char*, 3>& field : fields) {
        Field wire;
        wire.key = QString::fromUtf8(field[0]);
        wire.value = QString::fromUtf8(field[2]);
        // The canonical wire-metadata key, spelled as the agent ships it; the
        // library's flatten reads the display fallback from here.
        wire.extra.insert(QStringLiteral("labelFallback"), QString::fromUtf8(field[1]));
        group.fields.append(wire);
    }

    QList<RenderedRow> rendered;
    for (const IdentityRow& row : LibreKDE::identityRows({group})) {
        rendered.append({LibreKDE::localizedFieldLabel(row), LibreKDE::localizedFieldValue(row)});
    }
    return rendered;
}

/// A verdict token localized through the SAME path production uses — never a
/// hard-coded string, so these stay correct under whatever language the test
/// process happens to run in.
QString localizedVerdict(const char* token)
{
    IdentityRow row;
    row.groupKey = QStringLiteral("security_status");
    row.value = QString::fromUtf8(token);
    return LibreKDE::localizedFieldValue(row);
}

QString localizedNotPerformed()
{
    return localizedVerdict("NOT_PERFORMED");
}

/// Every label in @p rows, so a claim about WHICH rows exist can be made
/// without also claiming what order they came in — invariant 2 of the
/// identity-render contract (shared/agentclient/IdentityRows.h).
QStringList labelsOf(const QList<RenderedRow>& rows)
{
    QStringList labels;
    for (const RenderedRow& row : rows) {
        labels << row.label;
    }
    return labels;
}

} // namespace

TEST(IdentityRowFolding, FoldsStructuredCheckFieldsIntoOneRowPerCheck)
{
    const auto rows = rowsFor("security_status", {
                                                     {"check_0_id", "", "pa_csca_chain"},
                                                     {"check_0_label", "", "CSCA Certificate Chain"},
                                                     {"check_0_status", "", "NOT_PERFORMED"},
                                                 });
    ASSERT_EQ(rows.size(), 1) << "six fields must not become six rows";
    EXPECT_EQ(rows[0].label, QStringLiteral("CSCA Certificate Chain"));
    EXPECT_EQ(rows[0].value, localizedNotPerformed());
}

TEST(IdentityRowFolding, StillRendersTheJoinedShapeUntilTheProducerMoves)
{
    const auto rows =
        rowsFor("security_status", {
                                       {"pa_csca_chain", "CSCA Certificate Chain", "NOT_PERFORMED (no store)"},
                                   });
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].label, QStringLiteral("CSCA Certificate Chain"));
    EXPECT_TRUE(rows[0].value.endsWith(QStringLiteral(" (no store)")))
        << "the old shape must keep working while the producer still sends it";
}

// An unrecognised suffix — category and error, which this build reads and has
// no vocabulary for, and any suffix a newer agent appends — must not surface as
// its own row: it would appear before any catalog exists to render it
// meaningfully. `reason` used to sit in this list; it is recognised now, and
// its own tests below say what it renders as instead.
TEST(IdentityRowFolding, UnknownSuffixIsDroppedNotRendered)
{
    const auto rows = rowsFor("security_status", {
                                                     {"check_0_id", "", "pa_csca_chain"},
                                                     {"check_0_label", "", "CSCA Certificate Chain"},
                                                     {"check_0_status", "", "NOT_PERFORMED"},
                                                     {"check_0_category", "", "passive_authentication"},
                                                     {"check_0_error", "", "raw diagnostic text"},
                                                     {"check_0_futuresuffix", "", "something a newer agent sends"},
                                                 });
    ASSERT_EQ(rows.size(), 1) << "an unrecognised suffix must not become its own row";
    EXPECT_EQ(rows[0].label, QStringLiteral("CSCA Certificate Chain"));
    EXPECT_EQ(rows[0].value, localizedNotPerformed())
        << "category/error/an unknown suffix must not leak into the rendered value either";
}

// The whole point of the reason key: the check says WHAT TO DO, and says it in
// the reader's language. Each of the five reasons is pinned by content, because
// a reason that names the condition without naming the remedy leaves the reader
// exactly where the English sentence this replaces left them.
TEST(IdentityRowFolding, EveryReasonRendersAsAnInstruction)
{
    struct Case
    {
        const char* status;
        const char* reasonKey;
        const char* english;
    };
    // Four of the five are NOT_PERFORMED. Only a chain that was really
    // attempted and really failed is FAILED — the accusation verdict — and a
    // store nobody finished configuring must never produce it.
    static const std::array<Case, 5> cases{{
        {"NOT_PERFORMED", "csca.not-configured",
         "No CSCA certificates have been imported. Import an ICAO master list so this document's signer "
         "can be checked."},
        {"NOT_PERFORMED", "csca.anchors-unreadable",
         "The CSCA trust store could not be read. Check that its directory exists and that its "
         "permissions allow reading."},
        {"NOT_PERFORMED", "csca.anchors-undecodable",
         "The CSCA trust store holds no usable certificate. Import an ICAO master list again."},
        {"NOT_PERFORMED", "csca.no-anchor-for-issuer",
         "No imported CSCA certificate belongs to this document's issuer. Import a master list that "
         "covers the issuing country."},
        {"FAILED", "csca.chain-failed",
         "This document's signer does not chain to any imported CSCA certificate. Do not rely on this "
         "document; check it with the issuing authority."},
    }};

    for (const Case& c : cases) {
        const auto rows = rowsFor("security_status", {
                                                         {"check_0_id", "", "pa_csca_chain"},
                                                         {"check_0_label", "", "CSCA Certificate Chain"},
                                                         {"check_0_status", "", c.status},
                                                         {"check_0_reason", "", c.reasonKey},
                                                     });
        ASSERT_EQ(rows.size(), 1) << c.reasonKey;
        EXPECT_TRUE(rows[0].value.endsWith(QStringLiteral(" (") + QString::fromUtf8(c.english) + QLatin1Char(')')))
            << c.reasonKey << " rendered as: " << rows[0].value.toStdString();
        EXPECT_FALSE(rows[0].value.contains(QStringLiteral("csca."))) << "the raw key must not reach a reader";
    }
}

// A newer agent may ship a reason key this build has never heard of. It must
// DEGRADE — the same three-step the field labels already take (catalog hit, else
// the producer's own text, else the raw key) — never blank the row and never
// print the word "unknown".
TEST(IdentityRowFolding, UnknownReasonKeyDegradesToTheProducersDetail)
{
    const auto rows = rowsFor("security_status", {
                                                     {"check_0_id", "", "pa_csca_chain"},
                                                     {"check_0_label", "", "CSCA Certificate Chain"},
                                                     {"check_0_status", "", "NOT_PERFORMED"},
                                                     {"check_0_reason", "", "csca.something-invented-later"},
                                                     {"check_0_detail", "", "a sentence the newer agent wrote"},
                                                 });
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].value, localizedNotPerformed() + QStringLiteral(" (a sentence the newer agent wrote)"));
}

TEST(IdentityRowFolding, UnknownReasonKeyWithNoDetailShowsTheKeyRatherThanNothing)
{
    const auto rows = rowsFor("security_status", {
                                                     {"check_0_id", "", "pa_csca_chain"},
                                                     {"check_0_label", "", "CSCA Certificate Chain"},
                                                     {"check_0_status", "", "NOT_PERFORMED"},
                                                     {"check_0_reason", "", "csca.something-invented-later"},
                                                 });
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].value, localizedNotPerformed() + QStringLiteral(" (csca.something-invented-later)"))
        << "erasing the row would tell the reader nothing at all";
}

// A recognised reason and a detail together: the reason wins. Two parentheticals
// on one row is noise, and the reason is the half a catalog can translate — the
// producer that sends a reason has already replaced its English sentence.
TEST(IdentityRowFolding, ARecognisedReasonSupersedesTheProducersDetail)
{
    const auto rows = rowsFor("security_status", {
                                                     {"check_0_id", "", "pa_csca_chain"},
                                                     {"check_0_label", "", "CSCA Certificate Chain"},
                                                     {"check_0_status", "", "NOT_PERFORMED"},
                                                     {"check_0_reason", "", "csca.not-configured"},
                                                     {"check_0_detail", "", "No CSCA trust store configured"},
                                                 });
    ASSERT_EQ(rows.size(), 1);
    EXPECT_FALSE(rows[0].value.contains(QStringLiteral("No CSCA trust store configured")))
        << "the English sentence the key replaces must not ride along beside it";
    EXPECT_EQ(rows[0].value.count(QLatin1Char('(')), 1);
}

// check_N_detail is appended only when present — a check with no detail must
// not render a trailing "()".
TEST(IdentityRowFolding, DetailIsOmittedWhenAbsent)
{
    const auto rows = rowsFor("security_status", {
                                                     {"check_1_id", "", "chip_auth"},
                                                     {"check_1_label", "", "Chip Authentication"},
                                                     {"check_1_status", "", "PASSED"},
                                                 });
    ASSERT_EQ(rows.size(), 1);
    EXPECT_FALSE(rows[0].value.contains(QLatin1Char('(')));
}

// A group outside the verdict set is never folded, even if a field there
// happens to be keyed like a structured check — the scoping is on the GROUP,
// not merely on the key shape.
TEST(IdentityRowFolding, NonVerdictGroupsAreNeverFolded)
{
    const auto rows = rowsFor("personal", {
                                              {"check_0_id", "Some Field", "some_value"},
                                          });
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].label, QStringLiteral("Some Field"));
    EXPECT_EQ(rows[0].value, QStringLiteral("some_value"));
}

// Checks are ordered by their ordinal read as a NUMBER, so the eleventh check
// follows the third. Identity crosses the wire as map-of-maps and so arrives
// sorted by key TEXT, where "check_10_" sits between "check_1_" and "check_2_";
// an assembly that took each check's position from where its first field landed
// wedged the eleventh check into the middle of the list. This is the one row
// ORDER claim in this file, and it is the client library's own documented
// promise rather than a shape this repository chose.
TEST(IdentityRowFolding, ChecksAreOrderedByNumericOrdinalNotByKeyText)
{
    const auto rows = rowsFor("security_status", {
                                                     // Arrival order: key-sorted, exactly as the wire delivers it.
                                                     {"check_10_id", "", "eleventh"},
                                                     {"check_10_label", "", "Eleventh Check"},
                                                     {"check_10_status", "", "PASSED"},
                                                     {"check_2_id", "", "third"},
                                                     {"check_2_label", "", "Third Check"},
                                                     {"check_2_status", "", "PASSED"},
                                                 });
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].label, QStringLiteral("Third Check"));
    EXPECT_EQ(rows[1].label, QStringLiteral("Eleventh Check"));
}

// The group's aggregate roll-ups are neither swallowed by the check separation
// nor mistaken for a check of their own: they keep their row, and their value
// still goes through the verdict vocabulary. Asserted as a SET, because no view
// may depend on the order these arrive in.
TEST(IdentityRowFolding, AggregateVerdictsSurviveBesideTheChecks)
{
    const auto rows = rowsFor("security_status", {
                                                     {"check_0_id", "", "pa_csca_chain"},
                                                     {"check_0_label", "", "CSCA Certificate Chain"},
                                                     {"check_0_status", "", "NOT_PERFORMED"},
                                                     {"overall_integrity", "Data Integrity", "PASSED"},
                                                 });
    ASSERT_EQ(rows.size(), 2) << "the roll-up must not be swallowed by the check separation";
    EXPECT_EQ(labelsOf(rows).count(QStringLiteral("CSCA Certificate Chain")), 1);
    EXPECT_EQ(labelsOf(rows).count(QStringLiteral("Data Integrity")), 1);
    for (const RenderedRow& row : rows) {
        if (row.label == QStringLiteral("Data Integrity")) {
            EXPECT_EQ(row.value, localizedVerdict("PASSED"));
        }
    }
}
