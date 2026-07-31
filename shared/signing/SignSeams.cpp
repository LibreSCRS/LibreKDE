// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "SignSeams.h"

#include <LibreSCRS/AgentClient/Types.h> // LibreSCRS::AgentClient::CertificateInfo

#include <KLocalizedString>

#include <QInputDialog>
#include <QList>
#include <QLocale>
#include <QMessageBox>
#include <QString>
#include <QStringList>

namespace LibreKDE::Signing {

LibreKDE::CertChooser widgetCertChooser()
{
    using LibreSCRS::AgentClient::CertificateInfo;
    return [](const QList<CertificateInfo>& cands) -> std::optional<QString> {
        QStringList labels;
        labels.reserve(cands.size());
        for (const CertificateInfo& c : cands) {
            QString label = c.subject.isEmpty() ? c.id : c.subject;
            if (c.notAfter.isValid()) {
                // The expiry arrives as a QDateTime, so this chooser picks how to
                // render it. It is a dialog label and nothing parses it back, so
                // the user's own locale short form is the right choice.
                //
                // toLocalTime() is NOT optional: the agent's validity dates are
                // parsed from zoned ISO-8601, which yields a UTC-spec QDateTime,
                // and the short format prints that wall clock with no zone
                // marker. Rendering it unconverted shows a cert expiring at
                // 23:00Z as 23:00 on the previous day to anyone east of UTC. On
                // an unzoned value — a LocalTime spec — the conversion is a
                // no-op, so it cannot shift a date that was never zoned.
                label +=
                    QStringLiteral(" — %1").arg(QLocale().toString(c.notAfter.toLocalTime(), QLocale::ShortFormat));
            }
            labels << label;
        }
        bool ok = false;
        const QString chosen = QInputDialog::getItem(
            nullptr, i18nc("@title:window", "Choose a Signing Certificate"),
            i18nc("@label:listbox", "This card has several signing certificates. Choose one:"), labels, 0, false, &ok);
        if (!ok) {
            return std::nullopt;
        }
        const int idx = labels.indexOf(chosen);
        if (idx < 0 || idx >= cands.size()) {
            return std::nullopt;
        }
        return cands.at(idx).id;
    };
}

LibreKDE::OverwriteConfirmer widgetOverwriteConfirmer()
{
    return [](const QString& outputPath) -> bool {
        const auto answer = QMessageBox::question(
            nullptr, i18nc("@title:window", "Overwrite Signed File?"),
            i18nc("@info", "The file %1 already exists. Overwrite it with the new signature?", outputPath),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        return answer == QMessageBox::Yes;
    };
}

} // namespace LibreKDE::Signing
