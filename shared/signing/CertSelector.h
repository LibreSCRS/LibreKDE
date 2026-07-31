// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <LibreSCRS/AgentClient/Types.h> // LibreSCRS::AgentClient::CertificateInfo

#include <QList>
#include <QString>
#include <functional>
#include <optional>

/// @file
/// @brief Injectable seams for the two interactive decisions in a sign flow —
///        which signing certificate to use, and whether to overwrite an
///        existing output file. The Job takes these as std::function so tests
///        drive them headlessly and the real plugin wires them to KF6 dialogs.

namespace LibreKDE {

/// @brief Choose one signing certificate from @p candidates (length ≥ 2).
///
/// Return the chosen certificate's `id`, or `std::nullopt` to cancel the
/// operation. The Job only calls this when more than one signing-capable cert
/// is present; a single candidate is auto-selected without a prompt.
using CertChooser =
    std::function<std::optional<QString>(const QList<LibreSCRS::AgentClient::CertificateInfo>& candidates)>;

/// @brief Confirm overwriting @p outputPath (an absolute path that already
///        exists). Return true to overwrite, false to abort. Never called when
///        the target does not yet exist. The input is never modified in place,
///        so this only ever guards the derived signed-output filename.
using OverwriteConfirmer = std::function<bool(const QString& outputPath)>;

} // namespace LibreKDE
