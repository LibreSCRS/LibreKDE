// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// CredentialModel role coverage: a small CredentialList exercised through the
// QAbstractListModel surface the dashboard's ListView binds to. Pure (no D-Bus,
// no QML engine) — runs headless (QT_QPA_PLATFORM=offscreen) with only a
// QCoreApplication (ki18n needs an app for locale resolution).

#include "CredentialModel.h"

#include <LibreSCRS/AgentClient/CredentialTypes.h>

#include <QModelIndex>
#include <QVariant>
#include <gtest/gtest.h>

#include <iterator>

using namespace LibreKDE;
using Credentials::CredentialModel;

// The agent client library, qualified through an alias: it owns the credential
// record and its vocabulary, and the model holds those types unchanged. There is
// no second spelling of them to cross any more, so the alias is here for brevity
// alone — nothing in this file names a credential type on two sides.
namespace Client = LibreSCRS::AgentClient;

namespace {

// Build the two-record fixture the dashboard renders: an operational User PIN
// the holder can change, and a transport Signing PIN awaiting activation.
Client::CredentialList twoRecordFixture()
{
    Client::CredentialRecord user;
    user.id = QStringLiteral("user:0x86");
    user.kind = Client::CredentialKind::User;
    user.state = Client::CredentialState::Operational;
    user.retriesLeft = 3;
    user.retriesMax = 3;
    user.canChange = true;

    Client::CredentialRecord sign;
    sign.id = QStringLiteral("sign:0x81");
    sign.kind = Client::CredentialKind::Sign;
    sign.state = Client::CredentialState::Transport;
    sign.retriesLeft = 3;
    sign.retriesMax = 3;
    sign.activatable = true;
    sign.keyActivatable = true;
    sign.keyActivationPending = true;

    return {user, sign};
}

QVariant roleAt(const CredentialModel& m, int row, int role)
{
    return m.data(m.index(row, 0), role);
}

TEST(CredentialModel, RowCountMatchesRecords)
{
    CredentialModel m;
    EXPECT_EQ(m.rowCount(), 0);
    m.setRecords(twoRecordFixture());
    EXPECT_EQ(m.rowCount(), 2);
}

TEST(CredentialModel, LocalizedNamesArePopulated)
{
    CredentialModel m;
    m.setRecords(twoRecordFixture());

    EXPECT_EQ(roleAt(m, 0, CredentialModel::IdRole).toString(), QStringLiteral("user:0x86"));
    // Localized kind/state names must be non-empty (exact text is translation-
    // dependent; under LANGUAGE=en the source strings resolve).
    EXPECT_FALSE(roleAt(m, 0, CredentialModel::KindNameRole).toString().isEmpty());
    EXPECT_FALSE(roleAt(m, 0, CredentialModel::StateNameRole).toString().isEmpty());
    // Spelled as LITERALS on purpose — see StateRoleMatchesTheLiteralsTheDelegate
    // BranchesOn below, which owns the argument and covers all five values.
    EXPECT_EQ(roleAt(m, 0, CredentialModel::StateRole).toInt(), 2); // Operational
    EXPECT_EQ(roleAt(m, 1, CredentialModel::StateRole).toInt(), 1); // Transport
}

// The state role's int is a CONTRACT WITH QML, and this is the only thing that
// holds the two ends together.
//
// CredentialDelegate.qml does not import the enum; it branches on bare integers
// and documents them itself ("0 Unknown, 1 Transport, 2 Operational,
// 3 NeedsChange, 4 Blocked"), styling 4 as negative, 1 and 3 as neutral, 2 as
// positive. The model hands that int straight from the client library's
// CredentialState (CredentialModel.cpp, StateRole). So the QML file and the
// client library's enum are two independent declarations of one numbering, with
// no compiler edge between them.
//
// The expectations below are therefore LITERALS, never
// `int(Client::CredentialState::Operational)`. Written that way the assertion
// would take both sides from the same declaration and compare it with itself: it
// would pass under any renumbering, including one that silently repaints every
// blocked credential green. Written as literals it fails, naming the value that
// moved.
//
// No other guard in this repo constrains that numbering. The credential
// vocabulary coverage guard next door pins each enumerator to its WIRE TOKEN and
// anchors its counts on NAMED enumerators — both sides of every one of its
// assertions move together under a reorder, and it never compares an enumerator
// to an integer at all. Integers are what QML consumes, and this is the only
// place they are written down twice.
//
// If this test fails, do not edit the numbers to match — decide which side is
// right, and change CredentialDelegate.qml's switch in the same commit.
TEST(CredentialModel, StateRoleMatchesTheLiteralsTheDelegateBranchesOn)
{
    const struct
    {
        Client::CredentialState state;
        int delegateValue;
    } cases[] = {
        {Client::CredentialState::Unknown, 0},     {Client::CredentialState::Transport, 1},
        {Client::CredentialState::Operational, 2}, {Client::CredentialState::NeedsChange, 3},
        {Client::CredentialState::Blocked, 4},
    };

    Client::CredentialList records;
    for (const auto& c : cases) {
        Client::CredentialRecord r;
        r.id = QStringLiteral("cred:%1").arg(c.delegateValue);
        r.state = c.state;
        records.append(r);
    }

    CredentialModel m;
    m.setRecords(records);
    ASSERT_EQ(m.rowCount(), int(std::size(cases))) << "the walk collapsed; every assertion below would be vacuous";

    for (int row = 0; row < int(std::size(cases)); ++row) {
        EXPECT_EQ(roleAt(m, row, CredentialModel::StateRole).toInt(), cases[row].delegateValue)
            << "state role int diverged from the value CredentialDelegate.qml branches on, at row " << row;
    }
}

TEST(CredentialModel, CapabilityFlagsGateStrictlyPerRecord)
{
    CredentialModel m;
    m.setRecords(twoRecordFixture());

    // User PIN: change-only.
    EXPECT_TRUE(roleAt(m, 0, CredentialModel::CanChangeRole).toBool());
    EXPECT_FALSE(roleAt(m, 0, CredentialModel::ActivatableRole).toBool());
    EXPECT_FALSE(roleAt(m, 0, CredentialModel::KeyActivatableRole).toBool());

    // Signing PIN: activatable + key-activatable, NOT changeable in transport.
    EXPECT_FALSE(roleAt(m, 1, CredentialModel::CanChangeRole).toBool());
    EXPECT_TRUE(roleAt(m, 1, CredentialModel::ActivatableRole).toBool());
    EXPECT_TRUE(roleAt(m, 1, CredentialModel::KeyActivatableRole).toBool());

    // The standalone "Activate signing key" button gates on keyActivatable AND
    // key_activation_pending, so the pending flag must be a role.
    EXPECT_TRUE(roleAt(m, 1, CredentialModel::KeyActivationPendingRole).toBool());
    EXPECT_FALSE(roleAt(m, 0, CredentialModel::KeyActivationPendingRole).toBool());
}

TEST(CredentialModel, CountersTextCarriesTheRetryCount)
{
    CredentialModel m;
    m.setRecords(twoRecordFixture());
    // The attribution-labelled counters line must surface the retries-left count.
    EXPECT_TRUE(roleAt(m, 0, CredentialModel::CountersRole).toString().contains(QStringLiteral("3")));
}

TEST(CredentialModel, CountersAreTypedRetryAndUsageAndDropResetCounter)
{
    // The counters line names each counter's TYPE ("Attempts:"/"Uses:"), shows
    // "m of n" when a max is present, does NOT repeat the credential kind (the
    // delegate header carries it once), and does NOT render the DOCP reset
    // counter (unblocksLeft, tag 99). Tests run with LANGUAGE=en and no sr
    // catalog loaded, so the English source strings resolve.
    Client::CredentialRecord puk;
    puk.id = QStringLiteral("puk:0x93");
    puk.kind = Client::CredentialKind::Puk;
    puk.retriesLeft = 5;
    puk.retriesMax = 5;
    puk.usesLeft = 16;
    puk.usesMax = 20;
    puk.unblocksLeft = 5; // DOCP tag 99 — must NOT appear in the rendered line

    Client::CredentialRecord oneTry;
    oneTry.id = QStringLiteral("user:0x86");
    oneTry.kind = Client::CredentialKind::User;
    oneTry.retriesLeft = 1; // no retriesMax -> "Attempts: 1"

    CredentialModel m;
    m.setRecords({puk, oneTry});

    const QString pukLine = roleAt(m, 0, CredentialModel::CountersRole).toString();
    EXPECT_TRUE(pukLine.contains(QStringLiteral("Attempts: 5 of 5"))) << pukLine.toStdString();
    EXPECT_TRUE(pukLine.contains(QStringLiteral("Uses: 16 of 20"))) << pukLine.toStdString();
    EXPECT_FALSE(pukLine.contains(QStringLiteral("unblock")))
        << "the DOCP reset counter must not be rendered — " << pukLine.toStdString();
    EXPECT_FALSE(pukLine.contains(QStringLiteral("PUK")))
        << "the kind name must not be repeated in the counters line — " << pukLine.toStdString();

    const QString oneTryLine = roleAt(m, 1, CredentialModel::CountersRole).toString();
    EXPECT_EQ(oneTryLine, QStringLiteral("Attempts: 1"))
        << "no retriesMax -> bare 'Attempts: N', no ' of ' — " << oneTryLine.toStdString();
}

TEST(CredentialModel, RoleNamesExposeTheDelegateBindings)
{
    CredentialModel m;
    const QHash<int, QByteArray> names = m.roleNames();
    EXPECT_EQ(names.value(CredentialModel::IdRole), QByteArrayLiteral("id"));
    EXPECT_EQ(names.value(CredentialModel::KindNameRole), QByteArrayLiteral("kindName"));
    EXPECT_EQ(names.value(CredentialModel::StateNameRole), QByteArrayLiteral("stateName"));
    EXPECT_EQ(names.value(CredentialModel::StateRole), QByteArrayLiteral("state"));
    EXPECT_EQ(names.value(CredentialModel::CountersRole), QByteArrayLiteral("counters"));
    EXPECT_EQ(names.value(CredentialModel::GuidanceRole), QByteArrayLiteral("guidance"));
    EXPECT_EQ(names.value(CredentialModel::CanChangeRole), QByteArrayLiteral("canChange"));
    EXPECT_EQ(names.value(CredentialModel::UnblockableRole), QByteArrayLiteral("unblockable"));
    EXPECT_EQ(names.value(CredentialModel::ActivatableRole), QByteArrayLiteral("activatable"));
    EXPECT_EQ(names.value(CredentialModel::KeyActivatableRole), QByteArrayLiteral("keyActivatable"));
    EXPECT_EQ(names.value(CredentialModel::KeyActivationPendingRole), QByteArrayLiteral("keyActivationPending"));
}

TEST(CredentialModel, SetRecordsClearsPrevious)
{
    CredentialModel m;
    m.setRecords(twoRecordFixture());
    ASSERT_EQ(m.rowCount(), 2);
    m.setRecords({});
    EXPECT_EQ(m.rowCount(), 0);
}

} // namespace
