// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "SignSeams.h"

#include "AgentOperation.h" // LibreKDE::CertificateList / CertificateInfo

#include <KLocalizedString>

#include <QInputDialog>
#include <QMessageBox>
#include <QString>
#include <QStringList>

namespace LibreKDE::Signing {

LibreKDE::CertChooser widgetCertChooser()
{
    return [](const LibreKDE::CertificateList& cands) -> std::optional<QString> {
        QStringList labels;
        labels.reserve(cands.size());
        for (const LibreKDE::CertificateInfo& c : cands) {
            QString label = c.subjectCn.isEmpty() ? c.certId : c.subjectCn;
            if (!c.notAfter.isEmpty()) {
                label += QStringLiteral(" — %1").arg(c.notAfter);
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
        return cands.at(idx).certId;
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
