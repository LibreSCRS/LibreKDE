// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "MimeFormatMap.h"

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
    // detached.
    return {format, QStringLiteral("detached")};
}

} // namespace MimeFormatMap
} // namespace LibreKDE
