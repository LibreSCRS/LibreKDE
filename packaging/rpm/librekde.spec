# SPDX-License-Identifier: LGPL-2.1-or-later
Name:           librekde
Version:        0.1.0
Release:        1%{?dist}
Summary:        KDE Plasma 6 integration for LibreSCRS smart-card middleware

License:        LGPL-2.1-or-later
URL:            https://librescrs.github.io/
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.24
BuildRequires:  extra-cmake-modules >= 6.0
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-knotifications-devel
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  plasma-devel
BuildRequires:  librescrs-middleware-devel >= 4.0

Requires:       plasma-workspace
Requires:       librescrs-middleware >= 4.0

%description
Plasma 6 system-tray widget that surfaces the identity and PKI
capabilities of any LibreSCRS-supported smart card the moment it
is inserted.

%prep
%autosetup

%build
%cmake -DBUILD_TESTING=OFF
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md
%{_kf6_metainfodir}/org.librescrs.smartcard.metainfo.xml
%{_kf6_datadir}/plasma/plasmoids/org.librescrs.smartcard/
%{_kf6_qmldir}/org/librescrs/smartcard/
# Credential-management window (LIBREKDE_BUILD_CREDENTIALS, default ON): the
# executable and its NoDisplay .desktop (launched from the plasmoid, so no
# separate AppStream component).
%{_bindir}/librescrs-credentials-kde
%{_kf6_datadir}/applications/org.librescrs.credentials.desktop

%changelog
* Tue May 11 2026 hirashix0 <hirashix0@proton.me> - 0.1.0-1
- Initial release.
