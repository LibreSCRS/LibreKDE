# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0

cmake_minimum_required(VERSION 3.24.0 FATAL_ERROR)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GitVersion)

# Default to 0.1.0 when no git tag exists yet.
if(NOT DEFINED LIBREKDE_VERSION)
    set(LIBREKDE_VERSION "0.1.0")
endif()

project(LibreKDE
    VERSION ${LIBREKDE_VERSION}
    DESCRIPTION "KDE Plasma 6 integration for LibreSCRS smart-card middleware"
    HOMEPAGE_URL "https://librescrs.github.io/"
    LANGUAGES CXX
)

# ECM + KDE conventions: strict adherence to
# ECM/KDEInstallDirs/KDECMakeSettings/KDECompilerSettings.
set(KF_MIN_VERSION "6.0.0")
set(QT_MIN_VERSION "6.6.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

# KDECompilerSettings sets `-fno-exceptions` by default. LibreKDE code (and
# the Qt/KF6 APIs it consumes) uses exceptions, so re-enable them globally
# rather than per-target. Per-target `kde_enable_exceptions()` would also
# work but would duplicate the directive across every subdirectory.
add_compile_options(-fexceptions)
include(ECMQtDeclareLoggingCategory)
include(ECMInstallIcons)
include(ECMPoQmTools)

# LibreKDE is a thin client of org.librescrs.Agent over D-Bus (the shared
# `librekde-agentclient` library). The client closure links NO LibreMiddleware
# target — card I/O and secret entry live in the agent — so there is no
# `find_package(LibreMiddleware)` here.
# Qt6::Widgets is NOT in the always-on base set: it is used only by the shared
# signing seams (QInputDialog / QMessageBox in shared/signing/SignSeams.cpp). It
# is found — at ROOT directory scope, gated on a signing surface — just before
# add_subdirectory(shared) so the imported Qt6::Widgets target is visible in every
# subdirectory that transitively links librekde-signing. It MUST be found at root
# scope, NOT inside shared/signing: a subdirectory find would scope the target to
# that subtree only (QT_PROMOTE_TO_GLOBAL_TARGETS is unset by default), breaking
# every sibling consumer at generate time.
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Qml DBus Test)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS I18n CoreAddons Notifications Config)

option(LIBREKDE_BUILD_PLASMOID  "Build Plasma 6 widget"   ON)
option(LIBREKDE_BUILD_PURPOSE   "Build Purpose plugin"    ON)
option(LIBREKDE_BUILD_KRUNNER   "Build KRunner plugin"    OFF)
option(LIBREKDE_BUILD_KIO       "Build card:/ KIO worker"           ON)
option(LIBREKDE_BUILD_KWALLET   "Build KWallet backend"   OFF)
option(LIBREKDE_BUILD_CREDENTIALS "Build the credential-management window" ON)

option(BUILD_TESTING "Build tests" ON)
include(CTest)

# LibreAgent::ClientQt — the Qt client library for org.librescrs.Agent over
# D-Bus. Nothing links it yet (shared/agentclient still carries LibreKDE's own
# vendored client); this makes the shared library available to the build so a
# later phase can migrate onto it without also having to plumb its discovery.
include(cmake/FindOrUseLibreAgent.cmake)

# Qt6::Widgets at root scope (see the note above) — created whenever a signing
# surface (plasmoid or Purpose) is built, so librekde-signing and its consumers
# resolve the target from any subdirectory.
if(LIBREKDE_BUILD_PLASMOID OR LIBREKDE_BUILD_PURPOSE)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Widgets)
endif()

add_subdirectory(shared)

if(LIBREKDE_BUILD_PLASMOID)
    find_package(Plasma 6.0 REQUIRED)
    # The KIO framework provides KF6::KIOGui -> KIO::CommandLauncherJob, which the
    # plasmoid's "Manage credentials…" action uses to launch the standalone
    # credentials window with an explicit argv + startup-notification token.
    # (KIOGui is a target under the KIO umbrella, not a standalone find component.)
    # KF6::Service (KService) resolves LibreCelik via the freedesktop application
    # database for a cross-distro "is it installed?" probe (SmartCardHandler).
    find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS KIO Service)
    add_subdirectory(components/plasmoid)
endif()

if(LIBREKDE_BUILD_PURPOSE)
    find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Purpose)
    add_subdirectory(components/purpose)
endif()

if(LIBREKDE_BUILD_KIO)
    find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS KIO)
    add_subdirectory(components/kio)
endif()

if(LIBREKDE_BUILD_CREDENTIALS)
    find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Kirigami DBusAddons)
    # QApplication (org.kde.desktop QtQuick Controls style is QtWidgets-backed).
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS QuickControls2 Widgets)
    add_subdirectory(components/credentials)
endif()

# KRunner/KIO/KWallet add_subdirectory() blocks land with their respective
# future-phase plans. Their find_package() calls intentionally don't fire here
# because their option defaults are OFF.

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Translations — ki18n catalogues under po/<lang>/<domain>.po. TWO gettext
# domains (see Messages.sh):
#   - librekde                              — all C++ strings (kio worker,
#     shared signing/agentclient, Purpose, plasmoid backing lib), pinned per
#     target via TRANSLATION_DOMAIN.
#   - plasma_applet_org.librescrs.smartcard — the plasmoid package's QML
#     strings; libplasma installs a per-applet KLocalizedQmlContext with
#     exactly this domain (applet AND its config dialog), the stock Plasma
#     applet catalog layout.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/po)
    ki18n_install(po)
endif()

message(STATUS "LibreKDE ${PROJECT_VERSION} configured")
message(STATUS "  - plasmoid: ${LIBREKDE_BUILD_PLASMOID}")
message(STATUS "  - purpose : ${LIBREKDE_BUILD_PURPOSE}")
message(STATUS "  - krunner : ${LIBREKDE_BUILD_KRUNNER}")
message(STATUS "  - kio     : ${LIBREKDE_BUILD_KIO}")
message(STATUS "  - kwallet : ${LIBREKDE_BUILD_KWALLET}")
message(STATUS "  - credentials: ${LIBREKDE_BUILD_CREDENTIALS}")
