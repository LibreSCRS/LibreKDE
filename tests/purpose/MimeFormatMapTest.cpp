// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0

#include "MimeFormatMap.h"

#include <gtest/gtest.h>

using namespace LibreKDE;

// --- format/packaging choice ----------

TEST(MimeFormatMap, PdfIsPadesEnveloped)
{
    const SignChoice c = MimeFormatMap::resolve(QStringLiteral("application/pdf"));
    EXPECT_EQ(c.format, QStringLiteral("pades"));
    EXPECT_EQ(c.packaging, QStringLiteral("enveloped"));
    EXPECT_TRUE(c.enveloped());
}

TEST(MimeFormatMap, XmlIsXadesDetached)
{
    const SignChoice c = MimeFormatMap::resolve(QStringLiteral("application/xml"));
    EXPECT_EQ(c.format, QStringLiteral("xades"));
    EXPECT_EQ(c.packaging, QStringLiteral("detached"));
}

TEST(MimeFormatMap, TextXmlAliasIsXades)
{
    EXPECT_EQ(MimeFormatMap::resolve(QStringLiteral("text/xml")).format, QStringLiteral("xades"));
}

TEST(MimeFormatMap, JsonIsJadesDetached)
{
    const SignChoice c = MimeFormatMap::resolve(QStringLiteral("application/json"));
    EXPECT_EQ(c.format, QStringLiteral("jades"));
    EXPECT_EQ(c.packaging, QStringLiteral("detached"));
}

TEST(MimeFormatMap, AsicEIsAsiceDetached)
{
    // Wire string is `asice` (NO hyphen) per Card1.xml; the MIME is the hyphened
    // ASiC-E container type.
    const SignChoice c = MimeFormatMap::resolve(QStringLiteral("application/vnd.etsi.asic-e+zip"));
    EXPECT_EQ(c.format, QStringLiteral("asice"));
    EXPECT_EQ(c.packaging, QStringLiteral("detached"));
}

TEST(MimeFormatMap, UnknownFallsBackToCadesDetached)
{
    const SignChoice c = MimeFormatMap::resolve(QStringLiteral("application/octet-stream"));
    EXPECT_EQ(c.format, QStringLiteral("cades"));
    EXPECT_EQ(c.packaging, QStringLiteral("detached"));
    EXPECT_FALSE(c.enveloped());
}

TEST(MimeFormatMap, EmptyMimeFallsBackToCades)
{
    EXPECT_EQ(MimeFormatMap::resolve(QString()).format, QStringLiteral("cades"));
}

// --- explicit format override (packaging consistency) ----------------------

TEST(MimeFormatMap, ResolveFormatPadesIsEnveloped)
{
    const SignChoice c = MimeFormatMap::resolveFormat(QStringLiteral("pades"));
    EXPECT_EQ(c.format, QStringLiteral("pades"));
    EXPECT_EQ(c.packaging, QStringLiteral("enveloped"));
    EXPECT_TRUE(c.enveloped());
}

TEST(MimeFormatMap, ResolveFormatCadesIsDetached)
{
    const SignChoice c = MimeFormatMap::resolveFormat(QStringLiteral("cades"));
    EXPECT_EQ(c.format, QStringLiteral("cades"));
    EXPECT_EQ(c.packaging, QStringLiteral("detached"));
    EXPECT_FALSE(c.enveloped());
}

// A cross-family override (PDF would resolve to pades/enveloped) must NOT leave
// the MIME-derived `enveloped` packaging when overridden to a detached family —
// the override re-derives packaging from its own family, keeping format,
// packaging and output name consistent.
TEST(MimeFormatMap, ResolveFormatCrossFamilyOverrideRepackagesAsDetached)
{
    const SignChoice c = MimeFormatMap::resolveFormat(QStringLiteral("xades"));
    EXPECT_EQ(c.format, QStringLiteral("xades"));
    EXPECT_EQ(c.packaging, QStringLiteral("detached"));
}

// --- output naming --------------------------------------------

TEST(MimeFormatMap, EnvelopedPdfYieldsNameSigned)
{
    const SignChoice c = MimeFormatMap::resolve(QStringLiteral("application/pdf"));
    EXPECT_EQ(MimeFormatMap::outputName(QStringLiteral("report.pdf"), c), QStringLiteral("report-signed.pdf"));
}

TEST(MimeFormatMap, EnvelopedKeepsCompoundStem)
{
    const SignChoice c = MimeFormatMap::resolve(QStringLiteral("application/pdf"));
    EXPECT_EQ(MimeFormatMap::outputName(QStringLiteral("2026.q1.report.pdf"), c),
              QStringLiteral("2026.q1.report-signed.pdf"));
}

TEST(MimeFormatMap, CadesDetachedYieldsP7s)
{
    const SignChoice c = MimeFormatMap::resolve(QStringLiteral("application/octet-stream"));
    EXPECT_EQ(MimeFormatMap::outputName(QStringLiteral("data.bin"), c), QStringLiteral("data.bin.p7s"));
}

TEST(MimeFormatMap, XadesDetachedYieldsXmlSidecar)
{
    const SignChoice c = MimeFormatMap::resolve(QStringLiteral("application/xml"));
    EXPECT_EQ(MimeFormatMap::outputName(QStringLiteral("invoice.xml"), c), QStringLiteral("invoice.xml.xml"));
}

TEST(MimeFormatMap, JadesDetachedYieldsJsonSidecar)
{
    const SignChoice c = MimeFormatMap::resolve(QStringLiteral("application/json"));
    EXPECT_EQ(MimeFormatMap::outputName(QStringLiteral("payload.json"), c), QStringLiteral("payload.json.json"));
}

TEST(MimeFormatMap, AsiceDetachedYieldsP7sFallbackNaming)
{
    // ASiC-E is a detached container; it has no CAdES/.p7s, XAdES/.xml or
    // JAdES/.json sidecar rule, so it follows the generic detached
    // convention: the container itself is re-emitted alongside the input under a
    // `-signed` stem (enveloped-style naming is reserved for in-place embeds; an
    // ASiC-E artifact is a fresh container, so we name it `name-signed.<ext>`).
    const SignChoice c = MimeFormatMap::resolve(QStringLiteral("application/vnd.etsi.asic-e+zip"));
    EXPECT_EQ(MimeFormatMap::outputName(QStringLiteral("bundle.asice"), c), QStringLiteral("bundle-signed.asice"));
}

TEST(MimeFormatMap, EnvelopedNoExtensionAppendsSignedSuffix)
{
    const SignChoice c = MimeFormatMap::resolve(QStringLiteral("application/pdf"));
    EXPECT_EQ(MimeFormatMap::outputName(QStringLiteral("README"), c), QStringLiteral("README-signed"));
}
