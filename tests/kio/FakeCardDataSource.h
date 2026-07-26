// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 hirashix0
#pragma once

#include "CardDataSource.h"

/// @file
/// @brief In-memory `CardDataSource` for the pure CardTree / worker tests, with
///        an I/O-call counter so the lazy-PACE regression test can assert that
///        listing/stat/mimeType issue ZERO card reads.

namespace LibreKDETest {

class FakeCardDataSource : public LibreKDE::CardDataSource
{
public:
    explicit FakeCardDataSource(QList<LibreKDE::CardPresence> present) : m_present(std::move(present)) {}

    [[nodiscard]] QList<LibreKDE::CardPresence> listReadersWithCards() override
    {
        ++m_listCalls;
        return m_present;
    }

    [[nodiscard]] LibreKDE::IdentityResult readIdentity(const QString&) override
    {
        ++m_ioCalls;
        return m_identity;
    }
    [[nodiscard]] LibreKDE::CertListResult readCertificates(const QString&) override
    {
        ++m_ioCalls;
        return m_certs;
    }
    [[nodiscard]] LibreKDE::PhotoResult getPhoto(const QString&) override
    {
        ++m_ioCalls;
        return m_photo;
    }
    [[nodiscard]] LibreKDE::CertDerResult getCertificateDer(const QString&, const QString&) override
    {
        ++m_ioCalls;
        return m_der; // default NotAvailable
    }

    /// @brief Count of CARD-READ calls (readIdentity/readCertificates/getPhoto/
    ///        getCertificateDer). listReadersWithCards is NOT counted — it is the
    ///        zero-I/O registry read CardTree is allowed to use.
    [[nodiscard]] int ioCallCount() const
    {
        return m_ioCalls;
    }
    [[nodiscard]] int listCallCount() const
    {
        return m_listCalls;
    }

    void setIdentity(LibreKDE::IdentityResult r)
    {
        m_identity = std::move(r);
    }
    void setCertificates(LibreKDE::CertListResult r)
    {
        m_certs = std::move(r);
    }
    void setPhoto(LibreKDE::PhotoResult r)
    {
        m_photo = std::move(r);
    }
    void setCertDer(LibreKDE::CertDerResult r)
    {
        m_der = std::move(r);
    }

private:
    QList<LibreKDE::CardPresence> m_present;
    LibreKDE::IdentityResult m_identity;
    LibreKDE::CertListResult m_certs;
    LibreKDE::PhotoResult m_photo;
    LibreKDE::CertDerResult m_der;
    int m_ioCalls = 0;
    int m_listCalls = 0;
};

} // namespace LibreKDETest
