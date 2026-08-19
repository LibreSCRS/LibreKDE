// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "SignSeams.h"

#include <LibreSCRS/AgentClient/Types.h> // LibreSCRS::AgentClient::CertificateInfo

#include <KLocalizedString>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QList>
#include <QLocale>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>

namespace LibreKDE::Signing {

namespace {

/// @brief Modal single-selection dialog whose answer is the chosen INDEX.
///
/// Built by hand rather than via QInputDialog::getItem() because that helper
/// answers with the selected TEXT, and BOTH choosers in this file can
/// legitimately render the same text twice: two identical readers ("SCM
/// SCR3310" in both slots) holding cards whose type has not been read yet, or
/// two signing certificates issued together, carrying the same subject CN and
/// the same expiry. Mapping text back through indexOf() then always resolves
/// to the FIRST of them, so the user signs with the card — or the KEY — they
/// did not point at, silently, since the label they clicked is exactly the
/// label they get. An index is positional, so duplicate labels cannot alias.
///
/// Returns the index into @p labels, or nullopt when the user cancels.
[[nodiscard]] std::optional<int> chooseByIndex(const QString& windowTitle, const QString& promptText,
                                               const QStringList& labels)
{
    QDialog dialog(nullptr);
    dialog.setWindowTitle(windowTitle);
    auto* layout = new QVBoxLayout(&dialog);
    auto* prompt = new QLabel(promptText, &dialog);
    // The prompt is a translated literal, not card data — the agent-supplied
    // reader names, card types and certificate subjects go into the combo,
    // whose items are drawn through QStyle and never interpret rich text.
    // Pinned plain anyway so the label cannot start rendering markup if the
    // prompt ever becomes data.
    prompt->setTextFormat(Qt::PlainText);
    auto* combo = new QComboBox(&dialog);
    combo->addItems(labels);
    combo->setCurrentIndex(0);
    // QInputDialog paired its label with the input widget, which is what gives
    // the list a name in the accessibility tree; a hand-built dialog has to say
    // so itself or the combo reaches a screen reader unnamed.
    prompt->setBuddy(combo);
    combo->setAccessibleName(promptText);
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
    if (idx < 0 || idx >= labels.size()) {
        return std::nullopt;
    }
    return idx;
}

} // namespace

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
        const std::optional<int> idx =
            chooseByIndex(i18nc("@title:window", "Choose a Signing Certificate"),
                          i18nc("@label:listbox", "This card has several signing certificates. Choose one:"), labels);
        if (!idx || *idx >= cands.size()) {
            return std::nullopt;
        }
        return cands.at(*idx).id;
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

        const std::optional<int> idx =
            chooseByIndex(i18nc("@title:window", "Choose a Card to Sign With"),
                          i18nc("@label:listbox", "More than one card can sign. Choose one:"), labels);
        if (!idx || *idx >= cands.size()) {
            return std::nullopt;
        }
        return cands.at(*idx).cardId;
    };
}

} // namespace LibreKDE::Signing
