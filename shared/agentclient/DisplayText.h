// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once
#include <QString>

/// @file
/// @brief Rich-text neutralization for untrusted display values — ONE rule for
///        every host surface, so the plasmoid and the credentials window cannot
///        drift apart on what counts as hostile markup.

namespace LibreKDE::DisplayText {

/// @brief Neutralize rich-text promotion for card/agent/filename-derived values
///        rendered by sinks whose `textFormat` cannot be forced to
///        `Text.PlainText` (Kirigami internals and QQC2/PC3 labels are
///        `AutoText`, where `Qt::mightBeRichText` promotes tag-looking values
///        to StyledText). A value Qt would promote is HTML-escaped; the same
///        AutoText path then renders the escaped form as the ORIGINAL literal
///        characters (the entity sequences are themselves rich-detected —
///        verified empirically). Values without markup pass through
///        byte-identical, honouring the raw-card-data rule for every legitimate
///        string. Controllable sinks use `textFormat: Text.PlainText` directly
///        instead.
[[nodiscard]] QString plainDisplay(const QString& text);

} // namespace LibreKDE::DisplayText
