%global _lto_cflags %{nil}

Name:           librekde
Version:        5.0.0
Release:        1%{?dist}
Summary:        KDE Plasma 6 integration for the LibreSCRS smart-card stack

License:        LGPL-2.1-or-later
URL:            https://github.com/LibreSCRS/LibreKDE
Source0:        %{name}-%{version}.tar.gz

ExclusiveArch:  x86_64

BuildRequires:  cmake >= 3.24
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  pkgconf-pkg-config
BuildRequires:  git
BuildRequires:  extra-cmake-modules >= 6.0
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qttools-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-knotifications-devel
BuildRequires:  kf6-kconfig-devel
BuildRequires:  kf6-kio-devel
BuildRequires:  kf6-purpose-devel
BuildRequires:  kf6-kirigami-devel
BuildRequires:  kf6-kdbusaddons-devel
# libplasma-devel, not plasma-devel. The latter is not a package name on this
# distribution and the recipe carried it for a while regardless.
BuildRequires:  libplasma-devel
BuildRequires:  gtest-devel
BuildRequires:  dbus-daemon
BuildRequires:  python3
# LibreKDE links no middleware target of its own: it talks to the agent through
# the Qt client library, and that is what its version floor applies to.
BuildRequires:  librescrs-agent-client-qt-devel >= 5.0

%description
Plasma 6 integration for LibreSCRS: a system-tray plasmoid, a card:/ KIO
worker, a Purpose "Sign" plugin and a credential-management window.

The plasmoid, the KIO worker and the Purpose plugin are compiled objects that
live in system plugin directories, so no bundle format can deliver them.

%package common
Summary:        Shared data for the LibreSCRS KDE integration
BuildArch:      noarch

%description common
Message catalogues and AppStream metadata shared by the other packages.

%package plasmoid
Summary:        LibreSCRS smart-card plasmoid for KDE Plasma 6
Requires:       %{name}-common = %{version}-%{release}
Requires:       plasma-workspace
Requires:       librescrs-agent-client-qt%{?_isa} >= 5.0
Recommends:     librescrs-agent >= 5.0

%description plasmoid
A system-tray widget that shows what is on a LibreSCRS-supported card the
moment it is inserted.

%package kio
Summary:        card:/ KIO worker for the LibreSCRS KDE integration
Requires:       %{name}-common = %{version}-%{release}
Requires:       librescrs-agent-client-qt%{?_isa} >= 5.0

%description kio
Browse the contents of an inserted card from any KIO-aware application.

%package purpose
Summary:        Purpose "Sign" plugin for the LibreSCRS KDE integration
Requires:       %{name}-common = %{version}-%{release}
Requires:       librescrs-agent-client-qt%{?_isa} >= 5.0

%description purpose
Adds a Sign action to the KDE share menu.

%package credentials
Summary:        Credential-management window for LibreSCRS cards
Requires:       %{name}-common = %{version}-%{release}
Requires:       librescrs-agent-client-qt%{?_isa} >= 5.0

%description credentials
Change a PIN, unblock one with a PUK, and see how many attempts are left.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -GNinja -DBUILD_TESTING=OFF -DINSTALL_GTEST=OFF
%cmake_build

%install
%cmake_install
%find_lang librekde || :

%files common
%license LICENSE
%{_datadir}/locale/*/LC_MESSAGES/librekde.mo
%{_metainfodir}/org.librescrs.smartcard.metainfo.xml
%{_metainfodir}/org.librescrs.librekde.purpose.metainfo.xml

%files plasmoid
%{_datadir}/plasma/plasmoids/org.librescrs.smartcard/
%{_datadir}/locale/*/LC_MESSAGES/plasma_applet_org.librescrs.smartcard.mo
# %{_qt6_prefix} is the install prefix, not the QML root: it resolved to /usr/qml,
# a directory nothing creates. The QML root follows the library directory.
%{_libdir}/qt6/qml/org/librescrs/smartcard/

%files kio
%{_qt6_plugindir}/kf6/kio/

%files purpose
%{_qt6_plugindir}/kf6/purpose/

%files credentials
%{_bindir}/librescrs-credentials-kde
%{_datadir}/applications/org.librescrs.credentials.desktop

%changelog
* Fri Sep 04 2026 LibreSCRS <librescrs@proton.me> - 5.0.0-1
- Five binary packages, replacing a recipe that packaged two of them and named
  the wrong dependency for all of them.
