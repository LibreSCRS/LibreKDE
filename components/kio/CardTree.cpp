// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "CardTree.h"

#include <LibreSCRS/AgentClient/AgentCapabilities.h> // Client::Cap::*, Client::has()

// Short local spelling for the agent client library, as in AgentCardDataSource.
namespace Client = LibreSCRS::AgentClient;

namespace LibreKDE {

CardTree::CardTree(CardDataSource& source) : m_source(source) {}

QString CardTree::infoTxt()
{
    return QStringLiteral("info.txt");
}
QString CardTree::identityDirName()
{
    return QStringLiteral("Identity");
}
QString CardTree::pkiDirName()
{
    return QStringLiteral("PKI");
}
QString CardTree::identityTxt()
{
    return QStringLiteral("identity.txt");
}
QString CardTree::photoNodeName()
{
    return QStringLiteral("photo");
}
QString CardTree::photoLeafName(std::uint32_t caps)
{
    // eMRTD DG2 photos are virtually always JPEG2000; a plain eID photo is JPEG.
    // The extension is a capability-derived hint (no card read) so a file manager
    // can open the leaf directly; get()'s byte sniff confirms the true MIME.
    return photoNodeName() +
           (Client::has(caps, Client::Cap::EmrtdCrypto) ? QStringLiteral(".jp2") : QStringLiteral(".jpg"));
}
QString CardTree::certInfoTxt()
{
    return QStringLiteral("info.txt");
}
QString CardTree::certDerName()
{
    return QStringLiteral("certificate.der");
}
QString CardTree::certPemName()
{
    return QStringLiteral("certificate.pem");
}

QStringList CardTree::identityChildren(std::uint32_t caps)
{
    // The photo node is always offered for an IdentityData card: whether the
    // card actually yields a photo is unknown without a read, so the node
    // is static and the worker fails the get() honestly if there is none. The
    // capability-derived extension lets a file manager open it directly.
    return QStringList{identityTxt(), photoLeafName(caps)};
}

QStringList CardTree::segments(const QUrl& url)
{
    // card:/Gemalto/Identity → path "/Gemalto/Identity"; split, drop empties.
    QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts;
}

std::optional<CardPresence> CardTree::presenceFor(const QString& name) const
{
    const QList<CardPresence> present = m_source.listReadersWithCards();
    for (const CardPresence& p : present) {
        if (p.readerName == name) {
            return p;
        }
    }
    return std::nullopt;
}

CardNode CardTree::resolve(const QUrl& url) const
{
    const QStringList seg = segments(url);

    if (seg.isEmpty()) {
        return CardNode{NodeKind::Root, {}, {}, {}};
    }

    const auto presence = presenceFor(seg.at(0));
    if (!presence) {
        return CardNode{}; // unknown reader
    }
    const std::uint32_t caps = presence->capabilities;
    const bool hasIdentity = Client::has(caps, Client::Cap::IdentityData);
    const bool hasPki = Client::has(caps, Client::Cap::Pki);

    CardNode node;
    node.readerName = presence->readerName;
    node.presence = *presence;

    if (seg.size() == 1) {
        node.kind = NodeKind::Reader;
        return node;
    }

    const QString& s1 = seg.at(1);

    // card:/<reader>/info.txt
    if (seg.size() == 2 && s1 == infoTxt()) {
        node.kind = NodeKind::InfoLeaf;
        return node;
    }

    // card:/<reader>/Identity[/…]
    if (s1 == identityDirName() && hasIdentity) {
        if (seg.size() == 2) {
            node.kind = NodeKind::IdentityDir;
            return node;
        }
        if (seg.size() == 3) {
            if (seg.at(2) == identityTxt()) {
                node.kind = NodeKind::IdentityLeaf;
                return node;
            }
            if (seg.at(2) == photoLeafName(caps)) {
                node.kind = NodeKind::PhotoLeaf;
                return node;
            }
        }
        return CardNode{};
    }

    // card:/<reader>/PKI[/<certFolder>[/leaf]]
    if (s1 == pkiDirName() && hasPki) {
        if (seg.size() == 2) {
            node.kind = NodeKind::PkiDir;
            return node;
        }
        node.certFolder = seg.at(2);
        if (seg.size() == 3) {
            node.kind = NodeKind::CertDir;
            return node;
        }
        if (seg.size() == 4) {
            const QString& leaf = seg.at(3);
            if (leaf == certInfoTxt()) {
                node.kind = NodeKind::CertInfoLeaf;
                return node;
            }
            if (leaf == certDerName()) {
                node.kind = NodeKind::CertDerLeaf;
                return node;
            }
            if (leaf == certPemName()) {
                node.kind = NodeKind::CertPemLeaf;
                return node;
            }
        }
        return CardNode{};
    }

    return CardNode{};
}

QStringList CardTree::list(const QUrl& url) const
{
    const QStringList seg = segments(url);

    // Root: always one entry per present reader (no single-card flatten, C1).
    if (seg.isEmpty()) {
        QStringList names;
        for (const CardPresence& p : m_source.listReadersWithCards()) {
            names << p.readerName;
        }
        return names;
    }

    const auto presence = presenceFor(seg.at(0));
    if (!presence) {
        return {};
    }
    const std::uint32_t caps = presence->capabilities;

    // Reader dir: info.txt + the capability dirs (zero I/O).
    if (seg.size() == 1) {
        QStringList children{infoTxt()};
        if (Client::has(caps, Client::Cap::IdentityData)) {
            children << identityDirName();
        }
        if (Client::has(caps, Client::Cap::Pki)) {
            children << pkiDirName();
        }
        return children;
    }

    // Identity dir: fixed children (identity.txt + photo node), zero I/O.
    if (seg.size() == 2 && seg.at(1) == identityDirName() && Client::has(caps, Client::Cap::IdentityData)) {
        return identityChildren(caps);
    }

    // PKI dir cert folders need a card read — the worker fills them. The
    // pure tree returns nothing here.
    return {};
}

} // namespace LibreKDE
