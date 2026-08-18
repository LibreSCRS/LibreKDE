// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "SignSeams.h"

#include <LibreSCRS/AgentClient/Types.h> // LibreSCRS::AgentClient::CertificateInfo

#include <KLocalizedString>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QList>
#include <QLocale>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>

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

LibreKDE::CardChooser widgetCardChooser()
{
    return [](const QList<LibreKDE::CardChoice>& cands) -> std::optional<QString> {
        QStringList labels;
        labels.reserve(cands.size());
        for (const LibreKDE::CardChoice& c : cands) {
            // The reader name is what a person can act on; the card id is an
            // opaque token and only worth showing when there is nothing better.
            // The card type is appended when the agent has resolved it — it is
            // empty until a read does, so it can never be the whole label.
            QString label = c.readerName.isEmpty() ? c.cardId : c.readerName;
            if (!c.cardType.isEmpty()) {
                label += QStringLiteral(" — %1").arg(c.cardType);
            }
            labels << label;
        }

        // Built by hand rather than via QInputDialog::getItem() because that
        // helper answers with the selected TEXT, and this list can legitimately
        // hold the same text twice: two identical readers ("SCM SCR3310" in
        // both slots) holding cards whose type has not been read yet render
        // byte-identical labels. Mapping text back through indexOf() would then
        // always resolve to the FIRST of them and sign with the card the user
        // did not point at — silently, since the label they clicked is exactly
        // the label they get. The combo's INDEX is the selection; it is
        // positional, so duplicate labels cannot alias.
        QDialog dialog(nullptr);
        dialog.setWindowTitle(i18nc("@title:window", "Choose a Card to Sign With"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* prompt = new QLabel(i18nc("@label:listbox", "More than one card can sign. Choose one:"), &dialog);
        // Reader names and card types are agent-supplied strings; a QLabel
        // renders rich text by default, so pin it plain like every other place
        // this repo draws card-supplied text.
        prompt->setTextFormat(Qt::PlainText);
        auto* combo = new QComboBox(&dialog);
        combo->addItems(labels);
        combo->setCurrentIndex(0);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(prompt);
        layout->addWidget(combo);
        layout->addWidget(buttons);

        if (dialog.exec() != QDialog::Accepted) {
            return std::nullopt;
        }
        const int idx = combo->currentIndex();
        if (idx < 0 || idx >= cands.size()) {
            return std::nullopt;
        }
        return cands.at(idx).cardId;
    };
}

} // namespace LibreKDE::Signing
