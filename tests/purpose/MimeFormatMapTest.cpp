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
