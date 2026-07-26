// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// CredentialModel role coverage: a small CredentialList exercised through the
// QAbstractListModel surface the dashboard's ListView binds to. Pure (no D-Bus,
// no QML engine) — runs headless (QT_QPA_PLATFORM=offscreen) with only a
// QCoreApplication (ki18n needs an app for locale resolution).

#include "CredentialModel.h"
#include "CredentialTypes.h"

#include <QModelIndex>
#include <QVariant>
#include <gtest/gtest.h>

using namespace LibreKDE;
using Credentials::CredentialModel;

namespace {

// Build the two-record fixture the dashboard renders: an operational User PIN
// the holder can change, and a transport Signing PIN awaiting activation.
CredentialList twoRecordFixture()
{
    CredentialRecord user;
    user.id = QStringLiteral("user:0x86");
    user.kind = CredentialKind::User;
    user.state = CredentialState::Operational;
    user.retriesLeft = 3;
    user.retriesMax = 3;
    user.canChange = true;

    CredentialRecord sign;
    sign.id = QStringLiteral("sign:0x81");
    sign.kind = CredentialKind::Sign;
    sign.state = CredentialState::Transport;
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
    EXPECT_EQ(roleAt(m, 0, CredentialModel::StateRole).toInt(), int(CredentialState::Operational));
    EXPECT_EQ(roleAt(m, 1, CredentialModel::StateRole).toInt(), int(CredentialState::Transport));
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
    CredentialRecord puk;
    puk.id = QStringLiteral("puk:0x93");
    puk.kind = CredentialKind::Puk;
    puk.retriesLeft = 5;
    puk.retriesMax = 5;
    puk.usesLeft = 16;
    puk.usesMax = 20;
    puk.unblocksLeft = 5; // DOCP tag 99 — must NOT appear in the rendered line

    CredentialRecord oneTry;
    oneTry.id = QStringLiteral("user:0x86");
    oneTry.kind = CredentialKind::User;
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
