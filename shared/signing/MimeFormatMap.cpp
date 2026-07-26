// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "MimeFormatMap.h"

#include <QFileInfo>

namespace LibreKDE {
namespace MimeFormatMap {

SignChoice resolve(const QString& mimeType)
{
    // MIME→format choice. Wire strings are the agent's vocabulary
    // (Card1.xml `options`): pades|cades|xades|jades|asice, never `auto` (the
    // client commits to a concrete format so the output filename is fixed).
    if (mimeType == QLatin1String("application/pdf")) {
        return {QStringLiteral("pades"), QStringLiteral("enveloped")};
    }
    if (mimeType == QLatin1String("application/xml") || mimeType == QLatin1String("text/xml")) {
        return {QStringLiteral("xades"), QStringLiteral("detached")};
    }
    if (mimeType == QLatin1String("application/json")) {
        return {QStringLiteral("jades"), QStringLiteral("detached")};
    }
    if (mimeType == QLatin1String("application/vnd.etsi.asic-e+zip")) {
        return {QStringLiteral("asice"), QStringLiteral("detached")};
    }
    // Everything else: a detached CAdES side signature.
    return {QStringLiteral("cades"), QStringLiteral("detached")};
}

SignChoice resolveFormat(const QString& format)
{
    // PAdES is the only enveloped family; every other AdES format the wire
    // vocabulary admits is a detached side signature. Pairing the override
    // format with its mandated packaging here keeps an override from forwarding
    // a stale MIME-derived `enveloped`/`detached` (and mis-naming the output).
    if (format == QLatin1String("pades")) {
        return {QStringLiteral("pades"), QStringLiteral("enveloped")};
    }
    // cades | xades | jades | asice (and any unknown the agent will reject):
    // detached. `outputName` already maps each detached format to its sidecar.
    return {format, QStringLiteral("detached")};
}

QString outputName(const QString& inputName, const SignChoice& choice)
{
    const QFileInfo info(inputName);
    const QString suffix = info.suffix();                 // "" when no extension.
    const QString completeBase = info.completeBaseName(); // stem incl. inner dots.

    // Detached CAdES → a `.p7s` sidecar appended to the FULL input name (the
    // signature references the unchanged input alongside it). Detached XAdES /
    // JAdES → an `.xml` / `.json` sidecar likewise appended to the full name.
    if (!choice.enveloped()) {
        if (choice.format == QLatin1String("cades")) {
            return inputName + QStringLiteral(".p7s");
        }
        if (choice.format == QLatin1String("xades")) {
            return inputName + QStringLiteral(".xml");
        }
        if (choice.format == QLatin1String("jades")) {
            return inputName + QStringLiteral(".json");
        }
        // ASiC-E (and any future detached container with no sidecar rule): a
        // freshly emitted container, named `name-signed.<ext>` like an embed.
    }

    // Enveloped (or container) output: `name-signed.<ext>`, preserving the
    // original extension and any inner dots in the stem.
    if (suffix.isEmpty()) {
        return completeBase + QStringLiteral("-signed");
    }
    return completeBase + QStringLiteral("-signed.") + suffix;
}

} // namespace MimeFormatMap
} // namespace LibreKDE
