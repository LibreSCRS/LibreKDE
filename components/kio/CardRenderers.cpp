// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "CardRenderers.h"

#include <LibreSCRS/AgentClient/AgentCapabilities.h> // Cap::*, has()

// Short local spelling for the agent client library, as in AgentCardDataSource.
namespace Client = LibreSCRS::AgentClient;

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

QString renderCertInfoTxt(const LibreSCRS::AgentClient::CertificateInfo& cert, const RenderLabels& labels)
{
    QString out;
    const auto line = [&out](const QString& key, const QString& value) {
        out += QStringLiteral("%1: %2\n").arg(key, value);
    };

    line(labels.labelSubject, cert.subject);
    line(labels.labelIssuer, cert.issuer);
    if (cert.notAfter.isValid()) {
        // LOCALE-INDEPENDENT on purpose: Qt::ISODate is a fixed spelling that no
        // QLocale setting can alter, unlike QLocale::toString. This file is one
        // users diff and script against, so it must read the same everywhere.
        //
        // The zone half is normalized ONLY when there is a zone to normalize.
        // A certificate's validity dates are zoned in the wire format the agent
        // parses them from, and converting those to UTC gives one canonical
        // spelling instead of echoing whatever offset the producer wrote. But
        // converting a value that carries NO zone is the very instability this
        // line is avoiding: an unzoned datetime is interpreted in the reading
        // machine's local zone, so "2030-01-01T00:30:00" would come out as
        // 2029-12-31T15:30:00Z in Tokyo and 2030-01-01T08:30:00Z in Los Angeles
        // (measured). Left alone it prints its own wall clock, the same on every
        // machine — which is all an unzoned value ever meant.
        const QDateTime stamp = cert.notAfter.timeSpec() == Qt::LocalTime ? cert.notAfter : cert.notAfter.toUTC();
        line(labels.labelValidUntil, stamp.toString(Qt::ISODate));
    }

    const QStringList purposes = keyUsagePurposes(cert.keyUsageBits, labels);
    line(labels.labelPurpose, purposes.isEmpty() ? QString() : purposes.join(QStringLiteral(", ")));

    if (!cert.extendedKeyUsageOids.isEmpty()) {
        line(labels.labelExtendedKeyUsage, cert.extendedKeyUsageOids.join(QStringLiteral(", ")));
    }

    line(labels.labelSigningCapable, cert.signingCapable ? labels.valueYes : labels.valueNo);

    // The client hands back a deduced `trust` verdict, but rendering it needs a
    // localized name per verdict and none exists yet — so this line still says
    // "not yet evaluated" rather than inventing copy for a verdict it would then
    // have to translate. The verdict is deliberately NOT read from `cert.extra`
    // either: the typed member is the one source of truth.
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

    // Authoritative capability bits — the client library's mirror of the wire
    // contract (its values carry a "do not renumber" warning). PKI/eMRTD are
    // acronyms and stay untranslated.
    QStringList caps;
    if (Client::has(presence.capabilities, Client::Cap::Pki)) {
        caps << QStringLiteral("PKI");
    }
    if (Client::has(presence.capabilities, Client::Cap::IdentityData)) {
        caps << labels.capIdentity;
    }
    if (Client::has(presence.capabilities, Client::Cap::EmrtdCrypto)) {
        caps << QStringLiteral("eMRTD");
    }
    if (Client::has(presence.capabilities, Client::Cap::PinManagement)) {
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
