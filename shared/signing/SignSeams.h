// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CertSelector.h" // LibreKDE::CertChooser / LibreKDE::OverwriteConfirmer

/// @file
/// @brief Reusable QtWidgets modal seams for the two interactive decisions in a
///        sign flow — which signing certificate to use, and whether to overwrite
///        an existing output file. Co-located with the `SignJob` core so the
///        signing path and its default GUI seams live in ONE lib. Implemented
///        over QDialog / QMessageBox in the .cpp (Qt6::Widgets is a PRIVATE
///        dep of librekde-signing); calling a factory only constructs the
///        `std::function` — a dialog is created only when the seam is invoked
///        (i.e. multi-cert / a pre-existing output). Consumed by the Purpose
///        plugin (a QApplication host); the plasmoid injects its OWN
///        non-QtWidgets seams (a QML/Plasma popup must never raise a parentless
///        top-level QWidget modal), so it does NOT consume these.

namespace LibreKDE::Signing {

/// @brief A `CertChooser` backed by a modal single-selection dialog listing the
///        signing certificates' display subjects (+ the expiry date where
///        present). The answer is the combo's INDEX, never the rendered text:
///        two certificates can carry the same subject and expiry, and a
///        text-keyed lookup would silently sign with the first of them.
[[nodiscard]] LibreKDE::CertChooser widgetCertChooser();

/// @brief An `OverwriteConfirmer` backed by a modal QMessageBox Yes/No question.
[[nodiscard]] LibreKDE::OverwriteConfirmer widgetOverwriteConfirmer();

/// @brief A `CardChooser` backed by a modal single-selection dialog listing the
///        candidate cards, labelled by the reader holding each one. The answer
///        is the combo's INDEX, never the rendered text: two identical readers
///        holding unread cards produce byte-identical labels.
///
/// Only ever invoked with more than one candidate (see `chooseSigningCard`), so
/// constructing it costs nothing on the single-card desk that never sees it.
[[nodiscard]] LibreKDE::CardChooser widgetCardChooser();

} // namespace LibreKDE::Signing
