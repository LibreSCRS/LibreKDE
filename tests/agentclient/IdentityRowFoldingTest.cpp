// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Pins `foldSecurityCheckFields` against BOTH shapes a plugin may ship a
// security check as: the joined shape (one field per check, key = the check
// id, value "STATUS (detail)") and the structured shape (several
// `check_<N>_<suffix>` fields). The client must render one readable row per
// check either way — the structured shape must not explode into one row per
// wire field, and the joined shape must keep rendering exactly as it does
// today while the producer still sends it.

#include "IdentityRows.h"

#include <LibreSCRS/AgentClient/IdentityRows.h>

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <ostream>

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
/// same way SmartCardHandler and AgentCardDataSource produce them — fold the
/// group's raw wire rows, then resolve each survivor's label and value
/// through the SAME host-side functions every LibreKDE surface uses.
struct RenderedRow
{
    QString label;
    QString value;
};

/// Builds one group's raw wire rows from (fieldKey, labelFallback, value)
/// triples and runs them through the production fold + render path.
QList<RenderedRow> rowsFor(const char* groupKey, std::initializer_list<std::array<const char*, 3>> fields)
{
    QList<IdentityRow> raw;
    raw.reserve(static_cast<qsizetype>(fields.size()));
    for (const std::array<const char*, 3>& field : fields) {
        IdentityRow row;
        row.groupKey = QString::fromUtf8(groupKey);
        row.fieldKey = QString::fromUtf8(field[0]);
        row.labelFallback = QString::fromUtf8(field[1]);
        row.value = QString::fromUtf8(field[2]);
        raw.append(row);
    }

    QList<RenderedRow> rendered;
    for (const IdentityRow& row : LibreKDE::foldSecurityCheckFields(raw)) {
        rendered.append({LibreKDE::localizedFieldLabel(row), LibreKDE::localizedFieldValue(row)});
    }
    return rendered;
}

/// The localized "NOT_PERFORMED" verdict token, resolved through the SAME
/// path production uses — never a hard-coded string, so this stays correct
/// under whatever language the test process happens to run in.
QString localizedNotPerformed()
{
    IdentityRow row;
    row.groupKey = QStringLiteral("security_status");
    row.value = QStringLiteral("NOT_PERFORMED");
    return LibreKDE::localizedFieldValue(row);
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
    EXPECT_TRUE(rows[0].value.endsWith(QStringLiteral(" (no store)")))
        << "the old shape must keep working while the producer still sends it";
}

// An unrecognised suffix (category, error, and reason before a later change
// teaches this file its vocabulary) must not surface as its own row — it
// would appear before any catalog exists to render it meaningfully.
TEST(IdentityRowFolding, UnknownSuffixIsDroppedNotRendered)
{
    const auto rows = rowsFor("security_status", {
                                                     {"check_0_id", "", "pa_csca_chain"},
                                                     {"check_0_label", "", "CSCA Certificate Chain"},
                                                     {"check_0_status", "", "NOT_PERFORMED"},
                                                     {"check_0_category", "", "passive_authentication"},
                                                     {"check_0_error", "", "raw diagnostic text"},
                                                     {"check_0_reason", "", "csca.not-configured"},
                                                 });
    ASSERT_EQ(rows.size(), 1) << "an unrecognised suffix must not become its own row";
    EXPECT_EQ(rows[0].label, QStringLiteral("CSCA Certificate Chain"));
    EXPECT_EQ(rows[0].value, localizedNotPerformed())
        << "category/error/reason must not leak into the rendered value either";
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
