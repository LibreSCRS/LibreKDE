// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CardDataSource.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

/// @file
/// @brief Pure, KIO-free renderers for the `card:/` leaf files: identity.txt,
///        cert info.txt, the reader info.txt, the DER→PEM wrap, and the photo
///        MIME byte-sniff. i18n lives in the worker layer (these take/return
///        plain strings); the worker passes localized labels in where needed.

namespace LibreKDE {

/// @brief Localized KeyUsage bit names the renderer needs, supplied by the
///        worker (which owns the `librekde` i18n domain). The pure renderer must
///        not call i18n itself, so it takes the strings it prints.
struct RenderLabels
{
    // KeyUsage purpose names (X.509 ordinals 0..8), for the "Purpose:" line and
    // the cert-folder name. Index = bit ordinal.
    QStringList keyUsageNames; ///< 9 entries; empty falls back to built-in English.
    // Certificate PURPOSE names. A purpose is not a KeyUsage bit name: the bits
    // say what the key may do, the purpose says what the certificate is FOR.
    QString purposeSigning;        ///< "Digital Signature" (nonRepudiation present)
    QString purposeAuthentication; ///< "Authentication" (digitalSignature without it)
    QString trustNotEvaluated; ///< e.g. "not yet evaluated".
    QString qualifiedUnknown;  ///< e.g. "unknown".

    // info.txt body line labels — translatable (acronyms PKI/eMRTD are NOT).
    // The info.txt BODY is a display payload, not a URL key, so it is safe to
    // localize (unlike the cert-FOLDER name, which is a stable URL segment).
    QString labelSubject;          ///< "Subject"
    QString labelIssuer;           ///< "Issuer"
    QString labelValidUntil;       ///< "Valid until"
    QString labelPurpose;          ///< "Purpose"
    QString labelExtendedKeyUsage; ///< "Extended Key Usage"
    QString labelSigningCapable;   ///< "Signing capable"
    QString labelTrust;            ///< "Trust"
    QString labelQualified;        ///< "Qualified"
    QString labelChain;            ///< "Chain"
    QString labelReader;           ///< "Reader"
    QString labelCapabilities;     ///< "Capabilities"
    QString labelPreReadAuth;      ///< "Pre-read authentication"

    // Enumerated values.
    QString valueYes;         ///< "yes"
    QString valueNo;          ///< "no"
    QString valueNone;        ///< "none" (Capabilities: none)
    QString valueAuthNone;    ///< "None" (Pre-read authentication: None)
    QString capIdentity;      ///< "Identity" (capability flag label)
    QString capPinManagement; ///< "PIN management" (capability flag label)
};

/// @brief Default (English) labels — the renderers' fallback and the unit-test
///        baseline. The worker overrides these with i18n() strings.
[[nodiscard]] RenderLabels defaultRenderLabels();

/// @brief Decode the X.509 KeyUsage bitmask into its localized purpose names.
///        The mask is ordinal-indexed (bit i == 1u<<i == X.509 KeyUsage ordinal
///        i, matching the agent wire); ordinal i maps to keyUsageNames[i].
[[nodiscard]] QStringList keyUsagePurposes(quint32 keyUsageBits, const RenderLabels& labels = defaultRenderLabels());

/// @brief A single short purpose label for the cert FOLDER name; empty when no
///        usable purpose bit is set → caller falls back to certId.
///
/// NOT the first set bit. `nonRepudiation` (contentCommitment) is what marks a
/// signing certificate, and `digitalSignature` is set on AUTHENTICATION
/// certificates too (TLS client, challenge-response) — so first-bit-wins named
/// every eID authentication certificate "Digital Signature". `nonRepudiation`
/// therefore wins when present; `digitalSignature` without it is
/// authentication; anything else falls back to its KeyUsage bit name.
[[nodiscard]] QString primaryPurpose(quint32 keyUsageBits, const RenderLabels& labels = defaultRenderLabels());

/// @brief Render the demographic identity field list as a human-readable text.
[[nodiscard]] QString renderIdentityTxt(const QList<IdentityFieldView>& fields);

/// @brief Render a certificate's metadata as `info.txt`: subject/issuer/
///        validity, Purpose (from keyUsage), EKU, signingCapable, Trust (not
///        yet evaluated), Qualified (unknown until the agent signal).
///
/// `info.txt` is a file users diff and script against, so every value it prints
/// is formatted locale-independently — including the validity date, which
/// arrives as a `QDateTime` and is written back as ISO-8601 in UTC. That is the
/// opposite of the choice a dialog label makes, and deliberately so: nothing
/// parses a label back, whereas this file's whole point is that it is stable.
[[nodiscard]] QString renderCertInfoTxt(const LibreSCRS::AgentClient::CertificateInfo& cert,
                                        const RenderLabels& labels = defaultRenderLabels());

/// @brief Render the reader/card `info.txt`: reader name, capability flags,
///        pre-read auth method.
[[nodiscard]] QString renderInfoTxt(const CardPresence& presence, const RenderLabels& labels = defaultRenderLabels());

/// @brief Wrap raw DER bytes as a PEM CERTIFICATE block (64-col base64).
[[nodiscard]] QByteArray derToPem(const QByteArray& der);

/// @brief Sniff an image's true MIME from its magic bytes: JPEG2000
///        (`image/jp2`), JPEG (`image/jpeg`), PNG (`image/png`), else
///        `application/octet-stream`. No transcoding.
[[nodiscard]] QString sniffImageMime(const QByteArray& bytes);

} // namespace LibreKDE
