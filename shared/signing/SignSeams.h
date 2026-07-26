// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CertSelector.h" // LibreKDE::CertChooser / LibreKDE::OverwriteConfirmer

/// @file
/// @brief Reusable QtWidgets modal seams for the two interactive decisions in a
///        sign flow — which signing certificate to use, and whether to overwrite
///        an existing output file. Co-located with the `SignJob` core so the
///        signing path and its default GUI seams live in ONE lib. Implemented
///        over QInputDialog / QMessageBox in the .cpp (Qt6::Widgets is a PRIVATE
///        dep of librekde-signing); calling a factory only constructs the
///        `std::function` — a dialog is created only when the seam is invoked
///        (i.e. multi-cert / a pre-existing output). Consumed by the Purpose
///        plugin (a QApplication host); the plasmoid injects its OWN
///        non-QtWidgets seams (a QML/Plasma popup must never raise a parentless
///        top-level QWidget modal), so it does NOT consume these.

namespace LibreKDE::Signing {

/// @brief A `CertChooser` backed by a modal QInputDialog single-selection list
///        of the signing certificates' display CNs (+ notAfter where present).
[[nodiscard]] LibreKDE::CertChooser widgetCertChooser();

/// @brief An `OverwriteConfirmer` backed by a modal QMessageBox Yes/No question.
[[nodiscard]] LibreKDE::OverwriteConfirmer widgetOverwriteConfirmer();

} // namespace LibreKDE::Signing
