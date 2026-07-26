// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Pure renderers: image MIME byte-sniff (incl. JP2), cert info (purpose from
// keyUsage, Trust not-yet-evaluated, Qualified unknown), DER→PEM wrap.

#include "CardRenderers.h"

#include "AgentCapabilities.h"

#include <gtest/gtest.h>

using namespace LibreKDE;

TEST(CardRenderers, SniffsImageMime)
{
    EXPECT_EQ(sniffImageMime(QByteArray::fromHex("0000000C6A5020200D0A")), QStringLiteral("image/jp2"));
    EXPECT_EQ(sniffImageMime(QByteArray::fromHex("FF4FFF51")), QStringLiteral("image/jp2")); // raw codestream
    EXPECT_EQ(sniffImageMime(QByteArray::fromHex("FFD8FF")), QStringLiteral("image/jpeg"));
    EXPECT_EQ(sniffImageMime(QByteArray::fromHex("89504E47")), QStringLiteral("image/png"));
    EXPECT_EQ(sniffImageMime(QByteArrayLiteral("garbage")), QStringLiteral("application/octet-stream"));
    EXPECT_EQ(sniffImageMime(QByteArray()), QStringLiteral("application/octet-stream"));
}

TEST(CardRenderers, CertInfoShowsPurposeTrustNotEvaluatedQualifiedUnknown)
{
    CertInfoView v;
    v.subjectCn = QStringLiteral("Pera");
    v.issuerCn = QStringLiteral("MUP CA");
    v.keyUsageBits = 0x01u; // digitalSignature (ordinal 0, agent wire 1u<<0)
    v.trustStatus = 255;
    const QString txt = renderCertInfoTxt(v);
    EXPECT_TRUE(txt.contains(QStringLiteral("Purpose: Digital Signature"))) << txt.toStdString();
    EXPECT_TRUE(txt.contains(QStringLiteral("Trust: not yet evaluated"))) << txt.toStdString();
    EXPECT_TRUE(txt.contains(QStringLiteral("Qualified: unknown"))) << txt.toStdString();
    EXPECT_TRUE(txt.contains(QStringLiteral("Subject: Pera")));
    EXPECT_TRUE(txt.contains(QStringLiteral("Issuer: MUP CA")));
}

TEST(CardRenderers, KeyUsageDecodesMultipleBits)
{
    // The wire is ORDINAL-indexed (bit i == 1u<<i per X.509 KeyUsage ordinal),
    // matching the agent (LmSeams: keyUsageBits |= 1u<<ordinal) and the client
    // headers — NOT a DER-MSB-first packed BIT STRING. digitalSignature is
    // ordinal 0 (1u<<0) and keyCertSign is ordinal 5 (1u<<5).
    const quint32 mask = (1u << 0) | (1u << 5);
    const QStringList purposes = keyUsagePurposes(mask);
    EXPECT_TRUE(purposes.contains(QStringLiteral("Digital Signature")));
    EXPECT_TRUE(purposes.contains(QStringLiteral("Certificate Signing")));
    EXPECT_EQ(primaryPurpose(mask), QStringLiteral("Digital Signature"));
    EXPECT_TRUE(primaryPurpose(0u).isEmpty());
}

// Wire-convention pin: the agent emits keyUsageBits = 1u<<ordinal (source:
// agent LmSeams.cpp keyUsageBits |= (1u<<ordinal); wire doc Operation.
// Certificates1.xml "bit i per X.509 KeyUsage ordinal"). Drive an agent-encoded
// mask through the renderer and assert the human purpose — pins the wire
// convention so a DER-MSB-first decoder can never silently corrupt the Purpose
// line / cert-folder name again. This MUST fail with the old (0x80>>bit) decoder
// (which would read 1u<<0 == 0x01 as encipherOnly, ordinal 7).
TEST(CardRenderers, KeyUsageDecodesAgentOrdinalEncoding)
{
    // digitalSignature = ordinal 0 on the wire.
    EXPECT_EQ(primaryPurpose(1u << 0), QStringLiteral("Digital Signature"));
    // nonRepudiation = ordinal 1.
    EXPECT_EQ(primaryPurpose(1u << 1), QStringLiteral("Non-Repudiation"));
    // keyCertSign = ordinal 5.
    EXPECT_EQ(primaryPurpose(1u << 5), QStringLiteral("Certificate Signing"));
    // decipherOnly = ordinal 8 (the multi-byte ordinal, value 0x100).
    EXPECT_EQ(primaryPurpose(1u << 8), QStringLiteral("Decipher Only"));
    // A full keyUsage render lists exactly the set ordinals, in ordinal order.
    const QStringList all = keyUsagePurposes((1u << 0) | (1u << 8));
    EXPECT_EQ(all, (QStringList{QStringLiteral("Digital Signature"), QStringLiteral("Decipher Only")}));
}

TEST(CardRenderers, DerToPemWrapsBase64)
{
    const QByteArray pem = derToPem(QByteArray("\x30\x82", 2));
    EXPECT_TRUE(pem.startsWith("-----BEGIN CERTIFICATE-----"));
    EXPECT_TRUE(pem.trimmed().endsWith("-----END CERTIFICATE-----"));
}

TEST(CardRenderers, DerToPemWraps64Columns)
{
    // 96 DER bytes → 128 base64 chars → two 64-char lines.
    const QByteArray der(96, '\x01');
    const QByteArray pem = derToPem(der);
    const QList<QByteArray> lines = pem.split('\n');
    // lines: BEGIN, 64, 64, END, "" (trailing). The two middle lines are 64 wide.
    ASSERT_GE(lines.size(), 4);
    EXPECT_EQ(lines.at(1).size(), 64);
    EXPECT_EQ(lines.at(2).size(), 64);
}

TEST(CardRenderers, IdentityTxtGroupsAndLabels)
{
    QList<IdentityFieldView> fields{
        IdentityFieldView{QStringLiteral("personal"), QStringLiteral("given_name"), QStringLiteral("Given name"),
                          QStringLiteral("Ana")},
        IdentityFieldView{QStringLiteral("personal"), QStringLiteral("surname"), QStringLiteral("Surname"),
                          QStringLiteral("Anić")},
    };
    const QString txt = renderIdentityTxt(fields);
    EXPECT_TRUE(txt.contains(QStringLiteral("[personal]")));
    EXPECT_TRUE(txt.contains(QStringLiteral("Given name: Ana")));
    EXPECT_TRUE(txt.contains(QStringLiteral("Surname: Anić")));
}

TEST(CardRenderers, InfoTxtListsCapabilities)
{
    CardPresence p{QStringLiteral("Gemalto"), QStringLiteral("/card/0"), Cap::Pki | Cap::IdentityData,
                   QStringLiteral("Can")};
    const QString txt = renderInfoTxt(p);
    EXPECT_TRUE(txt.contains(QStringLiteral("Reader: Gemalto")));
    EXPECT_TRUE(txt.contains(QStringLiteral("PKI")));
    EXPECT_TRUE(txt.contains(QStringLiteral("Identity")));
    EXPECT_TRUE(txt.contains(QStringLiteral("Can")));
}

// eMRTD + PIN-management edges: a passport (IdentityData|EmrtdCrypto) lists eMRTD,
// and the PIN-management bit lists "PIN management". Closes the capability-label
// gap left by the bit-literal → Cap::/has() change.
TEST(CardRenderers, InfoTxtListsEmrtdAndPinManagementCapabilities)
{
    CardPresence passport{QStringLiteral("NFC"), QStringLiteral("/card/0"), Cap::IdentityData | Cap::EmrtdCrypto,
                          QStringLiteral("Mrz")};
    const QString ptxt = renderInfoTxt(passport);
    EXPECT_TRUE(ptxt.contains(QStringLiteral("Identity"))) << ptxt.toStdString();
    EXPECT_TRUE(ptxt.contains(QStringLiteral("eMRTD"))) << ptxt.toStdString();
    EXPECT_FALSE(ptxt.contains(QStringLiteral("PKI"))) << ptxt.toStdString();

    CardPresence pinCard{QStringLiteral("R"), QStringLiteral("/card/1"), Cap::Pki | Cap::PinManagement,
                         QStringLiteral("None")};
    const QString ptxt2 = renderInfoTxt(pinCard);
    EXPECT_TRUE(ptxt2.contains(QStringLiteral("PIN management"))) << ptxt2.toStdString();

    // An empty capability set renders the "none" sentinel and "None" pre-read auth.
    CardPresence empty{QStringLiteral("E"), QStringLiteral("/card/2"), Cap::None, QString()};
    const QString etxt = renderInfoTxt(empty);
    EXPECT_TRUE(etxt.contains(QStringLiteral("Capabilities: none"))) << etxt.toStdString();
    EXPECT_TRUE(etxt.contains(QStringLiteral("Pre-read authentication: None"))) << etxt.toStdString();
}
