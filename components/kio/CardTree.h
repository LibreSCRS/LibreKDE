// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CardDataSource.h"

#include <QString>
#include <QStringList>
#include <QUrl>
#include <optional>

/// @file
/// @brief Pure, KIO-free, D-Bus-free mapping of a `card:/…` URL to a virtual
///        node, and the capability-derived child set for a directory. Depends
///        ONLY on `CardDataSource::listReadersWithCards()` + the capability bits
///        — `list()` performs ZERO card I/O.

namespace LibreKDE {

/// @brief Kind of node a `card:/…` path resolves to.
enum class NodeKind {
    Invalid,      ///< Path does not resolve (unknown reader / missing capability dir).
    Root,         ///< `card:/` — lists the present readers (always; no single-card flatten, C1).
    Reader,       ///< `card:/<reader>` — lists info.txt + capability dirs.
    IdentityDir,  ///< `card:/<reader>/Identity` — lists identity.txt + photo.
    PkiDir,       ///< `card:/<reader>/PKI` — cert folders (resolved by the worker via I/O).
    CertDir,      ///< `card:/<reader>/PKI/<certFolder>` — one cert (resolved by the worker).
    InfoLeaf,     ///< `card:/<reader>/info.txt`.
    IdentityLeaf, ///< `card:/<reader>/Identity/identity.txt`.
    PhotoLeaf,    ///< `card:/<reader>/Identity/photo.<ext>` (cap-derived ext; true MIME on get, C3).
    CertInfoLeaf, ///< `card:/<reader>/PKI/<certFolder>/info.txt`.
    CertDerLeaf,  ///< `card:/<reader>/PKI/<certFolder>/certificate.der`.
    CertPemLeaf,  ///< `card:/<reader>/PKI/<certFolder>/certificate.pem`.
};

/// @brief A resolved node: its kind plus the path segments that identify it.
struct CardNode
{
    NodeKind kind = NodeKind::Invalid;
    QString readerName;    ///< Set for Reader and below.
    QString certFolder;    ///< Set for CertDir and its leaves (the purpose/certId folder name).
    CardPresence presence; ///< The matching presence (valid for Reader and below).
};

/// @brief Pure capability-driven `card:/` layout.
///
/// Holds a reference to a `CardDataSource` but only ever calls its zero-I/O
/// `listReadersWithCards()`. Folder children are derived from the capability
/// bits alone; the PKI cert folders are NOT enumerated here (that needs a card
/// read) — `list(PkiDir)` returns an empty list and the worker fills it.
class CardTree
{
public:
    explicit CardTree(CardDataSource& source);

    /// @brief Resolve a `card:/…` URL to a node (no I/O).
    [[nodiscard]] CardNode resolve(const QUrl& url) const;

    /// @brief The child names of a directory URL, derived from capabilities
    ///        ONLY (no I/O). Returns an empty list for non-directories, unknown
    ///        readers, and `PKI/` (cert folders are filled by the worker).
    [[nodiscard]] QStringList list(const QUrl& url) const;

    /// @brief The fixed leaf names inside an `Identity/` dir for @p caps.
    [[nodiscard]] static QStringList identityChildren(std::uint32_t caps);

    // Fixed node names (single source of truth, shared with the worker).
    static QString infoTxt();
    static QString identityDirName();
    static QString pkiDirName();
    static QString identityTxt();
    static QString photoNodeName();
    /// @brief The photo leaf name WITH a capability-derived image extension
    ///        (.jp2 for an eMRTD DG2 card, else .jpg) so a file manager can open
    ///        it directly. The true format is confirmed by get()'s byte sniff.
    static QString photoLeafName(std::uint32_t caps);
    static QString certInfoTxt();
    static QString certDerName();
    static QString certPemName();

private:
    /// @brief Split a `card:/…` URL into non-empty path segments.
    [[nodiscard]] static QStringList segments(const QUrl& url);
    /// @brief Find the presence whose readerName matches @p name.
    [[nodiscard]] std::optional<CardPresence> presenceFor(const QString& name) const;

    CardDataSource& m_source;
};

} // namespace LibreKDE
