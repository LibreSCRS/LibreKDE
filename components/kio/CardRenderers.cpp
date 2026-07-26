// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "CardRenderers.h"

#include "AgentCapabilities.h" // LibreKDE::Cap, has()

namespace LibreKDE {

namespace {

// X.509 KeyUsage ordinals (RFC 5280 4.2.1.3). The wire mask is ORDINAL-indexed:
// bit i == 1u<<i == X.509 KeyUsage ordinal i (NOT a DER-MSB-first packed BIT
// STRING). This matches the agent (LmSeams: keyUsageBits |= 1u<<ordinal), the
// wire doc (Operation.Certificates1.xml "bit i per X.509 KeyUsage ordinal"), and
// the client headers (AgentOperation.h / CardDataSource.h). The ordinals:
//   0 digitalSignature, 1 nonRepudiation/contentCommitment, 2 keyEncipherment,
//   3 dataEncipherment, 4 keyAgreement, 5 keyCertSign, 6 cRLSign,
//   7 encipherOnly, 8 decipherOnly (value 0x100).
constexpr int kKeyUsageCount = 9;

// English fallback names (the agent/worker localizes via RenderLabels).
const char* const kKeyUsageEnglish[kKeyUsageCount] = {
    "Digital Signature",   "Non-Repudiation", "Key Encipherment", "Data Encipherment", "Key Agreement",
    "Certificate Signing", "CRL Signing",     "Encipher Only",    "Decipher Only",
};

// True when the X.509 KeyUsage bit for ordinal @p ordinal is set. The wire mask
// is ordinal-indexed (bit i == 1u<<i == X.509 KeyUsage ordinal i), so the test is
// a direct shift — correct for all ordinals 0-8 including the multi-byte
// ordinal-8 decipherOnly (1u<<8 == 0x100).
bool keyUsageBitSet(quint32 mask, int ordinal)
{
    return (mask & (1u << static_cast<unsigned>(ordinal))) != 0u;
}

QString usageName(int ordinal, const RenderLabels& labels)
{
    if (ordinal >= 0 && ordinal < labels.keyUsageNames.size() && !labels.keyUsageNames.at(ordinal).isEmpty()) {
        return labels.keyUsageNames.at(ordinal);
    }
    if (ordinal >= 0 && ordinal < kKeyUsageCount) {
        return QString::fromUtf8(kKeyUsageEnglish[ordinal]);
    }
    return {};
}

} // namespace

RenderLabels defaultRenderLabels()
{
    RenderLabels l;
    l.keyUsageNames.reserve(kKeyUsageCount);
    for (int i = 0; i < kKeyUsageCount; ++i) {
        l.keyUsageNames << QString::fromUtf8(kKeyUsageEnglish[i]);
    }
    l.trustNotEvaluated = QStringLiteral("not yet evaluated");
    l.qualifiedUnknown = QStringLiteral("unknown");

    l.labelSubject = QStringLiteral("Subject");
    l.labelIssuer = QStringLiteral("Issuer");
    l.labelValidUntil = QStringLiteral("Valid until");
    l.labelPurpose = QStringLiteral("Purpose");
    l.labelExtendedKeyUsage = QStringLiteral("Extended Key Usage");
    l.labelSigningCapable = QStringLiteral("Signing capable");
    l.labelTrust = QStringLiteral("Trust");
    l.labelQualified = QStringLiteral("Qualified");
    l.labelChain = QStringLiteral("Chain");
    l.labelReader = QStringLiteral("Reader");
    l.labelCapabilities = QStringLiteral("Capabilities");
    l.labelPreReadAuth = QStringLiteral("Pre-read authentication");

    l.valueYes = QStringLiteral("yes");
    l.valueNo = QStringLiteral("no");
    l.valueNone = QStringLiteral("none");
    l.valueAuthNone = QStringLiteral("None");
    l.capIdentity = QStringLiteral("Identity");
    l.capPinManagement = QStringLiteral("PIN management");
    return l;
}

QStringList keyUsagePurposes(quint32 keyUsageBits, const RenderLabels& labels)
{
    QStringList out;
    for (int i = 0; i < kKeyUsageCount; ++i) {
        if (keyUsageBitSet(keyUsageBits, i)) {
            out << usageName(i, labels);
        }
    }
    return out;
}

QString primaryPurpose(quint32 keyUsageBits, const RenderLabels& labels)
{
    for (int i = 0; i < kKeyUsageCount; ++i) {
        if (keyUsageBitSet(keyUsageBits, i)) {
            return usageName(i, labels);
        }
    }
    return {};
}

QString renderIdentityTxt(const QList<IdentityFieldView>& fields)
{
    QString out;
    QString currentGroup;
    for (const IdentityFieldView& f : fields) {
        if (f.group != currentGroup) {
            currentGroup = f.group;
            if (!out.isEmpty()) {
                out += QLatin1Char('\n');
            }
            out += QStringLiteral("[%1]\n").arg(currentGroup);
        }
        const QString label = f.labelFallback.isEmpty() ? f.fieldKey : f.labelFallback;
        out += QStringLiteral("%1: %2\n").arg(label, f.value);
    }
    return out;
}

QString renderCertInfoTxt(const CertInfoView& cert, const RenderLabels& labels)
{
    QString out;
    const auto line = [&out](const QString& key, const QString& value) {
        out += QStringLiteral("%1: %2\n").arg(key, value);
    };

    line(labels.labelSubject, cert.subjectCn);
    line(labels.labelIssuer, cert.issuerCn);
    if (!cert.notAfter.isEmpty()) {
        line(labels.labelValidUntil, cert.notAfter);
    }

    const QStringList purposes = keyUsagePurposes(cert.keyUsageBits, labels);
    line(labels.labelPurpose, purposes.isEmpty() ? QString() : purposes.join(QStringLiteral(", ")));

    if (!cert.extendedKeyUsageOids.isEmpty()) {
        line(labels.labelExtendedKeyUsage, cert.extendedKeyUsageOids.join(QStringLiteral(", ")));
    }

    line(labels.labelSigningCapable, cert.signingCapable ? labels.valueYes : labels.valueNo);

    // trustStatus is Unknown (255) until the trust verdict — never imply a
    // Trusted/Untrusted verdict before the trust evaluation lands.
    line(labels.labelTrust, labels.trustNotEvaluated);
    // No agent qualified/QSCD signal yet (deferred) — always "unknown".
    line(labels.labelQualified, labels.qualifiedUnknown);

    if (!cert.chainSubjectCns.isEmpty()) {
        line(labels.labelChain, cert.chainSubjectCns.join(QStringLiteral(" -> ")));
    }
    return out;
}

QString renderInfoTxt(const CardPresence& presence, const RenderLabels& labels)
{
    QString out;
    const auto line = [&out](const QString& key, const QString& value) {
        out += QStringLiteral("%1: %2\n").arg(key, value);
    };
    line(labels.labelReader, presence.readerName);

    // Authoritative capability bits — see AgentCapabilities.h (the values carry a
    // "do not renumber" warning). PKI/eMRTD are acronyms and stay untranslated.
    QStringList caps;
    if (has(presence.capabilities, Cap::Pki)) {
        caps << QStringLiteral("PKI");
    }
    if (has(presence.capabilities, Cap::IdentityData)) {
        caps << labels.capIdentity;
    }
    if (has(presence.capabilities, Cap::EmrtdCrypto)) {
        caps << QStringLiteral("eMRTD");
    }
    if (has(presence.capabilities, Cap::PinManagement)) {
        caps << labels.capPinManagement;
    }
    line(labels.labelCapabilities, caps.isEmpty() ? labels.valueNone : caps.join(QStringLiteral(", ")));
    line(labels.labelPreReadAuth, presence.preReadAuth.isEmpty() ? labels.valueAuthNone : presence.preReadAuth);
    return out;
}

QByteArray derToPem(const QByteArray& der)
{
    QByteArray b64 = der.toBase64();
    QByteArray pem = QByteArrayLiteral("-----BEGIN CERTIFICATE-----\n");
    for (int i = 0; i < b64.size(); i += 64) {
        pem += b64.mid(i, 64);
        pem += '\n';
    }
    pem += QByteArrayLiteral("-----END CERTIFICATE-----\n");
    return pem;
}

QString sniffImageMime(const QByteArray& bytes)
{
    // JPEG2000: the JP2 signature box "\x00\x00\x00\x0C jP  \r\n\x87\n", or the
    // raw codestream SOC marker FF 4F FF 51.
    static const QByteArray jp2Box = QByteArray::fromHex("0000000C6A5020200D0A");
    static const QByteArray jp2Codestream = QByteArray::fromHex("FF4FFF51");
    if (bytes.startsWith(jp2Box) || bytes.startsWith(jp2Codestream)) {
        return QStringLiteral("image/jp2");
    }
    if (bytes.startsWith(QByteArray::fromHex("FFD8FF"))) {
        return QStringLiteral("image/jpeg");
    }
    if (bytes.startsWith(QByteArray::fromHex("89504E47"))) {
        return QStringLiteral("image/png");
    }
    return QStringLiteral("application/octet-stream");
}

} // namespace LibreKDE
