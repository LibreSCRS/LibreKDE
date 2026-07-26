#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 hirashix0
#
# KDE l10n extraction — TWO gettext domains:
#
#   librekde
#     Every C++ i18n*/ki18nd* string across the shipped components and shared
#     libraries (each target pins TRANSLATION_DOMAIN="librekde"). C++-only:
#     xgettext runs these in C++ mode.
#
#   plasma_applet_org.librescrs.smartcard
#     The plasmoid package's QML strings. libplasma installs a per-applet
#     KLocalizedQmlContext with exactly this domain (Applet::translationDomain()
#     = "plasma_applet_" + pluginId, for the applet and its config dialog), so
#     bare QML i18n*() resolves here — the stock Plasma applet catalog layout.
#     Extracted with --language=JavaScript: xgettext has no .qml extension
#     mapping (unknown extension falls back to C mode, which mis-parses QML,
#     e.g. truncating multi-part literals), while the JavaScript parser handles
#     QML string syntax correctly.
#
# Tests are not user-visible and are excluded.
$XGETTEXT $(find components shared -name '*.cpp' -o -name '*.h' | sort) \
    -o "$podir/librekde.pot"
# The credentials window ships QML alongside its C++ (already swept above). Its
# QML i18n*() strings pin the librekde domain too, so extract them into the same
# catalog via --join-existing. JavaScript mode: xgettext has no .qml handler and
# would mis-parse QML in C mode (see the plasmoid block below).
$XGETTEXT --language=JavaScript --join-existing \
    $(find components/credentials/qml -name '*.qml' | sort) \
    -o "$podir/librekde.pot"
$XGETTEXT --language=JavaScript \
    $(find components/plasmoid/package -name '*.qml' | sort) \
    -o "$podir/plasma_applet_org.librescrs.smartcard.pot"
