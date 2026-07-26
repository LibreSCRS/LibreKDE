// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// CardTree: pure capability-driven card:/ layout. Listing derives children from
// capabilities ONLY (always-reader-dir) and performs ZERO card I/O — the
// load-bearing invariant.

#include "AgentCapabilities.h"
#include "CardTree.h"
#include "FakeCardDataSource.h"

#include <gtest/gtest.h>

using namespace LibreKDE;
using namespace LibreKDETest;

namespace {
CardPresence presence(const QString& reader, const QString& path, std::uint32_t caps, const QString& preAuth)
{
    return CardPresence{reader, path, caps, preAuth};
}
} // namespace

TEST(CardTree, HybridCardAlwaysHasReaderDirThenIdentityAndPki)
{
    FakeCardDataSource src{{presence(QStringLiteral("Gemalto"), QStringLiteral("/card/0"), Cap::Pki | Cap::IdentityData,
                                     QStringLiteral("None"))}};
    CardTree tree(src);

    // Always reader-dir (NO single-card flatten).
    EXPECT_EQ(tree.list(QUrl(QStringLiteral("card:/"))), (QStringList{QStringLiteral("Gemalto")}));

    const QStringList root = tree.list(QUrl(QStringLiteral("card:/Gemalto")));
    EXPECT_TRUE(root.contains(QStringLiteral("info.txt")));
    EXPECT_TRUE(root.contains(QStringLiteral("Identity")));
    EXPECT_TRUE(root.contains(QStringLiteral("PKI")));

    EXPECT_EQ(tree.list(QUrl(QStringLiteral("card:/Gemalto/Identity"))),
              (QStringList{QStringLiteral("identity.txt"), QStringLiteral("photo.jpg")}));
}

TEST(CardTree, PassportHasIdentityNoPki) // capability edge
{
    FakeCardDataSource src{{presence(QStringLiteral("NFC"), QStringLiteral("/card/1"),
                                     Cap::IdentityData | Cap::EmrtdCrypto, QStringLiteral("Can"))}};
    CardTree tree(src);
    const QStringList root = tree.list(QUrl(QStringLiteral("card:/NFC")));
    EXPECT_TRUE(root.contains(QStringLiteral("Identity")));
    EXPECT_FALSE(root.contains(QStringLiteral("PKI")));
}

TEST(CardTree, PkiOnlyTokenHasPkiNoIdentity)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("Reader"), QStringLiteral("/card/2"), Cap::Pki, QStringLiteral("None"))}};
    CardTree tree(src);
    const QStringList root = tree.list(QUrl(QStringLiteral("card:/Reader")));
    EXPECT_TRUE(root.contains(QStringLiteral("PKI")));
    EXPECT_FALSE(root.contains(QStringLiteral("Identity")));
}

TEST(CardTree, PinManagementOnlyTokenHasNeitherIdentityNorPki) // capability edge
{
    FakeCardDataSource src{
        {presence(QStringLiteral("R"), QStringLiteral("/card/3"), Cap::PinManagement, QStringLiteral("None"))}};
    CardTree tree(src);
    const QStringList root = tree.list(QUrl(QStringLiteral("card:/R")));
    EXPECT_TRUE(root.contains(QStringLiteral("info.txt")));
    EXPECT_FALSE(root.contains(QStringLiteral("Identity")));
    EXPECT_FALSE(root.contains(QStringLiteral("PKI")));
}

TEST(CardTree, MultipleReadersEachListed)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("Gemalto"), QStringLiteral("/card/0"), Cap::Pki, QStringLiteral("None")),
         presence(QStringLiteral("NFC"), QStringLiteral("/card/1"), Cap::IdentityData, QStringLiteral("Can"))}};
    CardTree tree(src);
    const QStringList readers = tree.list(QUrl(QStringLiteral("card:/")));
    EXPECT_EQ(readers.size(), 2);
    EXPECT_TRUE(readers.contains(QStringLiteral("Gemalto")));
    EXPECT_TRUE(readers.contains(QStringLiteral("NFC")));
}

TEST(CardTree, ResolveClassifiesNodes)
{
    FakeCardDataSource src{{presence(QStringLiteral("Gemalto"), QStringLiteral("/card/0"), Cap::Pki | Cap::IdentityData,
                                     QStringLiteral("None"))}};
    CardTree tree(src);
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/"))).kind, NodeKind::Root);
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/Gemalto"))).kind, NodeKind::Reader);
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/Gemalto/info.txt"))).kind, NodeKind::InfoLeaf);
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/Gemalto/Identity"))).kind, NodeKind::IdentityDir);
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/Gemalto/Identity/identity.txt"))).kind, NodeKind::IdentityLeaf);
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/Gemalto/Identity/photo.jpg"))).kind, NodeKind::PhotoLeaf);
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/Gemalto/PKI"))).kind, NodeKind::PkiDir);
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/Gemalto/PKI/Signature"))).kind, NodeKind::CertDir);
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/Gemalto/PKI/Signature/info.txt"))).kind, NodeKind::CertInfoLeaf);
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/Gemalto/PKI/Signature/certificate.der"))).kind,
              NodeKind::CertDerLeaf);
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/Gemalto/PKI/Signature/certificate.pem"))).kind,
              NodeKind::CertPemLeaf);
}

TEST(CardTree, UnknownReaderAndMissingCapabilityResolveInvalid)
{
    FakeCardDataSource src{
        {presence(QStringLiteral("Reader"), QStringLiteral("/card/2"), Cap::Pki, QStringLiteral("None"))}};
    CardTree tree(src);
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/Nope"))).kind, NodeKind::Invalid);
    // PKI-only card has no Identity dir.
    EXPECT_EQ(tree.resolve(QUrl(QStringLiteral("card:/Reader/Identity"))).kind, NodeKind::Invalid);
}

TEST(CardTree, ListingDoesNoCardIo) // the load-bearing zero-card-I/O test
{
    FakeCardDataSource src{{presence(QStringLiteral("Gemalto"), QStringLiteral("/card/0"), Cap::Pki | Cap::IdentityData,
                                     QStringLiteral("None"))}};
    CardTree tree(src);
    (void)tree.list(QUrl(QStringLiteral("card:/")));
    (void)tree.list(QUrl(QStringLiteral("card:/Gemalto")));
    (void)tree.list(QUrl(QStringLiteral("card:/Gemalto/Identity")));
    (void)tree.resolve(QUrl(QStringLiteral("card:/Gemalto/Identity/identity.txt")));
    EXPECT_EQ(src.ioCallCount(), 0); // readIdentity/readCertificates/getPhoto never called
}
