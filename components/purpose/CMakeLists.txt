# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# The Purpose "Sign with LibreSCRS Smart Card" Share-menu plugin: a thin KF6
# Purpose shell over the shared LibreKDE::SignJob (librekde-signing). A client of
# org.librescrs.Agent: it forwards an input fd + a non-secret {format,packaging}
# choice to Card1.Sign via librekde-agentclient and writes the returned artifact
# next to the input. NO card I/O, NO secrets, NO LibreMiddleware.

set(CMAKE_AUTOMOC ON)

# The Purpose plugin shim: a kcoreaddons plugin under kf6/purpose that Purpose's
# loader dlopens. createJob() hands the host a SignPurposeJob (a Purpose::Job).
# Signing, the dialog seams, and the logging category all live in librekde-signing
# now, so this plugin is a thin shell with NO direct Qt6::Widgets link (it still
# links Qt6::Widgets TRANSITIVELY via librekde-signing's static-lib LINK_ONLY
# propagation, but instantiates no QWidget of its own).
kcoreaddons_add_plugin(librescrs_sign_purpose
    INSTALL_NAMESPACE "kf6/purpose"
    SOURCES
        librescrs_sign_purpose.h
        librescrs_sign_purpose.cpp
)
target_link_libraries(librescrs_sign_purpose
    PRIVATE
        Qt6::Core Qt6::DBus
        KF6::I18n KF6::CoreAddons
        KF6::Purpose
        librekde-agentclient
        librekde-signing
)
target_compile_features(librescrs_sign_purpose PRIVATE cxx_std_23)
# Pin the plugin shim's own bare i18nc() (the SignPurposeJob status strings) to
# "librekde" — dlopen-into-arbitrary-host rationale (never setApplicationDomain
# from a plugin: that would clobber the host's global domain).
target_compile_definitions(librescrs_sign_purpose PRIVATE TRANSLATION_DOMAIN="librekde")

# AppStream metainfo describing the Share action.
install(FILES org.librescrs.librekde.purpose.metainfo.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR}
)
