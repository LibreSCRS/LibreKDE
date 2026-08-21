// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
//
// Pure renderers: image MIME byte-sniff (incl. JP2), cert info (purpose from
// keyUsage, Trust not-yet-evaluated, Qualified unknown), DER→PEM wrap.

#include "CardRenderers.h"

#include <LibreSCRS/AgentClient/AgentCapabilities.h>

#include <QDateTime>
#include <QLocale>
#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

namespace Client = LibreSCRS::AgentClient;

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
    LibreSCRS::AgentClient::CertificateInfo v;
    v.subject = QStringLiteral("Pera");
    v.issuer = QStringLiteral("MUP CA");
    v.keyUsageBits = 0x01u; // digitalSignature (ordinal 0, agent wire 1u<<0)
    v.trust = LibreSCRS::AgentClient::TrustStatus::Unknown;
    const QString txt = renderCertInfoTxt(v);
    EXPECT_TRUE(txt.contains(QStringLiteral("Purpose: Digital Signature"))) << txt.toStdString();
    EXPECT_TRUE(txt.contains(QStringLiteral("Trust: not yet evaluated"))) << txt.toStdString();
    EXPECT_TRUE(txt.contains(QStringLiteral("Qualified: unknown"))) << txt.toStdString();
    EXPECT_TRUE(txt.contains(QStringLiteral("Subject: Pera")));
    EXPECT_TRUE(txt.contains(QStringLiteral("Issuer: MUP CA")));
}

// The validity line in a cert info.txt is a STABLE FILE's contents: users diff
// and script against it, so it must read the same under every locale and every
// reader's time zone. Both halves are asserted here, because both can drift:
// the FORMAT (a locale-aware formatter renders "1. 1. 2030. 00:00" under a
// Serbian locale) and the ZONE SPELLING (a renderer that does not normalize
// echoes whatever offset the producer happened to write, so the same instant
// reaches two users as two different strings).
//
// The zone half needs an input deliberately offset from UTC to show up at all:
// 01:00+01:00 is 00:00Z, and only a normalizing renderer prints the Z form.
TEST(CardRenderers, CertInfoValidUntilIsIsoUtcRegardlessOfLocaleAndZone)
{
    LibreSCRS::AgentClient::CertificateInfo v;
    v.notAfter = QDateTime::fromString(QStringLiteral("2030-01-01T01:00:00+01:00"), Qt::ISODate);
    ASSERT_TRUE(v.notAfter.isValid());

    const QString expected = QStringLiteral("Valid until: 2030-01-01T00:00:00Z");
    EXPECT_TRUE(renderCertInfoTxt(v).contains(expected)) << renderCertInfoTxt(v).toStdString();

    // Same assertion with a non-English default locale installed in-process. A
    // renderer that reached for QLocale would change its output here; this one
    // must not move a byte.
    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale(QStringLiteral("sr_RS")));
    const QString underOtherLocale = renderCertInfoTxt(v);
    QLocale::setDefault(previous);
    EXPECT_TRUE(underOtherLocale.contains(expected)) << underOtherLocale.toStdString();

    // An absent validity date prints no line at all (rather than an empty one).
    LibreSCRS::AgentClient::CertificateInfo noDate;
    EXPECT_FALSE(renderCertInfoTxt(noDate).contains(QStringLiteral("Valid until")));
}

// The counterpart the test above cannot cover, and the one that makes
// normalizing to UTC conditional rather than unconditional: a datetime carrying
// NO zone at all. There is nothing to normalize, and converting it anyway would
// interpret it in the READING machine's zone — turning a fixed wall clock into
// three different instants on three desks. It must print itself, verbatim.
//
// Drive it with a time deliberately close to midnight, since that is where an
// unwanted conversion changes the DATE and not merely the clock. Measured
// against an unconditionally-normalizing renderer: 2029-12-31T15:30:00Z under
// TZ=Asia/Tokyo, 2030-01-01T08:30:00Z under TZ=America/Los_Angeles — and even
// under TZ=UTC it fails, because normalizing stamps a "Z" this value never had.
TEST(CardRenderers, CertInfoValidUntilLeavesAnUnzonedDateAlone)
{
    LibreSCRS::AgentClient::CertificateInfo v;
    v.notAfter = QDateTime::fromString(QStringLiteral("2030-01-01T00:30:00"), Qt::ISODate);
    ASSERT_TRUE(v.notAfter.isValid());
    ASSERT_EQ(v.notAfter.timeSpec(), Qt::LocalTime) << "fixture no longer exercises the unzoned case";

    EXPECT_TRUE(renderCertInfoTxt(v).contains(QStringLiteral("Valid until: 2030-01-01T00:30:00\n")))
        << renderCertInfoTxt(v).toStdString();
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
    // digitalSignature WITHOUT nonRepudiation is an authentication certificate,
    // whatever else is set alongside it.
    EXPECT_EQ(primaryPurpose(mask), QStringLiteral("Authentication"));
    EXPECT_TRUE(primaryPurpose(0u).isEmpty());
}

// The reported defect, pinned: an eID AUTHENTICATION certificate carries
// digitalSignature + keyEncipherment and no nonRepudiation, and used to be
// listed in card:/ as the folder "Digital Signature (…)" — the name of the
// certificate the holder signs documents with. A signing certificate is the one
// bearing nonRepudiation (contentCommitment), and it keeps that name.
TEST(CardRenderers, AuthenticationCertIsNotNamedAfterTheSigningOne)
{
    const quint32 authentication = (1u << 0) | (1u << 2); // digitalSignature + keyEncipherment
    EXPECT_EQ(primaryPurpose(authentication), QStringLiteral("Authentication"));

    const quint32 signing = (1u << 0) | (1u << 1); // digitalSignature + nonRepudiation
    EXPECT_EQ(primaryPurpose(signing), QStringLiteral("Digital Signature"));

    // The full KeyUsage render is unchanged: it names every set bit, in order.
    EXPECT_EQ(keyUsagePurposes(authentication),
              (QStringList{QStringLiteral("Digital Signature"), QStringLiteral("Key Encipherment")}));
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
    // digitalSignature = ordinal 0 on the wire -> authentication, not signing.
    EXPECT_EQ(primaryPurpose(1u << 0), QStringLiteral("Authentication"));
    // nonRepudiation = ordinal 1 -> the signing certificate.
    EXPECT_EQ(primaryPurpose(1u << 1), QStringLiteral("Digital Signature"));
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

// identity.txt carries ONE `[group]` header per group, whatever order the rows
// arrive in. The renderer used to emit a header every time the group changed
// from the previous row (a run-length grouper), so a field list that revisits a
// group — two rows of "personal", one "document", another "personal" — printed
// `[personal]` TWICE and split one group across two sections of the file.
// Today's producer cannot deliver such a list (the wire type is a map, so group
// keys are unique and the rows arrive contiguous), but this renderer is public
// and takes a plain list, so its output shape must not depend on the order its
// caller happens to walk in.
TEST(CardRenderers, IdentityTxtEmitsOneHeaderPerGroupWhenGroupsInterleave)
{
    QList<IdentityFieldView> fields{
        IdentityFieldView{QStringLiteral("personal"), QStringLiteral("given_name"), QStringLiteral("Given name"),
                          QStringLiteral("Ana")},
        IdentityFieldView{QStringLiteral("document"), QStringLiteral("document_number"),
                          QStringLiteral("Document number"), QStringLiteral("T00000000")},
        IdentityFieldView{QStringLiteral("personal"), QStringLiteral("surname"), QStringLiteral("Surname"),
                          QStringLiteral("Anić")},
    };
    const QString txt = renderIdentityTxt(fields);
    EXPECT_EQ(txt.count(QStringLiteral("[personal]")), 1) << txt.toStdString();
    EXPECT_EQ(txt.count(QStringLiteral("[document]")), 1) << txt.toStdString();
    // Both "personal" rows sit under the single "personal" header, i.e. ahead of
    // the "document" one: the group is contiguous in the output even though it
    // was not in the input.
    EXPECT_LT(txt.indexOf(QStringLiteral("Surname: Anić")), txt.indexOf(QStringLiteral("[document]")))
        << txt.toStdString();
    EXPECT_LT(txt.indexOf(QStringLiteral("[personal]")), txt.indexOf(QStringLiteral("Given name: Ana")))
        << txt.toStdString();
}

// The invariant behind the case above, asserted over EVERY permutation of a
// four-row two-group list rather than one hand-picked arrangement: whatever
// order the rows arrive in, each group gets exactly one header and every row is
// rendered exactly once. This is the fence against a future renderer that is
// merely accidentally right for the order today's agent happens to emit.
TEST(CardRenderers, IdentityTxtHoldsOneHeaderPerGroupUnderEveryRowOrder)
{
    QList<IdentityFieldView> fields{
        IdentityFieldView{QStringLiteral("personal"), QStringLiteral("given_name"), QStringLiteral("Given name"),
                          QStringLiteral("Ana")},
        IdentityFieldView{QStringLiteral("document"), QStringLiteral("document_number"),
                          QStringLiteral("Document number"), QStringLiteral("T00000000")},
        IdentityFieldView{QStringLiteral("personal"), QStringLiteral("surname"), QStringLiteral("Surname"),
                          QStringLiteral("Anić")},
        IdentityFieldView{QStringLiteral("document"), QStringLiteral("expiry_date"), QStringLiteral("Expiry"),
                          QStringLiteral("01.01.2030")},
    };
    // Permute by index so the fixture above stays the single source of the rows.
    std::vector<int> order{0, 1, 2, 3};
    std::sort(order.begin(), order.end());
    int permutations = 0;
    do {
        QList<IdentityFieldView> permuted;
        permuted.reserve(fields.size());
        for (const int i : order) {
            permuted << fields.at(i);
        }
        const QString txt = renderIdentityTxt(permuted);
        ++permutations;
        EXPECT_EQ(txt.count(QStringLiteral("[personal]")), 1) << txt.toStdString();
        EXPECT_EQ(txt.count(QStringLiteral("[document]")), 1) << txt.toStdString();
        for (const IdentityFieldView& f : fields) {
            const QString line = QStringLiteral("%1: %2\n").arg(f.labelFallback, f.value);
            EXPECT_EQ(txt.count(line), 1) << line.toStdString() << " in\n" << txt.toStdString();
        }
    } while (std::next_permutation(order.begin(), order.end()));
    EXPECT_EQ(permutations, 24) << "the permutation walk did not cover 4!";
}

TEST(CardRenderers, InfoTxtListsCapabilities)
{
    CardPresence p{QStringLiteral("Gemalto"), QStringLiteral("/card/0"), Client::Cap::Pki | Client::Cap::IdentityData,
                   QStringLiteral("Can")};
    const QString txt = renderInfoTxt(p);
    EXPECT_TRUE(txt.contains(QStringLiteral("Reader: Gemalto")));
    EXPECT_TRUE(txt.contains(QStringLiteral("PKI")));
    EXPECT_TRUE(txt.contains(QStringLiteral("Identity")));
    EXPECT_TRUE(txt.contains(QStringLiteral("Can")));
}

// eMRTD + PIN-management edges: a passport (IdentityData|EmrtdCrypto) lists eMRTD,
// and the PIN-management bit lists "PIN management". Closes the capability-label
// gap left by the bit-literal → Client::Cap::/has() change.
TEST(CardRenderers, InfoTxtListsEmrtdAndPinManagementCapabilities)
{
    CardPresence passport{QStringLiteral("NFC"), QStringLiteral("/card/0"),
                          Client::Cap::IdentityData | Client::Cap::EmrtdCrypto, QStringLiteral("Mrz")};
    const QString ptxt = renderInfoTxt(passport);
    EXPECT_TRUE(ptxt.contains(QStringLiteral("Identity"))) << ptxt.toStdString();
    EXPECT_TRUE(ptxt.contains(QStringLiteral("eMRTD"))) << ptxt.toStdString();
    EXPECT_FALSE(ptxt.contains(QStringLiteral("PKI"))) << ptxt.toStdString();

    CardPresence pinCard{QStringLiteral("R"), QStringLiteral("/card/1"), Client::Cap::Pki | Client::Cap::PinManagement,
                         QStringLiteral("None")};
    const QString ptxt2 = renderInfoTxt(pinCard);
    EXPECT_TRUE(ptxt2.contains(QStringLiteral("PIN management"))) << ptxt2.toStdString();

    // An empty capability set renders the "none" sentinel and "None" pre-read auth.
    CardPresence empty{QStringLiteral("E"), QStringLiteral("/card/2"), Client::Cap::None, QString()};
    const QString etxt = renderInfoTxt(empty);
    EXPECT_TRUE(etxt.contains(QStringLiteral("Capabilities: none"))) << etxt.toStdString();
    EXPECT_TRUE(etxt.contains(QStringLiteral("Pre-read authentication: None"))) << etxt.toStdString();
}
