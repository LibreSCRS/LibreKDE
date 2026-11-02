// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include <QString>

/// @file
/// @brief Pure MIME → AdES {format, packaging} choice.
///
/// The format/packaging *vocabulary* and the ASiC-S exclusion are canonical in
/// the broker contract and surfaced as the `Card1.Sign` `options` wire
/// strings (agent `Card1.xml`): format ∈ {auto,pades,cades,xades,jades,asice},
/// packaging ∈ {auto,enveloped,detached}. This map only fixes the client-side
/// MIME→format *choice*; the output filename is the agent client library's
/// `signedOutputPath()`, the one rule every client uses. It links nothing — it
/// is a pure value transform, unit-tested in isolation.

namespace LibreKDE {

/// @brief The signing choice for one input MIME type.
///
/// `format`/`packaging` are the exact `Card1.Sign` wire strings (NOT a private
/// enum) so the Job forwards them straight into the `options` map.
struct SignChoice
{
    QString format;    ///< pades|cades|xades|jades|asice (wire vocabulary).
    QString packaging; ///< enveloped|detached (no enveloping).

    [[nodiscard]] bool enveloped() const
    {
        return packaging == QLatin1String("enveloped");
    }
};

/// @brief Pure MIME→format map.
namespace MimeFormatMap {

/// @brief Resolve a source MIME type to its AdES signing choice:
///        application/pdf → PAdES enveloped; application/xml (+text/xml) →
///        XAdES detached; application/json → JAdES detached;
///        application/vnd.etsi.asic-e+zip → ASiC-E detached; everything else →
///        CAdES detached. Never returns `auto` — the client picks a concrete
///        format from the MIME so the request and the output name agree.
[[nodiscard]] SignChoice resolve(const QString& mimeType);

/// @brief Resolve an explicit AdES @p format string (the wire vocabulary:
///        pades|cades|xades|jades|asice) to its canonical signing choice,
///        pairing it with the packaging its family mandates (pades →
///        enveloped, everything else → detached). Used for a per-request format
///        override so the chosen format, its packaging, and the derived output
///        name stay consistent — an override can never leave a MIME-derived
///        packaging mismatched with a cross-family format. An unknown format
///        falls back to detached (the agent validates the vocabulary).
[[nodiscard]] SignChoice resolveFormat(const QString& format);

} // namespace MimeFormatMap

} // namespace LibreKDE
