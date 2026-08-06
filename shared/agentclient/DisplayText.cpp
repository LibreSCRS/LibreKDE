// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#include "DisplayText.h"

#include <QTextDocument> // Qt::mightBeRichText

namespace LibreKDE::DisplayText {

QString plainDisplay(const QString& text)
{
    // Escape ONLY what AutoText would promote to StyledText, so a hostile value
    // renders as literal characters while every legitimate value stays
    // byte-identical.
    return Qt::mightBeRichText(text) ? text.toHtmlEscaped() : text;
}

} // namespace LibreKDE::DisplayText
